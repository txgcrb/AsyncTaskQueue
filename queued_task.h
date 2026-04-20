#pragma once

#include <type_traits>
#include <memory>

namespace vi
{

    // 异步执行任务的基础接口类
    // 该接口主要包含一个run()函数，用于在目标队列上执行任务
    // 更多细节请参见run()方法和TaskQueue类的说明
    class QueuedTask
    {
    public:
        // 虚析构函数，确保正确释放派生类资源
        virtual ~QueuedTask() = default;

        // 当任务在指定队列上执行时会调用此主函数
        // 返回值说明：
        // - 返回true：表示任务执行完成，任务对象将被删除
        // - 返回false：表示任务的所有权已被转移（比如任务重新提交到其他队列或被复用）
        // 这个函数在基类中只有声明，没有具体的实现，所有的实现逻辑都必须由派生类（子类）来完成
        virtual bool run() = 0; //= 0 表示这是一个纯虚函数
    };

    // QueuedTask的简单实现，用于支持std::bind和Lambda表达式
    // 模板参数Closure可以是任何可调用对象（函数、Lambda等）
    template <typename Closure>
    class ClosureTask : public QueuedTask
    {
    public:
        // 通过万能引用、完美转发构造任务，保持值类别,explicit禁止隐式转换
        explicit ClosureTask(Closure &&closure)
            : closure_(std::forward<Closure>(closure)) {}

        // 说明（补充）：
        // - 模板参数 `Closure` 在实例化时可能是一个左值引用类型或右值类型。
        // - 使用完美转发（`Closure&&` + `std::forward`）可接收临时对象或已有对象，
        //   并以合适的值类别构造内部存储 `closure_`。
        // - 内部成员使用 `typename std::decay<Closure>::type`，目的是去除引用和
        //   cv 修饰符，并保证 `closure_` 存储的是可拷贝/可移动的实际类型（即值类型），
        //   避免将引用类型直接保存导致悬空或生命周期问题。
    private:
        // 执行存储的可调用对象
        // 返回true表示执行完成后删除任务
        bool run() override
        {
            closure_();
            return true;
        }

        // 存储可调用对象的副本，确保 closure_ 存的是一份独立的、拥有所有权的拷贝，而不是别人的引用。
        // 避免悬空引用
        // std::decay<Closure>作用是把带有引用 &、右值引用 && 或者 const 修饰符的类型，统统剥掉外衣，还原成最干净的“值类型”
        typename std::decay<Closure>::type closure_;
        // std::decay_t<Closure> closure_;
    };

    // 辅助函数：将可调用对象转换为QueuedTask
    // 用于简化任务创建过程
    template <typename Closure>
    std::unique_ptr<QueuedTask> ToQueuedTask(Closure &&closure)
    {
        return std::make_unique<ClosureTask<Closure>>(std::forward<Closure>(closure));
    }

    // 说明（补充）：
    // - `ToQueuedTask` 是一个辅助函数模板，用来将任意可调用对象包装成
    //   一个 `QueuedTask` 的实现并返回 `std::unique_ptr<QueuedTask>`。
    // - 注意所有权语义：`std::make_unique` 返回的智能指针将任务的所有权移动
    //   到调用者，随后将此 unique_ptr 传入任务队列时，队列会继续持有或转移该所有权。

    // ClosureTask的扩展版本，支持指定清理代码
    // 这在使用Lambda表达式时特别有用，可以保证资源清理
    // 即使任务被丢弃（例如队列已满）时也会执行清理代码
    template <typename Closure, typename Cleanup>
    class ClosureTaskWithCleanup : public ClosureTask<Closure>
    {
    public:
        // 构造函数接收任务和清理函数，（初始化父类构造函数）
        ClosureTaskWithCleanup(Closure &&closure, Cleanup &&cleanup)
            : ClosureTask<Closure>(std::forward<Closure>(closure)), cleanup_(std::forward<Cleanup>(cleanup)) {}

        // 析构时执行清理函数
        ~ClosureTaskWithCleanup() override { cleanup_(); }

    private:
        // 存储清理函数
        // 获取 Cleanup 的“退化”类型，并定义一个名为 cleanup_ 的变量
        typename std::decay<Cleanup>::type cleanup_;
    };

}
