#include "task_queue_manager.h"
#include "task_queue.h"

namespace vi
{

    std::unique_ptr<TaskQueueManager> &TaskQueueManager::instance()
    {
        static std::unique_ptr<TaskQueueManager> _instance = nullptr;
        // 2. 一次性执行标志
        static std::once_flag ocf;
        // 3. 保证只初始化一次
        // 里使用 new 是为了绕过 std::make_unique 无法访问私有构造函数的限制
        std::call_once(ocf, []()
                       { _instance.reset(new TaskQueueManager()); });
        return _instance;
    }

    TaskQueueManager::TaskQueueManager()
    {
    }

    TaskQueueManager::~TaskQueueManager()
    {
        clear();
    }

    void TaskQueueManager::create(const std::vector<std::string> &nameList)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (const auto &name : nameList)
        {
            if (!exist(name))
            {
                m_queueMap[name] = TaskQueue::create(name);
            }
        }
    }

    void TaskQueueManager::clear()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queueMap.clear();
    }

    bool TaskQueueManager::exist(const std::string &name)
    {
        // find 的作用是寻找并返回一个迭代器（Iterator）,找到了，返回指向该元素的迭代器；没找到，返回 end()。
        return (m_queueMap.find(name) != m_queueMap.end());
    }

    bool TaskQueueManager::hasQueue(const std::string &name)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return exist(name);
    }

    TaskQueue *TaskQueueManager::queue(const std::string &name)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return exist(name) ? m_queueMap[name].get() : nullptr;
    }

}
