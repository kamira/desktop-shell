// tests/c1/test_c1_07.cpp — C1-07 全螢幕調光層 profile（gtest）
//
// 涵蓋：profile 組裝正確（建構情境預設顏色、初始 Inactive）、activate/deactivate/fade
// 行為、E4-30 強度控制（intensity 隨 activate/fade 更新、預設 preset 套用）、E1-01 具名
// 頂層整合（透傳 layer() 及底層 LayerStack 置頂）、挖洞透傳、能力閘控降級（NFR-03）、
// render_model 透傳，以及各類無效參數（非有限強度 / 目標、空挖洞名）。
#include "dim_overlay_profile.hpp"

#include <cmath>

#include <gtest/gtest.h>

namespace {

using ds::elements::DimColor;
using ds::elements::DimRenderModel;
using ds::elements::DimStatus;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::SurfaceLayer;
using ds::profiles::default_preset;
using ds::profiles::DimOverlayProfile;
using ds::profiles::DimProfileKind;
using ds::profiles::DimProfileState;

// 有 per-pixel alpha 能力的後端（capable 路徑）。
NullKernelBackend make_capable_backend() {
    return NullKernelBackend(ds::kernel::alpha_capable_matrix());
}

// -----------------------------------------------------------------------------
// 組裝正確：建構套用情境預設顏色、初始 Inactive
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, ConstructedInactiveWithFocusDefaults) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_EQ(profile.kind(), DimProfileKind::Focus);
    EXPECT_EQ(profile.state(), DimProfileState::Inactive);
    EXPECT_FALSE(profile.is_active());
    EXPECT_EQ(profile.cutout_count(), 0u);
    EXPECT_EQ(profile.layer(), SurfaceLayer::Topmost);

    const DimColor& c = profile.color();
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
}

TEST(DimOverlayProfile, ConstructedAppliesNightShiftWarmTintColor) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::NightShift, alpha, layers, "surface.dim.night");

    const DimColor& c = profile.color();
    // 暖色調：紅 > 綠 > 藍（降低藍光觀感）。
    EXPECT_GT(c.r, c.g);
    EXPECT_GT(c.g, c.b);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
}

TEST(DimOverlayProfile, DefaultPresetTableCoversAllKinds) {
    const auto focus = default_preset(DimProfileKind::Focus);
    const auto night = default_preset(DimProfileKind::NightShift);
    const auto popup = default_preset(DimProfileKind::PopupBackdrop);

    EXPECT_FLOAT_EQ(focus.intensity, 0.6f);
    EXPECT_FLOAT_EQ(night.intensity, 0.3f);
    EXPECT_FLOAT_EQ(popup.intensity, 0.7f);
}

// -----------------------------------------------------------------------------
// activate() —— 套用情境預設強度 / 顏色，淡入並啟用
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, ActivateNoArgUsesKindDefaultIntensity) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::PopupBackdrop, alpha, layers, "surface.dim.popup");

    EXPECT_EQ(profile.activate(), DimStatus::Ok);
    EXPECT_FLOAT_EQ(profile.intensity(), 0.7f);  // PopupBackdrop 預設強度
    EXPECT_TRUE(profile.is_active());
    EXPECT_EQ(profile.state(), DimProfileState::Active);
}

TEST(DimOverlayProfile, ActivateWithExplicitIntensityOverridesPreset) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_EQ(profile.activate(0.9f), DimStatus::Ok);
    EXPECT_FLOAT_EQ(profile.intensity(), 0.9f);
    EXPECT_TRUE(profile.is_active());
}

// -----------------------------------------------------------------------------
// deactivate() —— 淡出至 0，停用
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, DeactivateFadesToZeroAndBecomesInactive) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    ASSERT_EQ(profile.activate(), DimStatus::Ok);
    ASSERT_TRUE(profile.is_active());

    EXPECT_EQ(profile.deactivate(), DimStatus::Ok);
    EXPECT_FLOAT_EQ(profile.intensity(), 0.0f);
    EXPECT_FALSE(profile.is_active());
    EXPECT_EQ(profile.state(), DimProfileState::Inactive);
}

// -----------------------------------------------------------------------------
// fade() —— 淡變至任意目標強度，不套用預設顏色
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, FadeMovesIntensityAndTracksActiveState) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_EQ(profile.fade(0.4f), DimStatus::Ok);
    EXPECT_FLOAT_EQ(profile.intensity(), 0.4f);
    EXPECT_TRUE(profile.is_active());  // >0 視為啟用

    EXPECT_EQ(profile.fade(0.0f), DimStatus::Ok);
    EXPECT_FLOAT_EQ(profile.intensity(), 0.0f);
    EXPECT_FALSE(profile.is_active());  // ==0 視為停用
}

