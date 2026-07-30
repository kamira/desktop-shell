// E5-11 可點區段選取事件 — 介面（engine 層 / 角色，platform 相位 1 = Mac / null 期）
//
// 語意：可點文字區段的**選取事件**——滑鼠點擊（E5-01 `MouseEventRouter`）落在可點文字區段
// （E4-12 `ClickableTextElement`）時，發出「該區段被選取」事件，帶區段具名 id。本單元**橋接**
// E5-01 點擊事件 + E4-12 區段命中查詢，不重新實作任何按鍵狀態機或幾何命中邏輯——兩者皆完全
// 委派上游既有實作（與 E5-14 `RegionEventDispatcher` 橋接兩個上游單元同慣例）。
//
// 觸發時機（僅 `MouseAction::Click`）：E5-01 對一次 down/up 配對會分派 Down、Up、以及（若與
// 多擊游標相符）合成的 Click 三個事件；`inject_click()` 亦可直接注入單一 Click 事件。本單元
// **只**對 `MouseAction::Click` 做區段命中判定並可能發出選取事件——Down / Up 屬按鍵狀態變化，
// 不代表「選取」這個使用者意圖已完成，避免同一次點擊重複判定 / 誤發。
//
// 相位 1（Mac / null 期）硬約束：
//   - 純邏輯橋接，無真實視窗系統 / OS / 繪圖 API，不接真實滑鼠（事件仍由呼叫端 / 測試以 E5-01
//     的注入介面觸發，本單元轉呼）。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支。
//   - **NFR-02**：座標沿用 E1-04 `LocalPoint`（本地 / 相對，經 E4-12 對字符幾何做命中），
//     surface / 區段一律具名（`SurfaceId` / 區段 id 皆為具名字串），無畫面絕對座標、無數字
//     z-order / index。
//   - **無區段命中的點擊不發選取事件**（規格明定）：命中 surface 但未落在任何已標記區段、或
//     該 surface 根本未綁定 `ClickableTextElement`、或事件未命中任何 surface —— 皆不分派選取
//     事件給任何訂閱者（不悄悄發一個「空區段 id」的事件）。
//
// 建於上游之上（皆已合併，可讀不可改）：
//   - E5-01 `mouse_button_input.hpp`：`MouseButton` / `MouseAction` / `MouseButtonEvent` /
//     `MouseEventRouter` / `RouteStatus` / `SubscriptionId`——基本滑鼠按鍵事件的注入 / 命中
//     路由 / 訂閱派發。
//   - E4-12 `clickable_text.hpp`：`ClickableTextElement`——可點文字區段的標記 / 排版 /
//     `hit_span(point)` 命中查詢（回傳區段具名 id 或 `std::nullopt`）。
#ifndef DS_EVENTS_E5_11_SPAN_SELECTION_DISPATCHER_HPP
#define DS_EVENTS_E5_11_SPAN_SELECTION_DISPATCHER_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "clickable_text.hpp"      // E4-12（上游，可讀不可改）：ClickableTextElement / hit_span
#include "mouse_button_input.hpp"  // E5-01（上游，可讀不可改）：MouseEventRouter 等

namespace ds::events {

// 橋接後的選取事件：E5-01 原始滑鼠事件（Click） + E4-12 命中之區段具名 id。
//
// 僅在「點擊命中某已綁定 `ClickableTextElement` 的 surface、且命中該元件的已標記區段」時才會
// 建構並分派本事件——`span_id` 恆非空（保證：不存在「無命中」的 `SpanSelectionEvent`）。
struct SpanSelectionEvent {
    MouseButtonEvent mouse;  // 原始 E5-01 事件（button/action=Click/position/click_count/target）
    std::string span_id;     // E4-12 `hit_span()` 命中之區段具名 id（恆非空）
};

// 事件回呼型別。
using SpanSelectionListener = std::function<void(const SpanSelectionEvent&)>;

// ---------------------------------------------------------------------------
// SpanSelectionDispatcher —— 橋接 E5-01 滑鼠點擊事件路由 + E4-12 可點文字區段命中查詢。
//
// 內部持有一個 E5-01 `MouseEventRouter`（負責 surface 層級的命中 / 多擊 / 訂閱派發，完全委派、
// 不重造）與一組「具名 surface → E4-12 `ClickableTextElement`」對照表（本單元新增的橋接狀態，
// **不取得元件所有權**——呼叫端須確保綁定期間元件生命週期有效，同 E4-12 對 `FontMetrics` 的
// 慣例）。呼叫端一如使用 `MouseEventRouter`：`set_surfaces()` 設定參與命中測試的具名 surface，
// `inject_*()` 注入按鍵動作；本類別在 `MouseAction::Click` 事件命中某已綁定 surface 後，額外以
// 該元件的 `hit_span()` 對事件本地座標做區段命中查詢，命中時把結果組成 `SpanSelectionEvent`
// 分派給訂閱者（訂閱綁定特定 surface，語意同 E5-01）；未命中則不分派（見檔頭說明）。
//
// 分派前一律取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
// （與 E5-01 `MouseEventRouter` / E5-14 `RegionEventDispatcher` 同慣例）。
// ---------------------------------------------------------------------------
class SpanSelectionDispatcher {
public:
    SpanSelectionDispatcher() = default;

