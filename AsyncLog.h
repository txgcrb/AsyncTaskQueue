#ifndef __ASYNC_LOG__
#define __ASYNC_LOG__

#include <thread>
#include <condition_variable>
#include <mutex>

#include <iostream>
#include <any>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <utility>

namespace AsyncLog
{

    // 日志级别。数值越大通常表示日志越严重；当前版本只打印该数值，
    // 并没有根据级别过滤日志，也没有把级别转换为 DEBUG/INFO 等文本。
    enum LogLv
    {
        DEBUGS = 0,
        INFO = 1,
        WARN = 2,
        ERRORS = 3,
    };

    // 表示一条等待后台线程处理的日志任务。
    class LogTask
    {
    public:
        LogTask() {}
        LogTask(const LogTask &src) : _level(src._level), _logdatas(src._logdatas) {}
        LogTask(const LogTask &&src) : _level(src._level),
                                       _logdatas(std::move(src._logdatas)) {}

        // 一条日志由“级别 + 按调用顺序保存的参数”组成。
        // std::any 做类型擦除，让一个队列可以同时保存 int、string 等不同类型。
        LogLv _level;
        std::queue<std::any> _logdatas;
    };

    class AsyncLog
    {

    public:
        // Meyers Singleton：第一次写日志时才创建全局唯一实例。
        // C++11 保证函数内 static 的初始化是线程安全的。
        static AsyncLog &Instance()
        {
            static AsyncLog instance;
            return instance;
        }

        ~AsyncLog()
        {
            // 静态单例在进程正常退出时析构。先通知工作线程退出，再等待它结束，
            // 防止后台线程继续访问已经析构的成员。
            // 注意：当前 Stop() 会让线程立即退出，队列中尚未输出的日志会丢失。
            Stop();
            workthread.join();
            std::cout << "exit success" << std::endl;
        }

        template <typename T>
        std::any toAny(const T &value)
        {
            return std::any(value);
        }

        // 递归展开可变参数的旧写法。当前 AsyncWrite 使用 C++17 折叠表达式；
        // 如果要适配较早的语言标准，可在 AsyncWrite 中改用这个重载组。
   //利用可变参数模板递归展开，把任意数量、任意类型的参数逐个包装成 std::any 并存入 task->_logdatas
        template <typename Arg, typename... Args>
        void TaskEnque(std::shared_ptr<LogTask> task, Arg &&arg, Args &&...args)
        {
            task->_logdatas.push(std::any(arg));
            TaskEnque(task, std::forward<Args>(args)...);
        }

        template <typename Arg>
        void TaskEnque(std::shared_ptr<LogTask> task, Arg &&arg)
        {
            task->_logdatas.push(std::any(arg));
        }

        // 业务线程调用的核心入口。
        // 此处不做真正的输出，只把参数打包成任务并放进共享队列，因此调用方
        // 通常只承担参数复制和一次短暂加锁的开销，慢速输出由后台线程完成。
        template <typename... Args>
        void AsyncWrite(LogLv level, Args &&...args)
        {
            auto task = std::make_shared<LogTask>();

            // C++17 折叠表达式：从左到右把所有参数存入 std::any 队列。
            // 第一个参数约定为格式字符串，其余参数依次替换其中的 "{}"。
            // std::any 保存值（或退化后的指针），自定义类型不会自动支持格式化。
            (task->_logdatas.push(args), ...);
            // 不使用 C++17 时可改为：TaskEnque(task, args...);
            // TaskEnque(task, args...);
            task->_level = level;

            // 多个业务线程都可能写队列，push 和 size 判断要由同一把锁保护。
            std::unique_lock<std::mutex> lock(_mtx);
            _queue.push(task);

            // 队列原来为空时，后台线程可能正在休眠，需要唤醒它；原来非空时，
            // 消费者本来就会继续取任务，所以无需为每条日志都重复通知。
            bool notify = (_queue.size() == 1) ? true : false;
            lock.unlock();

            // 先解锁再通知，避免刚唤醒的线程立即阻塞在同一把互斥锁上。
            if (notify)
            {
                _empty_cond.notify_one();
            }
        }

        void Stop()
        {
            // 通知后台线程结束。
            // 学习提示：这里未在 _mtx 保护下写 _b_stop，而工作线程会并发读取它，
            // 严格来说存在数据竞争。生产代码应持锁修改，或使用 atomic<bool>。
            _b_stop = true;
            _empty_cond.notify_one();
        }

    private:
        AsyncLog() : _b_stop(false)
        {
            // 构造单例时启动唯一的消费者线程，它一直存活到单例析构。
            workthread = std::thread([this]()
                                     {
				for (;;) {
					std::unique_lock<std::mutex> lock(_mtx);

					// 必须用循环检查条件，因为 condition_variable 允许“虚假唤醒”。
					while (_queue.empty() && !_b_stop) {
						_empty_cond.wait(lock);
					}

					// 当前实现收到停止信号便立即退出，不会排空剩余任务。
					if (_b_stop) {
						return;
					}

					auto logtask = _queue.front();
                    _queue.pop();

					// 格式化和输出相对较慢，必须在锁外执行，否则会阻塞生产者入队。
					lock.unlock();
					processTask(logtask);
				} });
        }

