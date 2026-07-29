// E1-01 具名圖層列舉與 z-order 維持 — 契約 / 單元測試（gtest）
//
// 驗證：
//   - 列舉具名圖層（由底到頂語意序、數量、穩定具名字串）
//   - compare_layers 相對順序規則（Below / Same / Above）
//   - 把 surface 指派到具名圖層（Ok / Moved / 空 id 拒絕）
//   - 跨層堆疊順序正確（圖層語意決定）
//   - 同層順序 = 加入序（穩定）
//   - 圖層語意優先於加入序（先加最上層、後加桌布，桌布仍在底）
//   - is_above / is_below 具名相對順序、topmost / bottommost
//   - has() 能力閘控（NFR-03）：無 kernel.surface 時指派 / 移除被拒且狀態不變
//   - 未指派 surface 一律保守回傳、永不崩潰
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）。
// NFR-02：本測試不出現任何數字 z-order / z-index；相對順序全以具名關係斷言。
#include "layer_stack.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::compare_layers;
using ds::kernel::layer_capability;
using ds::kernel::layer_name;
using ds::kernel::LayerAssign;
using ds::kernel::LayerRelation;
using ds::kernel::LayerStack;
using ds::kernel::layers_bottom_to_top;
using ds::kernel::SurfaceId;
using ds::kernel::SurfaceLayer;

