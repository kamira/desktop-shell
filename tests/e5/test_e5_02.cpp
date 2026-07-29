// E5-02 懸停進出事件 — 單元測試（gtest）
//
// 驗證：移入發 Enter、移出發 Leave、跨 surface 移動發對應 Leave+Enter（順序）、同 surface
// 內移動不重發 Enter（改發 Move）、當前懸停查詢、無命中處理（含未登記任何 surface、以及
// hit-test 回 Invalid 時不改狀態不分派）。另涵蓋訂閱 / 解除訂閱、surface 登記 / 移除的基本
// 契約。全程平台中立、不依賴任何真實滑鼠 / 視窗系統 API（相位 1：注入式）。
#include "hover_tracker.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）

using ds::events::HoverEvent;
using ds::events::HoverEventKind;
using ds::events::HoverTracker;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_circle;
using ds::kernel::make_rect;
using ds::kernel::SurfaceId;
using ds::kernel::SurfaceLayer;

namespace {

// 兩個互不重疊的圓形 surface：A 圓心 (0,0) 半徑 2；B 圓心 (10,0) 半徑 2。
// 兩者間留有空白（例如 (5,0)）供「無命中」情境使用。
HitSurface MakeCircleSurface(const char* id, LocalPoint center, float radius) {
    HitSurface s;
    s.id = id;
    s.shape = make_circle(center, radius);
    s.layer = SurfaceLayer::Normal;
    return s;
}

HoverTracker MakeTwoSurfaceTracker() {
    HoverTracker t;
    t.add_surface(MakeCircleSurface("surface.a", LocalPoint{0.0f, 0.0f}, 2.0f));
    t.add_surface(MakeCircleSurface("surface.b", LocalPoint{10.0f, 0.0f}, 2.0f));
    return t;
}

// -----------------------------------------------------------------------------
// 移入 / 移出
// -----------------------------------------------------------------------------

// 移入某 surface：先前無懸停 → 分派單一 Enter，且 kind/surface/point 正確。
TEST(HoverTracker, MoveIntoSurfaceDispatchesEnter) {
    HoverTracker t = MakeTwoSurfaceTracker();
    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    EXPECT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].kind, HoverEventKind::Enter);
    EXPECT_EQ(events[0].surface, "surface.a");
    EXPECT_EQ(events[0].point.x, 0.0f);
    EXPECT_EQ(events[0].point.y, 0.0f);
}

// 移出至無命中處：先分派 Enter，再移出分派 Leave；懸停狀態清空。
TEST(HoverTracker, MoveOutOfSurfaceDispatchesLeave) {
    HoverTracker t = MakeTwoSurfaceTracker();
    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));  // 移入 A
    ASSERT_TRUE(t.inject_move(LocalPoint{5.0f, 0.0f}));  // 移到空白處（無命中）

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].kind, HoverEventKind::Enter);
    EXPECT_EQ(events[1].kind, HoverEventKind::Leave);
    EXPECT_EQ(events[1].surface, "surface.a");

    SurfaceId hovered;
    EXPECT_FALSE(t.current_hover(hovered));
}

// 跨 surface 移動：先分派 Leave（舊），再分派 Enter（新）——順序固定。
TEST(HoverTracker, CrossSurfaceMoveDispatchesLeaveThenEnter) {
    HoverTracker t = MakeTwoSurfaceTracker();
    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));   // 移入 A
    ASSERT_TRUE(t.inject_move(LocalPoint{10.0f, 0.0f}));  // 直接跨到 B

    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].kind, HoverEventKind::Enter);
    EXPECT_EQ(events[0].surface, "surface.a");
    EXPECT_EQ(events[1].kind, HoverEventKind::Leave);
    EXPECT_EQ(events[1].surface, "surface.a");
    EXPECT_EQ(events[2].kind, HoverEventKind::Enter);
    EXPECT_EQ(events[2].surface, "surface.b");

    SurfaceId hovered;
    ASSERT_TRUE(t.current_hover(hovered));
    EXPECT_EQ(hovered, "surface.b");
}

// 同一 surface 內移動：不重發 Enter，改發 Move；懸停對象不變。
TEST(HoverTracker, MoveWithinSameSurfaceDoesNotRefireEnter) {
    HoverTracker t = MakeTwoSurfaceTracker();
    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));    // 移入 A
    ASSERT_TRUE(t.inject_move(LocalPoint{0.5f, 0.5f}));    // A 內部移動
    ASSERT_TRUE(t.inject_move(LocalPoint{-0.5f, 0.2f}));   // A 內部再移動

    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].kind, HoverEventKind::Enter);
    // 之後兩次皆為 Move，沒有任何一次是 Enter（不重發）。
    EXPECT_EQ(events[1].kind, HoverEventKind::Move);
    EXPECT_EQ(events[1].surface, "surface.a");
    EXPECT_EQ(events[2].kind, HoverEventKind::Move);
    EXPECT_EQ(events[2].surface, "surface.a");

    SurfaceId hovered;
    ASSERT_TRUE(t.current_hover(hovered));
    EXPECT_EQ(hovered, "surface.a");
}

// -----------------------------------------------------------------------------
// 當前懸停查詢
// -----------------------------------------------------------------------------

