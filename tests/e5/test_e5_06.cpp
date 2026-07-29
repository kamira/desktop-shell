// E5-06 滾輪事件 — 單元測試（gtest）
//
// 驗證 WheelEventRouter 於相位 1（Mac / null 期）的注入式滾輪事件：
//   - 垂直滾動（delta_y）/ 水平滾動（delta_x）各自正確傳遞給訂閱者
//   - delta_x / delta_y 原樣傳遞（含負值、含混合水平+垂直）
//   - 經 E1-04 HitTester::topmost_hit() 命中路由：只分派給命中之 surface 的訂閱者，
//     具名圖層決定 topmost，未命中不分派給任何人
//   - 上游命中測試判定形狀無效 → WheelRouteStatus::Invalid（報錯不靜默）
//   - 多訂閱者依訂閱順序皆收
//   - 訂閱取消後不再收
//   - 零 delta（delta_x=0, delta_y=0）仍視為合法事件，正常命中路由
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實滑鼠 / 滾輪硬體後端，
// 事件皆為注入式。
#include "wheel_event_input.hpp"

#include <gtest/gtest.h>

#include <vector>

using ds::events::SubscriptionId;
using ds::events::WheelEvent;
using ds::events::WheelEventListener;
using ds::events::WheelEventRouter;
using ds::events::WheelRouteStatus;

using ds::kernel::AlphaMode;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_circle;
using ds::kernel::make_rect;
using ds::kernel::SurfaceLayer;

namespace {

// 一個 10x10 本地座標的不透明矩形 surface（覆蓋點 (5,5)）；同 e1_04 / e5_01 測試慣例。
HitSurface MakeOpaqueRect(const char* id, SurfaceLayer layer = SurfaceLayer::Normal) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(10.0f, 10.0f);
    s.layer = layer;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

// 一個 2x2 本地座標的不透明矩形 surface（**不**覆蓋點 (5,5)）——供「未命中此 surface」情境。
HitSurface MakeSmallOpaqueRect(const char* id) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(2.0f, 2.0f);
    s.layer = SurfaceLayer::Normal;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

// 一個以 center 為圓心、radius 為半徑的不透明圓形 surface——供「與 10x10 矩形（原點附近）
// 不重疊的另一個位置」情境，讓兩個 surface 可各自被獨立的點命中。
HitSurface MakeOpaqueCircle(const char* id, LocalPoint center, float radius) {
    HitSurface s;
    s.id = id;
    s.shape = make_circle(center, radius);
    s.layer = SurfaceLayer::Normal;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

constexpr LocalPoint kInside{5.0f, 5.0f};       // 落在 10x10 矩形內，不落在 2x2 矩形內
constexpr LocalPoint kOutside{99.0f, 99.0f};    // 不落在任何 surface 內
constexpr LocalPoint kFarAway{200.0f, 200.0f};  // 遠離原點——供獨立於 10x10 矩形的另一 surface 用

}  // namespace

// -----------------------------------------------------------------------------
// 垂直 / 水平滾動 + delta 傳遞
// -----------------------------------------------------------------------------

TEST(WheelScroll, VerticalScrollDeliversDeltaYToSubscriber) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<WheelEvent> received;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received.push_back(e); });

    const WheelRouteStatus status = router.inject_wheel(0.0f, 10.0f, kInside);

    EXPECT_EQ(status, WheelRouteStatus::Hit);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_FLOAT_EQ(received[0].delta_x, 0.0f);
    EXPECT_FLOAT_EQ(received[0].delta_y, 10.0f);
    EXPECT_EQ(received[0].target, "surface.a");
}

TEST(WheelScroll, HorizontalScrollDeliversDeltaXToSubscriber) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<WheelEvent> received;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received.push_back(e); });

    const WheelRouteStatus status = router.inject_wheel(7.5f, 0.0f, kInside);

    EXPECT_EQ(status, WheelRouteStatus::Hit);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_FLOAT_EQ(received[0].delta_x, 7.5f);
    EXPECT_FLOAT_EQ(received[0].delta_y, 0.0f);
}

