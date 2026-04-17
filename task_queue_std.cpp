#include "task_queue_std.h"
#include <assert.h>
#include <iostream>

namespace vi {

// 构造函数
// 初始化事件对象和工作线程
TaskQueueSTD::TaskQueueSTD(const std::string_view& queueName, std::size_t workerCount)
    : started_(/*manual_reset=*/false, /*initially_signaled=*/false)  // 线程启动事件，自动重置，初始未触发
    , stopped_(/*manual_reset=*/false, /*initially_signaled=*/false)  // 线程停止事件，自动重置，初始未触发
    , flag_notify_(/*manual_reset=*/false, /*initially_signaled=*/false)  // 任务通知事件，自动重置，初始未触发
    , worker_count_(workerCount == 0 ? 1 : workerCount)  // 至少一个工作线程
    , name_storage_(queueName)   // 存储队列名称
    , name_(name_storage_)       // string_view 指向内部字符串，避免悬空
{

    // 创建工作线程（线程池模式）
    for (std::size_t i = 0; i < worker_count_; ++i) {
        threads_.emplace_back([this]{
            CurrentTaskQueueSetter setCurrent(this);  // 设置当前线程的任务队列
            this->processTasks();  // 开始处理任务
        });
    }

    // 等待至少有一个线程启动完成
    started_.wait(vi::Event::kForever);
}

// 删除任务队列
// 确保工作线程安全退出并清理资源
void TaskQueueSTD::deleteThis() {
    assert(isCurrent() == false);  // 确保不是在当前任务队列的线程中调用

    std::unique_lock<std::mutex> lock(pending_mutex_);
    thread_should_quit_ = true;  // 设置退出标志
    lock.unlock();  // 先释放锁，然后立即通知

    notifyWake();  // 唤醒工作线程处理退出请求

    stopped_.wait(vi::Event::kForever);  // 等待线程完全停止

    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();  // 等待线程结束
        }
    }
    delete this;  // 删除对象
}

// 说明（补充）：
// - 构造函数中通过捕获 `this` 创建工作线程，线程启动后会立即用 `CurrentTaskQueueSetter`
//   将当前线程与本队列关联，这样在任务执行时 `TaskQueueBase::current()` 能返回正确的队列指针。
// - `deleteThis()` 使用了以下约束：
//   1) 只能由非队列执行线程调用（断言保证）；
//   2) 设置退出标志并唤醒线程；
//   3) 等待线程停止事件并 join 线程；
//   4) 最后 `delete this` 释放对象。
//   这种模式确保在对象销毁前，所有工作线程干净退出，并且不会在其他线程中悬挂对 `impl_` 的引用。

// 提交即时任务
// @param task 要执行的任务
void TaskQueueSTD::postTask(std::unique_ptr<QueuedTask> task) {
    postTask(std::move(task), TaskPriority::Normal);
}

// 带优先级的即时任务提交
void TaskQueueSTD::postTask(std::unique_ptr<QueuedTask> task, TaskPriority prio) {
    {
        std::unique_lock<std::mutex> lock(pending_mutex_);
        if (capacity_config_.max_pending > 0 &&
            (pending_high_.size() + pending_normal_.size() + pending_low_.size()) >=
                capacity_config_.max_pending) {
            if (capacity_config_.on_reject) {
                capacity_config_.on_reject(std::move(task));
            }
            // 丢弃新任务
            return;
        }

        OrderId order = thread_posting_order_++;  // 生成任务顺序ID
        PendingEntry entry;
        entry.order = order;
        entry.id    = 0;      // 非可取消任务，ID 为 0
        entry.task  = std::move(task);

        switch (prio) {
        case TaskPriority::High:
            pending_high_.push(std::move(entry));
            break;
        case TaskPriority::Normal:
            pending_normal_.push(std::move(entry));
            break;
        case TaskPriority::Low:
            pending_low_.push(std::move(entry));
            break;
        }
    }

    notifyWake();  // 唤醒工作线程处理新任务
}

