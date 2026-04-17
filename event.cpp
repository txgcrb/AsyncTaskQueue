#include "event.h"
#include <optional>
#include <chrono>

namespace vi {

namespace {

using Clock      = std::chrono::steady_clock;
using TimePoint  = Clock::time_point;
using Millis     = std::chrono::milliseconds;

// 生成一个从现在起延迟指定毫秒数后的绝对时间点
inline TimePoint make_deadline(int milliseconds_from_now) {
    return Clock::now() + Millis(milliseconds_from_now);
}

}  // namespace

Event::Event() : Event(false, false) {

}

Event::Event(bool manual_reset, bool initially_signaled)
    : is_manual_reset_(manual_reset), event_status_(initially_signaled) {

}

Event::~Event() {
}

void Event::set() {
    std::unique_lock<std::mutex> lock(event_mutex_);
    event_status_ = true;
    event_cond_.notify_all();
}

void Event::reset() {
    std::unique_lock<std::mutex> lock(event_mutex_);
    event_status_ = false;
}

bool Event::wait(const int give_up_after_ms, const int warn_after_ms) {
    // 计算“告警时间点”：等待过久但未超时，用于日志或调试（目前仅用于控制等待阶段划分）
    const std::optional<TimePoint> warn_tp =
            warn_after_ms == kForever ||
            (give_up_after_ms != kForever && warn_after_ms > give_up_after_ms)
            ? std::nullopt
            : std::make_optional(make_deadline(warn_after_ms));

    // 计算“放弃时间点”：真正超时返回 false
    const std::optional<TimePoint> give_up_tp =
            give_up_after_ms == kForever
            ? std::nullopt
            : std::make_optional(make_deadline(give_up_after_ms));

    std::unique_lock<std::mutex> lock(event_mutex_);

    // 封装一次等待操作，根据是否有绝对超时时间点选择 wait 或 wait_until
    const auto wait_impl = [&](const std::optional<TimePoint>& deadline) {
        std::cv_status status = std::cv_status::no_timeout;
        while (!event_status_ && status == std::cv_status::no_timeout) {
            if (!deadline) {
                event_cond_.wait(lock);
            } else {
                status = event_cond_.wait_until(lock, *deadline);
            }
        }
        return status;
    };

    std::cv_status error;
    if (!warn_tp) {
        error = wait_impl(give_up_tp);
    } else {
        error = wait_impl(warn_tp);
        if (error == std::cv_status::timeout) {
            error = wait_impl(give_up_tp);
        }
    }

    // 自动复位事件：只有一个等待线程会继续执行，其它线程会看到未触发状态
    if (error == std::cv_status::no_timeout && !is_manual_reset_) {
        event_status_ = false;
    }

    return (error == std::cv_status::no_timeout);
}

}
