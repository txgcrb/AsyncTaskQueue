#pragma once

#include <stdint.h>

#include <memory>
#include <string_view>
#include <type_traits>
#include <map>
#include <mutex>
#include <atomic>
#include "queued_task.h"
#include "task_queue_base.h"

namespace vi
{
    // TaskQueue类实现了一个异步任务队列
    // 保证任务按照FIFO（先进先出）顺序执行，并且任务之间不会重叠
    // 任务可能始终在同一个工作线程上执行，也可能不是
    // 使用isCurrent()可以检查任务是否在已知的任务队列上执行
    //
    // 以下是一些使用示例：
    //
    // 1) 异步执行Lambda表达式：
    //    class MyClass {
    //      ...
    //      TaskQueue queue_("MyQueue");
    //    };
    //
    //    void MyClass::StartWork() {
    //      queue_.postTask([]() { Work(); });
    //    ...
    //
    // 2) 在定时器上发布自定义任务。任务在每次运行后重新发布自己：
    //    class TimerTask : public QueuedTask {
    //     public:
    //      TimerTask() {}
    //     private:
    //      bool run() override {
    //        ++count_;
    //        TaskQueueBase::Current()->postDelayedTask(
    //            std::make_unique<TimerTask>(), 1000);
    //        // 所有权已转移到下一次执行
    //        // 返回false防止现在被删除
    //        return false;
    //      }
    //      int count_ = 0;
    //    };
    //    ...
    //    queue_.postDelayedTask(std::make_unique<TimerTask>(), 1000);
    //
    // 关于销毁的说明：
    // 当TaskQueue被删除时，待处理的任务将不会被执行，但它们会被删除
    // 任务的删除可能在TaskQueue本身被删除后异步发生
    // 也可能在TaskQueue实例被删除时同步发生
    // 这可能因操作系统而异，因此不应对待处理任务的生命周期做出假设

    class TaskQueueDeleter;

    // Implements a task queue that asynchronously executes tasks in a way that
    // guarantees that they're executed in FIFO order and that tasks never overlap.
    // Tasks may always execute on the same worker thread and they may not.
    // To DCHECK that tasks are executing on a known task queue, use IsCurrent().
    //
    // Here are some usage examples:
    //
    //   1) Asynchronously running a lambda:
    //
    //     class MyClass {
    //       ...
    //       TaskQueue queue_("MyQueue");
    //     };
    //
    //     void MyClass::StartWork() {
    //       queue_.PostTask([]() { Work(); });
    //     ...
    //
    //   2) Posting a custom task on a timer.  The task posts itself again after
    //      every running:
    //
    //     class TimerTask : public QueuedTask {
    //      public:
    //       TimerTask() {}
    //      private:
    //       bool Run() override {
    //         ++count_;
    //         TaskQueueBase::Current()->PostDelayedTask(
    //             absl::WrapUnique(this), 1000);
    //         // Ownership has been transferred to the next occurance,
    //         // so return false to prevent from being deleted now.
    //         return false;
    //       }
    //       int count_ = 0;
    //     };
    //     ...
    //     queue_.PostDelayedTask(std::make_unique<TimerTask>(), 1000);
    //
    // For more examples, see task_queue_unittests.cc.
    //
    // A note on destruction:
    //
    class TaskQueue
    {
    public:
        // 构造函数，接收任务队列实现的智能指针
        // explicit 禁用构造函数的隐式类型转换
        explicit TaskQueue(std::unique_ptr<TaskQueueBase, TaskQueueDeleter> taskQueue);
        ~TaskQueue();

        // 创建具有指定名称的任务队列
        static std::unique_ptr<TaskQueue> create(std::string_view name);

        // 用于检查当前是否在此队列的线程上
        bool isCurrent() const;

        // 返回任务队列实现的非拥有指针
        TaskQueueBase *get() { return impl_; }

        // TODO(tommi): For better debuggability, implement RTC_FROM_HERE.

        // 提交任务到队列
        // 任务的所有权转移给PostTask
        void postTask(std::unique_ptr<QueuedTask> task);

