// E4-08 圖層合成 — 單元測試（gtest）
//
// 涵蓋：多層合成（依加入順序產出合成計畫）、混合模式（normal/multiply/screen/overlay）、
// 每層透明度（含 clamp）、層序（加入 / 移除後仍具名有序）、經 E4-06 `SurfaceSwitcher`
// 定址存在性檢查、移除層、各類無效輸入報錯不靜默、NFR-02（具名清單，不外露數字索引 /
// z-order）。全程純記憶體邏輯，不涉及任何真實像素合成 / 平台後端。
#include "layer_compositor.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "surface_switcher.hpp"

using ds::render::BlendMode;
using ds::render::CompositeStatus;
using ds::render::CompositionPlan;
using ds::render::LayerCompositor;
using ds::render::SurfaceSwitcher;
using ds::render::SwitchStatus;

namespace {

using SurfaceId = ds::kernel::SurfaceId;

// 建一顆已註冊三個具名 surface 的 switcher，供多數測試共用。
SurfaceSwitcher make_switcher_with(const std::vector<SurfaceId>& ids) {
    SurfaceSwitcher sw;
    for (const auto& id : ids) {
        [[maybe_unused]] auto status = sw.register_surface(id);
    }
    return sw;
}

}  // namespace

// --- 多層合成 ---------------------------------------------------------------

TEST(Compose, EmptyCompositorProducesEmptyPlan) {
    SurfaceSwitcher sw = make_switcher_with({"layer.bg"});
    LayerCompositor comp(sw);

    CompositionPlan plan = comp.compose();
    EXPECT_TRUE(plan.layers.empty());
}

TEST(Compose, MultipleLayersAppearInAddedOrder) {
    SurfaceSwitcher sw = make_switcher_with({"layer.bg", "layer.mid", "layer.fg"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.bg", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.mid", BlendMode::Multiply, 0.5f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.fg", BlendMode::Screen, 0.75f), CompositeStatus::Ok);

    CompositionPlan plan = comp.compose();
    ASSERT_EQ(plan.layers.size(), 3u);
    EXPECT_EQ(plan.layers[0].surface_id, "layer.bg");
    EXPECT_EQ(plan.layers[1].surface_id, "layer.mid");
    EXPECT_EQ(plan.layers[2].surface_id, "layer.fg");
}

TEST(Compose, LayerCountAndHasLayerReflectAdditions) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a", "layer.b"});
    LayerCompositor comp(sw);
    EXPECT_EQ(comp.layer_count(), 0u);
    EXPECT_FALSE(comp.has_layer("layer.a"));

    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    EXPECT_EQ(comp.layer_count(), 1u);
    EXPECT_TRUE(comp.has_layer("layer.a"));
    EXPECT_FALSE(comp.has_layer("layer.b"));
}

// --- 混合模式 ----------------------------------------------------------------

TEST(BlendModeField, EachLayerRetainsItsOwnBlendMode) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a", "layer.b", "layer.c", "layer.d"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.b", BlendMode::Multiply, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.c", BlendMode::Screen, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.d", BlendMode::Overlay, 1.0f), CompositeStatus::Ok);

    CompositionPlan plan = comp.compose();
    ASSERT_EQ(plan.layers.size(), 4u);
    EXPECT_EQ(plan.layers[0].blend_mode, BlendMode::Normal);
    EXPECT_EQ(plan.layers[1].blend_mode, BlendMode::Multiply);
    EXPECT_EQ(plan.layers[2].blend_mode, BlendMode::Screen);
    EXPECT_EQ(plan.layers[3].blend_mode, BlendMode::Overlay);
}

TEST(BlendModeField, UnknownBlendModeValueRejectedNotSilent) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a"});
    LayerCompositor comp(sw);

    // 以 static_cast 硬塞一個不在 Normal/Multiply/Screen/Overlay 集合內的值，模擬呼叫端誤用。
    const auto bogus_mode = static_cast<BlendMode>(99);
    EXPECT_EQ(comp.add_layer("layer.a", bogus_mode, 1.0f), CompositeStatus::Invalid);
    EXPECT_FALSE(comp.has_layer("layer.a"));  // 未加入
}

// --- 每層透明度 --------------------------------------------------------------

TEST(Opacity, PerLayerOpacityIsRetained) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a", "layer.b"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, 0.25f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.b", BlendMode::Normal, 0.9f), CompositeStatus::Ok);

    CompositionPlan plan = comp.compose();
    ASSERT_EQ(plan.layers.size(), 2u);
    EXPECT_FLOAT_EQ(plan.layers[0].opacity, 0.25f);
    EXPECT_FLOAT_EQ(plan.layers[1].opacity, 0.9f);
}

TEST(Opacity, OutOfRangeOpacityIsClamped) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a", "layer.b"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, -3.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.b", BlendMode::Normal, 4.0f), CompositeStatus::Ok);

    CompositionPlan plan = comp.compose();
    ASSERT_EQ(plan.layers.size(), 2u);
    EXPECT_FLOAT_EQ(plan.layers[0].opacity, 0.0f);
    EXPECT_FLOAT_EQ(plan.layers[1].opacity, 1.0f);
}

TEST(Opacity, NonFiniteOpacityRejectedNotSilent) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a", "layer.b"});
    LayerCompositor comp(sw);

    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    const float inf_value = std::numeric_limits<float>::infinity();

    EXPECT_EQ(comp.add_layer("layer.a", BlendMode::Normal, nan_value), CompositeStatus::Invalid);
    EXPECT_EQ(comp.add_layer("layer.b", BlendMode::Normal, inf_value), CompositeStatus::Invalid);
    EXPECT_EQ(comp.layer_count(), 0u);  // 兩者皆未套用
}

