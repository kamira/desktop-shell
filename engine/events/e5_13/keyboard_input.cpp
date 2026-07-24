// E5-13 鍵盤與輸入提交事件 — 實作
//
// 純邏輯：兩條頻道的訂閱 / 分派。此檔不含任何平台分支或真實鍵盤後端。
#include "keyboard_input.hpp"

#include <utility>
#include <vector>

namespace ds::events {

SubscriptionId KeyboardInputSource::subscribe_key(KeyEventListener listener) {
    if (!listener) {
        return 0;  // 空 listener 為無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    key_listeners_.emplace(id, std::move(listener));
    return id;
}

SubscriptionId KeyboardInputSource::subscribe_text(TextInputListener listener) {
    if (!listener) {
        return 0;  // 空 listener 為無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    text_listeners_.emplace(id, std::move(listener));
    return id;
}

bool KeyboardInputSource::unsubscribe(SubscriptionId id) {
    // 兩頻道共用 id 空間，故 id 至多屬於其一。未知 / 無效 id 皆 no-op、回 false。
    if (key_listeners_.erase(id) > 0) {
        return true;
    }
    return text_listeners_.erase(id) > 0;
}

void KeyboardInputSource::inject_key(const KeyEvent& event) {
    // 先取快照，讓 listener 在回呼中訂閱 / 取消訂閱不影響本輪分派，
    // 也避免疊代中容器被改動導致未定義行為。依 SubscriptionId 遞增分派，順序穩定。
    std::vector<KeyEventListener> snapshot;
    snapshot.reserve(key_listeners_.size());
    for (const auto& kv : key_listeners_) {
        snapshot.push_back(kv.second);
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

void KeyboardInputSource::inject_text(const TextInputEvent& event) {
    std::vector<TextInputListener> snapshot;
    snapshot.reserve(text_listeners_.size());
    for (const auto& kv : text_listeners_) {
        snapshot.push_back(kv.second);
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

void KeyboardInputSource::inject_key_press(Key key, Modifiers mods) {
    inject_key(KeyEvent{key, mods, KeyAction::Press});
}

void KeyboardInputSource::inject_key_release(Key key, Modifiers mods) {
    inject_key(KeyEvent{key, mods, KeyAction::Release});
}

void KeyboardInputSource::submit_text(std::string text) {
    inject_text(TextInputEvent{std::move(text)});
}

}  // namespace ds::events
