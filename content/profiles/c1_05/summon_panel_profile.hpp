// content/profiles/c1_05/summon_panel_profile.hpp — C1-05 召喚面板 profile
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「召喚面板」（summon panel / command palette / spotlight）：以全域熱鍵叫出、用畢即收起的
// 臨時面板，內含一份可篩選、可選取的階層式選項清單。本單元不是新引擎邏輯，而是把四個
// 已合併的擴充點**組裝**成單一應用 profile：
//
//   - E1-14（`ds::kernel::TransientProfileManager`）：面板的短暫生命週期 ——
//     `open()` = `create()`（叫出，存活 `ttl` 個 tick）；`close()` / `select()` = `expire()`
//     （用畢即棄，或逾時自動收起）。
//   - E1-02（`ds::kernel::InputStrategy`）：面板顯示期間如何參與輸入 —— 獨占（`Capture`，
//     一般 spotlight 行為）或穿透（`ClickThrough`）等。本單元把該具名策略透傳給 E1-14
//     `TransientProfile.input`，並提供 `backend_input_policy()` / `hit_result()` 兩個組裝入口
//     直接呼叫 E1-02 的純函式（`to_backend_policy` / `hit_result`），供驗證組裝正確。
//   - E7-13（`ds::format::Item` / `build_forest`）：面板內的選項清單 —— 階層式項目樹，
//     可從宣告式文件（E7-01 `Value`）組裝，或程式化提供；`filter()` / `select()` 於整座
//     森林（含巢狀子項目）運作。
//   - E5-05（`ds::events::GlobalHotkeys`）：**事件驅動**的叫出入口 —— 把一個全域熱鍵綁定到
//     `open()`；熱鍵按下（真實後端）或 `inject()`（null 後端 / 測試）觸發時，面板自動叫出。
//     呼叫端須先以 `has()` 閘控（NFR-03）；本單元 `bind_hotkey()` 內再閘控一次。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。任何無效操作（重複開啟、對已關閉
// 面板篩選 / 選取、選取不存在 id、能力不存在時綁定熱鍵、重複綁定熱鍵）一律明確回傳
// false / 具名結果，不靜默。
//
// 已知限制（記錄備查，非本單元可修復）：E1-14 `TransientProfileManager::on_expire()` 沒有
// 提供移除回呼的 API。本物件解構時會強制 `close()`（若仍開啟）以確保其對應的短暫 profile
// 條目已從管理器移除，屆時本物件於管理器上登記的回呼即成為恆不再命中（依 id 過濾）的
// 死碼，不會在解構後被觸發。呼叫端仍須遵守前提：**不得**在本物件解構後、以相同 id 於
// 同一管理器重建新的 `SummonPanelProfile` 並期待兩者互不干擾。
#ifndef DS_CONTENT_PROFILES_C1_05_SUMMON_PANEL_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_05_SUMMON_PANEL_PROFILE_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "global_hotkey.hpp"       // E5-05（上游，可讀不可改）：GlobalHotkeys / Hotkey / HotkeyId
#include "item_tree.hpp"           // E7-13（上游，可讀不可改）：Item / ItemResult / ForestResult /
                                   //   build_forest（透傳 E7-01 Value / Document）
#include "transient_profile.hpp"  // E1-14（上游，可讀不可改）：TransientProfileManager /
                                   //   TransientProfile / TransientId / ExpiryReason
                                   //   （透傳 E1-02 InputStrategy / kDefaultStrategy /
                                   //    to_backend_policy / hit_result / route_decision，
                                   //    並透傳 E1-24 SurfaceProfile / SurfaceLayer 等）

namespace ds::profiles {

// 面板具名狀態（NFR-02：具名，非數字）。
enum class PanelState {
    Closed,  // 未叫出 / 已收起。
    Open,    // 叫出中，存活於 E1-14 短暫生命週期。
};

const char* to_string(PanelState s) noexcept;

// 面板關閉原因 —— 具名，供 on_close 回呼區分。部分收斂 E1-14 `ExpiryReason`，
// 額外多出 `Selected`（選取項目導致的關閉 —— E1-14 二態無法表達的產品語意）。
enum class CloseReason {
    Manual,    // 呼叫 close() 手動收起。
    Timeout,   // 逾時自動收起（E1-14 advance 推進跨過 ttl）。
    Selected,  // 選取了一個項目而收起（select() 成功）。
};

const char* to_string(CloseReason r) noexcept;

// select() 的具名結果。
enum class SelectResult {
    Selected,     // 成功選取，面板隨即關閉（CloseReason::Selected）。
    NotFound,     // 面板為開啟中，但整座森林內找不到該 id。
    PanelClosed,  // 面板目前未開啟，操作被拒（不得對已關閉面板選取）。
};

const char* to_string(SelectResult r) noexcept;

// ---------------------------------------------------------------------------
// SummonPanelProfile —— 召喚面板應用 profile：組裝 E1-14 + E1-02 + E7-13 + E5-05。
//
// 每個實例代表**一個**具名召喚面板（如 "panel.spotlight"）。注入式相依（皆不擁有其
// 生命週期，須比本物件活得久）：
//   - `ds::kernel::TransientProfileManager&`：本面板短暫生命週期的宿主。可與其他短暫
//     profile 共用同一管理器 —— 本物件內部以自身 id 過濾 on_expire 事件，不受干擾。
//   - `ds::events::GlobalHotkeys&`：本面板召喚熱鍵的宿主。
// ---------------------------------------------------------------------------
class SummonPanelProfile {
public:
    SummonPanelProfile(std::string id,
                        ds::kernel::TransientProfileManager& lifecycle,
                        ds::events::GlobalHotkeys& hotkeys,
                        ds::kernel::InputStrategy strategy = ds::kernel::InputStrategy::Capture);

