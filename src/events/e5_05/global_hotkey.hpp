// E5-05 全域熱鍵註冊 — 平台中立介面
//
// 註冊系統全域熱鍵（不論前景視窗為何皆能觸發），按下時同步呼叫回呼。
// 這是**能力閘控項**（NFR-03）：`host.global_hotkey` 於 E1-21 能力矩陣宣告為
// optional 且 default_available=false（受平台 / 權限限制可能不存在），故呼叫端
// **必須先 has() 閘控**，has()==false 時走降級路徑、不得直接註冊。
//
// 熱鍵以**具名修飾鍵 + 具名鍵**描述（Modifier / Key），**不用平台掃描碼**——
// 跨平台語意一致，由 API 面約束保證，不由語言 / OS 保證。
//
// 相位 1（Mac / null 期）約束：
//   - 只有平台中立介面 + null 後端；不綁任何真實平台後端。
//   - 不得出現 `#ifdef _WIN32` / win32 / cocoa / RegisterHotKey 等平台分支或真實 OS 呼叫。
//   - null 後端不連真實 OS——熱鍵「按下」由測試以 inject() 手動注入以驗證分派路徑。
//     相位 2 換真實後端時，介面與分派語意一行不動，後端只需在 OS 熱鍵事件到達時
//     呼叫既有分派路徑。
#ifndef DS_EVENTS_E5_05_GLOBAL_HOTKEY_HPP
#define DS_EVENTS_E5_05_GLOBAL_HOTKEY_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

namespace ds::events {

// 修飾鍵。以位元旗標表示，可用 operator| 組合（如 Control | Shift）。
// 具名而非平台掃描碼；Alt 於 macOS 對應 Option、Meta 對應 Command / Win / Super。
enum class Modifier : std::uint8_t {
    None = 0,
    Control = 1u << 0,
    Alt = 1u << 1,    // macOS: Option
    Shift = 1u << 2,
    Meta = 1u << 3,   // macOS: Command；Windows: Win；Linux: Super
};

// 組合修飾鍵（Control | Shift）。
constexpr Modifier operator|(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<std::uint8_t>(a) |
                                 static_cast<std::uint8_t>(b));
}
constexpr Modifier operator&(Modifier a, Modifier b) {
    return static_cast<Modifier>(static_cast<std::uint8_t>(a) &
                                 static_cast<std::uint8_t>(b));
}
// set 是否含 m（m 可為單一或組合旗標）。
constexpr bool has_modifier(Modifier set, Modifier m) {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(m)) ==
           static_cast<std::uint8_t>(m);
}

// 具名鍵。**不是**平台掃描碼——是跨平台一致的邏輯鍵識別碼。
// Unknown 為無效鍵（保留為預設 / 無效熱鍵標記）。
enum class Key : std::uint16_t {
    Unknown = 0,
    // 字母
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // 數字（主鍵盤）
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // 功能鍵
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // 常用控制鍵
    Space, Tab, Enter, Escape, Backspace, Delete,
    Left, Right, Up, Down,
};

// 一組全域熱鍵：修飾鍵組合 + 主鍵。純資料、平台中立——不含任何 OS 原生型別。
struct Hotkey {
    Modifier modifiers = Modifier::None;
    Key key = Key::Unknown;

    // 主鍵須為具名有效鍵；Key::Unknown 視為無效熱鍵（不可註冊）。
    bool valid() const { return key != Key::Unknown; }
};

// 相等：修飾鍵組合與主鍵皆相同。
inline bool operator==(const Hotkey& a, const Hotkey& b) {
    return a.modifiers == b.modifiers && a.key == b.key;
}
inline bool operator!=(const Hotkey& a, const Hotkey& b) { return !(a == b); }

// 全序（供 std::map 鍵用）：先比修飾鍵位元、再比主鍵。無平台語意。
inline bool operator<(const Hotkey& a, const Hotkey& b) {
    if (a.modifiers != b.modifiers) {
        return static_cast<std::uint8_t>(a.modifiers) <
               static_cast<std::uint8_t>(b.modifiers);
    }
    return static_cast<std::uint16_t>(a.key) < static_cast<std::uint16_t>(b.key);
}