TEST(HoverTracker, CurrentHoverQueryReflectsState) {
    HoverTracker t = MakeTwoSurfaceTracker();

    SurfaceId hovered;
    EXPECT_FALSE(t.current_hover(hovered));  // 初始無懸停

    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));
    ASSERT_TRUE(t.current_hover(hovered));
    EXPECT_EQ(hovered, "surface.a");

    ASSERT_TRUE(t.inject_move(LocalPoint{10.0f, 0.0f}));
    ASSERT_TRUE(t.current_hover(hovered));
    EXPECT_EQ(hovered, "surface.b");

    ASSERT_TRUE(t.inject_move(LocalPoint{5.0f, 0.0f}));  // 移出
    EXPECT_FALSE(t.current_hover(hovered));
}

// -----------------------------------------------------------------------------
// 無命中處理
// -----------------------------------------------------------------------------

// 未登記任何 surface：注入移動永遠無命中，不分派任何事件，回傳 true（Ok，只是沒命中）。
TEST(HoverTracker, NoSurfacesRegisteredNeverDispatches) {
    HoverTracker t;  // 空
    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    EXPECT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));
    EXPECT_TRUE(t.inject_move(LocalPoint{1.0f, 1.0f}));
    EXPECT_TRUE(events.empty());

    SurfaceId hovered;
    EXPECT_FALSE(t.current_hover(hovered));
}

// 已在無命中處，再次注入仍在無命中處：no-op，不分派事件（不重發 Leave）。
TEST(HoverTracker, RepeatedNoHitStaysSilent) {
    HoverTracker t = MakeTwoSurfaceTracker();
    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    ASSERT_TRUE(t.inject_move(LocalPoint{5.0f, 0.0f}));  // 空白處，先前本就無懸停
    ASSERT_TRUE(t.inject_move(LocalPoint{5.1f, 0.1f}));  // 仍在空白處

    EXPECT_TRUE(events.empty());
}

// hit-test 回 Invalid（形狀無效）：inject_move 回 false，不改狀態、不分派事件。
TEST(HoverTracker, InvalidShapeHitTestDoesNotDispatch) {
    HoverTracker t;
    HitSurface bad;
    bad.id = "surface.bad";
    bad.shape = make_rect(-1.0f, 4.0f);  // 負寬度 → 無效形狀
    t.add_surface(bad);

    std::vector<HoverEvent> events;
    t.subscribe([&](const HoverEvent& e) { events.push_back(e); });

    EXPECT_FALSE(t.inject_move(LocalPoint{0.0f, 0.0f}));
    EXPECT_TRUE(events.empty());

    SurfaceId hovered;
    EXPECT_FALSE(t.current_hover(hovered));
}

// -----------------------------------------------------------------------------
// 訂閱 / 解除訂閱
// -----------------------------------------------------------------------------

TEST(HoverTracker, SubscribeEmptyListenerReturnsZero) {
    HoverTracker t;
    EXPECT_EQ(t.subscribe(nullptr), 0u);
    EXPECT_EQ(t.listener_count(), 0u);
}

TEST(HoverTracker, UnsubscribeStopsReceivingEvents) {
    HoverTracker t = MakeTwoSurfaceTracker();
    int fired = 0;
    auto id = t.subscribe([&](const HoverEvent&) { ++fired; });
    ASSERT_NE(id, 0u);
    EXPECT_EQ(t.listener_count(), 1u);

    ASSERT_TRUE(t.unsubscribe(id));
    EXPECT_FALSE(t.unsubscribe(id));  // 未知 id 為 no-op
    EXPECT_EQ(t.listener_count(), 0u);

    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));
    EXPECT_EQ(fired, 0);
}

// 多訂閱者皆收，依訂閱順序分派。
TEST(HoverTracker, MultipleListenersReceiveInOrder) {
    HoverTracker t = MakeTwoSurfaceTracker();
    std::vector<int> order;
    t.subscribe([&](const HoverEvent&) { order.push_back(1); });
    t.subscribe([&](const HoverEvent&) { order.push_back(2); });

    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

// -----------------------------------------------------------------------------
// surface 登記 / 移除
// -----------------------------------------------------------------------------

TEST(HoverTracker, AddSurfaceReturnsTrueThenFalseOnOverwrite) {
    HoverTracker t;
    EXPECT_TRUE(t.add_surface(MakeCircleSurface("surface.a", LocalPoint{0.0f, 0.0f}, 2.0f)));
    EXPECT_EQ(t.surface_count(), 1u);
    EXPECT_FALSE(t.add_surface(MakeCircleSurface("surface.a", LocalPoint{1.0f, 1.0f}, 3.0f)));
    EXPECT_EQ(t.surface_count(), 1u);  // 覆蓋，非新增
}

TEST(HoverTracker, RemoveUnknownSurfaceIsNoOp) {
    HoverTracker t = MakeTwoSurfaceTracker();
    EXPECT_FALSE(t.remove_surface("surface.unknown"));
    EXPECT_EQ(t.surface_count(), 2u);
}

TEST(HoverTracker, RemoveHoveredSurfaceClearsHoverState) {
    HoverTracker t = MakeTwoSurfaceTracker();
    ASSERT_TRUE(t.inject_move(LocalPoint{0.0f, 0.0f}));  // 懸停於 A

    EXPECT_TRUE(t.remove_surface("surface.a"));
    EXPECT_EQ(t.surface_count(), 1u);

    SurfaceId hovered;
    EXPECT_FALSE(t.current_hover(hovered));  // 懸停狀態已清除
}

}  // namespace
