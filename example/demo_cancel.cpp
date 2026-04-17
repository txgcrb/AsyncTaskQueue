#include <iostream>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "../task_queue.h"
#include "../task_queue_manager.h"

using namespace std::chrono_literals;

int main() {
    using vi::TaskQueue;

    std::cout << "===== demo_cancel: 任务取消示例 =====" << std::endl;

    TQMgr->create({"cancel"});

    std::mutex mtx;
    std::condition_variable cv;
    bool task2_done = false;

    // 提交两个可取消的延迟任务
    auto id1 = TQ("cancel")->postCancellableDelayedTask([] {
        std::cout << "[cancel] task1 executed (should NOT see this if cancelled)" << std::endl;
    }, 5000);

    auto id2 = TQ("cancel")->postCancellableDelayedTask([&] {
        std::cout << "[cancel] task2 executed" << std::endl;
        {
            std::lock_guard<std::mutex> lock(mtx);
            task2_done = true;
        }
        cv.notify_one();
    }, 1000);

    std::cout << "submitted two cancellable tasks, id1=" << id1 << ", id2=" << id2 << std::endl;

    // 1 秒后取消第一个任务
    std::this_thread::sleep_for(1s);
    bool cancelled = TQ("cancel")->cancelTask(id1);
    std::cout << "cancel task1 result = " << std::boolalpha << cancelled << std::endl;

    // 等待 task2 执行完成
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, 10s, [&]{ return task2_done; })) {
            std::cout << "WARNING: task2 did not complete within timeout!" << std::endl;
        }
    }

    std::cout << "demo_cancel finished." << std::endl;
    return 0;
}