// 提交延迟任务
// @param task 要执行的任务
// @param ms 延迟执行的毫秒数
void TaskQueueSTD::postDelayedTask(std::unique_ptr<QueuedTask> task, uint32_t ms) {
    postDelayedTask(std::move(task), ms, TaskPriority::Normal);
}

// 带优先级的延迟任务提交
void TaskQueueSTD::postDelayedTask(std::unique_ptr<QueuedTask> task,
                                   uint32_t ms,
                                   TaskPriority /*prio*/) {
    auto fire_at = now() + Millis(ms);  // 计算任务执行时间点

    DelayedEntryTimeout delay;
    delay.next_fire_at_ = fire_at;  // 设置执行时间

    {
        std::unique_lock<std::mutex> lock(pending_mutex_);
        if (capacity_config_.max_delayed > 0 &&
            delayed_queue_.size() >= capacity_config_.max_delayed) {
            if (capacity_config_.on_reject) {
                capacity_config_.on_reject(std::move(task));
            }
            return;
        }

        delay.order_ = ++thread_posting_order_;  // 生成任务顺序ID
        DelayedValue value;
        value.id   = 0;              // 非可取消任务，ID 为 0
        value.task = std::move(task);
        delayed_queue_[delay] = std::move(value);  // 将任务添加到延迟任务队列
    }

    notifyWake();  // 唤醒工作线程处理新任务
}

// 可取消任务提交：即时任务
TaskQueueSTD::TaskId TaskQueueSTD::postCancellableTask(std::unique_ptr<QueuedTask> task) {
    std::unique_lock<std::mutex> lock(pending_mutex_);
    if (capacity_config_.max_pending > 0 &&
        (pending_high_.size() + pending_normal_.size() + pending_low_.size()) >=
            capacity_config_.max_pending) {
        if (capacity_config_.on_reject) {
            capacity_config_.on_reject(std::move(task));
        }
        return 0;
    }

    OrderId order = thread_posting_order_++;
    TaskId id     = next_task_id_++;

    PendingEntry entry;
    entry.order = order;
    entry.id    = id;
    entry.task  = std::move(task);

    pending_normal_.push(std::move(entry));

    lock.unlock();  // 先释放锁，然后立即通知
    notifyWake();   // 唤醒工作线程
    return id;
}

// 可取消任务提交：延迟任务
TaskQueueSTD::TaskId TaskQueueSTD::postCancellableDelayedTask(std::unique_ptr<QueuedTask> task,
                                                              uint32_t milliseconds) {
    auto fire_at = now() + Millis(milliseconds);

    DelayedEntryTimeout delay;
    delay.next_fire_at_ = fire_at;

    std::unique_lock<std::mutex> lock(pending_mutex_);
    if (capacity_config_.max_delayed > 0 &&
        delayed_queue_.size() >= capacity_config_.max_delayed) {
        if (capacity_config_.on_reject) {
            capacity_config_.on_reject(std::move(task));
        }
        return 0;
    }

    delay.order_ = ++thread_posting_order_;
    TaskId id    = next_task_id_++;

    DelayedValue value;
    value.id   = id;
    value.task = std::move(task);

    delayed_queue_[delay] = std::move(value);

    lock.unlock();  // 先释放锁，然后立即通知
    notifyWake();   // 唤醒工作线程
    return id;
}

// 取消任务
bool TaskQueueSTD::cancelTask(TaskId id) {
    if (id == 0) {
        return false;
    }
    std::unique_lock<std::mutex> lock(pending_mutex_);
    auto [it, inserted] = canceled_ids_.insert(id);
    return inserted;
}

// 配置队列容量
void TaskQueueSTD::configureCapacity(const CapacityConfig& config) {
    std::unique_lock<std::mutex> lock(pending_mutex_);
    capacity_config_ = config;
}

