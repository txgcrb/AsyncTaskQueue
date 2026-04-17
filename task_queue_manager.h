#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace vi
{

    class TaskQueue;

    // 任务队列管理器类
    // 负责管理和协调多个命名任务队列
    // 实现单例模式，确保全局只有一个管理器实例
    class TaskQueueManager
    {
    public:
        // 获取TaskQueueManager的单例实例
        // 返回智能指针引用，确保线程安全的访问
        static std::unique_ptr<TaskQueueManager> &instance();

        // 析构函数，清理所有任务队列
        ~TaskQueueManager();

        // 创建多个命名任务队列
        // @param nameList 任务队列名称列表
        void create(const std::vector<std::string> &nameList);

        // 获取指定名称的任务队列
        // @param name 任务队列名称
        // @return 任务队列指针，如果不存在返回nullptr
        TaskQueue *queue(const std::string &name);

        // 检查指定名称的任务队列是否存在
        // @param name 任务队列名称
        // @return 存在返回true，否则返回false
        bool hasQueue(const std::string &name);

    private:
        // 清理所有任务队列
        void clear();

        // 检查任务队列是否存在
        // @param name 任务队列名称
        // @return 存在返回true，否则返回false
        bool exist(const std::string &name);

    private:
        // 私有构造函数，实现单例模式
        TaskQueueManager();

        // 禁止移动构造
        TaskQueueManager(TaskQueueManager &&) = delete;

        // 禁止拷贝构造
        TaskQueueManager(const TaskQueueManager &) = delete;

        // 禁止赋值操作
        TaskQueueManager &operator=(const TaskQueueManager &) = delete;

    private:
        // 互斥锁，保护队列映射的线程安全访问
        std::mutex m_mutex;

        // 任务队列映射表
        // 键：队列名称
        // 值：任务队列智能指针
        std::unordered_map<std::string, std::unique_ptr<TaskQueue>> m_queueMap;
    };

} // namespace vi

// 全局访问宏，用于获取TaskQueueManager实例
#define TQMgr vi::TaskQueueManager::instance()

// 全局访问宏，用于获取指定名称的任务队列
#define TQ(name) TQMgr->queue(name)

// 说明（补充）：
// - `instance()` 返回一个对 `std::unique_ptr<TaskQueueManager>` 的引用，内部采用
//   `std::call_once` 与函数静态变量实现线程安全的延迟初始化（见实现文件）。
// - 之所以返回 `unique_ptr&` 而不是直接 `TaskQueueManager*`，是为了允许调用者
//   使用宏 `TQMgr` 以便简洁地访问单例指针（例如 `TQMgr->create({...})`）。
// - 宏 `TQ(name)` 是对 `TQMgr->queue(name)` 的简写，用于获得指定名称的队列指针，
//   如果不存在会返回 nullptr。使用宏时请注意可空指针检查以避免解引用空指针。
