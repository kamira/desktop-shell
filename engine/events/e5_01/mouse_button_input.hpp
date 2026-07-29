// E5-01 基本滑鼠按鍵事件 — 平台中立介面（platform 相位 1 = Mac / null 期）
//
// 語意：滑鼠按鍵事件（按下 / 放開 / 點擊）——左 / 中 / 右鍵、多擊（double / triple）、
// 以上游 E1-04 `HitTester` 命中測試判定事件落在哪個具名 surface、再把事件路由（dispatch）
// 給該 surface 的訂閱者。
//
// 本單元屬 engine 層（平台中立純邏輯），**不接真實滑鼠**：
//   - 事件不由真實 OS 滑鼠產生，而是由呼叫端 / 測試以 `inject_button()` / `inject_down()` /
//     `inject_up()` / `inject_click()` 注入 —— 完全可單元測試。
//   - 命中判定完全委由上游 E1-04 `HitTester::topmost_hit()`：具名圖層 + 宣告順序決定
//     命中優先（topmost），本單元不重新實作任何幾何邏輯。
//   - **NFR-02**：事件位置一律**元件本地 / 相對座標**（`ds::kernel::LocalPoint`），
//     非畫面絕對座標；路由目標一律**具名 SurfaceId**，無數字 handle / index。
//   - 相位 2 接真實滑鼠後端時，介面與分派語意一行不動，後端只需把 OS 滑鼠事件轉為
//     本地座標並呼叫既有 inject 路徑。
#ifndef DS_EVENTS_E5_01_MOUSE_BUTTON_INPUT_HPP
#define DS_EVENTS_E5_01_MOUSE_BUTTON_INPUT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）：LocalPoint / HitSurface / HitTester / TopmostHit

namespace ds::events {

// 滑鼠按鍵（具名，非平台原生按鍵碼）。
enum class MouseButton { Left, Middle, Right };

// 事件動作：按下 / 放開 / 點擊。Click 為 Down→Up 配對後的**合成事件**（見下方派發語意），
// 亦可經 `inject_button(button, MouseAction::Click, position)` 直接注入單次點擊
// （不影響 Down/Up 多擊追蹤狀態，click_count 固定為 1）。
enum class MouseAction { Down, Up, Click };

// 單一滑鼠按鍵事件。純資料、平台中立、本地座標（NFR-02）。
//
// `click_count`：1 = 單擊、2 = 雙擊、3 = 三擊……依連續同鍵 + 同命中 surface 的
// down/up 配對次數遞增（見 `MouseEventRouter` 類別註解的多擊追蹤規則）。
// `target`：經 E1-04 命中測試判定的具名 surface；未命中時為空（`SurfaceId{}`）。
struct MouseButtonEvent {
    MouseButton button = MouseButton::Left;
    MouseAction action = MouseAction::Down;
    ds::kernel::LocalPoint position{};
    std::size_t click_count = 1;
    ds::kernel::SurfaceId target;
};

// 事件回呼型別。
using MouseButtonListener = std::function<void(const MouseButtonEvent&)>;

// 訂閱代號。由 `subscribe()` 發出，供 `unsubscribe()` 使用。0 保留為無效值。
using SubscriptionId = std::uint64_t;

// 一次注入的路由結果 —— 報錯不靜默：呼叫端可分辨「命中並已路由」「未命中」「上游命中測試
// 判定形狀 / 座標無效」三種情形，不悄悄吞掉任何一種。
enum class RouteStatus {
    Hit,      // 命中一個具名 surface，事件已分派給該 surface 目前的訂閱者（訂閱者數可能為 0）
    NoHit,    // 未命中任何 surface：**不分派**給任何訂閱者（即使有訂閱者存在也收不到）
    Invalid,  // 上游 E1-04 判定參與命中測試的某 surface 形狀 / 座標無效
};

// ---------------------------------------------------------------------------
// MouseEventRouter —— 基本滑鼠按鍵事件的注入 / 命中路由 / 訂閱派發。
//
// 持有一組參與命中測試的具名 surface（`set_surfaces()`），以上游 E1-04 `HitTester`
// 對每次注入的按鍵動作做 topmost 命中判定，再把事件**只**分派給命中之 surface 目前的
// 訂閱者（依訂閱順序）；未命中不分派給任何人。
//
// 多擊（double / triple）追蹤規則（相位 1 無真實時間來源，故以**注入序列**而非計時器判定）：
//   - 內部保有單一「目前連續按鍵」游標（button + target + count）。
//   - Down：與游標的 button/target 相符 → count 遞增；不符（含游標未啟用）→ 開新游標，count=1。
//     以此次 count 作為本次 Down 事件的 `click_count`。
//   - Up：命中且與游標的 button/target 相符 → 以游標目前 count 作為本次 Up 事件（與隨後合成的
//     Click 事件）的 `click_count`，**游標保持啟用**（下一次同鍵同 surface 的 Down 可繼續遞增，
//     藉此構成雙擊 / 三擊）；不符（含未命中）→ 視為孤立事件，`click_count=1`，不合成 Click，
//     且**重置游標**（中斷任何進行中的多擊序列）。
//   - 任何不相符的 Down / Up（不同鍵或落在不同 surface）一律視為序列中斷，重置為新游標。
//
// 分派前一律取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
// （與 E5-13 `KeyboardInputSource` 同慣例）。
// ---------------------------------------------------------------------------
class MouseEventRouter {
public:
    MouseEventRouter() = default;

