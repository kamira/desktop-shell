// E4-06 Surface 編號定址與切換 — engine 層 / render 子系統
//
// 語意：以**具名識別**定址並管理一組具名 surface（如多頁面 / 多狀態畫面）——依名稱註冊、
// 查詢 / 列舉已知的 surface、切換「目前顯示」的是哪一個，並在切換發生時通知監聽者。
//
// 本單元**不**建立 / 銷毀任何實體 surface（那是上游 K1 / E1-03 的職責），純粹管理「一組
// 具名 surface 之間，目前顯示哪一個」的狀態機——典型用途如多頁面 / 多狀態畫面之間的切換。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - surface 一律以**具名** `ds::kernel::SurfaceId`（字串）指涉——沿用上游 E1-24 的具名
//     identifier 型別（經 E4-02 `image_element.hpp` -> E1-03 `alpha_surface.hpp` ->
//     `null_backend.hpp` 一路 PUBLIC 傳遞），**不**新增一套數字 handle / index。
//   - `list()` 回傳依**註冊順序**排列的具名 id 清單；此順序僅表示「先後」，不是疊放層級 /
//     z-order，對外也從未暴露任何數字索引。
//   - 「切換」是純粹的具名狀態轉移（目前顯示哪一個具名 surface），不含任何座標 / 層級運算。
//
// 不靜默失敗：
//   - `register_surface`：空 id、或已註冊的 id → `SwitchStatus::Invalid`（不覆蓋既有註冊）。
//   - `switch_to` / `unregister_surface`：未知 id → `SwitchStatus::NotFound`（不變更狀態、
//     不崩潰）。
//
// 相位 1 平台中立：純記憶體邏輯，無 `#ifdef` / win32 / cocoa / 任何真實繪圖或 OS API。
#ifndef DS_RENDER_E4_06_SURFACE_SWITCHER_HPP
#define DS_RENDER_E4_06_SURFACE_SWITCHER_HPP

#include <cstddef>
#include <functional>
#include <vector>

#include "image_element.hpp"  // E4-02（上游，可讀不可改）：僅借道其 include 鏈傳遞
                               // ds::kernel::SurfaceId（經 alpha_surface.hpp / null_backend.hpp）；
                               // 本單元不使用 ImageElement 本身，只沿用既有具名 surface id 型別。

namespace ds::render {

// 操作結果碼 —— 與同子系統各 engine 層單元同精神：明確、不靜默。
enum class SwitchStatus {
    Ok,        // 操作成功
    Invalid,   // 前置條件不合法：空 id、重複註冊
    NotFound,  // 具名 id 未註冊（switch_to / unregister_surface 查無此 id）
};

// 切換通知監聽器：`(from, to)` 皆為具名 id。`from` 於「切換前尚無目前 surface」時為空字串。
using SwitchListener = std::function<void(const ds::kernel::SurfaceId& from,
                                          const ds::kernel::SurfaceId& to)>;

// ---------------------------------------------------------------------------
// SurfaceSwitcher —— 具名 surface 的定址簿 + 切換器。
//
// 管理一組具名 surface：註冊 / 移除、依名稱查詢 / 列舉、切換「目前顯示」的 surface、並在
// 切換發生時依註冊順序通知所有監聽者。純記憶體狀態，平台中立，不相依任何實體 surface
// 生命週期（呼叫端可自行決定是否搭配上游 K1 / E1-03 建立對應實體 surface）。
// ---------------------------------------------------------------------------
class SurfaceSwitcher {
public:
    SurfaceSwitcher() = default;

    // --- 註冊 / 移除 ---
    // 註冊一個具名 surface。空 id、或已註冊的 id → `Invalid`（不覆蓋、不部分套用）。
    SwitchStatus register_surface(const ds::kernel::SurfaceId& id);
    // 移除已註冊的具名 surface。未知 id → `NotFound`（不崩潰）。若移除的正是目前 surface，
    // 目前狀態回到「尚無目前 surface」（`has_current()` 轉 false）；此路徑**不**觸發
    // `on_switch` 通知——通知僅保留給明確的 `switch_to` 呼叫。
    SwitchStatus unregister_surface(const ds::kernel::SurfaceId& id);

    // --- 查詢 / 列舉 ---
    // 該具名 id 是否已註冊。
    bool has(const ds::kernel::SurfaceId& id) const;
    // 目前已註冊的 surface 數量。
    std::size_t count() const noexcept { return entries_.size(); }
    // 依註冊順序列舉已註冊 id（具名清單；順序僅表示註冊先後，非 z-order，NFR-02）。
    std::vector<ds::kernel::SurfaceId> list() const;

    // --- 切換 ---
    // 切換「目前顯示」的 surface 為指定具名 id。id 未註冊 → `NotFound`（不變更目前狀態、
    // 不觸發通知）。切到目前已是的 surface 仍視為成功，且仍會觸發一次 `on_switch` 通知
    // （呼叫端可用此重新整理畫面）。
    SwitchStatus switch_to(const ds::kernel::SurfaceId& id);
    // 是否已有目前 surface（建構後、或最近一次移除目前 surface 後，尚未 switch_to 過任何
    // surface 時為 false）。
    bool has_current() const noexcept { return has_current_; }
    // 目前 surface 的具名 id；`!has_current()` 時回傳空字串（明確表達「無目前值」，
    // 不回傳任意 / 前一次殘留的值）。
    const ds::kernel::SurfaceId& current() const noexcept { return current_; }

    // --- 切換通知 ---
    // 註冊一個切換監聽器。每次 `switch_to` 成功時，依監聽器**註冊順序**依序呼叫一次，
    // 帶入 `(from, to)`。空監聽器（無效 `std::function`）不予註冊。
    void on_switch(SwitchListener listener);
    // 目前已註冊的監聽器數量（供測試 / 內省）。
    std::size_t listener_count() const noexcept { return listeners_.size(); }

private:
    std::vector<ds::kernel::SurfaceId> entries_;   // 具名鍵，順序即註冊順序
    std::vector<SwitchListener> listeners_;        // 依註冊順序呼叫
    ds::kernel::SurfaceId current_;                // 空 = 尚無目前 surface
    bool has_current_ = false;

    std::vector<ds::kernel::SurfaceId>::iterator find(const ds::kernel::SurfaceId& id);
    std::vector<ds::kernel::SurfaceId>::const_iterator find(
        const ds::kernel::SurfaceId& id) const;
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_06_SURFACE_SWITCHER_HPP