TEST(WheelScroll, MixedAndNegativeDeltasArePassedThroughUnmodified) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<WheelEvent> received;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received.push_back(e); });

    router.inject_wheel(-3.25f, -1.5f, kInside);   // 兩軸皆負值
    router.inject_wheel(2.0f, -4.0f, kInside);     // 混合水平 + 垂直

    ASSERT_EQ(received.size(), 2u);
    EXPECT_FLOAT_EQ(received[0].delta_x, -3.25f);
    EXPECT_FLOAT_EQ(received[0].delta_y, -1.5f);
    EXPECT_FLOAT_EQ(received[1].delta_x, 2.0f);
    EXPECT_FLOAT_EQ(received[1].delta_y, -4.0f);
}

TEST(WheelScroll, PositionIsPassedThroughUnmodified) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<WheelEvent> received;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received.push_back(e); });

    router.inject_wheel(1.0f, 1.0f, kInside);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_FLOAT_EQ(received[0].position.x, kInside.x);
    EXPECT_FLOAT_EQ(received[0].position.y, kInside.y);
}

// -----------------------------------------------------------------------------
// 零 delta
// -----------------------------------------------------------------------------

TEST(WheelScroll, ZeroDeltaStillDispatchesAsValidEvent) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<WheelEvent> received;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received.push_back(e); });

    const WheelRouteStatus status = router.inject_wheel(0.0f, 0.0f, kInside);

    EXPECT_EQ(status, WheelRouteStatus::Hit);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_FLOAT_EQ(received[0].delta_x, 0.0f);
    EXPECT_FLOAT_EQ(received[0].delta_y, 0.0f);
    EXPECT_EQ(received[0].target, "surface.a");
}

// -----------------------------------------------------------------------------
// 經 E1-04 命中路由給正確訂閱者
// -----------------------------------------------------------------------------

TEST(WheelHitRouting, OnlyDispatchesToSurfaceThatWasHit) {
    WheelEventRouter router;
    // "surface.small" 為 2x2，(5,5) 落在其外；"surface.big" 為 10x10，(5,5) 落在其內。
    router.set_surfaces({MakeSmallOpaqueRect("surface.small"), MakeOpaqueRect("surface.big")});

    std::vector<WheelEvent> small_received;
    std::vector<WheelEvent> big_received;
    router.subscribe("surface.small",
                      [&](const WheelEvent& e) { small_received.push_back(e); });
    router.subscribe("surface.big",
                      [&](const WheelEvent& e) { big_received.push_back(e); });

    const WheelRouteStatus status = router.inject_wheel(0.0f, 5.0f, kInside);

    EXPECT_EQ(status, WheelRouteStatus::Hit);
    EXPECT_TRUE(small_received.empty());
    ASSERT_EQ(big_received.size(), 1u);
    EXPECT_EQ(big_received[0].target, "surface.big");
}

TEST(WheelHitRouting, HigherNamedLayerWinsAndReceivesEvent) {
    WheelEventRouter router;
    router.set_surfaces({
        MakeOpaqueRect("surface.wallpaper", SurfaceLayer::Wallpaper),
        MakeOpaqueRect("surface.panel", SurfaceLayer::Overlay),
        MakeOpaqueRect("surface.window", SurfaceLayer::Normal),
    });

    std::vector<WheelEvent> panel_received;
    std::vector<WheelEvent> window_received;
    router.subscribe("surface.panel",
                      [&](const WheelEvent& e) { panel_received.push_back(e); });
    router.subscribe("surface.window",
                      [&](const WheelEvent& e) { window_received.push_back(e); });

    router.inject_wheel(0.0f, 3.0f, kInside);

    ASSERT_EQ(panel_received.size(), 1u);  // Overlay 為最高層
    EXPECT_TRUE(window_received.empty());
}

