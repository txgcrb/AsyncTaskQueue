#include <iostream>
#include <memory>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

struct Task
{
    int id;
    Task(int i) : id(i) {}
    ~Task() { std::cout << "Task " << id << " destroyed\n"; }
};

std::queue<int> buffer;     // 共享缓冲区，存放生产者生产的数据。
std::mutex mtx;             // 互斥锁。确保同一时间只有一个线程能操作 buffer 或打印日志。
std::condition_variable cv; // 条件变量。这是线程间的“信号灯”，用于通知消费者“有货了”或者让消费者“先睡会儿”

void producer()
{

    for (int i = 0; i < 5; ++i)
    {
        std::unique_lock<std::mutex> lock(mtx);

        cv.wait(lock, []
                { return buffer.empty(); });
        buffer.push(i);
        std::cout << "Produced: " << i << std::endl;
        cv.notify_one(); // 唤醒一个等待的消费者
    }
}

void consumer()
{
    while (true)
    {
        // std::unique_lock：一个 RAII（资源获取即初始化）包装器
        std::unique_lock<std::mutex> lock(mtx);
        // 等待直到缓冲区不为空
        cv.wait(lock, []
                { return !buffer.empty(); });

        int data = buffer.front();
        buffer.pop();
        std::cout << "Consumed: " << data << std::endl;
        cv.notify_one();
        if (data == 4)
            break;
    }
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
// int main()
// {
//     // 1. 创建方式 (C++14 推荐使用 std::make_unique)
//     // std::unique_ptr<Task> p1(new Task(1));
//     // 这里的auto是std::unique_ptr<Task>
//     auto p1 = std::make_unique<Task>(1);

//     if (p1)
//         std::cout << "Task1 ID: " << p1->id << std::endl;
//     // 2. 移动所有权 (p1 变为空nullptr，p2 接管内存)
//     std::unique_ptr<Task> p2 = std::move(p1);

//     // 3. 访问成员
//     if (p2)
//         std::cout << "Task2 ID: " << p2->id << std::endl;
//     // 4. 重置 (手动释放内存)
//     p2.reset();

//     return 0;
// }