    // 設定參與命中測試的具名 surface 集合（整組取代）。命中優先序（topmost）完全委由
    // E1-04 `HitTester::topmost_hit()`：具名圖層 + 本容器內的宣告順序決定，本類別不另加邏輯。
    void set_surfaces(std::vector<ds::kernel::HitSurface> surfaces);
    const std::vector<ds::kernel::HitSurface>& surfaces() const noexcept { return surfaces_; }

    // 訂閱指定具名 surface 的滑鼠按鍵事件（Down/Up/Click 皆經此頻道送達，以 `event.action`
    // 分辨）。`target` 為空、或 `listener` 為空 → 回 0（無效訂閱，不佔用代號）。
    SubscriptionId subscribe(const ds::kernel::SurfaceId& target, MouseButtonListener listener);

    // 取消訂閱。回傳是否確有移除；未知 / 無效 id 為 no-op，回 false。
    bool unsubscribe(SubscriptionId id);

    // 指定 surface 目前的訂閱者數。
    std::size_t listener_count(const ds::kernel::SurfaceId& target) const;

    // 核心注入：於本地座標 `position` 對 `button` 執行 `action`。
    //   - `Down` / `Up`：先以 E1-04 `topmost_hit()` 判定命中 surface，依上方多擊追蹤規則
    //     決定 `click_count`，分派本事件；`Up` 若與游標相符，額外合成並分派一個
    //     `MouseAction::Click` 事件（`click_count` 同 Up）。
    //   - `Click`：直接以 `topmost_hit()` 判定命中並分派單一 Click 事件（`click_count=1`），
    //     **不**讀取或修改多擊追蹤游標——供呼叫端快速注入單次點擊而不牽動 Down/Up 狀態機。
    // 回傳 `RouteStatus` 說明本次注入的命中結果（報錯不靜默）。
    RouteStatus inject_button(MouseButton button, MouseAction action,
                              const ds::kernel::LocalPoint& position);

    // 便捷：等同 `inject_button(button, MouseAction::Down, position)`。
    RouteStatus inject_down(MouseButton button, const ds::kernel::LocalPoint& position);
    // 便捷：等同 `inject_button(button, MouseAction::Up, position)`。
    RouteStatus inject_up(MouseButton button, const ds::kernel::LocalPoint& position);

    // 便捷：注入一次完整點擊（同位置的 Down 緊接 Up），依 Down/Up 多擊追蹤規則正常運作
    // （連續呼叫可構成雙擊 / 三擊）。回傳 Up 階段（即含合成 Click）的最終 `RouteStatus`。
    RouteStatus inject_click(MouseButton button, const ds::kernel::LocalPoint& position);

private:
    // 目前進行中的連續按鍵游標（多擊追蹤狀態）。
    struct ClickCursor {
        bool active = false;
        MouseButton button = MouseButton::Left;
        ds::kernel::SurfaceId target;
        std::size_t count = 0;
    };

    // 以 E1-04 命中測試判定 `position` 命中的 surface；把結果轉為 RouteStatus + 具名 id。
    // 回傳 pair：{status, id}；status != Hit 時 id 為空。
    std::pair<RouteStatus, ds::kernel::SurfaceId> resolve_hit(
        const ds::kernel::LocalPoint& position) const;

    // 分派事件給 target 目前的訂閱者（依訂閱順序，分派前取快照）。target 為空則不分派。
    void dispatch(const ds::kernel::SurfaceId& target, const MouseButtonEvent& event);

    ds::kernel::HitTester tester_;  // 無狀態（E1-04 保證），可安全直接持有實例
    std::vector<ds::kernel::HitSurface> surfaces_;
    std::map<SubscriptionId, std::pair<ds::kernel::SurfaceId, MouseButtonListener>> listeners_;
    SubscriptionId next_id_ = 1;
    ClickCursor cursor_;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_01_MOUSE_BUTTON_INPUT_HPP
