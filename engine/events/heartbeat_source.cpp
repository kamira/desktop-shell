// E5-04 心跳事件 — 實作
//
// 純邏輯：以邏輯時間累計驅動週期性觸發，不含任何平台分支或真實計時器。
#include "heartbeat_source.hpp"

#include <utility>

namespace ds::events {

SubscriptionId HeartbeatSource::subscribe(Tick interval, HeartbeatCallback cb) {
    if (interval == 0) {
        return 0;  // 無效間隔：不建立訂閱
    }
    const SubscriptionId id = next_id_++;
    subs_.push_back(Subscription{id, interval, /*accumulator=*/0, /*count=*/0, std::move(cb)});
    return id;
}

bool HeartbeatSource::unsubscribe(SubscriptionId id) {
    for (auto it = subs_.begin(); it != subs_.end(); ++it) {
        if (it->id == id) {
            subs_.erase(it);
            return true;
        }
    }
    return false;
}

std::size_t HeartbeatSource::advance(Tick dt) {
    if (dt == 0) {
        return 0;
    }
    now_ += dt;

    // 兩階段派發：先結算所有訂閱的狀態並收集到期事件，再統一觸發回呼。
    // 如此回呼中若訂閱 / 取消訂閱，也不會使正在遍歷的 subs_ 失效。
    struct Pending {
        HeartbeatCallback cb;
        HeartbeatEvent ev;
    };
    std::vector<Pending> pending;

    for (auto& s : subs_) {
        s.accumulator += dt;
        while (s.accumulator >= s.interval) {
            s.accumulator -= s.interval;
            ++s.count;
            pending.push_back(Pending{
                s.cb,
                HeartbeatEvent{s.id, s.interval, now_, s.count}});
        }
    }

    for (auto& p : pending) {
        if (p.cb) {
            p.cb(p.ev);
        }
    }
    return pending.size();
}

}  // namespace ds::events