// --- 層序（加入 / 移除後仍具名有序） -----------------------------------------

TEST(LayerOrder, ReflectsAddedSequence) {
    SurfaceSwitcher sw = make_switcher_with({"layer.bg", "layer.mid", "layer.fg"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.bg", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.mid", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.fg", BlendMode::Normal, 1.0f), CompositeStatus::Ok);

    std::vector<SurfaceId> expected = {"layer.bg", "layer.mid", "layer.fg"};
    EXPECT_EQ(comp.layer_order(), expected);
}

TEST(LayerOrder, RemovingMiddleLayerPreservesRelativeOrderOfRemainder) {
    SurfaceSwitcher sw = make_switcher_with({"layer.bg", "layer.mid", "layer.fg"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.bg", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.mid", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.fg", BlendMode::Normal, 1.0f), CompositeStatus::Ok);

    ASSERT_EQ(comp.remove_layer("layer.mid"), CompositeStatus::Ok);

    std::vector<SurfaceId> expected = {"layer.bg", "layer.fg"};
    EXPECT_EQ(comp.layer_order(), expected);
    EXPECT_EQ(comp.layer_count(), 2u);
}

// --- 經 E4-06 定址 -------------------------------------------------------------

TEST(Addressing, UnregisteredSurfaceRejectedWithNotFound) {
    SurfaceSwitcher sw = make_switcher_with({"layer.known"});
    LayerCompositor comp(sw);

    // "layer.ghost" 未在 switcher 註冊過 —— 未經 E4-06 定址存在，不可加入。
    EXPECT_EQ(comp.add_layer("layer.ghost", BlendMode::Normal, 1.0f), CompositeStatus::NotFound);
    EXPECT_FALSE(comp.has_layer("layer.ghost"));
    EXPECT_EQ(comp.layer_count(), 0u);
}

TEST(Addressing, RegisteringAfterFailedAddThenAllowsAdd) {
    SurfaceSwitcher sw = make_switcher_with({});
    LayerCompositor comp(sw);

    EXPECT_EQ(comp.add_layer("layer.late", BlendMode::Normal, 1.0f), CompositeStatus::NotFound);
    ASSERT_EQ(sw.register_surface("layer.late"), SwitchStatus::Ok);
    EXPECT_EQ(comp.add_layer("layer.late", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
}

// --- 移除層 -------------------------------------------------------------------

TEST(RemoveLayer, RemovesEntryAndAllowsReAdd) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    EXPECT_EQ(comp.remove_layer("layer.a"), CompositeStatus::Ok);
    EXPECT_FALSE(comp.has_layer("layer.a"));
    EXPECT_EQ(comp.layer_count(), 0u);

    // 移除後可重新加入（非殘留「已存在」狀態）。
    EXPECT_EQ(comp.add_layer("layer.a", BlendMode::Multiply, 0.4f), CompositeStatus::Ok);
    CompositionPlan plan = comp.compose();
    ASSERT_EQ(plan.layers.size(), 1u);
    EXPECT_EQ(plan.layers[0].blend_mode, BlendMode::Multiply);
}

TEST(RemoveLayer, UnknownLayerReportsNotFoundNotSilent) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a"});
    LayerCompositor comp(sw);
    EXPECT_EQ(comp.remove_layer("layer.ghost"), CompositeStatus::NotFound);
}

// --- 無效輸入報錯不靜默 -------------------------------------------------------

TEST(InvalidInput, EmptySurfaceIdRejected) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a"});
    LayerCompositor comp(sw);
    EXPECT_EQ(comp.add_layer("", BlendMode::Normal, 1.0f), CompositeStatus::Invalid);
    EXPECT_EQ(comp.layer_count(), 0u);
}

TEST(InvalidInput, DuplicateLayerRejectedAndDoesNotOverwrite) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a"});
    LayerCompositor comp(sw);

    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    EXPECT_EQ(comp.add_layer("layer.a", BlendMode::Multiply, 0.2f), CompositeStatus::Invalid);

    EXPECT_EQ(comp.layer_count(), 1u);  // 未新增第二筆
    CompositionPlan plan = comp.compose();
    ASSERT_EQ(plan.layers.size(), 1u);
    EXPECT_EQ(plan.layers[0].blend_mode, BlendMode::Normal);  // 未被覆蓋
    EXPECT_FLOAT_EQ(plan.layers[0].opacity, 1.0f);
}

// --- NFR-02：具名、無數字索引 / z-order 外露 ------------------------------------

TEST(NamedAddressing, LayerOrderContainsOnlyNamedIdsNoNumericIndexSurface) {
    SurfaceSwitcher sw = make_switcher_with({"layer.a", "layer.b"});
    LayerCompositor comp(sw);
    ASSERT_EQ(comp.add_layer("layer.a", BlendMode::Normal, 1.0f), CompositeStatus::Ok);
    ASSERT_EQ(comp.add_layer("layer.b", BlendMode::Screen, 0.6f), CompositeStatus::Ok);

    // layer_order() 的元素型別即 ds::kernel::SurfaceId（std::string）；順序反映加入先後，
    // 但介面本身不提供任何「以數字取第 N 層」的存取方式（無 operator[](int) / at(int) 之類 API）。
    for (const auto& id : comp.layer_order()) {
        EXPECT_FALSE(id.empty());
    }
    EXPECT_EQ(comp.layer_order().size(), comp.layer_count());

    // 混合模式為具名列舉，非數字係數。
    CompositionPlan plan = comp.compose();
    for (const auto& entry : plan.layers) {
        EXPECT_FALSE(entry.surface_id.empty());
    }
}
