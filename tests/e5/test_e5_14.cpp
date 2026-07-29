// E5-14 碰撞區域名事件參數 — 單元測試（gtest）
//
// 驗證 RegionEventDispatcher 於相位 1（Mac / null 期）橋接 E5-01 `MouseEventRouter`
// （滑鼠事件注入 / 命中路由）與 E1-05 `NamedRegionMap`（具名子區域查詢）：
//   - 命中具名子區域 → 派發之 RegionEvent 帶區域 id + 參數（region_hit=true）
//   - 多個具名子區域各自攜帶不同參數；重疊子區域依 E1-05 加入序（後加入者為上）
//   - 未登記子區域 / 未命中任一子區域 → 事件仍正常派發，但不附加區域參數
//     （region_hit=false、region_name 空、region_params 空）
//   - 經 E5-01 的 down/up/click 注入介面（含多擊 click_count 透傳）
//   - 經 E1-05 NamedRegionMap 的區域登記 / 查詢語意（依 surface 對照）
//   - 訂閱 / 取消訂閱（含無效訂閱、未知 id 取消 no-op、多訂閱者依序皆收）
//   - 未命中任何 surface（E5-01 NoHit）→ 不分派給任何人
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實滑鼠後端，事件皆為注入式。
#include "region_event_dispatcher.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::MouseButton;
using ds::events::RegionEvent;
using ds::events::RegionEventDispatcher;
using ds::events::RouteStatus;

using ds::kernel::AlphaMode;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_circle;
using ds::kernel::make_rect;
using ds::kernel::NamedRegionMap;
using ds::kernel::RegionParams;
using ds::kernel::SurfaceLayer;

namespace {

// 一個 20x20 本地座標的不透明矩形 surface；同 e5_01 / e1_05 測試慣例。
HitSurface MakeOpaqueRect(const char* id, SurfaceLayer layer = SurfaceLayer::Normal) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(20.0f, 20.0f);
    s.layer = layer;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

// 一個以 center 為圓心、radius 為半徑的不透明圓形 surface——供獨立於 20x20 矩形之外的
// 另一個具名 surface（不重疊）。
HitSurface MakeOpaqueCircle(const char* id, LocalPoint center, float radius) {
    HitSurface s;
    s.id = id;
    s.shape = make_circle(center, radius);
    s.layer = SurfaceLayer::Normal;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

constexpr LocalPoint kInside{5.0f, 5.0f};       // 落在 20x20 矩形內
constexpr LocalPoint kOutside{99.0f, 99.0f};    // 不落在任何 surface 內
constexpr LocalPoint kFarAway{200.0f, 200.0f};  // 遠離原點——另一個獨立 surface 用

}  // namespace

// -----------------------------------------------------------------------------
// 命中具名子區域 → 事件帶區域 id + 參數
// -----------------------------------------------------------------------------

TEST(RegionEvent, HitNamedRegionCarriesIdAndParams) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    NamedRegionMap regions;
    RegionParams params;
    params["action"] = std::string("close");
    params["code"] = std::int64_t(42);
    ASSERT_TRUE(regions.add_region("button.close", make_rect(10.0f, 10.0f), params));
    dispatcher.set_regions("panel.main", std::move(regions));

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.main", [&](const RegionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(status, RouteStatus::Hit);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].mouse.target, "panel.main");
    EXPECT_TRUE(received[0].region_hit);
    EXPECT_EQ(received[0].region_name, "button.close");
    ASSERT_TRUE(received[0].region_params.count("action"));
    EXPECT_EQ(std::get<std::string>(received[0].region_params.at("action")), "close");
    ASSERT_TRUE(received[0].region_params.count("code"));
    EXPECT_EQ(std::get<std::int64_t>(received[0].region_params.at("code")), 42);
}

// -----------------------------------------------------------------------------
// 多個具名子區域各自攜帶不同參數
// -----------------------------------------------------------------------------

TEST(RegionEvent, MultipleRegionsCarryDistinctParams) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.toolbar")});

    NamedRegionMap regions;
    RegionParams close_params;
    close_params["action"] = std::string("close");
    ASSERT_TRUE(regions.add_region("hotspot.close", make_circle(LocalPoint{2.0f, 2.0f}, 1.0f),
                                    close_params));
    RegionParams save_params;
    save_params["action"] = std::string("save");
    ASSERT_TRUE(regions.add_region("hotspot.save", make_circle(LocalPoint{18.0f, 18.0f}, 1.0f),
                                    save_params));
    dispatcher.set_regions("panel.toolbar", std::move(regions));

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.toolbar", [&](const RegionEvent& e) { received.push_back(e); });

    dispatcher.inject_click(MouseButton::Left, LocalPoint{2.0f, 2.0f});
    dispatcher.inject_click(MouseButton::Left, LocalPoint{18.0f, 18.0f});

    // 每次 click 注入合成 Down+Up+Click 三個事件，各自帶正確的區域資訊。
    ASSERT_TRUE(received.size() >= 2u);
    bool saw_close = false;
    bool saw_save = false;
    for (const auto& e : received) {
        if (e.region_hit && e.region_name == "hotspot.close") {
            EXPECT_EQ(std::get<std::string>(e.region_params.at("action")), "close");
            saw_close = true;
        }
        if (e.region_hit && e.region_name == "hotspot.save") {
            EXPECT_EQ(std::get<std::string>(e.region_params.at("action")), "save");
            saw_save = true;
        }
    }
    EXPECT_TRUE(saw_close);
    EXPECT_TRUE(saw_save);
}

