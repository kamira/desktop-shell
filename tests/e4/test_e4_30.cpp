// E4-30 全螢幕調光覆蓋 — gtest 單元測試
//
// 涵蓋：預設狀態、設定調光強度（含夾限 / 非有限報錯）、顏色（含夾限 / 非有限報錯）、
// show / hide、fade_to（含方向 / 抵達目標 / 淡出隱藏 / 非有限報錯）、置於具名頂層（E1-01
// 整合，NFR-02）、挖洞（新增 / 冪等 / 空名報錯 / 移除）、能力閘控降級（NFR-03：per-pixel
// alpha 不可用 → Unsupported；圖層置頂被拒 → 回滾）、以及 render_model 渲染描述。
#include "dim_overlay.hpp"

#include <cmath>
#include <vector>

#include "alpha_surface.hpp"     // 上游 E1-03：alpha_capable_matrix()
#include "capability_matrix.hpp"  // 上游 E1-21：CapabilityMatrix
#include "layer_stack.hpp"        // 上游 E1-01：LayerStack
#include "null_backend.hpp"       // 上游 E1-24：NullKernelBackend

#include <gtest/gtest.h>

namespace {

using ds::elements::DimColor;
using ds::elements::DimFade;
using ds::elements::DimOverlayElement;
using ds::elements::DimRenderModel;
using ds::elements::DimStatus;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::SurfaceLayer;

// 有 per-pixel alpha 能力的後端（capable 路徑）。
NullKernelBackend make_capable_backend() {
    return NullKernelBackend(ds::kernel::alpha_capable_matrix());
}

// --- 預設狀態 ---
TEST(DimOverlay, DefaultsAreHalfDimHiddenBlackTopmost) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_FLOAT_EQ(dim.intensity(), 0.5f);
    EXPECT_FALSE(dim.visible());
    EXPECT_FALSE(dim.surface_created());
    EXPECT_EQ(dim.cutout_count(), 0u);
    EXPECT_EQ(dim.layer(), SurfaceLayer::Topmost);
    EXPECT_EQ(dim.fade(), DimFade::None);
    EXPECT_FLOAT_EQ(dim.color().r, 0.0f);
    EXPECT_FLOAT_EQ(dim.color().g, 0.0f);
    EXPECT_FLOAT_EQ(dim.color().b, 0.0f);
}

// --- 設定調光強度：夾限 ---
TEST(DimOverlay, SetIntensityClampsToUnitRange) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_EQ(dim.set_intensity(0.3f), DimStatus::Ok);
    EXPECT_FLOAT_EQ(dim.intensity(), 0.3f);

    EXPECT_EQ(dim.set_intensity(1.7f), DimStatus::Ok);   // 上界夾限
    EXPECT_FLOAT_EQ(dim.intensity(), 1.0f);

    EXPECT_EQ(dim.set_intensity(-0.4f), DimStatus::Ok);  // 下界夾限
    EXPECT_FLOAT_EQ(dim.intensity(), 0.0f);
}

// --- 設定調光強度：非有限值報錯不靜默 ---
TEST(DimOverlay, SetIntensityRejectsNonFinite) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_EQ(dim.set_intensity(0.42f), DimStatus::Ok);
    EXPECT_EQ(dim.set_intensity(std::nanf("")), DimStatus::Invalid);
    EXPECT_EQ(dim.set_intensity(INFINITY), DimStatus::Invalid);
    EXPECT_FLOAT_EQ(dim.intensity(), 0.42f);  // 無效輸入不改狀態
}

// --- 設定顏色：夾限 + 非有限報錯 ---
TEST(DimOverlay, SetColorClampsAndRejectsNonFinite) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_EQ(dim.set_color(DimColor{1.5f, -0.2f, 0.5f}), DimStatus::Ok);
    EXPECT_FLOAT_EQ(dim.color().r, 1.0f);
    EXPECT_FLOAT_EQ(dim.color().g, 0.0f);
    EXPECT_FLOAT_EQ(dim.color().b, 0.5f);

    EXPECT_EQ(dim.set_color(DimColor{std::nanf(""), 0.0f, 0.0f}), DimStatus::Invalid);
    // 無效不改狀態
    EXPECT_FLOAT_EQ(dim.color().r, 1.0f);
    EXPECT_FLOAT_EQ(dim.color().b, 0.5f);
}