    // 解構：若仍開啟，強制 close()（避免懸置於 E1-14 管理器上的計時器 / 回呼指向本已銷毀
    // 物件）；若曾綁定熱鍵，解除其註冊。
    ~SummonPanelProfile();

    SummonPanelProfile(const SummonPanelProfile&) = delete;
    SummonPanelProfile& operator=(const SummonPanelProfile&) = delete;

    // --- 組裝：選項清單（E7-13）---

    // 以一個宣告式 Value（須為 List，每元素為 E7-13 項目 Map）取代現有選項森林。
    // 建構失敗（結構違反）→ 回 false，**現有選項不動**（不得靜默覆寫壞資料）；
    // 失敗細節可經 last_build_error() 取得。
    bool set_items(const ds::format::Value& declarative_list);

    // 程式化提供選項森林（供測試 / 不走宣告式文件的呼叫端）。
    void set_items(std::vector<ds::format::Item> items);

    const std::vector<ds::format::Item>& items() const noexcept { return items_; }

    // 最近一次 set_items(Value) 失敗的原因；從未失敗過則 message 為空字串。
    const ds::format::BuildError& last_build_error() const noexcept { return last_build_error_; }

    // --- 組裝：召喚熱鍵（E5-05，事件驅動）---

    // 綁定一個全域熱鍵：按下（或 inject()）觸發時自動 open(summon_ttl)。
    //   - 已綁定過（須先 unbind_hotkey()）→ false（不靜默覆寫既有綁定）。
    //   - hotkeys.has() 為 false（能力不存在，NFR-03）→ false，不呼叫 register_hotkey()。
    //   - hotkey 無效、或該熱鍵已被別處佔用（底層 register_hotkey 回 0）→ false。
    bool bind_hotkey(const ds::events::Hotkey& hotkey, ds::events::Tick summon_ttl);

    // 解除目前綁定的召喚熱鍵；未曾綁定 → false（no-op）。
    bool unbind_hotkey();

    bool hotkey_bound() const noexcept { return hotkey_id_ != 0; }

    // --- 行為：open / filter / select / close（E1-14 生命週期 + E1-02 輸入策略）---

    // 叫出面板：於 lifecycle 登記一個一次性 ttl（tick）的短暫 profile（強制 Ephemeral，
    // 輸入策略取本物件建構時設定的 strategy）。
    //   - 已開啟中（尚未 close / select / 逾時）→ false（不靜默重開；呼叫端須先 close()）。
    //   - ttl == 0（或其他 E1-14 拒絕的情形）→ false（委派 E1-14）。
    bool open(ds::events::Tick ttl);

    // 手動收起面板（CloseReason::Manual）。未開啟 → false（no-op，不靜默）。
    bool close();

    // 依子字串（大小寫敏感）比對 id 或 label，於整座選項森林（含巢狀子項目）篩選。
    //   - 面板未開啟 → 回空（不得對已關閉面板操作）。
    //   - query 為空字串 → 回全部項目（前序攤平，含巢狀）。
    std::vector<const ds::format::Item*> filter(const std::string& query) const;

    // 依 id 於整座森林尋找並選取一個項目：
    //   - 面板未開啟 → PanelClosed，out 不動。
    //   - 找不到 → NotFound，out 不動，面板保持開啟。
    //   - 找到 → 觸發 on_select 回呼，面板隨即關閉（CloseReason::Selected，觸發 on_close），
    //     out（若非 null）指向該項目（於呼叫當下仍有效 —— 項目資料未被清除，僅生命週期收起）。
    SelectResult select(const std::string& item_id, const ds::format::Item** out = nullptr);

    // --- 查詢 ---
    PanelState state() const noexcept { return state_; }
    bool is_open() const noexcept { return state_ == PanelState::Open; }
    const std::string& id() const noexcept { return id_; }
    ds::kernel::InputStrategy strategy() const noexcept { return strategy_; }

    // E1-02 組裝入口：本面板設定策略對映的後端策略 / 命中結果（純函式透傳，供驗證組裝
    // 正確）。與面板目前是否開啟無關 —— 這是策略本身的靜態對映，非執行期狀態。
    ds::kernel::InputPolicy backend_input_policy() const noexcept;
    ds::kernel::InputHitResult hit_result() const noexcept;

    // --- 事件掛勾 ---
    void on_open(std::function<void()> cb);
    void on_close(std::function<void(CloseReason)> cb);
    void on_select(std::function<void(const ds::format::Item&)> cb);

private:
    // E1-14 過期回呼進入點：依 id 過濾（管理器可能共用），更新狀態並觸發 on_close。
    void handle_expiry(const ds::kernel::TransientId& expired_id,
                        ds::kernel::ExpiryReason reason);

    std::string id_;
    ds::kernel::TransientProfileManager& lifecycle_;
    ds::events::GlobalHotkeys& hotkeys_;
    ds::kernel::InputStrategy strategy_;

    std::vector<ds::format::Item> items_;
    ds::format::BuildError last_build_error_{};

    ds::events::HotkeyId hotkey_id_ = 0;
    ds::events::Tick summon_ttl_ = 0;

    PanelState state_ = PanelState::Closed;
    // select() 前置設為 Selected，供 handle_expiry 於「手動觸發」(非 Timeout) 時取用；
    // close() 前置設回 Manual。單執行緒同步呼叫下無競態（expire() 同步觸發回呼）。
    CloseReason pending_manual_reason_ = CloseReason::Manual;

    std::vector<std::function<void()>> on_open_;
    std::vector<std::function<void(CloseReason)>> on_close_;
    std::vector<std::function<void(const ds::format::Item&)>> on_select_;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_05_SUMMON_PANEL_PROFILE_HPP
