// E5-10 逾時計時器 — 實作
//
// 純邏輯：以邏輯時間倒數驅動逾時觸發，不含任何平台分支或真實計時器。
#include "timeout_timer.hpp"

#include <algorithm>
#include <utility>

namespace ds::events {

TimerId TimeoutTimer::add(Tick delay, bool repeating, TimerCallback cb) {
    if (delay == 0) {
        return 0;  // 無效逾時：不建立
    }
    const TimerId id = next_id_++;
    timers_.push_back(Timer{id, /*interval=*/repeating ? delay : 0, /*remaining=*/delay,
                            /*count=*/0, repeating, std::move(cb)});
    return id;
}

TimerId TimeoutTimer::set_timeout(Tick delay, TimerCallback cb) {
    return add(delay, /*repeating=*/false, std::move(cb));
}

TimerId TimeoutTimer::set_interval(Tick interval, TimerCallback cb) {
    return add(interval, /*repeating=*/true, std::move(cb));
}

bool TimeoutTimer::cancel(TimerId id) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->id == id) {
            timers_.erase(it);
            return true;
        }
    }
    return false;
}

std::optional<Tick> TimeoutTimer::remaining(TimerId id) const {
    for (const auto& t : timers_) {
        if (t.id == id) {
            return t.remaining;
        }
    }
    return std::nullopt;
}

bool TimeoutTimer::is_active(TimerId id) const noexcept {
    for (const auto& t : timers_) {
        if (t.id == id) {
            return true;
        }
    }
    return false;
}

std::size_t TimeoutTimer::advance(Tick dt) {
    if (dt == 0) {
        return 0;
    }
    now_ += dt;

    // 兩階段派發：先結算所有計時器狀態、收集到期事件與待移除的一次性計時器，
    // 再統一觸發回呼。如此回呼中若設定 / 取消計時器，也不會使正在遍歷的 timers_ 失效。
    struct Pending {
        TimerCallback cb;
        TimerEvent ev;
    };
    std::vector<Pending> pending;
    std::vector<TimerId> expired;  // 觸發完的一次性 timeout，本輪結束後移除

    for (auto& t : timers_) {
        Tick left = dt;
        bool done = false;
        while (left >= t.remaining) {
            left -= t.remaining;
            ++t.count;
            pending.push_back(Pending{
                t.cb,
                TimerEvent{t.id, now_, t.count, t.repeating}});
            if (!t.repeating) {
                expired.push_back(t.id);  // 一次性：觸發一次即結束，餘量丟棄
                done = true;
                break;
            }
            t.remaining = t.interval;  // 重複：重置週期，繼續消化 left
        }
        if (!done) {
            t.remaining -= left;  // 未到期部分保留為餘量（不漂移）
        }
    }

    // 先移除觸發完的一次性計時器，再觸發回呼 —— 使回呼中 is_active(該 id) 已回 false。
    if (!expired.empty()) {
        timers_.erase(
            std::remove_if(timers_.begin(), timers_.end(),
                           [&](const Timer& t) {
                               return std::find(expired.begin(), expired.end(), t.id) !=
                                      expired.end();
                           }),
            timers_.end());
    }

    for (auto& p : pending) {
        if (p.cb) {
            p.cb(p.ev);
        }
    }
    return pending.size();
}

}  // namespace ds::events
