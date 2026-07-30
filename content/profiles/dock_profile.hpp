// content/profiles/c1_06/dock_profile.hpp — C1-06 Dock profile
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「Dock」（程式塢 / 工作列 / dock）：固定於螢幕某一邊緣的常駐列，滑到邊緣可（自動隱藏時）
// 叫出，內含可新增的固定項目（app / 捷徑圖示）。本單元不是新引擎邏輯，而是把三個已合併的
// 擴充點**組裝**成單一應用 profile：
//
//   - E1-16（`ds::kernel::EdgeHotZoneRegistry`，經本單元 `DockHotZoneBridge` 橋接）：
//     dock 固定邊緣的觸發熱區——`dock_to_edge()` 註冊該邊；`probe_hot_zone()` 以一次幾何
//     探測判定是否命中，命中且處於自動隱藏中的隱藏態則自動 `reveal()`。
//   - E1-01（`ds::kernel::LayerStack`）：dock 於堆疊上的**頂層**指派——固定成功時指派本
//     dock 至 `SurfaceLayer::Topmost`（NFR-02：具名圖層，非數字 z-order）；`undock()` 移除。
//   - E1-02（`ds::kernel::InputStrategy`）：dock 表面如何參與輸入（預設 `Interactive`）。
//     `backend_input_policy()` / `hit_result()` 直接透傳 E1-02 純函式，供驗證組裝正確
//     （與 C1-05 慣例一致）。
//
// **原上游命名碰撞——已於 CHG-20260730-02 解除**：E1-02（`input_strategy.hpp`）原於
// `ds::kernel` 宣告 `enum class HitResult`，與 E1-16（`edge_hot_zone.hpp` → 內部
// `#include "hit_test.hpp"` → E1-04）於同一命名空間宣告的 `struct HitResult` 同名不同型別，
// 兩標頭同時出現在同一翻譯單元會編譯失敗。CHG-20260730-02 已把 E1-02 的型別改名為
// `InputHitResult`，碰撞根因消除。`dock_hot_zone_bridge.hpp/.cpp`（把 E1-16 串接隔離到獨立
// 翻譯單元）因而不再是必要，僅作為本單元既有的內部間接層保留（移除屬獨立的可選清理）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。任何無效操作（重複固定、對未固定
// dock 設定自動隱藏 / 手動收合 / 探測熱區、無效邊、能力不存在時固定）一律明確回傳
// false / 具名結果，不靜默。
#ifndef DS_CONTENT_PROFILES_C1_06_DOCK_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_06_DOCK_PROFILE_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "dock_hot_zone_bridge.hpp"  // 本單元橋接層（不透明，安全與 E1-02 標頭共存）
#include "dock_types.hpp"            // DockEdge / DockPoint / DockScreenExtent / ...
#include "input_strategy.hpp"  // E1-02（上游，可讀不可改）：InputStrategy / InputPolicy / InputHitResult /
                                // to_backend_policy / hit_result
#include "layer_stack.hpp"  // E1-01（上游，可讀不可改）：LayerStack / SurfaceLayer / SurfaceId /
                             // LayerAssign / layer_capability

namespace ds::profiles {

// dock 的可見狀態（NFR-02：具名，非數字）。
enum class DockVisibility {
    Visible,
    Hidden,
};

const char* to_string(DockVisibility v) noexcept;

// dock_to_edge() 的具名結果。
enum class DockToEdgeResult {
    Ok,                     // 成功固定到邊緣，並於 E1-01 指派頂層。
    AlreadyDocked,          // 已固定，須先 undock()（不靜默重新固定）。
    InvalidEdge,            // 邊不屬於具名集合（委派 E1-16）。
    InvalidThickness,       // 厚度比例非有限、<= 0 或 > 1（委派 E1-16）。
    RejectedNoCapability,   // E1-01 layer 能力不存在（NFR-03）；不註冊熱區、不改任何狀態。
};

const char* to_string(DockToEdgeResult r) noexcept;

// reveal() 的具名結果。
enum class RevealResult {
    Revealed,       // 由隱藏轉為可見。
    AlreadyVisible,  // 已可見，no-op。
    NotDocked,      // 尚未固定邊緣，拒絕。
};

const char* to_string(RevealResult r) noexcept;

// add_item() 的具名結果。
enum class AddItemResult {
    Ok,
    DuplicateId,  // id 已存在，不靜默覆寫。
};

const char* to_string(AddItemResult r) noexcept;

// dock 上的一個固定項目（純資料）。本單元自有詞彙——三個上游擴充點皆不涵蓋此語意。
struct DockItem {
    std::string id;
    std::string label;
};

// ---------------------------------------------------------------------------
// DockProfile —— Dock（程式塢 / 工作列）應用 profile：組裝 E1-16 + E1-01 + E1-02。
//
// 每個實例代表**一個**具名 dock（如 "dock.main"）。注入式相依（不擁有其生命週期，須比本
// 物件活得久）：
//   - `ds::kernel::LayerStack&`：本 dock 頂層指派的宿主，可與其他 surface 共用同一堆疊。
// ---------------------------------------------------------------------------
class DockProfile {
public:
    explicit DockProfile(std::string id, ds::kernel::LayerStack& layer_stack,
                          ds::kernel::InputStrategy strategy = ds::kernel::InputStrategy::Interactive);