// -----------------------------------------------------------------------------
// E1-01 具名頂層整合（透傳 E4-30 -> E1-01 LayerStack）
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, ActivatePlacesOverlayOnNamedTopmostLayer) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    // 另放一個一般視窗，確認調光層確實堆在其上。
    ASSERT_EQ(layers.assign("surface.window", SurfaceLayer::Normal), ds::kernel::LayerAssign::Ok);

    EXPECT_EQ(profile.activate(), DimStatus::Ok);
    ASSERT_TRUE(layers.contains("surface.dim.focus"));
    const SurfaceLayer* l = layers.layer_of("surface.dim.focus");
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(*l, SurfaceLayer::Topmost);
    EXPECT_EQ(layers.topmost(), "surface.dim.focus");
    EXPECT_TRUE(layers.is_above("surface.dim.focus", "surface.window"));
}

// -----------------------------------------------------------------------------
// 挖洞（透傳 E4-30）
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, CutoutAddQueryRemoveThroughProfile) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_EQ(profile.add_cutout("region.clock"), DimStatus::Ok);
    EXPECT_EQ(profile.add_cutout("region.avatar"), DimStatus::Ok);
    EXPECT_EQ(profile.cutout_count(), 2u);
    EXPECT_TRUE(profile.has_cutout("region.clock"));

    EXPECT_EQ(profile.add_cutout("region.clock"), DimStatus::Ok);  // 冪等：不新增
    EXPECT_EQ(profile.cutout_count(), 2u);

    EXPECT_TRUE(profile.remove_cutout("region.clock"));
    EXPECT_FALSE(profile.has_cutout("region.clock"));
    EXPECT_EQ(profile.cutout_count(), 1u);
    EXPECT_FALSE(profile.remove_cutout("region.unknown"));  // 未知：不崩潰
}

// -----------------------------------------------------------------------------
// NFR-03 降級：per-pixel alpha 不可用 -> activate / fade / cutout 皆 Unsupported
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, IncapableBackendDegradesGracefully) {
    NullKernelBackend backend;  // 預設保守矩陣：無 per-pixel alpha 能力
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_FALSE(profile.alpha_supported());

    EXPECT_EQ(profile.activate(), DimStatus::Unsupported);
    EXPECT_FALSE(profile.is_active());

    EXPECT_EQ(profile.fade(0.5f), DimStatus::Unsupported);
    EXPECT_EQ(profile.add_cutout("region.clock"), DimStatus::Unsupported);
    EXPECT_EQ(profile.cutout_count(), 0u);
}

// -----------------------------------------------------------------------------
// render_model 透傳
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, RenderModelReflectsProfileState) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::PopupBackdrop, alpha, layers, "surface.dim.popup");

    ASSERT_EQ(profile.activate(), DimStatus::Ok);
    DimRenderModel model = profile.render_model();
    EXPECT_TRUE(model.visible);
    EXPECT_FLOAT_EQ(model.intensity, 0.7f);
    EXPECT_FLOAT_EQ(model.effective_opacity, 0.7f);
    EXPECT_EQ(model.layer_name, "layer.topmost");  // NFR-02：具名頂層，非數字 z-order
    EXPECT_TRUE(model.alpha_supported);
}

// -----------------------------------------------------------------------------
// 無效參數：非有限強度 / 目標、空挖洞名 —— 報錯不靜默，狀態不變
// -----------------------------------------------------------------------------

TEST(DimOverlayProfile, ActivateRejectsNonFiniteIntensity) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_EQ(profile.activate(std::nanf("")), DimStatus::Invalid);
    EXPECT_FALSE(profile.is_active());
    EXPECT_EQ(profile.activate(INFINITY), DimStatus::Invalid);
    EXPECT_FALSE(profile.is_active());
    EXPECT_FLOAT_EQ(profile.intensity(), 0.5f);  // E4-30 未改狀態的預設初始強度
}

TEST(DimOverlayProfile, FadeRejectsNonFiniteTarget) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    ASSERT_EQ(profile.activate(0.5f), DimStatus::Ok);
    EXPECT_EQ(profile.fade(std::nanf("")), DimStatus::Invalid);
    EXPECT_FLOAT_EQ(profile.intensity(), 0.5f);  // 無效不改狀態
    EXPECT_TRUE(profile.is_active());
}

TEST(DimOverlayProfile, AddCutoutRejectsEmptyName) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayProfile profile(DimProfileKind::Focus, alpha, layers, "surface.dim.focus");

    EXPECT_EQ(profile.add_cutout(""), DimStatus::Invalid);
    EXPECT_EQ(profile.cutout_count(), 0u);
}

}  // namespace
