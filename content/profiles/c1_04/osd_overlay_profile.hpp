// content/profiles/c1_04/osd_overlay_profile.hpp — C1-04 OSD 浮層 profile
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「OSD（On-Screen Display）浮層」：短暫顯示的通知 / 狀態浮層（音量 / 亮度調整提示、
// 剪貼簿變更提示等）——顯示數秒（邏輯 tick）後自動消失，不搶走使用者操作焦點。本單元
// 不是新引擎邏輯，而是把三個已合併的擴充點**組裝**成單一應用 profile：
//
//   - E1-14（`ds::kernel::TransientProfileManager`）：OSD 的短暫生命週期 ——
//     `show(message, ttl)` = `create()`（顯示，存活 `ttl` 個 tick）；`dismiss()` = `expire()`
//     （提早收起，或逾時自動消失）。
//   - E1-01（`ds::kernel::LayerStack`）：OSD 顯示期間**具名頂層**歸屬 —— 固定指派到
//     `SurfaceLayer::Overlay`（浮層，一般視窗之上）；`show()` 時 `assign()`，`dismiss()` /
//     逾時消失時 `remove()`。與 E1-14 `TransientProfile.surface.layer`（同樣強制 Overlay）
//     是兩件事——後者是 E1-24 四參數 profile 的欄位，前者是 E1-01 的堆疊維護器，本單元
//     兩者都組裝，示範一致性。
//   - E1-02（`ds::kernel::InputStrategy`）：OSD 顯示期間如何參與輸入 —— 慣例為
//     `ClickThrough`（點擊穿透，不擋下方操作），亦可設為其他三態。本單元把該具名策略
//     透傳給 E1-14 `TransientProfile.input`，並提供 `backend_input_policy()` / `hit_result()`
//     兩個組裝入口直接呼叫 E1-02 純函式，供驗證組裝正確。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。任何無效操作（重複顯示、對未顯示
// 的 OSD 更新內容 / 收起、E1-01 能力不可用時顯示）一律明確回傳 false，不靜默；失敗時
// 已完成的部分狀態會被回滾（show() 半途失敗不留殘留的 E1-14 短暫 profile 條目）。
#ifndef DS_CONTENT_PROFILES_C1_04_OSD_OVERLAY_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_04_OSD_OVERLAY_PROFILE_HPP

#include <functional>
#include <string>
#include <vector>

#include "layer_stack.hpp"        // E1-01（上游，可讀不可改）：LayerStack / SurfaceLayer /
                                   //   layer_name / LayerAssign
#include "transient_profile.hpp"  // E1-14（上游，可讀不可改）：TransientProfileManager /
                                   //   TransientProfile / TransientId / ExpiryReason
                                   //   （透傳 E1-02 InputStrategy / kDefaultStrategy /
                                   //    to_backend_policy / hit_result，並透傳 E1-24
                                   //    SurfaceProfile / SurfaceLayer 等）

namespace ds::profiles {

// OSD 具名狀態（NFR-02：具名，非數字）。
enum class OsdState {
    Hidden,   // 未顯示 / 已消失。
    Showing,  // 顯示中，存活於 E1-14 短暫生命週期。
};

const char* to_string(OsdState s) noexcept;

// OSD 消失原因 —— 具名，供 on_dismiss 回呼區分。與 E1-14 `ExpiryReason` 一一對應
// （本單元無額外產品語意需要擴充，故不像 C1-05 CloseReason 多出一態）。
enum class DismissReason {
    Manual,   // 呼叫 dismiss() 手動提早收起。
    Timeout,  // 逾時自動消失（E1-14 advance 推進跨過 ttl）。
};

const char* to_string(DismissReason r) noexcept;

// ---------------------------------------------------------------------------
// OsdOverlayProfile —— OSD 浮層應用 profile：組裝 E1-14 + E1-01 + E1-02。
//
// 每個實例代表**一個**具名 OSD（如 "osd.volume" / "osd.brightness"）。注入式相依（皆不
// 擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::TransientProfileManager&`：本 OSD 短暫生命週期的宿主。可與其他短暫
//     profile（含其他 OSD）共用同一管理器 —— 本物件內部以自身 id 過濾 on_expire 事件，
//     不受干擾。
//   - `ds::kernel::LayerStack&`：本 OSD 具名頂層歸屬的宿主，同樣可與其他 surface 共用。
// ---------------------------------------------------------------------------
class OsdOverlayProfile {
public:
    OsdOverlayProfile(std::string id,
                       ds::kernel::TransientProfileManager& lifecycle,
                       ds::kernel::LayerStack& layers,
                       ds::kernel::InputStrategy strategy = ds::kernel::InputStrategy::ClickThrough);

