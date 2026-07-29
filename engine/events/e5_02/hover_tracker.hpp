// E5-02 懸停進出事件 — 平台中立介面（platform 相位 1 = Mac / null 期）
//
// 語意：滑鼠移入 / 移出 surface 時發送 hover enter / leave 事件，並追蹤目前懸停中的 surface。
// 滑鼠移動時依上游 E1-04 `HitTester::topmost_hit` 重新判定命中的最上層 surface，依前次懸停
// 狀態分派對應的 Enter / Leave（跨 surface 移動時先 Leave 舊、後 Enter 新；同一 surface 內
// 移動不重發 Enter，改發 Move）。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 滑鼠移動由呼叫端 / 測試**注入**（`inject_move(point)`），**不綁任何真實滑鼠 / 視窗系統
//     API**。真實後端於相位 2 只需在 OS 滑鼠移動回呼到達時呼叫同一個 inject_move 路徑。
//   - 命中判定完全委由上游 E1-04 `HitTester`；本單元不重新實作幾何邏輯。
//   - **NFR-02**：座標一律沿用 E1-04 的本地 / 相對座標（`LocalPoint`），命中優先沿用 E1-04
//     的具名圖層 + 宣告順序（無數字 z-order）。
//   - hit-test 回報 `HitStatus::Invalid`（形狀 / 座標無效）時**報錯不靜默**：`inject_move`
//     回傳 false、不改狀態、不分派事件。
#ifndef DS_EVENTS_E5_02_HOVER_TRACKER_HPP
#define DS_EVENTS_E5_02_HOVER_TRACKER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）：LocalPoint / HitSurface / HitTester / HitStatus

namespace ds::events {

// 訂閱代號。由 subscribe() 發出，供 unsubscribe() 使用。0 保留為無效值。
using SubscriptionId = std::uint64_t;

// 懸停事件種類。
enum class HoverEventKind {
    Enter,  // 移入：新懸停於某 surface（先前非該 surface，含先前完全無懸停）
    Leave,  // 移出：離開某 surface（含轉往其他 surface 前的 leave、或移到無命中處）
    Move,   // 停留：仍懸停於同一 surface 內移動（不重發 Enter）
};

// 一次懸停事件所攜帶的資料。純資料、平台中立、本地座標（NFR-02）。
struct HoverEvent {
    HoverEventKind kind = HoverEventKind::Enter;
    ds::kernel::SurfaceId surface{};  // 事件對應的 surface（具名 id，來自 E1-04）
    ds::kernel::LocalPoint point{};   // 觸發本事件的注入點（本地座標）
};

// 懸停事件回呼。訂閱者於事件分派時被呼叫。
using HoverListener = std::function<void(const HoverEvent&)>;

// ---------------------------------------------------------------------------
// HoverTracker —— 懸停進出事件：追蹤目前懸停 surface，依注入式滑鼠移動 + E1-04 命中判定
// 分派 Enter / Leave / Move。
//
// 語意保證：
//   - `add_surface(surface)` 依 `surface.id` 新增或覆蓋一筆參與命中測試的 surface
//     （新增回 true，覆蓋既有回 false）。
//   - `remove_surface(id)` 移除一筆 surface（回傳是否確有移除）；若移除的正是目前懸停中
//     的 surface，僅清除內部懸停狀態（不合成 Leave 事件——沒有真實座標可供該事件）。
//   - `inject_move(point)` 以 E1-04 `HitTester::topmost_hit` 對目前登記的 surfaces 判定命中：
//       - hit-test 回 `Invalid` → 回傳 false，不改狀態、不分派事件（報錯不靜默）。
//       - 命中與先前懸停同一 surface → 分派 Move（不重發 Enter）。
//       - 命中與先前懸停不同 surface → 若先前有懸停先分派 Leave（舊），再分派 Enter（新）。
///      - 未命中任何 surface → 若先前有懸停則分派 Leave 並清空懸停；先前本就無懸停則 no-op
//         （不分派任何事件——無命中處理）。
//   - 分派前先取訂閱快照：listener 於回呼中訂閱 / 解除訂閱不影響本輪、不破壞疊代。
//   - 多訂閱者依 SubscriptionId 遞增（即訂閱順序）分派，順序穩定可測。
// ---------------------------------------------------------------------------
class HoverTracker {
public:
    HoverTracker() = default;

    // 訂閱懸停事件。回傳非 0 訂閱代號；listener 為空時回傳 0（無效訂閱）。
    SubscriptionId subscribe(HoverListener listener);

    // 解除訂閱。回傳是否確實移除；未知 id 為 no-op 並回傳 false。
    bool unsubscribe(SubscriptionId id);

    // 新增 / 覆蓋一筆參與命中測試的 surface（依 `surface.id`）。新增回 true，覆蓋既有回 false。
    bool add_surface(ds::kernel::HitSurface surface);

    // 移除一筆 surface；回傳是否確有移除（未知 id 回 false）。
    bool remove_surface(const ds::kernel::SurfaceId& id);

    // 注入一次滑鼠移動（相位 1：平台中立，呼叫端 / 測試注入，無真實滑鼠 API）。
    // 依目前登記的 surfaces 以 E1-04 判定命中，並分派對應事件。詳見類別註解。
    // 回傳 false 表本次 hit-test 為 Invalid（形狀 / 座標無效）；此時不改狀態、不分派事件。
    bool inject_move(const ds::kernel::LocalPoint& point);

    // 查詢目前懸停的 surface；若目前無懸停則回傳 false（out 不動）。
    bool current_hover(ds::kernel::SurfaceId& out) const;

    // 目前訂閱者數量。
    std::size_t listener_count() const noexcept { return listeners_.size(); }

    // 目前登記的 surface 數量。
    std::size_t surface_count() const noexcept { return surfaces_.size(); }

private:
    void dispatch(const HoverEvent& event) const;

    // 以有序容器保存以保證分派順序穩定（依 SubscriptionId 遞增即訂閱順序）。
    std::map<SubscriptionId, HoverListener> listeners_;
    std::vector<ds::kernel::HitSurface> surfaces_;
    ds::kernel::HitTester tester_;

    bool has_hover_ = false;
    ds::kernel::SurfaceId hovered_{};
    SubscriptionId next_id_ = 1;  // 0 保留為無效
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_02_HOVER_TRACKER_HPP
