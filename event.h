#pragma once

#include <mutex>
#include <condition_variable>

namespace vi {

class Event {
public:
    static const int kForever = -1;

    Event();
    Event(bool manual_reset, bool initially_signaled);
    ~Event();

    void set();
    void reset();

    // Waits for the event to become signaled, but logs a warning if it takes more
    // than `warn_after_ms` milliseconds, and gives up completely if it takes more
    // than `give_up_after_ms` milliseconds. (If `warn_after_ms >=
    // give_up_after_ms`, no warning will be logged.) Either or both may be
    // `kForever`, which means wait indefinitely.
    //
    // Returns true if the event was signaled, false if there was a timeout or
    // some other error.
    bool wait(int give_up_after_ms, int warn_after_ms);

    // Waits with the given timeout and a reasonable default warning timeout.
    bool wait(int give_up_after_ms) {
        return wait(give_up_after_ms, give_up_after_ms == kForever ? 3000 : kForever);
    }

    // 说明（补充）：
    // - `Event` 是一个轻量级的线程同步原语，语义类似于 Windows 的 Event 对象。
    // - 构造参数 `manual_reset`：
    //     - true 表示手动复位模式（manual-reset）：调用 `set()` 后所有等待的线程都会被唤醒，
    //       状态保持为 signaled，直到显式调用 `reset()`。
    //     - false 表示自动复位模式（auto-reset）：当有线程被唤醒后，事件会自动复位为非 signaled，
    //       只允许一个等待线程通过（适合一次性通知）。
    // - `initially_signaled` 指定初始状态是否为已触发。
    // - `kForever` 表示无限等待（不超时）。
    // - `wait(give_up_after_ms, warn_after_ms)` 的两个超时参数有特别含义：
    //     - `warn_after_ms`：在超过该时间但未超过真正放弃时间时可用于打日志/告警，
    //       并不会直接导致返回 false（除非也超过了 `give_up_after_ms`）。
    //     - `give_up_after_ms`：真正的超时时间，超过该时间会返回 false 表示等待失败。
    //   这两个参数允许区分“警告等待很久”与“真正超时放弃”。

private:
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

private:
    std::mutex event_mutex_;
    std::condition_variable event_cond_;
    const bool is_manual_reset_;
    bool event_status_;
};

}