// 获取下一个要执行的任务
// @return NextTask 包含下一个要执行的任务信息
TaskQueueSTD::NextTask TaskQueueSTD::getNextTask() {
    NextTask result{};

    auto tick = now();  // 获取当前时间点

    std::unique_lock<std::mutex> lock(pending_mutex_);

    // 检查是否需要退出
    if (thread_should_quit_) {
        result.final_task_ = true;
        return result;
    }

    // 检查延迟任务队列
    while (!delayed_queue_.empty()) {
        auto delayed_entry = delayed_queue_.begin();
        const auto& delay_info = delayed_entry->first;
        auto& delay_val = delayed_entry->second;

        // 若任务已被取消，则直接丢弃
        if (delay_val.id != 0 &&
            canceled_ids_.find(delay_val.id) != canceled_ids_.end()) {
            delayed_queue_.erase(delayed_entry);
            continue;
        }

        // 检查延迟任务是否到期
        if (tick >= delay_info.next_fire_at_) {
            // 如果有即时任务，且即时任务的顺序ID更小，优先执行即时任务
            // 优先级队列中找出 order 最小的任务
            // 说明：这里的比较逻辑用于实现“延迟任务不会抢占更早提交的即时任务”这一策略：
            // - `order` 是单调递增的顺序编号，表示任务的提交先后顺序。
            // - 当延迟任务到期（时间已到）时，会检查是否存在 order 更小的即时任务，
            //   如果存在则优先执行即时任务，以保证 FIFO 语义的全局一致性（尽量避免时间触发改变提交顺序）。
            PendingEntry* best_entry = nullptr;

            auto pick_best = [&](std::queue<PendingEntry>& q) {
                if (!q.empty()) {
                    PendingEntry& e = q.front();
                    if (!best_entry || e.order < best_entry->order) {
                        best_entry = &e;
                    }
                }
            };

            pick_best(pending_high_);
            pick_best(pending_normal_);
            pick_best(pending_low_);

            if (best_entry && best_entry->order < delay_info.order_) {
                // 选择了更早的即时任务
                // 根据 best_entry 实际位于哪个队列来弹出
                auto pop_from = [&](std::queue<PendingEntry>& q) {
                    PendingEntry e = std::move(q.front());
                    q.pop();
                    return e;
                };

                PendingEntry chosen;
                if (!pending_high_.empty() && &pending_high_.front() == best_entry) {
                    chosen = pop_from(pending_high_);
                } else if (!pending_normal_.empty() && &pending_normal_.front() == best_entry) {
                    chosen = pop_from(pending_normal_);
                } else {
                    chosen = pop_from(pending_low_);
                }

                // 检查是否被取消
                if (chosen.id != 0 &&
                    canceled_ids_.find(chosen.id) != canceled_ids_.end()) {
                    // 丢弃并继续寻找下一个任务
                    continue;
                }

                // 注意：此处仅把任务对象移动到 `result.run_task_` 中，随后在返回前会释放锁。
                // 执行任务的实际工作在 `processTasks()` 中进行；该函数在释放锁后再执行任务，
                // 以避免在持锁期间运行用户代码导致长时间阻塞其它提交/取消操作。
                result.run_task_ = std::move(chosen.task);
                return result;
            }

            // 执行到期的延迟任务
            // 延迟任务在这里被从延迟队列取出并转移到返回值，锁随后被释放，
            // 任务的执行（包括可能再次把自身重新投递）发生在锁外。
            result.run_task_ = std::move(delay_val.task);
            delayed_queue_.erase(delayed_entry);
            return result;
        }

        // 设置需要等待的时间（毫秒）
        auto diff = std::chrono::duration_cast<Millis>(delay_info.next_fire_at_ - tick);
        result.sleep_time_ms_ = diff.count();
        break;
    }

    // 如果有即时任务，返回第一个即时任务（按优先级：High -> Normal -> Low）
    auto pop_pending = [&](std::queue<PendingEntry>& q) -> std::unique_ptr<QueuedTask> {
        while (!q.empty()) {
            PendingEntry e = std::move(q.front());
            q.pop();
            if (e.id != 0 &&
                canceled_ids_.find(e.id) != canceled_ids_.end()) {
                continue; // 丢弃已取消任务
            }
            return std::move(e.task);
        }
        return nullptr;
    };

    if (!pending_high_.empty()) {
        result.run_task_ = pop_pending(pending_high_);
        if (result.run_task_) return result;
    }
    if (!pending_normal_.empty()) {
        result.run_task_ = pop_pending(pending_normal_);
        if (result.run_task_) return result;
    }
    if (!pending_low_.empty()) {
        result.run_task_ = pop_pending(pending_low_);
        if (result.run_task_) return result;
    }

    return result;
}

