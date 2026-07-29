// E5-14 碰撞區域名事件參數 — 介面（engine 層 / 事件，platform 相位 1 = Mac / null 期）
//
// 語意：把命中的**具名碰撞區域**資訊帶入事件參數——當滑鼠事件（E5-01 `MouseEventRouter`）
// 命中某具名 surface 後，若該 surface 另外登記了一組 E1-05 `NamedRegionMap` 具名子區域，
// 就把該子區域的**具名 id + 參數**附加到派發給訂閱者的事件上，讓訂閱者知道「點了哪個具名
// 區域、帶什麼參數」。本單元**橋接** E5-01 事件路由 + E1-05 區域查詢，不重新實作任何幾何
// 或按鍵語意——兩者皆完全委派上游既有實作。
//
// 相位 1（Mac / null 期）硬約束：
//   - 純邏輯橋接，無真實視窗系統 / OS / 繪圖 API，不接真實滑鼠（事件仍由呼叫端 / 測試以
//     E5-01 的注入介面觸發，本單元轉呼）。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支。
//   - **NFR-02**：座標沿用 E1-04 `LocalPoint`（本地 / 相對），surface / 區域一律具名
//     （`SurfaceId` / 區域 `name` 皆為具名字串），無畫面絕對座標、無數字 z-order / index。
//   - 未命中任何具名子區域的事件**正常派發**（不附加區域參數，`region_hit=false`、
//     `region_name` 為空、`region_params` 為空字典）——報錯不靜默的另一面：不悄悄丟棄
//     「無區域資訊」這個合法情形，交由旗標明確表達。
//
// 建於上游之上（皆已合併，可讀不可改）：
//   - E5-01 `mouse_button_input.hpp`：`MouseButton` / `MouseAction` / `MouseButtonEvent` /
//     `MouseEventRouter` / `RouteStatus` / `SubscriptionId`——基本滑鼠按鍵事件的注入 /
//     命中路由 / 訂閱派發。
//   - E1-05 `named_region_map.hpp`：`RegionParams` / `RegionHit` / `NamedRegionMap`——具名
//     碰撞區域集合的登記 / 命中查詢。
#ifndef DS_EVENTS_E5_14_REGION_EVENT_DISPATCHER_HPP
#define DS_EVENTS_E5_14_REGION_EVENT_DISPATCHER_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mouse_button_input.hpp"  // E5-01（上游，可讀不可改）：MouseEventRouter 等
#include "named_region_map.hpp"    // E1-05（上游，可讀不可改）：NamedRegionMap / RegionHit / RegionParams

namespace ds::events {

// 橋接後的事件：E5-01 原始滑鼠事件 + E1-05 區域命中資訊。
//
// `region_hit == false`（未命中任一具名子區域、或該 surface 根本未登記任何具名子區域）時，
// `region_name` 為空字串、`region_params` 為空字典——事件仍正常派發，只是不帶區域參數。
struct RegionEvent {
    MouseButtonEvent mouse;              // 原始 E5-01 事件（button/action/position/click_count/target）
    bool region_hit = false;             // 是否命中具名子區域（E1-05 `NamedRegionMap::hit()` 語意）
    std::string region_name;             // 命中之具名子區域 id；未命中為空
    ds::kernel::RegionParams region_params;  // 命中子區域參數；未命中為空字典
};

// 事件回呼型別。
using RegionEventListener = std::function<void(const RegionEvent&)>;

// ---------------------------------------------------------------------------
// RegionEventDispatcher —— 橋接 E5-01 滑鼠事件路由 + E1-05 具名區域查詢。
//
// 內部持有一個 E5-01 `MouseEventRouter`（負責 surface 層級的命中 / 多擊 / 訂閱派發，完全
// 委派、不重造）與一組「具名 surface → E1-05 `NamedRegionMap`」對照表（本單元新增的橋接
// 狀態）。呼叫端一如使用 `MouseEventRouter`：`set_surfaces()` 設定參與命中測試的具名
// surface，`inject_*()` 注入按鍵動作；本類別在事件命中某 surface 後，額外以該 surface
// 登記的 `NamedRegionMap`（若有）對事件位置做子區域查詢，把結果併入 `RegionEvent` 再分派
// 給訂閱者（訂閱綁定特定 surface，語意同 E5-01）。
//
// 分派前一律取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
// （與 E5-01 `MouseEventRouter` / E5-13 `KeyboardInputSource` 同慣例）。
// ---------------------------------------------------------------------------
class RegionEventDispatcher {
public:
    RegionEventDispatcher() = default;

