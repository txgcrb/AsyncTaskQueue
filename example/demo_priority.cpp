#include <iostream>
#include <thread>
#include <chrono>

#include "../task_queue.h"
#include "../task_queue_manager.h"

using namespace std::chrono_literals;

int main() {
    using vi::TaskQueue;

    std::cout << "===== demo_priority: 任务优先级示例 =====" << std::endl;

    TQMgr->create({"priority"});

    auto q = TQ("priority");

    // 先提交一些低优先级任务
    for (int i = 0; i < 5; ++i) {
        q->postTask([i] {
            std::cout << "[priority] LOW  task " << i << std::endl;
        }, TaskQueue::TaskPriority::Low);
    }

    // 再提交一些普通优先级任务
    for (int i = 0; i < 5; ++i) {
        q->postTask([i] {
            std::cout << "[priority] NORMAL task " << i << std::endl;
        }, TaskQueue::TaskPriority::Normal);
    }

    // 最后提交一些高优先级任务
    for (int i = 0; i < 5; ++i) {
        q->postTask([i] {
            std::cout << "[priority] HIGH task " << i << std::endl;
        }, TaskQueue::TaskPriority::High);
    }

    std::this_thread::sleep_for(3s);

    std::cout << "demo_priority finished." << std::endl;
    return 0;
}