// 重疊子區域：依 E1-05 加入序（後加入者為上）。
TEST(RegionEvent, OverlappingRegionsLaterAddedWins) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.overlap")});

    NamedRegionMap regions;
    RegionParams first_params;
    first_params["layer"] = std::string("first");
    ASSERT_TRUE(regions.add_region("region.first", make_rect(10.0f, 10.0f), first_params));
    RegionParams second_params;
    second_params["layer"] = std::string("second");
    ASSERT_TRUE(regions.add_region("region.second", make_rect(10.0f, 10.0f), second_params));
    dispatcher.set_regions("panel.overlap", std::move(regions));

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.overlap", [&](const RegionEvent& e) { received.push_back(e); });

    dispatcher.inject_click(MouseButton::Left, LocalPoint{5.0f, 5.0f});

    ASSERT_FALSE(received.empty());
    EXPECT_EQ(received.back().region_name, "region.second");
}

// -----------------------------------------------------------------------------
// 無區域命中的事件正常派（無區域參數）
// -----------------------------------------------------------------------------

TEST(RegionEvent, SurfaceWithoutRegionsDispatchesNormally) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.no_regions")});
    // 刻意不呼叫 set_regions —— 此 surface 未登記任何子區域集合。

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.no_regions", [&](const RegionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(status, RouteStatus::Hit);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_FALSE(received[0].region_hit);
    EXPECT_TRUE(received[0].region_name.empty());
    EXPECT_TRUE(received[0].region_params.empty());
}

TEST(RegionEvent, HitSurfaceButMissNamedRegionDispatchesNormally) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.partial")});

    NamedRegionMap regions;
    RegionParams params;
    params["action"] = std::string("close");
    ASSERT_TRUE(regions.add_region("button.close", make_rect(2.0f, 2.0f), params));
    dispatcher.set_regions("panel.partial", std::move(regions));

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.partial", [&](const RegionEvent& e) { received.push_back(e); });

    // 命中 surface（20x20），但落在具名子區域（2x2）之外。
    const RouteStatus status = dispatcher.inject_down(MouseButton::Left, LocalPoint{15.0f, 15.0f});
    EXPECT_EQ(status, RouteStatus::Hit);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_FALSE(received[0].region_hit);
    EXPECT_TRUE(received[0].region_name.empty());
    EXPECT_TRUE(received[0].region_params.empty());
}

// 未命中任何 surface（E5-01 NoHit）→ 不分派給任何人。
TEST(RegionEvent, NoSurfaceHitDispatchesToNoOne) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    NamedRegionMap regions;
    ASSERT_TRUE(regions.add_region("button.close", make_rect(10.0f, 10.0f)));
    dispatcher.set_regions("panel.main", std::move(regions));

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.main", [&](const RegionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_down(MouseButton::Left, kOutside);
    EXPECT_EQ(status, RouteStatus::NoHit);
    EXPECT_TRUE(received.empty());
}

// -----------------------------------------------------------------------------
// 經 E5-01 滑鼠事件：down/up/click 動作與多擊 click_count 透傳
// -----------------------------------------------------------------------------

TEST(RegionEvent, BridgesMouseActionAndButton) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.main", [&](const RegionEvent& e) { received.push_back(e); });

    dispatcher.inject_down(MouseButton::Right, kInside);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].mouse.button, MouseButton::Right);
    EXPECT_EQ(received[0].mouse.action, ds::events::MouseAction::Down);
    EXPECT_EQ(received[0].mouse.click_count, 1u);

    dispatcher.inject_up(MouseButton::Right, kInside);
    // Up 相符游標 → 額外合成並分派 Click（同 E5-01 語意）。
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[1].mouse.action, ds::events::MouseAction::Up);
    EXPECT_EQ(received[2].mouse.action, ds::events::MouseAction::Click);
}

TEST(RegionEvent, BridgesDoubleClickCount) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.main", [&](const RegionEvent& e) { received.push_back(e); });

    dispatcher.inject_click(MouseButton::Left, kInside);
    dispatcher.inject_down(MouseButton::Left, kInside);

    ASSERT_FALSE(received.empty());
    EXPECT_EQ(received.back().mouse.click_count, 2u);
}

// -----------------------------------------------------------------------------
// 經 E1-05 區域查詢：不同 surface 各自對照獨立的 NamedRegionMap
// -----------------------------------------------------------------------------