        // 单例不允许复制或赋值，确保程序里只有一个队列和一个消费者线程。
        AsyncLog &operator=(const AsyncLog &) = delete;
        AsyncLog(const AsyncLog &) = delete;

        bool convert2Str(const std::any &data, std::string &str)
        {
            // std::any 擦除了静态类型，消费时需根据 type() 恢复具体类型。
            // 当前只支持以下六类；bool、long、自定义类型等会转换失败，该参数
            // 随后不会出现在最终日志中。
            std::ostringstream ss;//创建一个字符串流。
            if (data.type() == typeid(int))
            {
                ss << std::any_cast<int>(data);//把 std::any 里面的值取出来，并且当成 int。
            }
            else if (data.type() == typeid(float))
            {
                ss << std::any_cast<float>(data);
            }
            else if (data.type() == typeid(double))
            {
                ss << std::any_cast<double>(data);
            }
            else if (data.type() == typeid(std::string))
            {
                ss << std::any_cast<std::string>(data);
            }
            else if (data.type() == typeid(char *))
            {
                ss << std::any_cast<char *>(data);
            }
            else if (data.type() == typeid(char const *))
            {
                ss << std::any_cast<char const *>(data);
            }
            else
            {
                return false;
            }
            str = ss.str();
            return true;
        }

        void processTask(std::shared_ptr<LogTask> task)
        {
            // 本函数完全运行在后台消费者线程中。
            std::cout << "log level is " << task->_level << std::endl;

            if (task->_logdatas.empty())
            {
                return;
            }

            // 第一个参数被当作格式串（也允许传入受支持的其他类型）。
            auto head = task->_logdatas.front();
            task->_logdatas.pop();

            std::string formatstr = "";
            bool bsuccess = convert2Str(head, formatstr);
            if (!bsuccess)
            {
                return;
            }

            for (; !(task->_logdatas.empty());)
            {
                auto data = task->_logdatas.front();
                // 每取出一个参数，就替换下一个 "{}"；占位符不足则追加到末尾。
                formatstr = formatString(formatstr, data);
                task->_logdatas.pop();
            }

            std::cout << "log string is " << formatstr << std::endl;
        }

        template <typename... Args>
        std::string formatString(const std::string &format, Args... args)
        {
            std::string result = format;
            size_t pos = 0;

            // 从 pos 开始找下一个占位符，避免后续参数重复替换前面的内容。
            auto replacePlaceholder = [&](const std::string &placeholder, const std::any &replacement)
            {
                std::string str_replement = "";
                bool bsuccess = convert2Str(replacement, str_replement);
                if (!bsuccess)
                {
                    return;
                }
                // 查找并替换 "{}"
                size_t placeholderPos = result.find(placeholder, pos);
                if (placeholderPos != std::string::npos)
                {
                     // “替换 + 长度自适应”，不是 memcpy。
                    // 这里不使用 std::string::replace(pos, 2, str_replement) 的原因是，
                    // 如果 str_replement 比 2 长，replace 会把原来的 2 个字符覆盖掉，导致后续内容被截断。
                    result.replace(placeholderPos, placeholder.length(), str_replement);
                    pos = placeholderPos + str_replement.length();
                }
                else
                {
                    // 参数多于占位符时，不丢弃参数，而是用空格追加到末尾。
                    result = result + " " + str_replement;
                }
            };

            (replacePlaceholder("{}", args), ...);
            return result;
        }

        std::condition_variable _empty_cond;
        // 生产者和消费者之间共享的任务队列。
        std::queue<std::shared_ptr<LogTask>> _queue;
        bool _b_stop;
        // 保护 _queue；当前版本没有用它保护 _b_stop（见 Stop 注释）。
        std::mutex _mtx;
        // 唯一的后台消费者线程。
        std::thread workthread;
    };

    // 以下四个自由函数是推荐给调用方使用的便捷 API。
    // std::forward 会保留实参的左值/右值属性，再转交给通用写入接口。
    template <typename... Args>
    void ELog(Args &&...args)
    {
        AsyncLog::Instance().AsyncWrite(ERRORS, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void DLog(Args &&...args)
    {
        AsyncLog::Instance().AsyncWrite(DEBUGS, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void ILog(Args &&...args)
    {
        AsyncLog::Instance().AsyncWrite(INFO, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void WLog(Args &&...args)
    {
        AsyncLog::Instance().AsyncWrite(WARN, std::forward<Args>(args)...);
    }

}

#endif