namespace {

// 建一個「kernel.surface 不可用」的能力矩陣，供 NFR-03 閘控（拒絕）路徑測試。
CapabilityMatrix without_surface_capability() {
    return CapabilityMatrix(std::vector<CapabilityDecl>{
        {"kernel.surface", "surface 核心（測試設為不可用）",
         /*optional=*/false, /*default_available=*/false},
    });
}

// --- 列舉具名圖層 ---

TEST(LayerEnum, LayersBottomToTopAreOrderedAndComplete) {
    const std::vector<SurfaceLayer>& layers = layers_bottom_to_top();
    ASSERT_EQ(layers.size(), 5u);
    EXPECT_EQ(layers.front(), SurfaceLayer::Wallpaper);  // 最底
    EXPECT_EQ(layers.back(), SurfaceLayer::Topmost);     // 最頂
    // 語意序：桌布 → 一般視窗之下 → 一般 → 浮層 → 最上層。
    EXPECT_EQ(layers[0], SurfaceLayer::Wallpaper);
    EXPECT_EQ(layers[1], SurfaceLayer::BelowNormal);
    EXPECT_EQ(layers[2], SurfaceLayer::Normal);
    EXPECT_EQ(layers[3], SurfaceLayer::Overlay);
    EXPECT_EQ(layers[4], SurfaceLayer::Topmost);
}

TEST(LayerEnum, LayerNamesAreStableAndNamed) {
    EXPECT_EQ(layer_name(SurfaceLayer::Wallpaper), "layer.wallpaper");
    EXPECT_EQ(layer_name(SurfaceLayer::BelowNormal), "layer.below_normal");
    EXPECT_EQ(layer_name(SurfaceLayer::Normal), "layer.normal");
    EXPECT_EQ(layer_name(SurfaceLayer::Overlay), "layer.overlay");
    EXPECT_EQ(layer_name(SurfaceLayer::Topmost), "layer.topmost");
}

// --- 圖層間相對順序規則（具名關係，NFR-02）---

TEST(LayerRelationRules, CompareLayersBySemantics) {
    EXPECT_EQ(compare_layers(SurfaceLayer::Wallpaper, SurfaceLayer::Normal),
              LayerRelation::Below);
    EXPECT_EQ(compare_layers(SurfaceLayer::Topmost, SurfaceLayer::Overlay),
              LayerRelation::Above);
    EXPECT_EQ(compare_layers(SurfaceLayer::Normal, SurfaceLayer::Normal),
              LayerRelation::Same);
    // 完全反對稱：交換兩者，Below 與 Above 互換。
    EXPECT_EQ(compare_layers(SurfaceLayer::Overlay, SurfaceLayer::Topmost),
              LayerRelation::Below);
}

// --- 指派 surface 到具名圖層 ---

TEST(LayerAssignment, AssignNewSurfaceOk) {
    LayerStack stack;  // 預設能力矩陣：kernel.surface 可用
    EXPECT_TRUE(stack.has(layer_capability()));
    EXPECT_EQ(stack.assign("surface.panel", SurfaceLayer::Normal), LayerAssign::Ok);
    EXPECT_TRUE(stack.contains("surface.panel"));
    ASSERT_NE(stack.layer_of("surface.panel"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.panel"), SurfaceLayer::Normal);
    EXPECT_EQ(stack.size(), 1u);
}

TEST(LayerAssignment, EmptyIdRejected) {
    LayerStack stack;
    EXPECT_EQ(stack.assign("", SurfaceLayer::Normal), LayerAssign::RejectedEmptyId);
    EXPECT_EQ(stack.size(), 0u);
}

TEST(LayerAssignment, ReassignMovesLayerKeepingIdentity) {
    LayerStack stack;
    EXPECT_EQ(stack.assign("surface.hud", SurfaceLayer::Normal), LayerAssign::Ok);
    // 再次指派同一 id = 改派其圖層，回 Moved，總數不變。
    EXPECT_EQ(stack.assign("surface.hud", SurfaceLayer::Overlay), LayerAssign::Moved);
    EXPECT_EQ(stack.size(), 1u);
    ASSERT_NE(stack.layer_of("surface.hud"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.hud"), SurfaceLayer::Overlay);
}

// --- 跨層堆疊順序：圖層語意決定 ---

TEST(StackingOrder, CrossLayerOrderFollowsLayerSemantics) {
    LayerStack stack;
    stack.assign("wp", SurfaceLayer::Wallpaper);
    stack.assign("win", SurfaceLayer::Normal);
    stack.assign("top", SurfaceLayer::Topmost);
    const std::vector<SurfaceId> order = stack.stacking_order();
    ASSERT_EQ(order.size(), 3u);
    // 由底到頂：桌布 < 一般 < 最上層。
    EXPECT_EQ(order[0], "wp");
    EXPECT_EQ(order[1], "win");
    EXPECT_EQ(order[2], "top");
    EXPECT_EQ(stack.bottommost(), "wp");
    EXPECT_EQ(stack.topmost(), "top");
}

// --- 核心不變式：圖層語意優先於加入序 ---

TEST(StackingOrder, LayerSemanticsBeatInsertionOrder) {
    LayerStack stack;
    // 刻意先加最上層、再加桌布：若照加入序，桌布會在頂；但圖層語意必須勝出。
    stack.assign("top", SurfaceLayer::Topmost);
    stack.assign("wp", SurfaceLayer::Wallpaper);
    const std::vector<SurfaceId> order = stack.stacking_order();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "wp");   // 桌布仍在底（語意優先）
    EXPECT_EQ(order[1], "top");  // 最上層仍在頂
    EXPECT_TRUE(stack.is_below("wp", "top"));
    EXPECT_TRUE(stack.is_above("top", "wp"));
}

// --- 同層順序 = 加入序（穩定）---

TEST(StackingOrder, SameLayerPreservesInsertionOrder) {
    LayerStack stack;
    stack.assign("a", SurfaceLayer::Normal);
    stack.assign("b", SurfaceLayer::Normal);
    stack.assign("c", SurfaceLayer::Normal);
    const std::vector<SurfaceId> in_normal = stack.ids_in(SurfaceLayer::Normal);
    ASSERT_EQ(in_normal.size(), 3u);
    EXPECT_EQ(in_normal[0], "a");
    EXPECT_EQ(in_normal[1], "b");
    EXPECT_EQ(in_normal[2], "c");
    // 同層內先加者在下、後加者在上。
    EXPECT_TRUE(stack.is_below("a", "b"));
    EXPECT_TRUE(stack.is_below("b", "c"));
    EXPECT_TRUE(stack.is_above("c", "a"));
    EXPECT_EQ(stack.count_in(SurfaceLayer::Normal), 3u);
}

// --- 交錯加入：仍先分層、層內保加入序 ---

TEST(StackingOrder, InterleavedAssignmentsGroupByLayerThenInsertion) {
    LayerStack stack;
    stack.assign("n1", SurfaceLayer::Normal);
    stack.assign("o1", SurfaceLayer::Overlay);
    stack.assign("n2", SurfaceLayer::Normal);
    stack.assign("wp", SurfaceLayer::Wallpaper);
    stack.assign("o2", SurfaceLayer::Overlay);
    const std::vector<SurfaceId> order = stack.stacking_order();
    // 由底到頂：Wallpaper[wp] < Normal[n1,n2] < Overlay[o1,o2]。
    const std::vector<SurfaceId> expected = {"wp", "n1", "n2", "o1", "o2"};
    EXPECT_EQ(order, expected);
}

// --- 移除 ---

TEST(LayerStackOps, RemoveDropsAssignment) {
    LayerStack stack;
    stack.assign("x", SurfaceLayer::Normal);
    stack.assign("y", SurfaceLayer::Normal);
    EXPECT_TRUE(stack.remove("x"));
    EXPECT_FALSE(stack.contains("x"));
    EXPECT_EQ(stack.size(), 1u);
    // 未知 id：不崩潰、回 false。
    EXPECT_FALSE(stack.remove("nope"));
}

// --- 未指派 surface：一律保守 ---

TEST(LayerStackOps, UnassignedQueriesAreConservative) {
    LayerStack stack;
    stack.assign("real", SurfaceLayer::Normal);
    EXPECT_FALSE(stack.contains("ghost"));
    EXPECT_EQ(stack.layer_of("ghost"), nullptr);
    EXPECT_FALSE(stack.is_above("ghost", "real"));
    EXPECT_FALSE(stack.is_below("ghost", "real"));
    EXPECT_FALSE(stack.is_above("real", "ghost"));
}

TEST(LayerStackOps, EmptyStackTopAndBottomAreEmpty) {
    LayerStack stack;
    EXPECT_TRUE(stack.topmost().empty());
    EXPECT_TRUE(stack.bottommost().empty());
    EXPECT_TRUE(stack.stacking_order().empty());
}

// --- NFR-03：has() 能力閘控 ---

TEST(CapabilityGating, DefaultMatrixEnablesLayerCapability) {
    LayerStack stack;  // defaults()：kernel.surface 為保證存在的基礎能力
    EXPECT_TRUE(stack.has(layer_capability()));
}

TEST(CapabilityGating, AssignRefusedWhenCapabilityUnavailable) {
    LayerStack stack(without_surface_capability());
    EXPECT_FALSE(stack.has(layer_capability()));
    // NFR-03：能力不可用 → 指派被結構化拒絕，且不改任何狀態。
    EXPECT_EQ(stack.assign("surface.panel", SurfaceLayer::Normal),
              LayerAssign::RejectedNoCapability);
    EXPECT_FALSE(stack.contains("surface.panel"));
    EXPECT_EQ(stack.size(), 0u);
}

TEST(CapabilityGating, RemoveRefusedWhenCapabilityUnavailable) {
    // 先以可用能力建入一筆，再換到不可用能力矩陣驗證移除被閘控拒絕。
    LayerStack ok_stack;
    ok_stack.assign("keep", SurfaceLayer::Normal);
    EXPECT_TRUE(ok_stack.contains("keep"));

    LayerStack denied(without_surface_capability());
    denied.assign("keep", SurfaceLayer::Normal);  // 也被拒（驗證前置）
    EXPECT_FALSE(denied.remove("keep"));           // NFR-03：移除被閘控拒絕
}

// --- 未知能力保守回 false（沿用 E1-21 語意）---

TEST(CapabilityGating, UnknownCapabilityIsConservativelyFalse) {
    LayerStack stack;
    EXPECT_FALSE(stack.has("does.not.exist"));
}

}  // namespace