// 处理任务的主循环
void TaskQueueSTD::processTasks() {
    started_.set();  // 通知线程已启动

    while (true) {
        auto task = getNextTask();  // 获取下一个要执行的任务

        if (task.final_task_) {  // 检查是否需要退出
            break;
        }

        if (task.run_task_) {  // 如果有任务要执行
            // 执行任务（增加异常保护，防止任务抛出异常导致线程崩溃）
            // 说明：getNextTask 已经在持锁期间把任务移动到 `task.run_task_`，
            // 在这里释放智能指针并获得裸指针以便调用 `run()`。
            // 这样做的原因是：
            // - `run()` 可能会把任务对象重新投递到队列（将 `this` 再次以 unique_ptr 传入），
            //   在这种情况下返回 false 并不意味着立即 delete，而是表示所有权被转移。
            // - 使用裸指针能够在 `run()` 返回 true 时手动 delete，返回 false 时避免重复 delete。
            QueuedTask* release_ptr = task.run_task_.release();
            try {
                if (release_ptr->run()) {
                    delete release_ptr;  // 如果任务返回true，删除任务
                }
            } catch (const std::exception& e) {
                std::cerr << "[TaskQueueSTD:" << name_ << "] task threw std::exception: "
                          << e.what() << std::endl;
                delete release_ptr;
            } catch (...) {
                std::cerr << "[TaskQueueSTD:" << name_ << "] task threw unknown exception"
                          << std::endl;
                delete release_ptr;
            }

            ++executed_task_count_;  // 统计已执行任务数
            continue;  // 继续处理下一个任务
        }

        // 如果没有任务，等待新任务
        // 注意：当 sleep_time_ms_ <= 0 时，可能是有延迟任务即将到期（时间差小于1ms被截断为0）
        // 或者已经过期，此时应该立即重新检查而不是等待
        if (task.sleep_time_ms_ > 0) {
            flag_notify_.wait(static_cast<int>(task.sleep_time_ms_));  // 等待指定时间
        } else if (task.sleep_time_ms_ == 0) {
            // sleep_time_ms_ == 0 可能表示：
            // 1. 没有任何任务（既没有即时任务也没有延迟任务） -> 应该无限等待
            // 2. 有延迟任务但时间差小于1ms -> 应该立即重新检查
            // 我们需要区分这两种情况
            std::unique_lock<std::mutex> lock(pending_mutex_);
            bool has_delayed = !delayed_queue_.empty();
            lock.unlock();
            
            if (has_delayed) {
                // 不等待，立即进入下一次循环
            } else {
                flag_notify_.wait(vi::Event::kForever);  // 无限等待
            }
        } else {
            // sleep_time_ms_ < 0 表示有延迟任务已经过期，立即重新检查
            // 不等待，立即进入下一次循环
        }
    }

    stopped_.set();  // 通知线程已停止
}

// 唤醒工作线程
void TaskQueueSTD::notifyWake() {
    // 设置通知事件
    // 注意：必须在添加任务后调用此方法
    // 否则可能会出现线程被唤醒但没有任务可处理的情况
    flag_notify_.set();
}

// 获取当前时间点
TaskQueueSTD::TimePoint TaskQueueSTD::now() {
    return Clock::now();
}

// 获取队列名称
const std::string_view& TaskQueueSTD::name() const {
    return name_;
}

// 获取队列统计信息
TaskQueueBase::QueueStats TaskQueueSTD::stats() const {
    QueueStats result;
    result.executed_task_count = executed_task_count_;

    std::unique_lock<std::mutex> lock(pending_mutex_);
    result.pending_task_count =
        pending_high_.size() + pending_normal_.size() + pending_low_.size();
    result.delayed_task_count = delayed_queue_.size();
    return result;
}

}