TEST(WheelHitRouting, DifferentSurfacesEachReceiveWhenHit) {
    WheelEventRouter router;
    router.set_surfaces(
        {MakeOpaqueRect("surface.a"), MakeOpaqueCircle("surface.b", kFarAway, 5.0f)});

    std::vector<WheelEvent> received_a;
    std::vector<WheelEvent> received_b;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received_a.push_back(e); });
    router.subscribe("surface.b", [&](const WheelEvent& e) { received_b.push_back(e); });

    router.inject_wheel(0.0f, 1.0f, kInside);
    router.inject_wheel(0.0f, 2.0f, kFarAway);

    ASSERT_EQ(received_a.size(), 1u);
    EXPECT_FLOAT_EQ(received_a[0].delta_y, 1.0f);
    ASSERT_EQ(received_b.size(), 1u);
    EXPECT_FLOAT_EQ(received_b[0].delta_y, 2.0f);
}

// -----------------------------------------------------------------------------
// 無命中不派
// -----------------------------------------------------------------------------

TEST(WheelHitRouting, NoHitDoesNotDispatchToAnySubscriber) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<WheelEvent> received;
    router.subscribe("surface.a", [&](const WheelEvent& e) { received.push_back(e); });

    const WheelRouteStatus status = router.inject_wheel(1.0f, 1.0f, kOutside);

    EXPECT_EQ(status, WheelRouteStatus::NoHit);
    EXPECT_TRUE(received.empty());
}

TEST(WheelHitRouting, InvalidSurfaceShapeReportsInvalidAndDoesNotDispatch) {
    WheelEventRouter router;
    HitSurface bad;
    bad.id = "surface.bad";
    bad.shape = make_rect(-1.0f, 4.0f);  // 無效（負範圍）
    router.set_surfaces({MakeOpaqueRect("surface.ok"), bad});

    std::vector<WheelEvent> received;
    router.subscribe("surface.ok", [&](const WheelEvent& e) { received.push_back(e); });

    const WheelRouteStatus status = router.inject_wheel(1.0f, 1.0f, kInside);

    EXPECT_EQ(status, WheelRouteStatus::Invalid);
    EXPECT_TRUE(received.empty());
}

// -----------------------------------------------------------------------------
// 訂閱 / 取消訂閱
// -----------------------------------------------------------------------------

TEST(WheelSubscription, MultipleListenersOnSameSurfaceAllReceiveInOrder) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<int> order;
    router.subscribe("surface.a", [&](const WheelEvent&) { order.push_back(1); });
    router.subscribe("surface.a", [&](const WheelEvent&) { order.push_back(2); });

    router.inject_wheel(0.0f, 1.0f, kInside);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(WheelSubscription, UnsubscribeStopsFurtherDispatch) {
    WheelEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    int count = 0;
    const auto id = router.subscribe("surface.a", [&](const WheelEvent&) { ++count; });
    router.inject_wheel(0.0f, 1.0f, kInside);
    EXPECT_EQ(count, 1);

    const bool removed = router.unsubscribe(id);
    EXPECT_TRUE(removed);

    router.inject_wheel(0.0f, 1.0f, kInside);
    EXPECT_EQ(count, 1);  // 取消後不再收
}

TEST(WheelSubscription, UnsubscribeUnknownIdIsNoOp) {
    WheelEventRouter router;
    EXPECT_FALSE(router.unsubscribe(999999));
}

TEST(WheelSubscription, EmptyTargetOrNullListenerIsInvalidSubscription) {
    WheelEventRouter router;
    EXPECT_EQ(router.subscribe("", [](const WheelEvent&) {}), 0u);
    EXPECT_EQ(router.subscribe("surface.a", WheelEventListener{}), 0u);
}

TEST(WheelSubscription, ListenerCountReflectsSubscriptionsPerSurface) {
    WheelEventRouter router;
    EXPECT_EQ(router.listener_count("surface.a"), 0u);

    const auto id1 = router.subscribe("surface.a", [](const WheelEvent&) {});
    router.subscribe("surface.a", [](const WheelEvent&) {});
    router.subscribe("surface.b", [](const WheelEvent&) {});

    EXPECT_EQ(router.listener_count("surface.a"), 2u);
    EXPECT_EQ(router.listener_count("surface.b"), 1u);

    router.unsubscribe(id1);
    EXPECT_EQ(router.listener_count("surface.a"), 1u);
}