        // 调度任务在指定毫秒数后执行
        // 精度应被视为"尽力而为"
        // 在某些情况下，如Windows上所有高精度计时器都被用完时
        // 可能会有最多15毫秒的偏差（通常是8毫秒）
        // 可以通过限制延迟任务的使用来缓解这个问题
        void postDelayedTask(std::unique_ptr<QueuedTask> task, uint32_t milliseconds);

        // 用于Lambda表达式和函数对象的任务提交模板方法
        // std::enable_if用于确保带有std::unique_ptr<QueuedTask>的PostTask调用
        // 不会被这个模板捕获
        template <class Closure, typename std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>::type * = nullptr>
        void postTask(Closure &&closure)
        {
            postTask(ToQueuedTask(std::forward<Closure>(closure)));
        }

        // 说明（补充）：
        // - 该模板重载允许直接传入 Lambda 或任意可调用对象，而无需手动封装为
        //   `std::unique_ptr<QueuedTask>`。内部通过 `ToQueuedTask` 将闭包包装为 `QueuedTask`。
        // - `std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>`
        //   是一个 SFINAE 技巧，用来避免当调用方本身传入 `std::unique_ptr<QueuedTask>` 时，
        //   该模板被错误匹配（因为有非模板的 `postTask(std::unique_ptr<QueuedTask>)`），
        //   从而避免二义性或意外拷贝/移动语义改变。
        // - 注意所有权：传入的闭包会被复制或移动到生成的 `ClosureTask` 内部，最终以
        //   `std::unique_ptr<QueuedTask>` 的形式传递给队列；队列接收后拥有该任务对象的所有权。

        // 延迟任务的模板方法版本
        template <class Closure, typename std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>::type * = nullptr>
        void postDelayedTask(Closure &&closure, uint32_t milliseconds)
        {
            postDelayedTask(ToQueuedTask(std::forward<Closure>(closure)), milliseconds);
        }

        // 说明：postDelayedTask 的模板版本与上面相似，仅在传入延迟毫秒数后把封装的任务
        // 提交到延迟队列。delay 时间的准确度如上文所述为"尽力而为"。

        // 任务ID与优先级类型别名（需要在使用前声明）
        using TaskId = TaskQueueBase::TaskId;
        using TaskPriority = TaskQueueBase::TaskPriority;

        // 周期任务：每隔 interval_ms 调用一次 closure
        // closure 需为可调用对象，返回 void
        // 返回 TaskId，可用于取消周期任务
        template <class Closure>
        TaskId postPeriodicTask(Closure &&closure, uint32_t interval_ms);

        // 说明（补充）：
        // - 周期任务使用内部类 `PeriodicTask` 实现；该任务对象在每次运行后会把自身
        //   以延迟任务的方式重新投递到当前队列，从而实现循环执行。
        // - 因为任务会在运行时将 `this` 指针重新包装成 `std::unique_ptr<QueuedTask>` 并
        //   传递给队列，所以 `run()` 返回 false，表示所有权已经转移到队列，当前执行路径
        //   不应删除该对象。

        // 重试任务：调用 closure，期待其返回 bool 表示成功/失败
        // max_retry 表示最大重试次数，策略由 RetryStrategy 决定
        struct RetryStrategy
        {
            enum class Type
            {
                Fixed,      // 固定间隔：base_delay_ms
                Linear,     // 线性退避：base, 2*base, 3*base...
                Exponential // 指数退避：base, 2*base, 4*base...
            };

            Type type{Type::Fixed};
            uint32_t base_delay_ms{1000};
        };

        template <class Closure>
        void postRetryTask(Closure &&closure, int max_retry, const RetryStrategy &strategy);

        // 说明（补充）：
        // - 重试任务 `RetryTask` 要求闭包返回 `bool`，用于表示本次是否成功。
        // - 如果失败，任务会根据 `RetryStrategy` 计算下一次重试的延迟并把自身再次
        //   提交为延迟任务。与周期任务类似，任务将 `this` 的所有权重新转移到队列，
        //   因此 `run()` 返回 false 表示当前不应删除对象。

        // 查询队列统计信息（转发到底层实现）
        TaskQueueBase::QueueStats stats() const { return impl_->stats(); }

        // 带优先级的任务提交接口
        void postTask(std::unique_ptr<QueuedTask> task, TaskPriority prio)
        {
            impl_->postTask(std::move(task), prio);
        }

