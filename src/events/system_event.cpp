// E5-08 系統事件 — null 後端實作
//
// 平台中立的訂閱 / 分派邏輯。此檔不含任何平台分支或真實後端。
#include "system_event.hpp"

#include <utility>
#include <vector>

namespace ds::events {

SubscriptionId NullSystemEventSource::subscribe(SystemEventListener listener) {
    // 空 listener 為無效訂閱：不佔用代號，回傳 0。
    if (!listener) {
        return 0;
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::move(listener));
    return id;
}

bool NullSystemEventSource::unsubscribe(SubscriptionId id) {
    // 未知 id（含 0）為 no-op，回傳 false。
    return listeners_.erase(id) > 0;
}

std::size_t NullSystemEventSource::listener_count() const {
    return listeners_.size();
}

void NullSystemEventSource::inject(const SystemEvent& event) {
    // 依 SubscriptionId 遞增（即訂閱順序）分派，順序穩定。
    // 先複製一份當前訂閱快照，讓 listener 在回呼中訂閱 / 解除訂閱不影響本輪分派，
    // 也避免疊代中容器被改動導致未定義行為。
    std::vector<SystemEventListener> snapshot;
    snapshot.reserve(listeners_.size());
    for (const auto& kv : listeners_) {
        snapshot.push_back(kv.second);
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

}  // namespace ds::events
