// content/profiles/c1_03/balloon_profile.hpp — C1-03 氣球 profile（角色對話氣球 / 氣泡通知）
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「氣球 profile」是角色說話的**對話氣球**（也適用一般氣泡通知）：掛在一個角色立繪
// （C1-02 `PortraitProfile`）旁，顯示一段**逐字顯示**的文字，存活一段（邏輯）時間後**自動
// 消失**，也可提早手動關閉。本單元不是新引擎邏輯，而是把三個已合併的擴充點**組裝**成單一
// 「氣球」應用 profile + 行為：
//
//   - E4-11（`ds::elements::TypewriterElement`）：**逐字顯示** —— 對話氣球的文字內容以打字機
//     效果漸次顯示，外部以注入式 `advance(dt)` 推進顯示進度。
//   - E1-14（`ds::kernel::TransientProfileManager`）：**短暫生命週期** —— 氣球建立時登記一次性
//     存活時限（ttl，邏輯 tick），時間到即自動過期消失；也可手動提早 `dismiss()`。時間全走
//     E5-10 `TimeoutTimer` 注入式 tick，不綁真實 OS 時鐘。
//   - E1-11（`ds::kernel::SubordinateLayout`）：**依附角色定位** —— 氣球以具名 anchor（E1-07）
//     相對其角色 surface 定位（預設「角色頭頂上方」），角色移動時氣球跟隨（每次 `resolve()`
//     皆以呼叫端提供的角色**當下**已解析矩形重新計算，不快取絕對座標）。
//
// 依附對象為 C1-02（`ds::profiles::PortraitProfile`）：`show_balloon()` 僅接受一個**已載入**
// 的立繪 profile 作為依附角色（未載入 → `Invalid`，無有效角色 surface 可依附）——本單元只讀取
// 其 `id()` / `is_loaded()`，不持有其生命週期、不修改其狀態。
//
// **既有上游命名碰撞（不可修改上游解決，見 content/profiles/c1_06 已記錄之相同手法）**：
// C1-02 經 E5-14 → E5-01 傳遞 `#include "hit_test.hpp"`（E1-04，於 `ds::kernel` 宣告
// `struct HitResult`）；E1-14 經 `#include "input_strategy.hpp"`（E1-02）於**同一**
// `ds::kernel` 命名空間宣告**另一個不同型別**的 `enum class HitResult`。兩者的標頭若同時
// `#include` 進同一翻譯單元會編譯失敗（本機以 g++ 實測重現）。因此本標頭**刻意不直接**
// `#include "transient_profile.hpp"`（E1-14）或 `"portrait_profile.hpp"`（C1-02）：
//   - 角色狀態改經 `character_bridge.hpp` 的中立前置宣告 + 純函式讀取（其 `.cpp` 才真正
//     `#include "portrait_profile.hpp"`，見該標頭說明）。
//   - E1-14 `TransientProfileManager` + 專屬 `TimeoutTimer` 的實際串接以 pimpl（`Impl`）隔離
//     於 `balloon_profile.cpp`（本單元唯一 `#include "transient_profile.hpp"` 之處）。
//   - 本標頭僅使用 `ds::events::Tick`（經 E4-11 → E4-09 → E5-04 標頭鏈透傳，非經 E1-14）表達
//     tick 增量與存活倒數查詢，不需引入 E1-14 標頭本身。
//
// 每個 `BalloonProfile` 實例代表**一個**具名氣球，內部自持三份服務（皆為純記憶體 / 純邏輯，
// 不需注入後端）：專屬的 E5-10 計時器 + 綁定其上的 E1-14 管理器（每顆氣球獨立計時，互不
// 干擾，天然支援「多氣球」同時顯示）、E1-11 附著記錄（僅記錄本氣球自身一筆）、E4-11 逐字
// 顯示元件（綁定注入式 `FontMetrics&`，承 E4-01 慣例，不取得其所有權）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02；定位以具名 anchor + 相對偏移表達，
// 時間以注入式 tick 表達，不觸真實時鐘）。空文字、未載入角色、ttl 為 0、重複顯示、未顯示時
// 收尾等一律明確回傳具名結果，不靜默。
#ifndef DS_CONTENT_PROFILES_C1_03_BALLOON_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_03_BALLOON_PROFILE_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "subordinate_layout.hpp"  // E1-11（上游，可讀不可改）：SubordinateLayout / AnchorStatus；
                                    //   經其標頭傳遞 E1-07 anchor_model.hpp（Anchor / AnchorSpec /
                                    //   Offset / Size / ResolvedPlacement）
#include "typewriter_element.hpp"  // E4-11（上游，可讀不可改）：TypewriterElement / Tick；經其
                                    //   標頭傳遞 E4-01 text_layout.hpp（FontMetrics /
                                    //   LayoutConstraints / LayoutResult）與 E4-09 → E5-04（
                                    //   ds::events::Tick，本標頭藉此取得 Tick 型別而不需引入
                                    //   E1-14 標頭）