    // 轉呼 E5-01：設定參與命中測試的具名 surface 集合（整組取代）。命中優先序完全委由
    // E1-04（經 E5-01）`HitTester::topmost_hit()`，本類別不另加邏輯。
    void set_surfaces(std::vector<ds::kernel::HitSurface> surfaces);
    const std::vector<ds::kernel::HitSurface>& surfaces() const noexcept { return router_.surfaces(); }

    // 為指定具名 surface 設定（或取代）其具名子區域集合（E1-05 `NamedRegionMap`）。呼叫端
    // 負責在傳入前以 `NamedRegionMap::add_region()` 登記好子區域；本函式轉移持有。
    // `surface` 為空 → 不設定，no-op（無效具名）。
    void set_regions(const ds::kernel::SurfaceId& surface, ds::kernel::NamedRegionMap regions);

    // 該具名 surface 是否已登記子區域集合（不論集合內是否有任何區域）。
    bool has_regions(const ds::kernel::SurfaceId& surface) const;

    // 移除指定具名 surface 的子區域集合（之後該 surface 命中的事件不再附加區域參數，回落
    // 到「正常派發、無區域資訊」）。回傳是否確有移除；未知 surface 為 no-op，回 false。
    bool remove_regions(const ds::kernel::SurfaceId& surface);

    // 訂閱指定具名 surface 的橋接事件（`RegionEvent`）。`surface` 為空、或 `listener` 為空
    // → 回 0（無效訂閱，不佔用代號）。
    SubscriptionId subscribe(const ds::kernel::SurfaceId& surface, RegionEventListener listener);

    // 取消訂閱。回傳是否確有移除；未知 / 無效 id 為 no-op，回 false。
    bool unsubscribe(SubscriptionId id);

    // 指定 surface 目前的（本類別）訂閱者數。
    std::size_t listener_count(const ds::kernel::SurfaceId& surface) const;

    // 轉呼 E5-01 注入介面：語意與 `MouseEventRouter` 完全相同（命中判定 / 多擊追蹤皆委派
    // 內部 router），差別在於命中後會經橋接把 `RegionEvent`（含區域資訊）分派給本類別的
    // 訂閱者，而非直接把 `MouseButtonEvent` 分派給 E5-01 router 的訂閱者（本類別不對外
    // 暴露內部 router 的訂閱表）。
    RouteStatus inject_button(MouseButton button, MouseAction action,
                              const ds::kernel::LocalPoint& position);
    RouteStatus inject_down(MouseButton button, const ds::kernel::LocalPoint& position);
    RouteStatus inject_up(MouseButton button, const ds::kernel::LocalPoint& position);
    RouteStatus inject_click(MouseButton button, const ds::kernel::LocalPoint& position);

private:
    // 橋接核心：E5-01 router 命中並分派出一個 `MouseButtonEvent` 時呼叫（本類別在
    // `set_surfaces()` 時對每個具名 surface 訂閱此回呼）。以 `event.target` 對照的
    // `NamedRegionMap`（若有登記）查詢 `event.position`，組成 `RegionEvent` 並分派給本類別
    // 於該 surface 的訂閱者。
    void bridge(const MouseButtonEvent& event);

    // 分派 `RegionEvent` 給 `surface` 目前的訂閱者（依訂閱順序，分派前取快照）。
    void dispatch(const ds::kernel::SurfaceId& surface, const RegionEvent& event);

    // 重新對內部 router 訂閱橋接回呼：`set_surfaces()` 每次呼叫都整組取代 surface 清單，
    // 故先取消先前對 router 的全部內部訂閱，再對新清單裡每個具名 surface（去重）各訂閱一次
    // `bridge()`，確保每個具名 surface 命中時都會走到橋接邏輯（不論其是否登記了子區域）。
    void resubscribe_router(const std::vector<ds::kernel::HitSurface>& surfaces);

    MouseEventRouter router_;  // 完全委派 E5-01：surface 命中 / 多擊追蹤 / 底層訂閱派發

    // 具名 surface → 該 surface 的具名子區域集合（本單元新增的橋接狀態；未登記者查詢時
    // 視為「無子區域」，`RegionEvent::region_hit` 恆為 false）。
    std::map<ds::kernel::SurfaceId, ds::kernel::NamedRegionMap> regions_;

    // 本類別自身的訂閱表（`RegionEvent` 訂閱者），與內部 router 的訂閱表各自獨立。
    std::map<SubscriptionId, std::pair<ds::kernel::SurfaceId, RegionEventListener>> listeners_;
    SubscriptionId next_id_ = 1;

    // 目前對內部 router 訂閱之橋接回呼的代號集合（`set_surfaces()` 換手時用於清理舊訂閱）。
    std::vector<SubscriptionId> router_subscriptions_;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_14_REGION_EVENT_DISPATCHER_HPP
