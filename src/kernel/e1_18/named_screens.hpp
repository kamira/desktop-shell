// E1-18 具名螢幕與每螢幕實例 — 平台中立介面
//
// 多螢幕環境下需要列舉螢幕、並為每一螢幕保存彼此獨立的狀態。此單元宣告一組平台中立
// 介面：以**具名 ScreenId**（而非數字索引）列舉螢幕，每一具名螢幕可掛上一份**獨立的
// 每螢幕實例狀態**（`PerScreen<T>`），並可與 E1-17 的每螢幕縮放搭配（例如以
// `PerScreen<double>` 保存各螢幕的 scale_factor）。
//
// 相位 1（Mac / null 期）約束：
//   - 只有介面 + 宣告式（null）行為，不綁任何真實平台後端，不真的查詢 OS。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支；跨平台性由 API 面約束
//     保證，不由語言保證。
//   - null 後端回傳內嵌的單一具名主螢幕；真實後端上線後由後端以實際列舉覆寫。
//
// 硬約束（NFR-02）：本介面**不得出現絕對座標與數字 z-order**。
//   - 螢幕一律以**具名識別碼**（`ScreenId`）指涉並列舉，不以 index / 位置 / (x,y) 指涉。
//   - 幾何一律以**具名角色 + 具名相對錨點**（`ScreenRole` / `ScreenAnchor`）表達，
//     不以像素座標表達；沒有 `setPosition(x, y)` 式 API。
//   - z-order 以具名角色（primary / secondary）表達，不以數字層級表達。
#ifndef DS_KERNEL_E1_18_NAMED_SCREENS_HPP
#define DS_KERNEL_E1_18_NAMED_SCREENS_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ds::kernel {

// 螢幕的穩定具名識別碼（如 "screen.primary" / "screen.external-hdmi"）。
//
// 刻意用具名字串而非數字 index 或座標：NFR-02 禁止以絕對座標 / 數字 z-order 指涉螢幕。
// 與 E1-17 的 `ScreenId` 為相同型別（`std::string` 別名），故兩單元的具名螢幕可互通。
using ScreenId = std::string;

// 螢幕的具名角色。以具名角色取代「數字 z-order / 主次層級索引」（NFR-02）。
enum class ScreenRole {
    Primary,    // 指定的主螢幕（每個非空拓撲概念上有一個主螢幕）
    Secondary,  // 其餘附加螢幕
};

// 螢幕在排列中的**具名相對錨點** —— 以具名方向表達幾何佈局，取代絕對 (x,y) 座標（NFR-02）。
//
// 只描述「相對於主螢幕在哪一側」，不含任何像素尺寸或位移量。真實後端上線後可由後端
// 依實際排列填入對應的具名錨點；核心永不看到絕對座標。
enum class ScreenAnchor {
    Center,  // 參考錨（通常即主螢幕自身）
    Left,
    Right,
    Above,
    Below,
};

// 單一具名螢幕的宣告。
//
// 幾何僅以具名角色 + 具名相對錨點表達 —— 純資料，不含任何平台判斷邏輯，亦不含任何
// 絕對座標或數字 z-order。
struct Screen {
    ScreenId id;           // 穩定具名識別碼（列舉與指涉的唯一鍵）
    std::string description;  // 人類可讀說明
    ScreenRole role;       // 具名角色（取代數字 z-order）
    ScreenAnchor anchor;   // 具名相對錨點（取代絕對座標）
};

// 具名螢幕的列舉 / 查詢介面。
//
// 建構來源有二：
//   - ScreenRegistry::defaults()：內嵌的預設拓撲（相位 1 的單一資料來源；單一具名主螢幕）。
//   - ScreenRegistry(screens)：由外部一組宣告建構（供測試，及未來由真實後端以實際列舉填入）。
//
// 列舉一律以**具名 ScreenId** 進行（`ids()` / `screens()` 依宣告順序），永不以數字 index
// 對外暴露螢幕身分。各螢幕彼此獨立：查詢某螢幕不受其他螢幕影響。
class ScreenRegistry {
public:
    // 由一組具名螢幕宣告建構。若同一 id 重複，後者覆蓋前者（後定義者為準；供後端覆寫）。
    explicit ScreenRegistry(std::vector<Screen> screens);