    // 解構：若仍固定，強制 undock()（避免 E1-01 堆疊上殘留指向本已銷毀物件語意的條目）。
    ~DockProfile();

    DockProfile(const DockProfile&) = delete;
    DockProfile& operator=(const DockProfile&) = delete;

    // --- 組裝：固定到邊緣（E1-16 + E1-01）---

    // 固定 dock 到具名邊緣：先以 E1-01 `has(layer_capability())` 閘控（NFR-03，未過不註冊
    // 熱區、不改狀態）；再委派 E1-16 註冊該邊的熱區（無效邊 / 厚度回對應結果，不固定）；
    // 成功後指派本 dock 至 E1-01 `SurfaceLayer::Topmost`。
    //   - 已固定中 → AlreadyDocked（不靜默重新固定；呼叫端須先 undock()）。
    DockToEdgeResult dock_to_edge(DockEdge edge, float thickness_ratio,
                                   std::string reveal_action = "dock.reveal");

    // 解除固定：移除 E1-01 頂層指派，重置自動隱藏 / 可見狀態。未固定 → false（no-op）。
    bool undock();

    bool is_docked() const noexcept { return docked_; }
    std::optional<DockEdge> docked_edge() const;

    // --- 行為：auto_hide / reveal / hide ---

    // 開關自動隱藏。未固定 → false（自動隱藏依附於已固定邊緣，無邊緣則無意義）。
    // 開啟時立即轉為 Hidden（觸發 on_hide）；關閉時立即轉回 Visible（觸發 on_reveal）。
    bool set_auto_hide(bool enabled);
    bool auto_hide() const noexcept { return auto_hide_; }

    // 手動叫出（可見）。未固定 → NotDocked；已可見 → AlreadyVisible（no-op）；
    // 否則轉為 Visible，觸發 on_reveal。
    RevealResult reveal();

    // 手動收合（隱藏）。未固定 → false；已隱藏 → false（no-op，不靜默重複）；
    // 否則轉為 Hidden，觸發 on_hide。
    bool hide();

    DockVisibility visibility() const noexcept { return visibility_; }

    // --- E1-16 熱區叫出：一次幾何探測，命中本 dock 固定邊緣的熱區時自動 reveal() ---
    //
    // 僅在「已固定 + 已啟用自動隱藏 + 目前隱藏中」才有意義；否則回 false（no-op，不誤觸發）。
    // 命中回傳 true（已自動 reveal）；未命中或命中非本 dock 固定之邊緣回 false。
    bool probe_hot_zone(const DockPoint& point, const DockScreenExtent& screen);

    // --- add_item ---

    // 新增一個固定項目；id 重複 → DuplicateId（不靜默覆寫既有項目）。
    AddItemResult add_item(std::string id, std::string label);
    const std::vector<DockItem>& items() const noexcept { return items_; }

    // --- E1-02 組裝入口（同 C1-05 慣例：純函式透傳，供驗證組裝正確）---
    ds::kernel::InputPolicy backend_input_policy() const noexcept;
    ds::kernel::InputHitResult hit_result() const noexcept;
    ds::kernel::InputStrategy strategy() const noexcept { return strategy_; }

    // --- 查詢 ---
    const std::string& id() const noexcept { return id_; }

    // --- 事件掛勾 ---
    void on_reveal(std::function<void()> cb);
    void on_hide(std::function<void()> cb);
    void on_item_added(std::function<void(const DockItem&)> cb);

private:
    void set_visibility_and_notify(DockVisibility v);

    std::string id_;
    ds::kernel::LayerStack& layer_stack_;
    ds::kernel::InputStrategy strategy_;

    DockHotZoneBridge hot_zones_;
    bool docked_ = false;
    std::optional<DockEdge> docked_edge_;

    bool auto_hide_ = false;
    DockVisibility visibility_ = DockVisibility::Visible;

    std::vector<DockItem> items_;

    std::vector<std::function<void()>> on_reveal_;
    std::vector<std::function<void()>> on_hide_;
    std::vector<std::function<void(const DockItem&)>> on_item_added_;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_06_DOCK_PROFILE_HPP
