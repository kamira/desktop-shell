// E5-12 全域指標手勢 — null 後端實作
//
// 平台中立的能力閘控 + 手勢訂閱 / 分派邏輯。此檔不含任何平台分支或真實後端。
#include "global_gesture.hpp"

#include <utility>
#include <vector>

namespace ds::events {

NullGlobalGestures::NullGlobalGestures(bool available) : available_(available) {}

NullGlobalGestures NullGlobalGestures::from_capability(
    const ds::kernel::CapabilityMatrix& matrix,
    const ds::kernel::CapabilityId& id) {
    // 能力矩陣為單一資料來源；未宣告能力回 false（保守），本後端因此不可用。
    return NullGlobalGestures(matrix.has(id));
}

bool NullGlobalGestures::has() const {
    return available_;
}

SubscriptionId NullGlobalGestures::subscribe(GestureType gesture,
                                             GestureListener cb) {
    // 能力閘控：不可用時一律拒絕訂閱、回 0（明確回報不可用）。
    // 這是降級契約的基石——呼叫端縱使漏查 has()，也不會建立一個永不觸發的假訂閱。
    if (!available_) {
        return 0;
    }
    // 空 listener 為無效訂閱：不佔用代號，回傳 0。
    if (!cb) {
        return 0;
    }
    const SubscriptionId id = next_id_++;
    entries_.emplace(id, Entry{gesture, std::move(cb)});
    return id;
}

bool NullGlobalGestures::unsubscribe(SubscriptionId id) {
    // 未知 id（含 0）為 no-op，回傳 false。
    return entries_.erase(id) > 0;
}

std::size_t NullGlobalGestures::listener_count() const {
    return entries_.size();
}

void NullGlobalGestures::inject(const Gesture& gesture) {
    // 降級路徑：能力不可用時注入為 no-op——不誤觸、不崩潰。
    if (!available_) {
        return;
    }
    // 依 SubscriptionId 遞增（即訂閱順序）分派，順序穩定。
    // 先複製一份符合手勢類型的訂閱快照，讓 listener 在回呼中訂閱 / 解除訂閱不影響本輪分派，
    // 也避免疊代中容器被改動導致未定義行為。
    std::vector<GestureListener> snapshot;
    snapshot.reserve(entries_.size());
    for (const auto& kv : entries_) {
        // 只分派給訂閱了「該手勢類型」的訂閱者。
        if (kv.second.gesture == gesture.type) {
            snapshot.push_back(kv.second.listener);
        }
    }
    for (const auto& listener : snapshot) {
        listener(gesture);
    }
}

}  // namespace ds::events