        void postDelayedTask(std::unique_ptr<QueuedTask> task,
                             uint32_t milliseconds,
                             TaskPriority prio)
        {
            impl_->postDelayedTask(std::move(task), milliseconds, prio);
        }

        template <class Closure,
                  typename std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>::type * = nullptr>
        void postTask(Closure &&closure, TaskPriority prio)
        {
            postTask(ToQueuedTask(std::forward<Closure>(closure)), prio);
        }

        template <class Closure,
                  typename std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>::type * = nullptr>
        void postDelayedTask(Closure &&closure, uint32_t milliseconds, TaskPriority prio)
        {
            postDelayedTask(ToQueuedTask(std::forward<Closure>(closure)), milliseconds, prio);
        }

        // 可取消任务提交接口
        TaskId postCancellableTask(std::unique_ptr<QueuedTask> task)
        {
            return impl_->postCancellableTask(std::move(task));
        }

        TaskId postCancellableDelayedTask(std::unique_ptr<QueuedTask> task,
                                          uint32_t milliseconds)
        {
            return impl_->postCancellableDelayedTask(std::move(task), milliseconds);
        }

        template <class Closure,
                  typename std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>::type * = nullptr>
        TaskId postCancellableTask(Closure &&closure)
        {
            return impl_->postCancellableTask(ToQueuedTask(std::forward<Closure>(closure)));
        }

        // test conflict windows 123
        //  SFINAE（Substitution Failure Is Not An Error，替换失败并非错误）
        //   class Closure：模板参数。不管你传进来的是什么，编译器都会把它的实际类型推导并赋值给 Closure。
        //   std::enable_if< 条件 >：类型萃取器，条件为真返回void，为假内部什么都不定义
        //! std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value
        // 判断 Closure 类型是否不能转换为 std::unique_ptr<QueuedTask>类型：如果为 true（不能转换），这个模板会被启用；如果为 false（可以转换），这个模板会被SFINAE排除。
        // typename ... ::type：告诉编译器 ... 为数据类型，前条件为真type为void，前条件为假无type报错（替换失败）
        template <class Closure,
                  typename std::enable_if<!std::is_convertible<Closure, std::unique_ptr<QueuedTask>>::value>::type * = nullptr>
        TaskId postCancellableDelayedTask(Closure &&closure, uint32_t milliseconds)
        {
            return impl_->postCancellableDelayedTask(
                ToQueuedTask(std::forward<Closure>(closure)), milliseconds);
        }
        // 如果没有enable_if会产生重载歧义，当第一个参数为指针时，编译器认为两个版本都可以接收

        // 取消指定任务（支持周期任务）
        bool cancelTask(TaskId id)
        {
            // 先检查是否是周期任务
            {
                std::lock_guard<std::mutex> lock(periodic_tasks_mutex_);
                auto it = periodic_tasks_.find(id);
                if (it != periodic_tasks_.end())
                {
                    // 设置取消标志
                    it->second->store(true);
                    // 清理映射
                    periodic_tasks_.erase(it);
                    return true;
                }
            }
            // 如果不是周期任务，调用底层的 cancelTask
            return impl_->cancelTask(id);
        }

        // 配置队列容量
        void configureCapacity(const TaskQueueBase::CapacityConfig &config)
        {
            impl_->configureCapacity(config);
        }

    private:
        // 禁止拷贝构造和赋值
        TaskQueue &operator=(const TaskQueue &) = delete;
        TaskQueue(const TaskQueue &) = delete;

    private:
        // 任务队列实现的指针
        TaskQueueBase *const impl_;

        // 周期任务的取消标志映射（TaskId -> 取消标志）
        std::map<TaskId, std::shared_ptr<std::atomic<bool>>> periodic_tasks_;
        // 保护 periodic_tasks_ 的互斥锁
        std::mutex periodic_tasks_mutex_;
        // 下一个周期任务ID
        std::atomic<TaskId> next_periodic_id_{1};
    };

    // ================= 周期任务与重试任务的内部实现 =================

    // 周期任务实现：每次运行后自动把自己以延迟任务形式重新投递到当前队列
    template <typename Closure>
    class PeriodicTask : public QueuedTask
    {
    public:
        PeriodicTask(Closure &&closure, uint32_t interval_ms, std::shared_ptr<std::atomic<bool>> cancelled)
            : closure_(std::forward<Closure>(closure)),
              interval_ms_(interval_ms),
              cancelled_(cancelled) {}

