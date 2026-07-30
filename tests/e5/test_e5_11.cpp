// E5-11 可點區段選取事件 — 單元測試（gtest）
//
// 驗證 SpanSelectionDispatcher 於相位 1（Mac / null 期）橋接 E5-01 `MouseEventRouter`（滑鼠
// 事件注入 / 命中路由）與 E4-12 `ClickableTextElement`（可點文字區段的命中查詢）：
//   - 點擊命中已標記區段 → 派發之 SpanSelectionEvent 帶區段具名 id
//   - 經 E5-01 的 down/up（合成 Click）與 inject_click 皆能觸發（僅 Click 動作判定區段）
//   - 經 E4-12 的 hit_span 命中查詢（多區段各自獨立命中、字元間隙不算命中）
//   - 無區段命中的點擊不發選取事件（未標記字元、未綁定區段來源、未命中任何 surface）
//   - 訂閱 / 取消訂閱（含無效訂閱、未知 id 取消 no-op、多訂閱者依序皆收）
//   - bind_span_source / has_span_source / unbind_span_source
//   - set_surfaces() 整組取代後橋接對新清單正確運作
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實滑鼠後端，事件皆為注入式。
#include "span_selection_dispatcher.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "clickable_text.hpp"
#include "hit_test.hpp"
#include "text_layout.hpp"

using ds::elements::ClickableTextElement;
using ds::events::MouseButton;
using ds::events::SpanSelectionDispatcher;
using ds::events::SpanSelectionEvent;
using ds::events::RouteStatus;

using ds::kernel::AlphaMode;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_rect;
using ds::kernel::SurfaceLayer;

using ds::render::FixedFontMetrics;

namespace {

// 一個本地座標矩形 surface（不透明）；同 e4_12 / e5_01 / e5_14 測試慣例。
HitSurface MakeOpaqueRect(const char* id, float width, float height,
                          SurfaceLayer layer = SurfaceLayer::Normal) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(width, height);
    s.layer = layer;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

// 等寬度量：advance=10、行高=20、ascent=16（承 E4-01/E4-12 測試慣例）。
FixedFontMetrics Mono() { return FixedFontMetrics(10.0, 20.0, 16.0); }

// 建一個「hello world」的可點文字元件，標記兩個區段：
//   [0,5) "hello" → id "greet"（字符 x in [0,50)）
//   [6,11) "world" → id "target"（字符 x in [60,110)）
// 索引 5（空白）不屬於任何區段（字符 x in [50,60)）。
ClickableTextElement MakeHelloWorld(const FixedFontMetrics& fm) {
    ClickableTextElement el(fm);
    el.set_text("hello world");
    el.add_span(0, 5, "greet");
    el.add_span(6, 11, "target");
    return el;
}

constexpr LocalPoint kInGreet{25.0f, 10.0f};    // 'l'（index 2），落在 "greet" 區段內
constexpr LocalPoint kInTarget{65.0f, 10.0f};   // 'o'（index 7），落在 "target" 區段內
constexpr LocalPoint kInGap{55.0f, 10.0f};      // 空白（index 5），落在 surface 內但無區段
constexpr LocalPoint kOutsideSurface{500.0f, 500.0f};  // 不落在任何 surface 內

}  // namespace

// -----------------------------------------------------------------------------
// 點擊命中已標記區段 → 選取事件帶區段 id
// -----------------------------------------------------------------------------

TEST(SpanSelection, ClickOnSpanDispatchesSelectionEventWithId) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_click(MouseButton::Left, kInGreet);
    EXPECT_EQ(status, RouteStatus::Hit);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].span_id, "greet");
    EXPECT_EQ(received[0].mouse.target, "panel.text");
    EXPECT_EQ(received[0].mouse.action, ds::events::MouseAction::Click);
}

// -----------------------------------------------------------------------------
// 經 E5-01 點擊：down/up 合成 Click 觸發；Down/Up 本身不觸發（僅 Click 判定區段）
// -----------------------------------------------------------------------------

TEST(SpanSelection, DownUpSequenceDispatchesExactlyOneSelectionViaSyntheticClick) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent& e) { received.push_back(e); });

    dispatcher.inject_down(MouseButton::Left, kInTarget);
    dispatcher.inject_up(MouseButton::Left, kInTarget);

    // Down 本身、Up 本身皆不判定區段；Up 相符游標合成的 Click 才觸發一次選取。
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].span_id, "target");
}

TEST(SpanSelection, InjectClickBypassesDownUpStateAndStillDispatches) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent& e) { received.push_back(e); });

    dispatcher.inject_click(MouseButton::Left, kInGreet);
    dispatcher.inject_click(MouseButton::Left, kInTarget);

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].span_id, "greet");
    EXPECT_EQ(received[1].span_id, "target");
}

// -----------------------------------------------------------------------------
// 經 E4-12 命中：多區段各自獨立命中
// -----------------------------------------------------------------------------

TEST(SpanSelection, MultipleSpansHitIndependently) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<std::string> ids;
    dispatcher.subscribe("panel.text",
                         [&](const SpanSelectionEvent& e) { ids.push_back(e.span_id); });

    dispatcher.inject_click(MouseButton::Left, kInGreet);
    dispatcher.inject_click(MouseButton::Left, kInTarget);

    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], "greet");
    EXPECT_EQ(ids[1], "target");
}

// -----------------------------------------------------------------------------
// 無區段命中的點擊不發選取事件
// -----------------------------------------------------------------------------