    // 解構：若仍顯示中，強制 dismiss()（避免懸置於 E1-14 管理器 / E1-01 圖層堆疊上的
    // 條目指向本已銷毀物件——與 C1-05 SummonPanelProfile 同樣的已知限制：E1-14
    // `on_expire()` 沒有移除回呼的 API，解構前主動收起是唯一手段）。
    ~OsdOverlayProfile();

    OsdOverlayProfile(const OsdOverlayProfile&) = delete;
    OsdOverlayProfile& operator=(const OsdOverlayProfile&) = delete;

    // --- 行為：show / update / dismiss（E1-14 生命週期 + E1-01 頂層 + E1-02 輸入策略）---

    // 顯示 OSD：於 lifecycle 登記一個一次性 ttl（tick）的短暫 profile（強制 Ephemeral，
    // 輸入策略取本物件建構時設定的 strategy，surface.layer 強制 Overlay），並於 layers
    // 指派本 OSD 到 `SurfaceLayer::Overlay`（E1-01 具名頂層）。
    //   - 顯示中（尚未 dismiss / 逾時）→ false（不靜默重顯；呼叫端須先 dismiss() 或等待
    //     逾時，或改用 update() 更新內容）。
    //   - ttl == 0（或其他 E1-14 拒絕的情形）→ false（委派 E1-14），不動 layers。
    //   - E1-14 成功但 layers.assign() 因能力不可用 / 空 id 而拒絕 → 回滾（expire 剛建立的
    //     短暫 profile 條目），回 false，不留殘留狀態（NFR-03 組裝一致性）。
    bool show(std::string message, ds::events::Tick ttl);

    // 更新顯示中 OSD 的內容（不改變逾時倒數）。未顯示 → false（no-op，不靜默）。
    bool update(std::string message);

    // 手動提早收起 OSD（DismissReason::Manual）。未顯示 → false（no-op，不靜默）。
    bool dismiss();

    // --- 查詢 ---
    OsdState state() const noexcept { return state_; }
    bool is_showing() const noexcept { return state_ == OsdState::Showing; }
    const std::string& id() const noexcept { return id_; }
    const std::string& message() const noexcept { return message_; }
    ds::kernel::InputStrategy strategy() const noexcept { return strategy_; }

    // E1-01 組裝入口：本 OSD 固定歸屬的具名頂層（靜態常數，與是否正在顯示無關）。
    ds::kernel::SurfaceLayer layer() const noexcept { return ds::kernel::SurfaceLayer::Overlay; }
    // 該頂層的穩定字串名（透傳 E1-01 `layer_name()`，如 "layer.overlay"）。
    std::string layer_name() const;
    // 本 OSD 目前是否已指派於 layers（顯示中應為 true；未顯示 / 解構後應為 false）。
    bool assigned_to_layer_stack() const;

    // E1-02 組裝入口：本 OSD 設定策略對映的後端策略 / 命中結果（純函式透傳，供驗證組裝
    // 正確）。與 OSD 目前是否顯示無關 —— 這是策略本身的靜態對映，非執行期狀態。
    ds::kernel::InputPolicy backend_input_policy() const noexcept;
    ds::kernel::HitResult hit_result() const noexcept;

    // --- 事件掛勾 ---
    void on_show(std::function<void(const std::string&)> cb);
    void on_update(std::function<void(const std::string&)> cb);
    void on_dismiss(std::function<void(DismissReason)> cb);

private:
    // E1-14 過期回呼進入點：依 id 過濾（管理器可能共用），更新狀態、自 layers 移除、
    // 觸發 on_dismiss。
    void handle_expiry(const ds::kernel::TransientId& expired_id,
                        ds::kernel::ExpiryReason reason);

    std::string id_;
    ds::kernel::TransientProfileManager& lifecycle_;
    ds::kernel::LayerStack& layers_;
    ds::kernel::InputStrategy strategy_;

    std::string message_;
    OsdState state_ = OsdState::Hidden;

    std::vector<std::function<void(const std::string&)>> on_show_;
    std::vector<std::function<void(const std::string&)>> on_update_;
    std::vector<std::function<void(DismissReason)>> on_dismiss_;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_04_OSD_OVERLAY_PROFILE_HPP
