// E5-01 基本滑鼠按鍵事件 — 單元測試（gtest）
//
// 驗證 MouseEventRouter 於相位 1（Mac / null 期）的注入式滑鼠按鍵事件：
//   - down / up / click 三種動作，左 / 中 / 右鍵
//   - 多擊（double / triple）：連續同鍵同 surface 的 down/up 配對遞增 click_count；
//     不同鍵 / 不同 surface / 孤立 up 中斷序列
//   - 經 E1-04 HitTester::topmost_hit() 命中路由：只分派給命中之 surface 的訂閱者，
//     具名圖層決定 topmost，未命中不分派給任何人
//   - 上游命中測試判定形狀無效 → RouteStatus::Invalid（報錯不靜默）
//   - 訂閱 / 取消訂閱（含無效訂閱、未知 id 取消 no-op、同 surface 多訂閱者皆收）
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實滑鼠後端，事件皆為注入式。
#include "mouse_button_input.hpp"

#include <gtest/gtest.h>

#include <vector>

using ds::events::MouseButton;
using ds::events::MouseButtonEvent;
using ds::events::MouseEventRouter;
using ds::events::RouteStatus;

using ds::kernel::AlphaMode;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_circle;
using ds::kernel::make_rect;
using ds::kernel::SurfaceLayer;

namespace {

// 一個 10x10 本地座標的不透明矩形 surface（覆蓋點 (5,5)）；同 e1_04 測試慣例。
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
    s.shape = ds::kernel::make_circle(center, radius);
    s.layer = SurfaceLayer::Normal;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

constexpr LocalPoint kInside{5.0f, 5.0f};   // 落在 10x10 矩形內，不落在 2x2 矩形內
constexpr LocalPoint kOutside{99.0f, 99.0f};  // 不落在任何 surface 內
constexpr LocalPoint kFarAway{200.0f, 200.0f};  // 遠離原點——供獨立於 10x10 矩形的另一 surface 用

}  // namespace

// -----------------------------------------------------------------------------
// down / up / click 基本動作
// -----------------------------------------------------------------------------

TEST(MouseButtonDown, DispatchesToCorrectSubscriberWithClickCountOne) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    const RouteStatus status = router.inject_down(MouseButton::Left, kInside);

    EXPECT_EQ(status, RouteStatus::Hit);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].button, MouseButton::Left);
    EXPECT_EQ(received[0].action, ds::events::MouseAction::Down);
    EXPECT_EQ(received[0].target, "surface.a");
    EXPECT_EQ(received[0].click_count, 1u);
}

TEST(MouseButtonUp, DispatchesWithoutSynthesizingClickWhenNoPriorDown) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    const RouteStatus status = router.inject_up(MouseButton::Left, kInside);

    EXPECT_EQ(status, RouteStatus::Hit);
    // 孤立的 Up（無相符的進行中 Down 游標）：只分派 Up，不合成 Click。
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].action, ds::events::MouseAction::Up);
    EXPECT_EQ(received[0].click_count, 1u);
}

TEST(MouseButtonClick, DownThenUpSynthesizesClickEvent) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    router.inject_down(MouseButton::Left, kInside);
    router.inject_up(MouseButton::Left, kInside);

    ASSERT_EQ(received.size(), 3u);  // Down, Up, 合成 Click
    EXPECT_EQ(received[0].action, ds::events::MouseAction::Down);
    EXPECT_EQ(received[1].action, ds::events::MouseAction::Up);
    EXPECT_EQ(received[2].action, ds::events::MouseAction::Click);
    EXPECT_EQ(received[1].click_count, 1u);
    EXPECT_EQ(received[2].click_count, 1u);
    EXPECT_EQ(received[2].target, "surface.a");
}

TEST(MouseButtonClick, DirectClickActionDispatchesSingleClickWithoutTouchingCursor) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    // 進行中的 Down（游標啟用，count=1）。
    router.inject_down(MouseButton::Left, kInside);
    // 直接注入一個 Click（不應牽動上面的游標）。
    const RouteStatus status =
        router.inject_button(MouseButton::Left, ds::events::MouseAction::Click, kInside);
    // 讓上面的 Down 配對一個 Up：游標應仍是 count=1（未被直接 Click 干擾）。
    router.inject_up(MouseButton::Left, kInside);

    EXPECT_EQ(status, RouteStatus::Hit);
    ASSERT_EQ(received.size(), 4u);  // Down, 直接 Click(count1), Up(count1), 合成 Click(count1)
    EXPECT_EQ(received[1].action, ds::events::MouseAction::Click);
    EXPECT_EQ(received[1].click_count, 1u);
    EXPECT_EQ(received[2].click_count, 1u);
    EXPECT_EQ(received[3].click_count, 1u);
}

