#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

#include "../task_queue.h"
#include "../task_queue_manager.h"

using namespace std::chrono_literals;

int main() {
    using vi::TaskQueue;

    std::cout << "===== demo_periodic: 周期任务示例 =====" << std::endl;

    // 1. 创建任务队列
    TQMgr->create({"periodic"});

    // 2. 创建一个周期任务：每 1 秒打印一次
    std::atomic<int> counter{0};

    auto id1 = TQ("periodic")->postPeriodicTask([&counter]() {
        int c = ++counter;
        std::cout << "[periodic] tick, count = " << c << std::endl;
    }, 1000);

    // 3. 主线程等待一段时间后退出程序
    std::this_thread::sleep_for(5s);
     bool cancelled = TQ("periodic")->cancelTask(id1);
    std::cout << "cancel task1 result = " << std::boolalpha << cancelled << std::endl;
    std::this_thread::sleep_for(5s);

    std::cout << "demo_periodic finished." << std::endl;
    return 0;
}


