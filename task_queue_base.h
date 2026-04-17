#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <functional>
#include "queued_task.h"

namespace vi {

// 任务队列基类
// 以FIFO（先进先出）顺序异步执行任务，保证任务不会重叠执行
// 任务可能总是在同一个工作线程上执行，也可能不是
// 使用isCurrent()可以检查任务是否在已知的任务队列上执行
class TaskQueueBase {
public:
    // 任务唯一标识，用于取消等操作
    using TaskId = std::uint64_t;
    // 开始销毁任务队列
    // 返回时确保：
    // 1. 没有任务正在运行
    // 2. 新任务无法在该队列上启动
    // 负责资源释放，释放可能在deleteThis期间同步发生，也可能在返回后异步发生
    // 非任务队列线程上的代码不应该对任务队列的释放时间做任何假设
    // 因此在调用Delete后不应该调用任何方法
    // 在任务队列线程上运行的代码不应该调用Delete，但可以假设任务队列仍然存在
    // 并且可以调用其他方法，例如postTask
    virtual void deleteThis() = 0;

    // 调度任务执行。任务按FIFO顺序执行
    // 如果task->Run()返回true，任务会在下一个QueuedTask开始执行前
    // 在任务队列上被删除
    // 当TaskQueue被删除时，待处理的任务将不会被执行，但会被删除
    // 任务的删除可能在TaskQueue上同步发生，也可能在TaskQueue删除后异步发生
    // 这可能因实现而异，因此不应对待处理任务的生命周期做出假设
    virtual void postTask(std::unique_ptr<QueuedTask> task) = 0;

    // 调度任务在指定的毫秒数后执行
    // 精度应被视为"尽力而为"
    // 在某些情况下（例如在Windows上所有高精度计时器都被用完时）
    // 可能会有最多15毫秒的偏差（通常是8毫秒）
    virtual void postDelayedTask(std::unique_ptr<QueuedTask> task, uint32_t milliseconds) = 0;

    // 任务优先级
    enum class TaskPriority {
        Low,
        Normal,
        High
    };

    // 支持优先级的任务提交接口，默认忽略优先级直接调用无优先级版本
    virtual void postTask(std::unique_ptr<QueuedTask> task, TaskPriority /*prio*/) {
        postTask(std::move(task));
    }

    virtual void postDelayedTask(std::unique_ptr<QueuedTask> task,
                                 uint32_t milliseconds,
                                 TaskPriority /*prio*/) {
        postDelayedTask(std::move(task), milliseconds);
    }

    // 可取消任务接口，默认实现仅调用普通提交接口，不支持取消（返回0表示“不可取消”）
    virtual TaskId postCancellableTask(std::unique_ptr<QueuedTask> task) {
        postTask(std::move(task));
        return 0;
    }

    virtual TaskId postCancellableDelayedTask(std::unique_ptr<QueuedTask> task,
                                              uint32_t milliseconds) {
        postDelayedTask(std::move(task), milliseconds);
        return 0;
    }

    // 尝试取消指定任务，返回 true 表示找到并标记为取消
    virtual bool cancelTask(TaskId /*id*/) { return false; }

    // 队列运行时统计信息
    struct QueueStats {
        std::uint64_t executed_task_count{0};   // 已执行任务总数
        std::size_t   pending_task_count{0};    // 即时队列中待执行任务数
        std::size_t   delayed_task_count{0};    // 延迟队列中待执行任务数
    };

    // 获取当前队列的统计信息
    // 默认实现返回空统计，具体实现可以覆盖
    virtual QueueStats stats() const { return QueueStats{}; }

    // 队列容量控制配置
    struct CapacityConfig {
        // 即时任务队列最大容量（0 表示不限制）
        std::size_t max_pending{0};
        // 延迟任务队列最大容量（0 表示不限制）
        std::size_t max_delayed{0};
        // 当任务被拒绝时的回调，可用于记录日志或做降级处理
        // 若为空，则简单丢弃任务
        // 注意：回调在调用线程中执行
        std::function<void(std::unique_ptr<QueuedTask>)> on_reject;
    };

    // 配置队列容量限制，默认不做任何处理
    virtual void configureCapacity(const CapacityConfig& /*config*/) {}

    // 返回当前运行线程的任务队列
    // 如果当前线程没有关联任务队列，返回nullptr
    static TaskQueueBase* current();
    // 说明（补充）：
    // - `current()` typically 由实现使用 thread-local 存储（见 task_queue_base.cpp），
    //   用来在任务执行期间获得“当前”任务队列指针。
    // - 这种方式允许任务内部通过 `TaskQueueBase::current()` 调用队列接口，
    //   例如 `postDelayedTask`，从而将任务重新排入同一队列。
    
    // 检查当前任务队列是否是正在执行的任务队列
    bool isCurrent() const { return current() == this; }

    // 获取任务队列名称
    virtual const std::string_view& name() const = 0;

protected:
    // 当前任务队列设置器
    // 用于在任务执行期间设置和恢复当前任务队列
    class CurrentTaskQueueSetter {
    public:
        // 构造时设置当前任务队列
        explicit CurrentTaskQueueSetter(TaskQueueBase* taskQueue);
        
        // 禁止拷贝构造和赋值
        CurrentTaskQueueSetter(const CurrentTaskQueueSetter&) = delete;
        CurrentTaskQueueSetter& operator=(const CurrentTaskQueueSetter&) = delete;
        
        // 析构时恢复之前的任务队列
        ~CurrentTaskQueueSetter();

        // 说明（补充）：
        // - `CurrentTaskQueueSetter` 是一个典型的 RAII 工具：构造时把传入的
        //   `taskQueue` 设为线程的当前队列，析构时恢复先前的值。
        // - 这个类保证在执行单个任务期间 `TaskQueueBase::current()` 返回正确的
        //   队列指针，从而让任务在运行时能够安全地访问并重新排队自己或调度
        //   其它延迟/周期任务。

    private:
        // 保存之前的任务队列指针
        TaskQueueBase* const _previous;
    };

    // TaskQueue的用户应该调用Delete而不是直接删除此对象
    virtual ~TaskQueueBase() = default;
};

// 任务队列删除器
// 用于智能指针管理TaskQueueBase对象
struct TaskQueueDeleter {
    void operator()(TaskQueueBase* taskQueue) const { taskQueue->deleteThis(); }
};

// 说明（补充）：
// - `TaskQueueDeleter` 是为 `std::unique_ptr<TaskQueueBase, TaskQueueDeleter>`
//   设计的自定义删除器（deleter）。当智能指针被销毁时，会调用 `deleteThis()`
//   而不是直接 `delete` 对象，这允许 TaskQueue 的删除过程由实现自行
//   控制（例如在线程安全的方式下关闭工作线程再释放对象）。

}  // namespace vi