// -----------------------------------------------------------------------------
// 左 / 中 / 右鍵
// -----------------------------------------------------------------------------

TEST(MouseButtonKinds, LeftMiddleRightRouteWithCorrectButtonField) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    router.inject_down(MouseButton::Left, kInside);
    router.inject_down(MouseButton::Middle, kInside);
    router.inject_down(MouseButton::Right, kInside);

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0].button, MouseButton::Left);
    EXPECT_EQ(received[1].button, MouseButton::Middle);
    EXPECT_EQ(received[2].button, MouseButton::Right);
}

// -----------------------------------------------------------------------------
// 多擊（double / triple）
// -----------------------------------------------------------------------------

TEST(MouseMultiClick, SecondCycleOnSameButtonAndSurfaceIsDoubleClick) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    router.inject_click(MouseButton::Left, kInside);  // Down+Up+Click, count=1
    router.inject_click(MouseButton::Left, kInside);  // Down+Up+Click, count=2

    ASSERT_EQ(received.size(), 6u);
    EXPECT_EQ(received[2].action, ds::events::MouseAction::Click);
    EXPECT_EQ(received[2].click_count, 1u);
    EXPECT_EQ(received[5].action, ds::events::MouseAction::Click);
    EXPECT_EQ(received[5].click_count, 2u);
}

TEST(MouseMultiClick, ThirdCycleIsTripleClick) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    router.inject_click(MouseButton::Left, kInside);
    router.inject_click(MouseButton::Left, kInside);
    router.inject_click(MouseButton::Left, kInside);

    ASSERT_EQ(received.size(), 9u);
    EXPECT_EQ(received[8].action, ds::events::MouseAction::Click);
    EXPECT_EQ(received[8].click_count, 3u);
}

TEST(MouseMultiClick, DifferentButtonResetsClickCount) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    router.inject_click(MouseButton::Left, kInside);   // count=1
    router.inject_click(MouseButton::Right, kInside);  // 不同鍵 → 重置為 count=1

    ASSERT_EQ(received.size(), 6u);
    EXPECT_EQ(received[5].button, MouseButton::Right);
    EXPECT_EQ(received[5].click_count, 1u);
}

TEST(MouseMultiClick, DifferentSurfaceResetsClickCount) {
    MouseEventRouter router;
    // "surface.a"（10x10 矩形，覆蓋 kInside）與 "surface.b"（遠處的圓，覆蓋 kFarAway）
    // 彼此不重疊：kInside 只命中 a，kFarAway 只命中 b。
    router.set_surfaces(
        {MakeOpaqueRect("surface.a"), MakeOpaqueCircle("surface.b", kFarAway, 5.0f)});

    std::vector<MouseButtonEvent> received_a;
    std::vector<MouseButtonEvent> received_b;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received_a.push_back(e); });
    router.subscribe("surface.b", [&](const MouseButtonEvent& e) { received_b.push_back(e); });

    router.inject_click(MouseButton::Left, kInside);    // 命中 a，count=1
    router.inject_click(MouseButton::Left, kFarAway);   // 命中 b（不同 surface）→ 重置為 count=1

    ASSERT_EQ(received_a.size(), 3u);
    EXPECT_EQ(received_a[2].click_count, 1u);
    ASSERT_EQ(received_b.size(), 3u);
    EXPECT_EQ(received_b[2].click_count, 1u);  // 若未重置本應為 2——驗證跨 surface 中斷序列
}

TEST(MouseMultiClick, IsolatedUpBreaksSequenceForNextDown) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    router.inject_down(MouseButton::Left, kInside);   // 游標啟用，count=1
    router.inject_up(MouseButton::Left, kOutside);    // Up 落在未命中處：孤立、重置游標

    received.clear();
    router.inject_down(MouseButton::Left, kInside);  // 應視為全新序列：count=1（非 2）
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].click_count, 1u);
}

// -----------------------------------------------------------------------------
// 經 E1-04 命中路由給正確訂閱者
// -----------------------------------------------------------------------------

TEST(MouseHitRouting, OnlyDispatchesToSurfaceThatWasHit) {
    MouseEventRouter router;
    // "surface.small" 為 2x2，(5,5) 落在其外；"surface.big" 為 10x10，(5,5) 落在其內。
    router.set_surfaces({MakeSmallOpaqueRect("surface.small"), MakeOpaqueRect("surface.big")});

    std::vector<MouseButtonEvent> small_received;
    std::vector<MouseButtonEvent> big_received;
    router.subscribe("surface.small",
                      [&](const MouseButtonEvent& e) { small_received.push_back(e); });
    router.subscribe("surface.big",
                      [&](const MouseButtonEvent& e) { big_received.push_back(e); });

    const RouteStatus status = router.inject_down(MouseButton::Left, kInside);

    EXPECT_EQ(status, RouteStatus::Hit);
    EXPECT_TRUE(small_received.empty());
    ASSERT_EQ(big_received.size(), 1u);
    EXPECT_EQ(big_received[0].target, "surface.big");
}