TEST(SpanSelection, ClickInGapBetweenSpansDoesNotDispatch) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_click(MouseButton::Left, kInGap);
    EXPECT_EQ(status, RouteStatus::Hit);  // 命中 surface，但字元間隙不屬於任何區段
    EXPECT_TRUE(received.empty());
}

TEST(SpanSelection, SurfaceWithoutBoundElementDoesNotDispatch) {
    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.unbound", 110.0f, 20.0f)});
    // 刻意不呼叫 bind_span_source。

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.unbound",
                         [&](const SpanSelectionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_click(MouseButton::Left, kInGreet);
    EXPECT_EQ(status, RouteStatus::Hit);
    EXPECT_TRUE(received.empty());
}

TEST(SpanSelection, NoSurfaceHitDispatchesToNoOne) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent& e) { received.push_back(e); });

    const RouteStatus status = dispatcher.inject_click(MouseButton::Left, kOutsideSurface);
    EXPECT_EQ(status, RouteStatus::NoHit);
    EXPECT_TRUE(received.empty());
}

// -----------------------------------------------------------------------------
// bind_span_source / has_span_source / unbind_span_source
// -----------------------------------------------------------------------------

TEST(SpanSelection, HasSpanSourceAndUnbindSpanSource) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});

    EXPECT_FALSE(dispatcher.has_span_source("panel.text"));
    dispatcher.bind_span_source("panel.text", el);
    EXPECT_TRUE(dispatcher.has_span_source("panel.text"));

    EXPECT_TRUE(dispatcher.unbind_span_source("panel.text"));
    EXPECT_FALSE(dispatcher.has_span_source("panel.text"));
    EXPECT_FALSE(dispatcher.unbind_span_source("panel.text"));  // 已移除：no-op 回 false

    // 解除綁定後，命中 surface 的點擊不再判定區段。
    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent& e) { received.push_back(e); });
    dispatcher.inject_click(MouseButton::Left, kInGreet);
    EXPECT_TRUE(received.empty());
}

TEST(SpanSelection, BindSpanSourceRejectsEmptySurface) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.bind_span_source("", el);
    EXPECT_FALSE(dispatcher.has_span_source(""));
}

// -----------------------------------------------------------------------------
// 訂閱 / 取消訂閱
// -----------------------------------------------------------------------------

TEST(SpanSelection, MultipleSubscribersAllReceiveInOrder) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    std::vector<int> order;
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent&) { order.push_back(1); });
    dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent&) { order.push_back(2); });

    dispatcher.inject_click(MouseButton::Left, kInGreet);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(SpanSelection, UnsubscribeStopsReceivingEvents) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.text", el);

    int count = 0;
    const auto id =
        dispatcher.subscribe("panel.text", [&](const SpanSelectionEvent&) { ++count; });
    ASSERT_NE(id, 0u);

    dispatcher.inject_click(MouseButton::Left, kInGreet);
    EXPECT_EQ(count, 1);

    EXPECT_TRUE(dispatcher.unsubscribe(id));
    dispatcher.inject_click(MouseButton::Left, kInGreet);
    EXPECT_EQ(count, 1);  // 取消後不再收
}

TEST(SpanSelection, UnsubscribeUnknownIdIsNoOp) {
    SpanSelectionDispatcher dispatcher;
    EXPECT_FALSE(dispatcher.unsubscribe(999999));
}

TEST(SpanSelection, SubscribeRejectsEmptyTargetOrListener) {
    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});

    EXPECT_EQ(dispatcher.subscribe("", [](const SpanSelectionEvent&) {}), 0u);
    EXPECT_EQ(dispatcher.subscribe("panel.text", nullptr), 0u);
}

TEST(SpanSelection, ListenerCountReflectsSubscriptions) {
    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.text", 110.0f, 20.0f)});

    EXPECT_EQ(dispatcher.listener_count("panel.text"), 0u);
    const auto id1 = dispatcher.subscribe("panel.text", [](const SpanSelectionEvent&) {});
    dispatcher.subscribe("panel.text", [](const SpanSelectionEvent&) {});
    EXPECT_EQ(dispatcher.listener_count("panel.text"), 2u);

    dispatcher.unsubscribe(id1);
    EXPECT_EQ(dispatcher.listener_count("panel.text"), 1u);
}

// -----------------------------------------------------------------------------
// set_surfaces() 整組取代後橋接對新清單正確運作
// -----------------------------------------------------------------------------

TEST(SpanSelection, SetSurfacesReplacesSurfaceSetAndRebridges) {
    FixedFontMetrics fm = Mono();
    ClickableTextElement el_old = MakeHelloWorld(fm);
    ClickableTextElement el_new = MakeHelloWorld(fm);

    SpanSelectionDispatcher dispatcher;
    dispatcher.set_surfaces({MakeOpaqueRect("panel.old", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.old", el_old);

    std::vector<SpanSelectionEvent> received;
    dispatcher.subscribe("panel.new", [&](const SpanSelectionEvent& e) { received.push_back(e); });

    // 取代 surface 集合為新的具名 surface，並綁定新的區段來源。
    dispatcher.set_surfaces({MakeOpaqueRect("panel.new", 110.0f, 20.0f)});
    dispatcher.bind_span_source("panel.new", el_new);

    const RouteStatus status = dispatcher.inject_click(MouseButton::Left, kInGreet);
    EXPECT_EQ(status, RouteStatus::Hit);
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].mouse.target, "panel.new");
    EXPECT_EQ(received[0].span_id, "greet");
}