    // 轉呼 E5-01：設定參與命中測試的具名 surface 集合（整組取代）。命中優先序完全委由 E1-04
    // （經 E5-01）`HitTester::topmost_hit()`，本類別不另加邏輯。
    void set_surfaces(std::vector<ds::kernel::HitSurface> surfaces);
    const std::vector<ds::kernel::HitSurface>& surfaces() const noexcept { return router_.surfaces(); }

    // 為指定具名 surface 綁定其可點文字區段來源（E4-12 `ClickableTextElement`）。**不取得所有
    // 權**：呼叫端須保證 `element` 的生命週期涵蓋綁定期間（同 E4-12 對外部 `FontMetrics` 的
    // 慣例）；重複呼叫以最後一次為準（整個取代，非疊加）。`surface` 為空 → 不設定，no-op。
    void bind_span_source(const ds::kernel::SurfaceId& surface,
                          const ds::elements::ClickableTextElement& element);

    // 該具名 surface 是否已綁定可點文字區段來源。
    bool has_span_source(const ds::kernel::SurfaceId& surface) const;

    // 解除指定具名 surface 的可點文字區段綁定（之後該 surface 命中的點擊不再判定區段選取，
    // 回落到「不分派選取事件」）。回傳是否確有移除；未知 surface 為 no-op，回 false。
    bool unbind_span_source(const ds::kernel::SurfaceId& surface);

    // 訂閱指定具名 surface 的區段選取事件（`SpanSelectionEvent`）。`surface` 為空、或
    // `listener` 為空 → 回 0（無效訂閱，不佔用代號）。
    SubscriptionId subscribe(const ds::kernel::SurfaceId& surface, SpanSelectionListener listener);

    // 取消訂閱。回傳是否確有移除；未知 / 無效 id 為 no-op，回 false。
    bool unsubscribe(SubscriptionId id);

    // 指定 surface 目前的（本類別）訂閱者數。
    std::size_t listener_count(const ds::kernel::SurfaceId& surface) const;

    // 轉呼 E5-01 注入介面：語意與 `MouseEventRouter` 完全相同（命中判定 / 多擊追蹤皆委派內部
    // router），差別在於 `MouseAction::Click` 事件命中已綁定 surface 且命中區段時，會額外分派
    // `SpanSelectionEvent` 給本類別的訂閱者（本類別不對外暴露內部 router 的訂閱表）。
    RouteStatus inject_button(MouseButton button, MouseAction action,
                              const ds::kernel::LocalPoint& position);
    RouteStatus inject_down(MouseButton button, const ds::kernel::LocalPoint& position);
    RouteStatus inject_up(MouseButton button, const ds::kernel::LocalPoint& position);
    RouteStatus inject_click(MouseButton button, const ds::kernel::LocalPoint& position);

private:
    // 橋接核心：E5-01 router 命中並分派出一個 `MouseButtonEvent` 時呼叫（本類別在
    // `set_surfaces()` 時對每個具名 surface 訂閱此回呼）。僅處理 `MouseAction::Click`；若該
    // surface 已綁定 `ClickableTextElement` 且 `hit_span(event.position)` 命中，組成
    // `SpanSelectionEvent` 分派給本類別於該 surface 的訂閱者；否則不分派（見檔頭說明）。
    void bridge(const MouseButtonEvent& event);

    // 分派 `SpanSelectionEvent` 給 `surface` 目前的訂閱者（依訂閱順序，分派前取快照）。
    void dispatch(const ds::kernel::SurfaceId& surface, const SpanSelectionEvent& event);

    // 重新對內部 router 訂閱橋接回呼：`set_surfaces()` 每次呼叫都整組取代 surface 清單，故先
    // 取消先前對 router 的全部內部訂閱，再對新清單裡每個具名 surface（去重）各訂閱一次
    // `bridge()`，確保每個具名 surface 命中時都會走到橋接邏輯（不論其是否綁定了區段來源）。
    void resubscribe_router(const std::vector<ds::kernel::HitSurface>& surfaces);

    MouseEventRouter router_;  // 完全委派 E5-01：surface 命中 / 多擊追蹤 / 底層訂閱派發

    // 具名 surface → 該 surface 綁定的可點文字區段來源（不取得所有權；未綁定者查詢時視為
    // 「無區段來源」，命中該 surface 的點擊恆不分派選取事件）。
    std::map<ds::kernel::SurfaceId, const ds::elements::ClickableTextElement*> elements_;

    // 本類別自身的訂閱表（`SpanSelectionEvent` 訂閱者），與內部 router 的訂閱表各自獨立。
    std::map<SubscriptionId, std::pair<ds::kernel::SurfaceId, SpanSelectionListener>> listeners_;
    SubscriptionId next_id_ = 1;

    // 目前對內部 router 訂閱之橋接回呼的代號集合（`set_surfaces()` 換手時用於清理舊訂閱）。
    std::vector<SubscriptionId> router_subscriptions_;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_11_SPAN_SELECTION_DISPATCHER_HPP