// --- show / hide（capable）---
TEST(DimOverlay, ShowCreatesSurfaceAndHideMarksInvisible) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers, "surface.dim");

    EXPECT_EQ(dim.show(), DimStatus::Ok);
    EXPECT_TRUE(dim.visible());
    EXPECT_TRUE(dim.surface_created());
    EXPECT_TRUE(alpha.has_alpha_surface("surface.dim"));

    EXPECT_EQ(dim.hide(), DimStatus::Ok);
    EXPECT_FALSE(dim.visible());
    // 隱藏不銷毀底層 surface（可低成本再顯示）
    EXPECT_TRUE(dim.surface_created());
}

// --- 置於具名頂層（E1-01 整合，NFR-02）---
TEST(DimOverlay, ShowPlacesSurfaceOnNamedTopmostLayer) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers, "surface.dim");

    // 另放一個一般視窗，確認調光覆蓋確實堆在其上。
    ASSERT_EQ(layers.assign("surface.window", SurfaceLayer::Normal), ds::kernel::LayerAssign::Ok);

    EXPECT_EQ(dim.show(), DimStatus::Ok);
    ASSERT_TRUE(layers.contains("surface.dim"));
    const SurfaceLayer* l = layers.layer_of("surface.dim");
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(*l, SurfaceLayer::Topmost);
    EXPECT_EQ(layers.topmost(), "surface.dim");
    EXPECT_TRUE(layers.is_above("surface.dim", "surface.window"));
}

// --- set_intensity 顯示後同步到底層 alpha 不透明度 ---
TEST(DimOverlay, IntensitySyncsToAlphaOpacityAfterShow) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers, "surface.dim");

    ASSERT_EQ(dim.set_intensity(0.8f), DimStatus::Ok);
    ASSERT_EQ(dim.show(), DimStatus::Ok);

    const ds::kernel::AlphaProfile* ap = alpha.alpha_profile("surface.dim");
    ASSERT_NE(ap, nullptr);
    EXPECT_FLOAT_EQ(ap->opacity, 0.8f);

    ASSERT_EQ(dim.set_intensity(0.25f), DimStatus::Ok);  // 已建立 → 應同步
    const ds::kernel::AlphaProfile* ap2 = alpha.alpha_profile("surface.dim");
    ASSERT_NE(ap2, nullptr);
    EXPECT_FLOAT_EQ(ap2->opacity, 0.25f);
}

// --- fade_to：淡入方向、抵達目標、顯示 ---
TEST(DimOverlay, FadeToRaisesIntensityAndShows) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    ASSERT_EQ(dim.set_intensity(0.2f), DimStatus::Ok);
    EXPECT_EQ(dim.fade_to(0.9f), DimStatus::Ok);
    EXPECT_EQ(dim.fade(), DimFade::In);
    EXPECT_FLOAT_EQ(dim.fade_target(), 0.9f);
    EXPECT_FLOAT_EQ(dim.intensity(), 0.9f);  // 相位 1：直接抵達目標
    EXPECT_TRUE(dim.visible());
}

// --- fade_to：淡出至 0 = 隱藏 ---
TEST(DimOverlay, FadeToZeroFadesOutAndHides) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    ASSERT_EQ(dim.show(), DimStatus::Ok);
    ASSERT_TRUE(dim.visible());

    EXPECT_EQ(dim.fade_to(0.0f), DimStatus::Ok);
    EXPECT_EQ(dim.fade(), DimFade::Out);
    EXPECT_FLOAT_EQ(dim.intensity(), 0.0f);
    EXPECT_FALSE(dim.visible());
}

// --- fade_to：夾限 + 非有限報錯 ---
TEST(DimOverlay, FadeToClampsAndRejectsNonFinite) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_EQ(dim.fade_to(3.0f), DimStatus::Ok);
    EXPECT_FLOAT_EQ(dim.fade_target(), 1.0f);   // 夾至上界
    EXPECT_FLOAT_EQ(dim.intensity(), 1.0f);

    EXPECT_EQ(dim.fade_to(std::nanf("")), DimStatus::Invalid);
    EXPECT_FLOAT_EQ(dim.intensity(), 1.0f);      // 無效不改狀態
}

