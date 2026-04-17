#pragma once

#include <string>
#include <algorithm>
#include <map>
#include <memory>
#include <queue>
#include <utility>
#include <thread>
#include <chrono>
#include <unordered_set>
#include <vector>
#include "queued_task.h"
#include "event.h"
#include "task_queue_base.h"

namespace vi {

    // TaskQueueSTD类是TaskQueueBase的具体实现
    // 提供了一个标准的任务队列实现，支持即时任务和延迟任务的处理
class TaskQueueSTD final : public TaskQueueBase {
public:
    // 构造函数
    // @param queueName 队列名称，用于标识和调试
    // @param workerCount 工作线程数量，默认为1（单线程队列），>1 时为线程池模式
    TaskQueueSTD(const std::string_view& queueName, std::size_t workerCount = 1);
    
    // 析构函数
    ~TaskQueueSTD() override = default;

    // 删除任务队列
    // 确保所有任务都被正确清理
    void deleteThis() override;

    // 提交即时任务到队列
    // @param task 待执行的任务
    void postTask(std::unique_ptr<QueuedTask> task) override;

    // 提交延迟任务到队列
    // @param task 待执行的任务
    // @param milliseconds 延迟执行的毫秒数
    void postDelayedTask(std::unique_ptr<QueuedTask> task, uint32_t milliseconds) override;

    // 获取队列名称
    // @return 队列名称
    const std::string_view& name() const override;

    // 获取队列统计信息
    QueueStats stats() const override;

    // 带优先级的任务提交
    void postTask(std::unique_ptr<QueuedTask> task, TaskPriority prio) override;
    void postDelayedTask(std::unique_ptr<QueuedTask> task,
                         uint32_t milliseconds,
                         TaskPriority prio) override;

    // 可取消任务提交与取消
    TaskId postCancellableTask(std::unique_ptr<QueuedTask> task) override;
    TaskId postCancellableDelayedTask(std::unique_ptr<QueuedTask> task,
                                      uint32_t milliseconds) override;
    bool cancelTask(TaskId id) override;

    // 配置队列容量
    void configureCapacity(const CapacityConfig& config) override;

private:
    // 时间相关别名，统一使用 steady_clock 作为计时源
    using Clock      = std::chrono::steady_clock;
    using TimePoint  = Clock::time_point;
    using Millis     = std::chrono::milliseconds;

    // 用于标识任务的顺序ID类型
    using OrderId = uint64_t;

    // 任务ID类型
    using TaskId = TaskQueueBase::TaskId;

    // 延迟任务的超时信息结构
    struct DelayedEntryTimeout {
        // 下次执行时间
        TimePoint next_fire_at_{};
        // 任务的顺序ID
        OrderId order_{};

        // 比较运算符，用于在map中排序
        // 优先按执行时间排序，时间相同则按顺序ID排序
        bool operator<(const DelayedEntryTimeout& o) const {
            return std::tie(next_fire_at_, order_) < std::tie(o.next_fire_at_, o.order_);
        }
    };

    // 说明（补充）：
    // - 使用 `std::map<DelayedEntryTimeout, DelayedValue>` 来保存延迟任务，
    //   利用 `DelayedEntryTimeout::operator<` 按照触发时间（`next_fire_at_`）
    //   排序，相同时按 `order_` 保证先后顺序稳定性。
    // - 采用 `map` 的原因是需要快速取出最早的到期任务（`begin()`），同时
    //   保证插入操作按键自动排序。
    // - `order_` 的存在确保在时间点相同的情况下，后入先出等不确定性被最小化，
    //   并且与即时任务的顺序比较可以做出确定性的选择。

    // 下一个要执行的任务的信息结构
    struct NextTask {
        // 是否是最终任务（用于退出处理）
        bool final_task_{false};
        // 要执行的任务
        std::unique_ptr<QueuedTask> run_task_;
        // 需要等待的时间（毫秒）
        int64_t sleep_time_ms_{};
    };

    // 获取下一个要执行的任务
    // 根据任务的类型（即时/延迟）和时间来决定返回哪个任务
    NextTask getNextTask();

    // 处理任务的主循环
    // 在工作线程中运行，不断获取和执行任务
    void processTasks();

    // 唤醒工作线程
    // 当有新任务添加时调用此方法
    void notifyWake();

    // 获取当前时间点
    static TimePoint now();

private:
    // 线程启动事件
    vi::Event started_;

    // 线程停止事件
    vi::Event stopped_;

    // 任务通知事件
    // 用于唤醒工作线程处理新任务
    vi::Event flag_notify_;

    // 工作线程数量
    std::size_t worker_count_{1};

    // 工作线程
    std::vector<std::thread> threads_;

    // 任务队列互斥锁
    mutable std::mutex pending_mutex_;

    // 说明（补充）：
    // - `pending_mutex_` 保护所有与队列状态相关的成员（即时队列、延迟队列、
    //   取消集合、计数器等）。任何访问或修改这些容器的代码必须先获取此锁，
    //   以保证线程安全。
    // - 设计上避免对锁持有期间执行耗时操作（例如执行任务），因此 `getNextTask()`
    //   在持有锁时只选择并移动出待执行任务对象，然后释放锁后再执行任务。

    // 线程退出标志
    bool thread_should_quit_ {false};

    // 任务顺序计数器
    OrderId thread_posting_order_ {};

    // 即时任务队列（按优先级拆分）
    struct PendingEntry {
        OrderId order{};
        TaskId  id{};
        std::unique_ptr<QueuedTask> task;
    };

    std::queue<PendingEntry> pending_high_;
    std::queue<PendingEntry> pending_normal_;
    std::queue<PendingEntry> pending_low_;

    // 延迟任务队列
    // 使用map自动按执行时间排序
    // key是任务的超时信息，value包含任务ID和任务本身
    struct DelayedValue {
        TaskId id{};
        std::unique_ptr<QueuedTask> task;
    };

    std::map<DelayedEntryTimeout, DelayedValue> delayed_queue_;

    // 被取消的任务ID集合
    std::unordered_set<TaskId> canceled_ids_;

    // 说明（补充）：
    // - 当任务通过 `postCancellableTask` 或 `postCancellableDelayedTask` 提交时，
    //   会分配一个唯一 `TaskId`。调用 `cancelTask(id)` 会把 id 插入 `canceled_ids_`。
    // - 在取任务时（即时或延迟），会检查任务的 id 是否在 `canceled_ids_` 中，
    //   如果存在则丢弃任务并继续查找下一个可执行任务。

    // 下一个任务ID
    TaskId next_task_id_{1};

    // 已执行任务计数
    std::uint64_t executed_task_count_{0};

    // 队列名称存储
    std::string name_storage_;
    // 指向内部字符串的视图，避免 string_view 悬空
    std::string_view name_;

    // 队列容量配置
    CapacityConfig capacity_config_{};
};

}

