#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

#include "../task_queue.h"
#include "../event.h"
#include "../task_queue_manager.h"

using namespace std;

int main()
{

    cout << "Hello World!" << endl;

    TQMgr->create({"worker1", "worker2", "worker3"});

    vi::Event ev;

    std::atomic<int> i{0};

    TQ("worker1")->postTask([&ev, &i]()
                            { cout << "exec task in 'worker1' queue: " << ", i = " << i++ << endl; });

    TQ("worker1")->postDelayedTask([&ev, &i]()
                                   { cout << "exec delayed task in 'worker1' queue: " << ", i = " << i++ << endl; }, 1000);

    TQ("worker2")->postDelayedTask([&ev, &i]()
                                   {
        cout << "exec delayed task in 'worker2' queue: " << ", i = " << i++ << endl;
        if (i == 3) {
            cout << "event is set" << endl;
            ev.set();
        } }, 2000);

    cout << "event wait start 1" << endl;
    ev.wait(vi::Event::kForever);
    cout << "event wait end 1" << endl;

    // 这里创建 一个新任务，重复执行三次，每次间隔1秒，即是没到三次时重新提交，先封装一个任务
    std::atomic<int> k{0};
    std::function<void()> task; // 显示声明
    task = [&ev, &k, &task]()
    {
        cout << "执行任务次数: " << ", k = " << ++k << endl;

        if (k < 3)
        {
            TQ("worker1")->postDelayedTask(task, 1000);
        }
        else
        {
            cout << "重复执行结束: " << ", k = " << k << endl;
            ev.set();
        }
    };

    TQ("worker1")->postDelayedTask(task, 1000);

    cout << "event wait start 2" << endl;
    ev.wait(vi::Event::kForever);
    cout << "event wait end 2" << endl;

    return 0;
}
