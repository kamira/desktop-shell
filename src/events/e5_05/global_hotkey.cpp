// E5-05 全域熱鍵註冊 — null 後端實作
//
// 平台中立的註冊 / 分派邏輯。此檔不含任何平台分支或真實後端。
#include "global_hotkey.hpp"

#include <utility>

namespace ds::events {

NullGlobalHotkeys::NullGlobalHotkeys(bool available) : available_(available) {}

bool NullGlobalHotkeys::has() const { return available_; }

HotkeyId NullGlobalHotkeys::register_hotkey(const Hotkey& hotkey,
                                            HotkeyCallback callback) {
    // 能力不存在：即便被呼叫也拒絕，強化 NFR-03 閘控（呼叫端本應先 has()）。
    if (!available_) {
        return 0;
    }
    // 無效熱鍵（無主鍵）。
    if (!hotkey.valid()) {
        return 0;
    }
    // 空回呼為無效註冊。
    if (!callback) {
        return 0;
    }
    // 獨佔性：同一熱鍵已被註冊即為衝突，拒絕第二筆（既有註冊維持不變）。
    if (by_hotkey_.find(hotkey) != by_hotkey_.end()) {
        return 0;
    }
    const HotkeyId id = next_id_++;
    by_id_.emplace(id, Entry{hotkey, std::move(callback)});
    by_hotkey_.emplace(hotkey, id);
    return id;
}

bool NullGlobalHotkeys::unregister(HotkeyId id) {
    // 未知 id（含 0）為 no-op，回傳 false。
    const auto it = by_id_.find(id);
    if (it == by_id_.end()) {
        return false;
    }
    by_hotkey_.erase(it->second.hotkey);
    by_id_.erase(it);
    return true;
}

void NullGlobalHotkeys::inject(const Hotkey& hotkey) {
    const auto hit = by_hotkey_.find(hotkey);
    if (hit == by_hotkey_.end()) {
        return;  // 未註冊的熱鍵：no-op。
    }
    const auto eit = by_id_.find(hit->second);
    if (eit == by_id_.end()) {
        return;  // 一致性保險；正常不會發生。
    }
    // 先複製回呼再呼叫，讓回呼中解除自己的註冊不影響本次分派、也避免容器被改動。
    HotkeyCallback cb = eit->second.callback;
    cb(hotkey);
}

std::size_t NullGlobalHotkeys::count() const { return by_id_.size(); }

bool NullGlobalHotkeys::is_registered(const Hotkey& hotkey) const {
    return by_hotkey_.find(hotkey) != by_hotkey_.end();
}

}  // namespace ds::events
