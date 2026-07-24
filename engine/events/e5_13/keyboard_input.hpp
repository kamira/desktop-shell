// E5-13 鍵盤與輸入提交事件 — 平台中立介面
//
// 鍵盤按鍵事件（KeyEvent：具名鍵 + 修飾鍵 + press/release）與文字輸入提交
// （TextInputEvent：如就地輸入框 commit 的整段文字）的訂閱 / 派發介面。
//
// 本單元屬 engine 層（平台中立純邏輯），**不綁任何真實 OS 鍵盤**：
//   - 鍵以**具名鍵**（`Key`）表達，非平台掃描碼 / virtual-key code；跨平台語意一致。
//   - 事件不由真實鍵盤產生，而是由呼叫端 / 測試以 inject_*() 注入 —— 完全可單元測試。
//   - 相位 2 接真實後端時，介面與分派語意一行不動，後端只需在鍵盤事件到達時
//     把它轉為 `KeyEvent` / `TextInputEvent` 並呼叫既有 inject 路徑。
#ifndef DS_EVENTS_E5_13_KEYBOARD_INPUT_HPP
#define DS_EVENTS_E5_13_KEYBOARD_INPUT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace ds::events {

// 具名鍵。**非平台掃描碼**——跨平台語意一致的抽象鍵集合。
// 非窮舉，但涵蓋文字輸入與導覽常用鍵；未知鍵一律映為 Unknown。
enum class Key {
    Unknown = 0,

    // 字母（A–Z，與大小寫無關；大小寫由 Modifier::Shift 表達）
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // 數字列（0–9）
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // 編輯 / 控制鍵
    Enter, Escape, Backspace, Tab, Space, Delete, Insert,

    // 導覽鍵
    ArrowUp, ArrowDown, ArrowLeft, ArrowRight,
    Home, End, PageUp, PageDown,

    // 功能鍵（F1–F12）
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // 修飾鍵本身（作為被按下的鍵；其「按住」狀態另以 Modifiers 表達）
    ShiftKey, ControlKey, AltKey, MetaKey,
};

// 修飾鍵位元遮罩。Meta = Command / Super / Windows 鍵（跨平台統一語意）。
enum class Modifier : std::uint8_t {
    None    = 0,
    Shift   = 1u << 0,
    Control = 1u << 1,
    Alt     = 1u << 2,
    Meta    = 1u << 3,
};

// Modifier 的組合遮罩（同型別，位元或起來即多個修飾鍵同時按住）。
using Modifiers = Modifier;

// 位元運算：讓 Modifier 可如旗標般組合與查詢。
inline Modifier operator|(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<std::uint8_t>(a) |
                                 static_cast<std::uint8_t>(b));
}
inline Modifier operator&(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<std::uint8_t>(a) &
                                 static_cast<std::uint8_t>(b));
}
inline Modifier& operator|=(Modifier& a, Modifier b) {
    a = a | b;
    return a;
}

// 查詢 mods 是否含指定修飾鍵 flag。
inline bool has_modifier(Modifiers mods, Modifier flag) {
    return (static_cast<std::uint8_t>(mods) & static_cast<std::uint8_t>(flag)) != 0;
}

// 按鍵動作：按下 / 放開。
enum class KeyAction { Press, Release };

// 單一鍵盤事件。純資料、平台中立——不含任何 OS 原生型別或掃描碼。
struct KeyEvent {
    Key key;              // 具名鍵
    Modifiers modifiers;  // 觸發當下按住的修飾鍵遮罩
    KeyAction action;     // 按下 / 放開
};

// 文字輸入提交事件。承載一段已提交（commit）的文字，例如就地輸入框按 Enter 送出。
// text 為平台中立字串（UTF-8），可為空（例如提交空內容）。
struct TextInputEvent {
    std::string text;
};

// 事件回呼型別。
using KeyEventListener = std::function<void(const KeyEvent&)>;
using TextInputListener = std::function<void(const TextInputEvent&)>;

// 訂閱代號。由 subscribe_*() 發出，供 unsubscribe() 使用。0 保留為無效值。
// 兩個頻道（按鍵 / 文字）共用同一遞增 id 空間，故 unsubscribe(id) 永不歧義。
using SubscriptionId = std::uint64_t;

// 鍵盤與輸入提交事件來源。
//
// 提供兩條獨立頻道：按鍵事件（KeyEvent）與文字提交事件（TextInputEvent），
// 各自可訂閱 / 取消訂閱。事件由 inject_*() 注入後**同步**分派給該頻道所有訂閱者。
//
// 分派語意（相位 2 真實後端須遵守的契約）：
//   - 同頻道多訂閱者皆收，依訂閱順序（SubscriptionId 遞增）分派、順序穩定。
//   - 取消訂閱後不再收；未知 / 無效 id 取消為 no-op 並回傳 false。
//   - 空 listener 為無效訂閱，回傳 0、不佔用訂閱槽。
//   - 分派前取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB。
class KeyboardInputSource {
public:
    KeyboardInputSource() = default;

    // 訂閱按鍵事件。回傳非 0 訂閱代號；listener 為空回傳 0（無效訂閱）。
    SubscriptionId subscribe_key(KeyEventListener listener);

    // 訂閱文字提交事件。回傳非 0 訂閱代號；listener 為空回傳 0（無效訂閱）。
    SubscriptionId subscribe_text(TextInputListener listener);

    // 取消訂閱（自動判別頻道）。回傳是否確有移除；未知 / 無效 id 回 false。
    bool unsubscribe(SubscriptionId id);

    // 各頻道現存訂閱者數。
    std::size_t key_listener_count() const noexcept { return key_listeners_.size(); }
    std::size_t text_listener_count() const noexcept { return text_listeners_.size(); }

    // 注入一個按鍵事件，同步分派給所有按鍵訂閱者（依訂閱順序）。
    void inject_key(const KeyEvent& event);

    // 注入一個文字提交事件，同步分派給所有文字訂閱者（依訂閱順序）。
    void inject_text(const TextInputEvent& event);

    // 便捷注入：按下 / 放開一個具名鍵（可帶修飾鍵遮罩）。
    void inject_key_press(Key key, Modifiers mods = Modifier::None);
    void inject_key_release(Key key, Modifiers mods = Modifier::None);

    // 便捷注入：提交一段文字（等同 inject_text(TextInputEvent{text})）。
    void submit_text(std::string text);

private:
    // 以有序容器保存以保證分派順序穩定（依 SubscriptionId 遞增即訂閱順序）。
    std::map<SubscriptionId, KeyEventListener> key_listeners_;
    std::map<SubscriptionId, TextInputListener> text_listeners_;
    SubscriptionId next_id_ = 1;  // 兩頻道共用；0 保留為無效
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_13_KEYBOARD_INPUT_HPP