namespace ds::profiles {

// C1-02 依附角色：前置宣告即足（show_balloon 僅取其 const 參考；完整定義由 .cpp 引入）。
class PortraitProfile;

// 氣球的具名生命週期狀態（NFR-02：具名，非數字）。
enum class BalloonState {
    Hidden,   // 未顯示：尚未 show_balloon，或已消失（逾時 / 手動 dismiss）。
    Showing,  // 顯示中：已依附角色、正逐字顯示、存活計時進行中。
};

const char* to_string(BalloonState s) noexcept;

// 顯示操作的具名結果 —— 結構化、不靜默（與上游 `AnchorStatus` / `PortraitStatus` 同風格）。
enum class BalloonStatus {
    Ok,             // 操作成功。
    Invalid,        // 前置條件不滿足：空 id、空文字、角色未載入、ttl 為 0、無效 anchor 等。
    AlreadyShowing, // 已顯示中再次 show_balloon（不靜默覆寫）；呼叫端須先 dismiss() 或等待逾時。
};

const char* to_string(BalloonStatus s) noexcept;

// ---------------------------------------------------------------------------
// BalloonProfile —— 角色對話氣球 / 氣泡通知 profile：組裝 E4-11 + E1-14 + E1-11（依附 C1-02）。
// ---------------------------------------------------------------------------
class BalloonProfile {
public:
    // metrics：字型度量（承 E4-01 慣例，不取得所有權；生命週期須涵蓋本物件）。
    // constraints：逐字顯示的排版約束（轉交 E4-11 / E4-01）。
    explicit BalloonProfile(std::string id, const ds::render::FontMetrics& metrics,
                             ds::render::LayoutConstraints constraints = {});

    // 解構：Impl（E1-14 服務）需完整定義才可銷毀，故於 .cpp 定義（pimpl 慣例）。
    ~BalloonProfile();

    BalloonProfile(const BalloonProfile&) = delete;
    BalloonProfile& operator=(const BalloonProfile&) = delete;

    // --- 顯示 / 消失 ---

    // 顯示一顆對話氣球，依附於 `character`（須已載入），存活 `ttl` 個 tick 後自動消失。
    //   - id 為空、text 為空（不靜默顯示空氣球）、ttl == 0 → Invalid。
    //   - character 未載入（`is_loaded()==false`）→ Invalid（無有效角色 surface 可依附）。
    //   - 已顯示中 → AlreadyShowing（不靜默覆寫；先 dismiss() 或等待逾時再重新顯示）。
    //   - spec 無效（越界 anchor / 非有限 offset）、或依附本身無效（如與角色同名造成自附）→
    //     Invalid，不改任何狀態。
    //   - 成功：E4-11 設定顯示文字（進度歸零）、E1-11 依附角色（具名 anchor + 相對偏移，預設
    //     角色頭頂上方）、E1-14 登記 ttl tick 存活計時 → Ok，state() 轉為 Showing。
    // spec 預設把氣球錨定在角色**頭頂上方**（TopCenter + 向上偏移），可由呼叫端覆寫。
    BalloonStatus show_balloon(const PortraitProfile& character, const std::string& text,
                                ds::events::Tick ttl,
                                const ds::kernel::AnchorSpec& spec = ds::kernel::AnchorSpec{
                                    ds::kernel::Anchor::TopCenter, ds::kernel::Offset{0.0f, -0.15f}});

    // 以邏輯時間增量 dt（tick）推進：先推進 E1-14 存活計時（可能觸發本次逾時自動消失），
    // 若消失後仍在顯示中才推進 E4-11 逐字顯示進度（消失當下不再推進文字）。未顯示中 → no-op。
    void advance(ds::events::Tick dt);

    // 手動提早結束顯示（E1-14 手動過期）：解除角色依附、逐字進度歸零、state() 轉為 Hidden。
    // 未顯示中 → false（no-op，不靜默）。成功 → true。
    bool dismiss();

    // --- 查詢 ---

    BalloonState state() const noexcept { return state_; }
    bool is_visible() const noexcept { return state_ == BalloonState::Showing; }
    const std::string& id() const noexcept { return id_; }

    // 目前依附的角色具名 SurfaceId；未顯示中回空字串。
    const std::string& anchor_parent() const noexcept { return parent_id_; }

    // 逐字顯示進度（E4-11 透傳）；未顯示中一律回 0 / true（無內容可顯示）。
    std::size_t visible_count() const;
    std::size_t total_count() const;
    bool is_text_complete() const;

    // 目前顯示內容的渲染描述（E4-11 `render_model()` 透傳）；未顯示中回空渲染描述。
    ds::render::LayoutResult render_model() const;

    // 距自動消失還需幾個 tick；未顯示中回 std::nullopt（透傳 E1-14 `remaining()`）。
    std::optional<ds::events::Tick> remaining() const;

    // 在給定「角色已解析矩形」與「氣球元件尺寸」下，解析氣球目前的絕對佈局（E1-11
    // `resolve_child` 透傳）——角色矩形每次呼叫皆可不同，藉此達成「角色移動氣球跟隨」，本物件
    // 不快取結果。未顯示中（未依附）→ Invalid（不寫 out）。
    ds::kernel::AnchorStatus resolve(const ds::kernel::ResolvedPlacement& character_placement,
                                     const ds::kernel::Size& balloon_size,
                                     ds::kernel::ResolvedPlacement& out) const;

private:
    // 逾時 / 手動到期共用的收尾：解除角色依附（E1-11）、逐字進度歸零（E4-11）、state() 轉
    // Hidden、清空 `parent_id_`。由 Impl 建構時登記的 E1-14 `on_expire` 回呼呼叫（見 .cpp）。
    void teardown_display();

    std::string id_;
    ds::kernel::SubordinateLayout layout_;        // E1-11：僅記錄本氣球自身一筆附著。
    ds::elements::TypewriterElement typewriter_;  // E4-11：逐字顯示（承注入式 FontMetrics）。
    std::string parent_id_;  // 目前依附的角色 SurfaceId；未顯示中為空。
    BalloonState state_ = BalloonState::Hidden;

    // pimpl：隱藏 E1-14 `TransientProfileManager` + 專屬 `TimeoutTimer`（理由見本檔頂部說明的
    // 上游命名碰撞）。完整定義於 balloon_profile.cpp。
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_03_BALLOON_PROFILE_HPP