    private:
        bool run() override
        {
            // 检查是否已取消
            if (cancelled_ && cancelled_->load())
            {
                return true; // 任务已取消，停止执行
            }

            // 执行业务逻辑
            closure_();

            // 再次检查是否已取消（在执行过程中可能被取消）
            if (cancelled_ && cancelled_->load())
            {
                return true; // 任务已取消，停止执行
            }

            // 将自身重新投递到当前任务队列，实现周期执行
            TaskQueueBase *current = TaskQueueBase::current();
            if (current)
            {
                current->postDelayedTask(
                    std::unique_ptr<QueuedTask>(this),
                    interval_ms_);
                // 所有权已转移到队列中，返回 false 防止当前删除
                return false;
            }

            // 如果当前不在任务队列线程上，则不再重试
            return true;
        }

        typename std::decay<Closure>::type closure_;
        uint32_t interval_ms_{};
        std::shared_ptr<std::atomic<bool>> cancelled_;
    };

    // 重试任务实现：closure 返回 bool 表示本次是否成功
    template <typename Closure>
    class RetryTask : public QueuedTask
    {
    public:
        RetryTask(Closure &&closure, int max_retry, const TaskQueue::RetryStrategy &strategy)
            : closure_(std::forward<Closure>(closure)),
              max_retry_(max_retry),
              strategy_(strategy) {}

    private:
        using Result = decltype(std::declval<Closure &>()());
        static_assert(std::is_same<Result, bool>::value,
                      "RetryTask closure must return bool to indicate success");

        static uint32_t computeDelay(const TaskQueue::RetryStrategy &s, int attempt)
        {
            // attempt 从 1 开始
            switch (s.type)
            {
            case TaskQueue::RetryStrategy::Type::Fixed:
                return s.base_delay_ms;
            case TaskQueue::RetryStrategy::Type::Linear:
                return s.base_delay_ms * static_cast<uint32_t>(attempt);
            case TaskQueue::RetryStrategy::Type::Exponential:
                return s.base_delay_ms * static_cast<uint32_t>(1u << (attempt - 1));
            }
            return s.base_delay_ms;
        }

        bool run() override
        {
            bool ok = closure_();
            if (ok)
            {
                return true; // 成功，任务结束
            }

            if (attempt_ >= max_retry_)
            {
                return true; // 已达到最大重试次数，停止
            }

            ++attempt_;
            TaskQueueBase *current = TaskQueueBase::current();
            if (current)
            {
                uint32_t delay = computeDelay(strategy_, attempt_);
                current->postDelayedTask(
                    std::unique_ptr<QueuedTask>(this),
                    delay);
                // 所有权已转移到队列中，返回 false 防止当前删除
                return false;
            }

            // 如果当前不在任务队列线程上，则不再重试
            return true;
        }

        typename std::decay<Closure>::type closure_;
        int max_retry_{0};
        int attempt_{0};
        TaskQueue::RetryStrategy strategy_;
    };

    // TaskQueue 周期任务接口实现
    template <class Closure>
    TaskQueue::TaskId TaskQueue::postPeriodicTask(Closure &&closure, uint32_t interval_ms)
    {
        // 创建共享的取消标志
        auto cancelled = std::make_shared<std::atomic<bool>>(false);

        // 生成唯一的周期任务 ID
        TaskId id = next_periodic_id_.fetch_add(1);

        // 保存取消标志映射
        {
            std::lock_guard<std::mutex> lock(periodic_tasks_mutex_);
            periodic_tasks_[id] = cancelled;
        }

        // 投递第一个周期任务
        postDelayedTask(
            std::make_unique<PeriodicTask<Closure>>(std::forward<Closure>(closure), interval_ms, cancelled),
            interval_ms);

        return id;
    }

    // TaskQueue 重试任务接口实现
    template <class Closure>
    void TaskQueue::postRetryTask(Closure &&closure, int max_retry, const RetryStrategy &strategy)
    {
        postTask(
            std::make_unique<RetryTask<Closure>>(std::forward<Closure>(closure), max_retry, strategy));
    }

}
