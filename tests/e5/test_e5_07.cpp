// E5-07 區域內連續拖曳判定 — 單元測試（gtest）
//
// 驗證 RegionDragRecognizer 於相位 1（Mac / null 期）把一連串指標事件（Down/Move/Up）
// 判定為「在某具名區域內的連續拖曳」：
//   - 按下（Down）落在具名區域內 → 起拖，分派 Begin
//   - 移動（Move）持續落在同一具名區域內 → 分派 Move（可多次）
//   - 放開（Up）仍在同一具名區域內 → 分派 End，拖曳結束
//   - 中途移動離開該具名區域（含落到別的區域 / 離開整個 surface）→ 分派 Cancel，拖曳中斷
//   - 未在具名區域內按下 → 不起拖（no-op，無事件分派）
//   - 多次拖曳序列（Begin→...→End / Cancel 可重複多輪）
//   - 狀態機不亂序：非拖曳中的 Move / Up 為 no-op；拖曳中重複 Down 為 no-op
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實滑鼠後端，事件皆為注入式。
#include "region_drag_recognizer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::DragEvent;
using ds::events::DragEventKind;
using ds::events::PointerAction;
using ds::events::RegionDragRecognizer;

using ds::kernel::AlphaMode;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_rect;
using ds::kernel::NamedRegionMap;
using ds::kernel::RegionParams;

namespace {

// 一個 20x20 本地座標的不透明矩形 surface；同 e5_01 / e1_05 / e5_14 測試慣例。
HitSurface MakeOpaqueRect(const char* id) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(20.0f, 20.0f);
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

// 一個 20x20 surface，登記單一具名子區域 "hotspot"（10x10，左上角）+ 一組參數。
NamedRegionMap MakeRegionsWithHotspot() {
    NamedRegionMap regions;
    RegionParams params;
    params["kind"] = std::string("draggable");
    regions.add_region("hotspot", make_rect(10.0f, 10.0f), params);
    return regions;
}

constexpr LocalPoint kInHotspot{5.0f, 5.0f};      // 落在 10x10 "hotspot" 子區域內
constexpr LocalPoint kInHotspot2{6.0f, 4.0f};     // 仍落在 "hotspot" 內（另一個點）
constexpr LocalPoint kInSurfaceOutsideHotspot{15.0f, 15.0f};  // 落在 surface 內但非 hotspot
constexpr LocalPoint kOutsideSurface{99.0f, 99.0f};           // 完全不落在 surface 內

RegionDragRecognizer MakeRecognizer() {
    return RegionDragRecognizer(MakeOpaqueRect("panel.drag"), MakeRegionsWithHotspot());
}

}  // namespace

// -----------------------------------------------------------------------------
// 按下區域內起拖
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, DownInsideRegionBeginsDrag) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);

    EXPECT_TRUE(recognizer.dragging());
    EXPECT_EQ(recognizer.active_region(), "hotspot");
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[0].surface, "panel.drag");
    EXPECT_EQ(received[0].region_name, "hotspot");
    ASSERT_TRUE(received[0].region_params.count("kind"));
    EXPECT_EQ(std::get<std::string>(received[0].region_params.at("kind")), "draggable");
}

// -----------------------------------------------------------------------------
// 移動保持在區域內產生 Move
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, MoveWithinRegionEmitsMove) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);
    recognizer.feed_move(kInHotspot2);
    recognizer.feed_move(kInHotspot);

    EXPECT_TRUE(recognizer.dragging());
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[1].kind, DragEventKind::Move);
    EXPECT_EQ(received[1].region_name, "hotspot");
    EXPECT_EQ(received[2].kind, DragEventKind::Move);
}

// -----------------------------------------------------------------------------
// 放開結束拖曳
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, UpWithinRegionEndsDrag) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);
    recognizer.feed_move(kInHotspot2);
    recognizer.feed_up(kInHotspot);

    EXPECT_FALSE(recognizer.dragging());
    EXPECT_EQ(recognizer.active_region(), "");
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[1].kind, DragEventKind::Move);
    EXPECT_EQ(received[2].kind, DragEventKind::End);
    EXPECT_EQ(received[2].region_name, "hotspot");
}

// -----------------------------------------------------------------------------
// 中途離開區域中斷（Cancel）—— 移動到 surface 內但非該子區域
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, MoveLeavingRegionCancelsDrag) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);
    recognizer.feed_move(kInSurfaceOutsideHotspot);  // 離開 hotspot（但仍在 surface 內）

    EXPECT_FALSE(recognizer.dragging());
    EXPECT_EQ(recognizer.active_region(), "");
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[1].kind, DragEventKind::Cancel);
    EXPECT_EQ(received[1].region_name, "hotspot");  // 中斷前所在的具名區域

    // 中斷後再次移動 → 已是 Idle，no-op（狀態機不亂序）。
    recognizer.feed_move(kInHotspot);
    EXPECT_EQ(received.size(), 2u);
}

// -----------------------------------------------------------------------------
// 中途離開區域中斷（Cancel）—— 移動到完全離開 surface
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, MoveLeavingSurfaceEntirelyCancelsDrag) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);
    recognizer.feed_move(kOutsideSurface);

    EXPECT_FALSE(recognizer.dragging());
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[1].kind, DragEventKind::Cancel);
}

