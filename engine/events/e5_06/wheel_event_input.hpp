// E5-06 滾輪事件 — 平台中立介面（platform 相位 1 = Mac / null 期）
//
// 語意：滑鼠滾輪事件——垂直 / 水平捲動量（delta_x / delta_y），以上游 E1-04
// `HitTester::topmost_hit()` 判定事件落在哪個具名 surface，再把事件路由（dispatch）給
// 該 surface 目前的訂閱者。
//
// 本單元屬 engine 層（平台中立純邏輯），**不接真實滑鼠 / 真實滾輪硬體**：
//   - 事件不由真實 OS 滾輪產生，而是由呼叫端 / 測試以 `inject_wheel()` 注入 ——
//     完全可單元測試。
//   - 命中判定完全委由上游 E1-04 `HitTester::topmost_hit()`：具名圖層 + 宣告順序決定
//     命中優先（topmost），本單元不重新實作任何幾何邏輯（同 E5-01 慣例）。
//   - **NFR-02**：事件位置一律**元件本地 / 相對座標**（`ds::kernel::LocalPoint`），
//     非畫面絕對座標；路由目標一律**具名 SurfaceId**，無數字 handle / index。
//   - `delta_x` / `delta_y` 為呼叫端注入的捲動量（正負號、單位由呼叫端定義，本單元不
//     詮釋、不做任何裁切 / 正規化，原樣傳遞給訂閱者）；零值 delta 亦視為合法事件正常
//     路由（不特別過濾）。
//   - 相位 2 接真實滾輪後端時，介面與分派語意一行不動，後端只需把 OS 滾輪事件轉為
//     本地座標 + delta 並呼叫既有 `inject_wheel()` 路徑。
#ifndef DS_EVENTS_E5_06_WHEEL_EVENT_INPUT_HPP
#define DS_EVENTS_E5_06_WHEEL_EVENT_INPUT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）：LocalPoint / HitSurface / HitTester / TopmostHit

namespace ds::events {

// 訂閱代號。由 `subscribe()` 發出，供 `unsubscribe()` 使用。0 保留為無效值。
using SubscriptionId = std::uint64_t;

// 一次滾輪事件。純資料、平台中立、本地座標（NFR-02）。
//
// `delta_x` / `delta_y`：水平 / 垂直捲動量，原樣自 `inject_wheel()` 傳遞而來，本單元
// 不解讀正負號或單位意義（由呼叫端 / 上層元件依自身捲動慣例詮釋）。
// `target`：經 E1-04 命中測試判定的具名 surface；未命中時為空（`SurfaceId{}`）。
struct WheelEvent {
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    ds::kernel::LocalPoint position{};
    ds::kernel::SurfaceId target;
};

// 事件回呼型別。
using WheelEventListener = std::function<void(const WheelEvent&)>;

// 一次注入的路由結果 —— 報錯不靜默：呼叫端可分辨「命中並已路由」「未命中」「上游命中測試
// 判定形狀 / 座標無效」三種情形，不悄悄吞掉任何一種（同 E5-01 `RouteStatus` 慣例；獨立
// 具名以避免與 E5-01 同時納入時的型別衝突）。
enum class WheelRouteStatus {
    Hit,      // 命中一個具名 surface，事件已分派給該 surface 目前的訂閱者（訂閱者數可能為 0）
    NoHit,    // 未命中任何 surface：**不分派**給任何訂閱者（即使有訂閱者存在也收不到）
    Invalid,  // 上游 E1-04 判定參與命中測試的某 surface 形狀 / 座標無效
};

// ---------------------------------------------------------------------------
// WheelEventRouter —— 滾輪事件的注入 / 命中路由 / 訂閱派發。
//
// 持有一組參與命中測試的具名 surface（`set_surfaces()`），以上游 E1-04 `HitTester`
// 對每次注入的滾輪動作做 topmost 命中判定，再把事件**只**分派給命中之 surface 目前的
// 訂閱者（依訂閱順序）；未命中不分派給任何人。無狀態化多擊 / 游標追蹤（滾輪語意本身
// 無「配對」概念）——每次注入獨立判定、獨立分派。
//
// 分派前一律取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
// （與 E5-01 `MouseEventRouter` 同慣例）。
// ---------------------------------------------------------------------------
class WheelEventRouter {
public:
    WheelEventRouter() = default;

    // 設定參與命中測試的具名 surface 集合（整組取代）。命中優先序（topmost）完全委由
    // E1-04 `HitTester::topmost_hit()`：具名圖層 + 本容器內的宣告順序決定，本類別不另加邏輯。
    void set_surfaces(std::vector<ds::kernel::HitSurface> surfaces);
    const std::vector<ds::kernel::HitSurface>& surfaces() const noexcept { return surfaces_; }

    // 訂閱指定具名 surface 的滾輪事件。`target` 為空、或 `listener` 為空 → 回 0
    // （無效訂閱，不佔用代號）。
    SubscriptionId subscribe(const ds::kernel::SurfaceId& target, WheelEventListener listener);

    // 取消訂閱。回傳是否確有移除；未知 / 無效 id 為 no-op，回 false。
    bool unsubscribe(SubscriptionId id);

    // 指定 surface 目前的訂閱者數。
    std::size_t listener_count(const ds::kernel::SurfaceId& target) const;

    // 核心注入：於本地座標 `position` 注入一次垂直 / 水平捲動量 `delta_x` / `delta_y`。
    //   - 先以 E1-04 `topmost_hit()` 判定命中 surface；命中則以完整 `WheelEvent`
    //     （含 delta_x / delta_y / position / target）分派給該 surface 目前的訂閱者。
    //   - 未命中任何 surface → 不分派給任何人，回 `WheelRouteStatus::NoHit`。
    //   - 上游命中測試判定形狀 / 座標無效 → 回 `WheelRouteStatus::Invalid`，不分派。
    //   - `delta_x` / `delta_y` 皆為 0（零值捲動）亦視為合法事件，正常命中路由，
    //     不特別過濾（報錯不靜默、行為與非零 delta 一致）。
    WheelRouteStatus inject_wheel(float delta_x, float delta_y,
                                  const ds::kernel::LocalPoint& position);

private:
    // 以 E1-04 命中測試判定 `position` 命中的 surface；把結果轉為 WheelRouteStatus + 具名 id。
    std::pair<WheelRouteStatus, ds::kernel::SurfaceId> resolve_hit(
        const ds::kernel::LocalPoint& position) const;

    // 分派事件給 target 目前的訂閱者（依訂閱順序，分派前取快照）。target 為空則不分派。
    void dispatch(const ds::kernel::SurfaceId& target, const WheelEvent& event);

    ds::kernel::HitTester tester_;  // 無狀態（E1-04 保證），可安全直接持有實例
    std::vector<ds::kernel::HitSurface> surfaces_;
    std::map<SubscriptionId, std::pair<ds::kernel::SurfaceId, WheelEventListener>> listeners_;
    SubscriptionId next_id_ = 1;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_06_WHEEL_EVENT_INPUT_HPP