TEST(RegionEvent, DifferentSurfacesHaveIndependentRegionMaps) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces(
        {MakeOpaqueRect("panel.a"), MakeOpaqueCircle("panel.b", LocalPoint{200.0f, 200.0f}, 5.0f)});

    NamedRegionMap regions_a;
    RegionParams params_a;
    params_a["owner"] = std::string("a");
    ASSERT_TRUE(regions_a.add_region("region.a1", make_rect(10.0f, 10.0f), params_a));
    dispatcher.set_regions("panel.a", std::move(regions_a));

    NamedRegionMap regions_b;
    RegionParams params_b;
    params_b["owner"] = std::string("b");
    ASSERT_TRUE(regions_b.add_region("region.b1", make_circle(LocalPoint{200.0f, 200.0f}, 5.0f),
                                      params_b));
    dispatcher.set_regions("panel.b", std::move(regions_b));

    std::vector<RegionEvent> received_a;
    std::vector<RegionEvent> received_b;
    dispatcher.subscribe("panel.a", [&](const RegionEvent& e) { received_a.push_back(e); });
    dispatcher.subscribe("panel.b", [&](const RegionEvent& e) { received_b.push_back(e); });

    dispatcher.inject_down(MouseButton::Left, kInside);
    dispatcher.inject_down(MouseButton::Left, kFarAway);

    ASSERT_EQ(received_a.size(), 1u);
    EXPECT_EQ(received_a[0].region_name, "region.a1");
    ASSERT_EQ(received_b.size(), 1u);
    EXPECT_EQ(received_b[0].region_name, "region.b1");
}

TEST(RegionEvent, HasRegionsAndRemoveRegions) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    EXPECT_FALSE(dispatcher.has_regions("panel.main"));

    NamedRegionMap regions;
    ASSERT_TRUE(regions.add_region("button.close", make_rect(10.0f, 10.0f)));
    dispatcher.set_regions("panel.main", std::move(regions));
    EXPECT_TRUE(dispatcher.has_regions("panel.main"));

    EXPECT_TRUE(dispatcher.remove_regions("panel.main"));
    EXPECT_FALSE(dispatcher.has_regions("panel.main"));
    EXPECT_FALSE(dispatcher.remove_regions("panel.main"));  // 未知（已移除）：no-op 回 false
}

// -----------------------------------------------------------------------------
// 訂閱 / 取消訂閱
// -----------------------------------------------------------------------------

TEST(RegionEvent, MultipleSubscribersAllReceiveInOrder) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    std::vector<int> order;
    dispatcher.subscribe("panel.main", [&](const RegionEvent&) { order.push_back(1); });
    dispatcher.subscribe("panel.main", [&](const RegionEvent&) { order.push_back(2); });

    dispatcher.inject_down(MouseButton::Left, kInside);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(RegionEvent, UnsubscribeStopsReceivingEvents) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    int count = 0;
    const auto id = dispatcher.subscribe("panel.main", [&](const RegionEvent&) { ++count; });
    ASSERT_NE(id, 0u);

    dispatcher.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(count, 1);

    EXPECT_TRUE(dispatcher.unsubscribe(id));
    dispatcher.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(count, 1);  // 取消後不再收
}

TEST(RegionEvent, UnsubscribeUnknownIdIsNoOp) {
    RegionEventDispatcher dispatcher;
    EXPECT_FALSE(dispatcher.unsubscribe(999999));
}

TEST(RegionEvent, SubscribeRejectsEmptyTargetOrListener) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    EXPECT_EQ(dispatcher.subscribe("", [](const RegionEvent&) {}), 0u);
    EXPECT_EQ(dispatcher.subscribe("panel.main", nullptr), 0u);
}

TEST(RegionEvent, ListenerCountReflectsSubscriptions) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.main")});

    EXPECT_EQ(dispatcher.listener_count("panel.main"), 0u);
    const auto id1 = dispatcher.subscribe("panel.main", [](const RegionEvent&) {});
    dispatcher.subscribe("panel.main", [](const RegionEvent&) {});
    EXPECT_EQ(dispatcher.listener_count("panel.main"), 2u);

    dispatcher.unsubscribe(id1);
    EXPECT_EQ(dispatcher.listener_count("panel.main"), 1u);
}

// set_surfaces() 可重複呼叫（整組取代），橋接仍對新清單正確運作。
TEST(RegionEvent, SetSurfacesReplacesSurfaceSetAndRebridges) {
    RegionEventDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.old")});

    NamedRegionMap regions;
    ASSERT_TRUE(regions.add_region("button.old", make_rect(10.0f, 10.0f)));
    dispatcher.set_regions("panel.old", std::move(regions));

    std::vector<RegionEvent> received;
    dispatcher.subscribe("panel.new", [&](const RegionEvent& e) { received.push_back(e); });

    // 取代 surface 集合為新的具名 surface。
    dispatcher.set_surfaces({MakeOpaqueRect("panel.new")});
    NamedRegionMap new_regions;
    ASSERT_TRUE(new_regions.add_region("button.new", make_rect(10.0f, 10.0f)));
    dispatcher.set_regions("panel.new", std::move(new_regions));

    const RouteStatus status = dispatcher.inject_down(MouseButton::Left, kInside);
    EXPECT_EQ(status, RouteStatus::Hit);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].mouse.target, "panel.new");
    EXPECT_TRUE(received[0].region_hit);
    EXPECT_EQ(received[0].region_name, "button.new");
}