// -----------------------------------------------------------------------------
// Up 時已離開區域 → Cancel（而非 End）
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, UpOutsideRegionCancelsInsteadOfEnds) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);

    // 直接以 Up 落在非 hotspot 處（未經中間 Move 離開事件），仍應判定為 Cancel。
    recognizer.feed_up(kInSurfaceOutsideHotspot);

    EXPECT_FALSE(recognizer.dragging());
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[1].kind, DragEventKind::Cancel);
}

// -----------------------------------------------------------------------------
// 未在區域內按下不起拖
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, DownOutsideRegionDoesNotBeginDrag) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInSurfaceOutsideHotspot);  // 命中 surface，但非具名子區域
    EXPECT_FALSE(recognizer.dragging());
    EXPECT_TRUE(received.empty());

    recognizer.feed_down(kOutsideSurface);  // 完全未命中 surface
    EXPECT_FALSE(recognizer.dragging());
    EXPECT_TRUE(received.empty());
}

// -----------------------------------------------------------------------------
// 多次拖曳序列
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, MultipleDragSequencesInARow) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    // 第一輪：正常完成（Begin → Move → End）。
    recognizer.feed_down(kInHotspot);
    recognizer.feed_move(kInHotspot2);
    recognizer.feed_up(kInHotspot);

    // 第二輪：中途取消（Begin → Cancel）。
    recognizer.feed_down(kInHotspot2);
    recognizer.feed_move(kInSurfaceOutsideHotspot);

    // 第三輪：再次正常完成（Begin → End）。
    recognizer.feed_down(kInHotspot);
    recognizer.feed_up(kInHotspot2);

    ASSERT_EQ(received.size(), 7u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[1].kind, DragEventKind::Move);
    EXPECT_EQ(received[2].kind, DragEventKind::End);
    EXPECT_EQ(received[3].kind, DragEventKind::Begin);
    EXPECT_EQ(received[4].kind, DragEventKind::Cancel);
    EXPECT_EQ(received[5].kind, DragEventKind::Begin);
    EXPECT_EQ(received[6].kind, DragEventKind::End);
    EXPECT_FALSE(recognizer.dragging());
}

// -----------------------------------------------------------------------------
// 狀態機不亂序：非拖曳中的 Move / Up 為 no-op
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, MoveAndUpWithoutActiveDragAreNoOps) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_move(kInHotspot);  // 尚未按下，忽略
    recognizer.feed_up(kInHotspot);    // 尚未按下，忽略

    EXPECT_FALSE(recognizer.dragging());
    EXPECT_TRUE(received.empty());
}

// -----------------------------------------------------------------------------
// 狀態機不亂序：拖曳中重複 Down 為 no-op（不重新起拖 / 不重複分派 Begin）
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, RepeatedDownWhileDraggingIsNoOp) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed_down(kInHotspot);
    recognizer.feed_down(kInHotspot2);  // 已在拖曳中：忽略，不重新起拖

    EXPECT_TRUE(recognizer.dragging());
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);

    recognizer.feed_up(kInHotspot);
    EXPECT_FALSE(recognizer.dragging());
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[1].kind, DragEventKind::End);
}

// -----------------------------------------------------------------------------
// 統一入口 feed() 與 feed_down/feed_move/feed_up 等價
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, UnifiedFeedEntryPointMatchesDedicatedMethods) {
    RegionDragRecognizer recognizer = MakeRecognizer();
    std::vector<DragEvent> received;
    recognizer.subscribe([&](const DragEvent& e) { received.push_back(e); });

    recognizer.feed(PointerAction::Down, kInHotspot);
    recognizer.feed(PointerAction::Move, kInHotspot2);
    recognizer.feed(PointerAction::Up, kInHotspot);

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0].kind, DragEventKind::Begin);
    EXPECT_EQ(received[1].kind, DragEventKind::Move);
    EXPECT_EQ(received[2].kind, DragEventKind::End);
}

// -----------------------------------------------------------------------------
// 訂閱 / 取消訂閱：無效訂閱、未知 id 取消 no-op、多訂閱者依序皆收
// -----------------------------------------------------------------------------

TEST(RegionDragRecognizer, SubscriptionLifecycle) {
    RegionDragRecognizer recognizer = MakeRecognizer();

    // 無效訂閱：空 listener。
    EXPECT_EQ(recognizer.subscribe(nullptr), 0u);
    EXPECT_EQ(recognizer.listener_count(), 0u);

    // 未知 id 取消：no-op。
    EXPECT_FALSE(recognizer.unsubscribe(12345));

    std::vector<int> order;
    const auto id1 = recognizer.subscribe([&](const DragEvent&) { order.push_back(1); });
    const auto id2 = recognizer.subscribe([&](const DragEvent&) { order.push_back(2); });
    ASSERT_NE(id1, 0u);
    ASSERT_NE(id2, 0u);
    EXPECT_EQ(recognizer.listener_count(), 2u);

    recognizer.feed_down(kInHotspot);
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);

    EXPECT_TRUE(recognizer.unsubscribe(id1));
    EXPECT_EQ(recognizer.listener_count(), 1u);
    EXPECT_FALSE(recognizer.unsubscribe(id1));  // 重複取消：no-op

    order.clear();
    recognizer.feed_up(kInHotspot);
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 2);
}