// 熱鍵觸發回呼。按下已註冊熱鍵時被呼叫，帶入該熱鍵以便單一回呼分辨多個註冊。
using HotkeyCallback = std::function<void(const Hotkey&)>;

// 註冊代號。由 register_hotkey() 發出，供 unregister() 使用。0 保留為無效值。
using HotkeyId = std::uint64_t;

// 全域熱鍵註冊的抽象介面。
//
// 相位 1 唯一實作為 NullGlobalHotkeys；相位 2 起可加入 win32 等真實後端，
// 各後端只需實作本介面並在 OS 熱鍵事件到達時呼叫既有分派路徑。
class GlobalHotkeys {
public:
    virtual ~GlobalHotkeys() = default;

    // NFR-03 能力閘控入口：本後端 / 平台是否支援全域熱鍵。
    // 呼叫端**必須**先 has()，false 時走降級路徑、不得呼叫 register_hotkey()。
    virtual bool has() const = 0;

    // 註冊一組全域熱鍵。成功回傳非 0 代號；下列情形回傳 0（無效）：
    //   - has()==false（能力不存在）
    //   - hotkey 無效（key == Key::Unknown）
    //   - callback 為空
    //   - hotkey 已被註冊（全域熱鍵具獨佔性，重複註冊視為衝突而拒絕）
    // 命名為 register_hotkey（`register` 為 C++ 保留字，不可作識別碼）。
    virtual HotkeyId register_hotkey(const Hotkey& hotkey,
                                     HotkeyCallback callback) = 0;

    // 解除註冊。回傳是否確實移除了一筆註冊；未知 id（含 0）為 no-op 並回傳 false。
    virtual bool unregister(HotkeyId id) = 0;
};

// null 後端參考實作。
//
// 不連任何真實 OS——熱鍵「按下」僅能由 inject() 手動注入（供測試與相位 1 契約驗證）。
// 以建構參數 available 模擬「平台是否支援全域熱鍵」：
//   - available==false（預設，對映 E1-21 host.global_hotkey 的 default_available）：
//     has() 回 false，register_hotkey() 一律回 0（能力不存在）——用以驗證閘控路徑。
//   - available==true：register_hotkey() 可正常註冊，inject(hotkey) 分派給對應回呼。
// 分派語意即為相位 2 真實後端須遵守的契約：獨佔註冊（重複熱鍵衝突）、
// 解除註冊後不再觸發、未知 id 解除為 no-op。
class NullGlobalHotkeys : public GlobalHotkeys {
public:
    explicit NullGlobalHotkeys(bool available = false);

    bool has() const override;
    HotkeyId register_hotkey(const Hotkey& hotkey,
                             HotkeyCallback callback) override;
    bool unregister(HotkeyId id) override;

    // 手動注入一次熱鍵按下，同步分派給該熱鍵的註冊回呼（若有）。
    // 這是 null 後端的觸發入口；真實後端改由 OS 熱鍵回呼觸發相同的分派。
    // 未註冊的熱鍵為 no-op、不崩潰。
    void inject(const Hotkey& hotkey);

    // 目前已註冊的熱鍵數量。
    std::size_t count() const;

    // 該熱鍵目前是否已註冊。
    bool is_registered(const Hotkey& hotkey) const;

private:
    struct Entry {
        Hotkey hotkey;
        HotkeyCallback callback;
    };

    bool available_;
    HotkeyId next_id_ = 1;  // 0 保留為無效
    std::map<HotkeyId, Entry> by_id_;       // id → 註冊項（分派 payload）
    std::map<Hotkey, HotkeyId> by_hotkey_;  // hotkey → id（獨佔性檢查 + inject 查找）
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_05_GLOBAL_HOTKEY_HPP