// --- 挖洞：新增 / 查詢 / 冪等 / 移除 ---
TEST(DimOverlay, CutoutsAddQueryDedupRemove) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_EQ(dim.add_cutout("region.clock"), DimStatus::Ok);
    EXPECT_EQ(dim.add_cutout("region.avatar"), DimStatus::Ok);
    EXPECT_EQ(dim.cutout_count(), 2u);
    EXPECT_TRUE(dim.has_cutout("region.clock"));

    EXPECT_EQ(dim.add_cutout("region.clock"), DimStatus::Ok);  // 冪等：不新增
    EXPECT_EQ(dim.cutout_count(), 2u);

    EXPECT_TRUE(dim.remove_cutout("region.clock"));
    EXPECT_FALSE(dim.has_cutout("region.clock"));
    EXPECT_EQ(dim.cutout_count(), 1u);
    EXPECT_FALSE(dim.remove_cutout("region.unknown"));  // 未知：不崩潰
}

// --- 挖洞：空名報錯不靜默 ---
TEST(DimOverlay, AddCutoutRejectsEmptyName) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_EQ(dim.add_cutout(""), DimStatus::Invalid);
    EXPECT_EQ(dim.cutout_count(), 0u);
}

// --- NFR-03 降級：per-pixel alpha 不可用 → show / fade / cutout 皆 Unsupported ---
TEST(DimOverlay, IncapableBackendDegradesGracefully) {
    NullKernelBackend backend;  // 預設保守矩陣：無 per-pixel alpha 能力
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers);

    EXPECT_FALSE(dim.alpha_supported());

    EXPECT_EQ(dim.show(), DimStatus::Unsupported);
    EXPECT_FALSE(dim.visible());
    EXPECT_FALSE(dim.surface_created());

    EXPECT_EQ(dim.fade_to(0.7f), DimStatus::Unsupported);
    EXPECT_EQ(dim.add_cutout("region.clock"), DimStatus::Unsupported);
    EXPECT_EQ(dim.cutout_count(), 0u);

    // 純狀態更新（強度 / 顏色）不需能力：仍可用（記錄意圖，供降級顯示）。
    EXPECT_EQ(dim.set_intensity(0.6f), DimStatus::Ok);
    EXPECT_FLOAT_EQ(dim.intensity(), 0.6f);
}

// --- NFR-03 回滾：圖層置頂被能力閘控拒絕 → 回滾已建立的 alpha surface ---
TEST(DimOverlay, LayerRejectionRollsBackAlphaSurface) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    // 圖層堆疊缺 kernel.surface 能力 → assign 一律被拒（NFR-03）。
    LayerStack layers(CapabilityMatrix(std::vector<ds::kernel::CapabilityDecl>{}));
    DimOverlayElement dim(alpha, layers, "surface.dim");

    EXPECT_EQ(dim.show(), DimStatus::Unsupported);
    EXPECT_FALSE(dim.visible());
    EXPECT_FALSE(dim.surface_created());
    // 回滾：alpha surface 不得殘留半份狀態。
    EXPECT_FALSE(alpha.has_alpha_surface("surface.dim"));
    EXPECT_EQ(alpha.alpha_surface_count(), 0u);
}

// --- render_model：宣告式渲染描述 ---
TEST(DimOverlay, RenderModelReflectsState) {
    NullKernelBackend backend = make_capable_backend();
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement dim(alpha, layers, "surface.dim");

    ASSERT_EQ(dim.set_intensity(0.7f), DimStatus::Ok);
    ASSERT_EQ(dim.set_color(DimColor{0.1f, 0.1f, 0.2f}), DimStatus::Ok);
    ASSERT_EQ(dim.add_cutout("region.clock"), DimStatus::Ok);

    // 隱藏時：有效不透明度為 0，但強度仍記錄。
    DimRenderModel hidden = dim.render_model();
    EXPECT_FALSE(hidden.visible);
    EXPECT_FLOAT_EQ(hidden.intensity, 0.7f);
    EXPECT_FLOAT_EQ(hidden.effective_opacity, 0.0f);
    EXPECT_EQ(hidden.layer_name, "layer.topmost");  // NFR-02：具名頂層，非數字 z-order
    EXPECT_TRUE(hidden.alpha_supported);
    ASSERT_EQ(hidden.cutouts.size(), 1u);
    EXPECT_EQ(hidden.cutouts[0], "region.clock");

    // 顯示後：有效不透明度 = 強度。
    ASSERT_EQ(dim.show(), DimStatus::Ok);
    DimRenderModel shown = dim.render_model();
    EXPECT_TRUE(shown.visible);
    EXPECT_FLOAT_EQ(shown.effective_opacity, 0.7f);
    EXPECT_FLOAT_EQ(shown.color.b, 0.2f);
}

}  // namespace