TEST(MouseHitRouting, HigherNamedLayerWinsAndReceivesEvent) {
    MouseEventRouter router;
    router.set_surfaces({
        MakeOpaqueRect("surface.wallpaper", SurfaceLayer::Wallpaper),
        MakeOpaqueRect("surface.panel", SurfaceLayer::Overlay),
        MakeOpaqueRect("surface.window", SurfaceLayer::Normal),
    });

    std::vector<MouseButtonEvent> panel_received;
    std::vector<MouseButtonEvent> window_received;
    router.subscribe("surface.panel",
                      [&](const MouseButtonEvent& e) { panel_received.push_back(e); });
    router.subscribe("surface.window",
                      [&](const MouseButtonEvent& e) { window_received.push_back(e); });

    router.inject_down(MouseButton::Left, kInside);

    ASSERT_EQ(panel_received.size(), 1u);  // Overlay 為最高層
    EXPECT_TRUE(window_received.empty());
}

TEST(MouseHitRouting, NoHitDoesNotDispatchToAnySubscriber) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.a", [&](const MouseButtonEvent& e) { received.push_back(e); });

    const RouteStatus down_status = router.inject_down(MouseButton::Left, kOutside);
    const RouteStatus up_status = router.inject_up(MouseButton::Left, kOutside);

    EXPECT_EQ(down_status, RouteStatus::NoHit);
    EXPECT_EQ(up_status, RouteStatus::NoHit);
    EXPECT_TRUE(received.empty());
}

TEST(MouseHitRouting, InvalidSurfaceShapeReportsInvalidAndDoesNotDispatch) {
    MouseEventRouter router;
    HitSurface bad;
    bad.id = "surface.bad";
    bad.shape = make_rect(-1.0f, 4.0f);  // 無效（負範圍）
    router.set_surfaces({MakeOpaqueRect("surface.ok"), bad});

    std::vector<MouseButtonEvent> received;
    router.subscribe("surface.ok", [&](const MouseButtonEvent& e) { received.push_back(e); });

    const RouteStatus status = router.inject_down(MouseButton::Left, kInside);

    EXPECT_EQ(status, RouteStatus::Invalid);
    EXPECT_TRUE(received.empty());
}

// -----------------------------------------------------------------------------
// 訂閱 / 取消訂閱
// -----------------------------------------------------------------------------

TEST(MouseSubscription, MultipleListenersOnSameSurfaceAllReceiveInOrder) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    std::vector<int> order;
    router.subscribe("surface.a", [&](const MouseButtonEvent&) { order.push_back(1); });
    router.subscribe("surface.a", [&](const MouseButtonEvent&) { order.push_back(2); });

    router.inject_down(MouseButton::Left, kInside);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(MouseSubscription, UnsubscribeStopsFurtherDispatch) {
    MouseEventRouter router;
    router.set_surfaces({MakeOpaqueRect("surface.a")});

    int count = 0;
    const auto id =
        router.subscribe("surface.a", [&](const MouseButtonEvent&) { ++count; });
    router.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(count, 1);

    const bool removed = router.unsubscribe(id);
    EXPECT_TRUE(removed);

    router.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(count, 1);  // 取消後不再收
}

TEST(MouseSubscription, UnsubscribeUnknownIdIsNoOp) {
    MouseEventRouter router;
    EXPECT_FALSE(router.unsubscribe(999999));
}

TEST(MouseSubscription, EmptyTargetOrNullListenerIsInvalidSubscription) {
    MouseEventRouter router;
    EXPECT_EQ(router.subscribe("", [](const MouseButtonEvent&) {}), 0u);
    EXPECT_EQ(router.subscribe("surface.a", ds::events::MouseButtonListener{}), 0u);
}

TEST(MouseSubscription, ListenerCountReflectsSubscriptionsPerSurface) {
    MouseEventRouter router;
    EXPECT_EQ(router.listener_count("surface.a"), 0u);

    const auto id1 = router.subscribe("surface.a", [](const MouseButtonEvent&) {});
    router.subscribe("surface.a", [](const MouseButtonEvent&) {});
    router.subscribe("surface.b", [](const MouseButtonEvent&) {});

    EXPECT_EQ(router.listener_count("surface.a"), 2u);
    EXPECT_EQ(router.listener_count("surface.b"), 1u);

    router.unsubscribe(id1);
    EXPECT_EQ(router.listener_count("surface.a"), 1u);
}
