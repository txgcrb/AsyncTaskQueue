#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

#include "../task_queue.h"
#include "../task_queue_manager.h"

using namespace std::chrono_literals;

int main() {
    using vi::TaskQueue;

    std::cout << "===== demo_retry: 延迟重试任务示例 =====" << std::endl;

    TQMgr->create({"retry"});

    std::atomic<int> attempt{0};

    TaskQueue::RetryStrategy strategy;
    strategy.type = TaskQueue::RetryStrategy::Type::Exponential;
    strategy.base_delay_ms = 500; // 0.5s, 1s, 2s, 4s...

    TQ("retry")->postRetryTask(
        [&attempt]() -> bool {
            int n = ++attempt;
            std::cout << "[retry] attempt " << n << std::endl;
            // 模拟前几次失败，第 3 次成功
            if (n < 3) {
                std::cout << "[retry] failed, will retry..." << std::endl;
                return false;
            }
            std::cout << "[retry] success on attempt " << n << std::endl;
            return true;
        },
        /*max_retry=*/5,
        strategy);

    std::this_thread::sleep_for(6s);

    std::cout << "demo_retry finished." << std::endl;
    return 0;
}