    // 內嵌預設拓撲（相位 1 唯一資料來源）：單一具名主螢幕 `screen.primary`，
    // 角色 Primary、錨點 Center。null 期不真的查詢 OS。
    static ScreenRegistry defaults();

    // 該具名螢幕是否存在於拓撲中。
    bool is_known(const ScreenId& id) const;

    // 查詢單一具名螢幕宣告；**未知螢幕回 nullptr（保守）**。指標於本物件存活期間有效。
    const Screen* find(const ScreenId& id) const;

    // 全部螢幕的**具名識別碼**（宣告順序）—— 對外的列舉入口，永不暴露數字 index。
    std::vector<ScreenId> ids() const;

    // 全部螢幕宣告（宣告順序）。
    const std::vector<Screen>& screens() const noexcept { return screens_; }

    // 螢幕數量。
    std::size_t size() const noexcept { return screens_.size(); }

    // 是否無任何螢幕。
    bool empty() const noexcept { return screens_.empty(); }

    // 主螢幕查詢（保守）：回傳第一個角色為 Primary 的螢幕；若無任何 Primary，退回第一個
    // 螢幕；**空拓撲回 nullptr**。以具名角色決定，不用數字層級。
    const Screen* primary() const;

    // 該具名螢幕是否為主螢幕。**未知螢幕回 false（保守）**。
    bool is_primary(const ScreenId& id) const;

    // 查詢某具名螢幕的錨點；**未知螢幕保守回 ScreenAnchor::Center**（中性參考錨）。
    ScreenAnchor anchor_of(const ScreenId& id) const;

private:
    std::vector<Screen> screens_;
};

// 每螢幕獨立實例狀態：為每一**具名螢幕**保存一份彼此獨立的 T 狀態。
//
// 以 ScreenId（具名）為鍵，永不以數字 index 為鍵。可與 E1-17 搭配：
// 例如 `PerScreen<double>` 保存各螢幕的縮放係數，`PerScreen<自訂狀態>` 保存各螢幕的
// 視窗佈局等。查詢未知螢幕一律保守（回 nullptr / 提供的 fallback），永不崩潰。
//
// 純資料容器、無平台分支、無絕對座標。標頭內定義（樣板）。
template <typename T>
class PerScreen {
public:
    // 設定某具名螢幕的狀態；同一 id 重複設定則覆蓋（後設定者為準）。
    void set(const ScreenId& id, T value) {
        for (auto& e : entries_) {
            if (e.first == id) {
                e.second = std::move(value);
                return;
            }
        }
        entries_.emplace_back(id, std::move(value));
    }

    // 該具名螢幕是否已有狀態。
    bool has(const ScreenId& id) const { return find(id) != nullptr; }

    // 查詢某具名螢幕的狀態；**未知回 nullptr（保守）**。指標於容器未被改動期間有效。
    const T* find(const ScreenId& id) const {
        for (const auto& e : entries_) {
            if (e.first == id) {
                return &e.second;
            }
        }
        return nullptr;
    }

    // 查詢某具名螢幕的狀態；**未知回呼叫端提供的 fallback（保守，呼叫端永遠安全）**。
    T get_or(const ScreenId& id, T fallback) const {
        const T* v = find(id);
        return v != nullptr ? *v : fallback;
    }

    // 移除某具名螢幕的狀態；回傳是否確有移除。
    bool erase(const ScreenId& id) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->first == id) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    // 全部具名鍵（設定順序）—— 列舉一律以具名 ScreenId。
    std::vector<ScreenId> ids() const {
        std::vector<ScreenId> out;
        out.reserve(entries_.size());
        for (const auto& e : entries_) {
            out.push_back(e.first);
        }
        return out;
    }

    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }
    void clear() { entries_.clear(); }

private:
    // 以具名鍵配對值；線性小容量（螢幕數量少），順序即設定順序。
    std::vector<std::pair<ScreenId, T>> entries_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_18_NAMED_SCREENS_HPP
