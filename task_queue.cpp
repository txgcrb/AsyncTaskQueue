#include "task_queue.h"
#include "task_queue_base.h"
#include "task_queue_std.h"

namespace vi
{
    TaskQueue::TaskQueue(std::unique_ptr<TaskQueueBase, TaskQueueDeleter> taskQueue)
        : impl_(taskQueue.release()) {}

    // 说明（补充）：
    // - 构造函数接受一个带自定义删除器 `TaskQueueDeleter` 的 `std::unique_ptr<TaskQueueBase, ...>`。
    //   调用 `release()` 将裸指针赋给 `impl_`，智能指针的删除器仍然在传入者处持有，
    //   但 `TaskQueue` 的生命周期由 `impl_->deleteThis()` 控制（见析构）。
    // - 这种设计允许在创建时使用 `unique_ptr` 包装实现对象以便异常安全构造，
    //   同时 `TaskQueue` 自身负责在析构时触发实现的销毁流程（而不是直接 delete）。

    TaskQueue::~TaskQueue()
    {
        // There might running task that tries to rescheduler itself to the TaskQueue
        // and not yet aware TaskQueue destructor is called.
        // Calling back to TaskQueue::PostTask need impl_ pointer still be valid, so
        // do not invalidate impl_ pointer until Delete returns.
        impl_->deleteThis();
    }

    bool TaskQueue::isCurrent() const
    {
        return impl_->isCurrent();
    }

    void TaskQueue::postTask(std::unique_ptr<QueuedTask> task)
    {
        return impl_->postTask(std::move(task));
    }

    void TaskQueue::postDelayedTask(std::unique_ptr<QueuedTask> task, uint32_t milliseconds)
    {
        return impl_->postDelayedTask(std::move(task), milliseconds);
    }

    std::unique_ptr<TaskQueue> TaskQueue::create(std::string_view name)
    {
        return std::make_unique<TaskQueue>(std::unique_ptr<TaskQueueBase, TaskQueueDeleter>(new TaskQueueSTD(name)));
    }

}
