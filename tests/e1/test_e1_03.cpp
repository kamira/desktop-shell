// E1-03 逐像素 alpha surface — 單元測試（gtest）
//
// 驗證 AlphaSurfaceService 於相位 1（Mac / null 期）的介面擴充 + null 行為：
//   - per-pixel alpha 為可選能力，使用前經 has() 閘控（NFR-03）——有能力 / 無能力兩情境
//   - 能力不可用時的**降級路徑**：所有變更操作回 Unsupported 且不改任何狀態
//   - 建立 per-pixel alpha surface：委由後端 K1 建實體 surface + 記憶體記錄 alpha 狀態
//   - alpha 設定 / 查詢：mode / opacity 更新、opacity clamp 至 [0,1]、非有限值拒絕
//   - null 後端記憶體模擬：alpha 狀態與後端 surface 表一致、銷毀同步釋放兩者
//   - NFR-02 具名表達：surface 以具名 SurfaceId 指涉、alpha 以具名模式 + 正規化不透明度
//   - 生命週期：建立 / 重複拒絕 / 空 id 拒絕 / 銷毀 / 未知 id 不崩潰
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實繪圖 API。
#include "alpha_surface.hpp"

#include <gtest/gtest.h>

#include <limits>

#include "null_backend.hpp"

using ds::kernel::AlphaMode;
using ds::kernel::AlphaProfile;
using ds::kernel::AlphaStatus;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::alpha_incapable_matrix;
using ds::kernel::kPerPixelAlphaCapability;
using ds::kernel::NullKernelBackend;
using ds::kernel::SurfaceProfile;

namespace {

// 一個具備 per-pixel alpha 能力的後端（注入 alpha_capable_matrix，已 init）。
NullKernelBackend MakeCapableBackend() {
    NullKernelBackend b(alpha_capable_matrix());
    b.init();
    return b;
}

// -----------------------------------------------------------------------------
// 能力閘控（NFR-03）
// -----------------------------------------------------------------------------

// 上游保守 defaults() 未宣告 per-pixel alpha 鍵；具備能力矩陣則宣告為可用。
TEST(AlphaCapability, MatrixDeclaresCapabilityOnlyWhenCapable) {
    EXPECT_FALSE(alpha_incapable_matrix().has(kPerPixelAlphaCapability));
    EXPECT_TRUE(alpha_capable_matrix().has(kPerPixelAlphaCapability));
    // 可選能力：capable 矩陣中宣告為 optional（呼叫端須閘控 + 降級）。
    EXPECT_TRUE(alpha_capable_matrix().is_optional(kPerPixelAlphaCapability));
    // 追加不破壞上游既有宣告。
    EXPECT_TRUE(alpha_capable_matrix().has("render.paint"));
}

// supported() 直接反映後端 has()：有能力矩陣為真、保守矩陣為假。
TEST(AlphaCapability, SupportedReflectsBackendHas) {
    NullKernelBackend capable(alpha_capable_matrix());
    NullKernelBackend incapable(alpha_incapable_matrix());
    AlphaSurfaceService svc_ok(capable);
    AlphaSurfaceService svc_no(incapable);
    EXPECT_TRUE(svc_ok.supported());
    EXPECT_FALSE(svc_no.supported());
}

// 降級路徑：能力不可用時，建立 / 設定一律回 Unsupported，且不留下任何狀態。
TEST(AlphaCapability, DowngradePathWhenUnsupported) {
    NullKernelBackend backend(alpha_incapable_matrix());
    backend.init();
    AlphaSurfaceService svc(backend);

    EXPECT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Unsupported);
    // 未建立任何 alpha 記錄，也未在後端建立實體 surface（降級：零副作用）。
    EXPECT_EQ(svc.alpha_surface_count(), 0u);
    EXPECT_FALSE(svc.has_alpha_surface("surface.pet"));
    EXPECT_FALSE(backend.has_surface("surface.pet"));

    // 對不存在的 surface 設定亦回 Unsupported（能力閘門先於 id 檢查）。
    EXPECT_EQ(svc.set_opacity("surface.pet", 0.5f), AlphaStatus::Unsupported);
    EXPECT_EQ(svc.set_mode("surface.pet", AlphaMode::Opaque),
              AlphaStatus::Unsupported);
    EXPECT_EQ(svc.set_alpha("surface.pet", AlphaProfile{}), AlphaStatus::Unsupported);
}

// -----------------------------------------------------------------------------
// 建立 per-pixel alpha surface + null 記憶體模擬
// -----------------------------------------------------------------------------

// 建立成功：後端實體 surface 與服務 alpha 記錄一致（記憶體模擬）。
TEST(AlphaSurface, CreateRegistersBackendAndAlphaState) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);

    AlphaProfile ap;
    ap.mode = AlphaMode::PerPixel;
    ap.opacity = 0.75f;
    EXPECT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}, ap),
              AlphaStatus::Ok);

    // 服務層記錄。
    EXPECT_TRUE(svc.has_alpha_surface("surface.pet"));
    EXPECT_EQ(svc.alpha_surface_count(), 1u);
    // 後端實體 surface（K1 原語建立）。
    EXPECT_TRUE(backend.has_surface("surface.pet"));
    EXPECT_EQ(backend.surface_count(), 1u);

    const AlphaProfile* got = svc.alpha_profile("surface.pet");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(got->opacity, 0.75f);
    EXPECT_TRUE(svc.is_per_pixel("surface.pet"));
}

// 預設 AlphaProfile：per-pixel、完全不透明。
TEST(AlphaSurface, DefaultProfileIsPerPixelOpaque) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);
    ASSERT_EQ(svc.create_alpha_surface("surface.panel", SurfaceProfile{}),
              AlphaStatus::Ok);
    const AlphaProfile* got = svc.alpha_profile("surface.panel");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(got->opacity, 1.0f);
}

// 空 id 拒絕；重複建立拒絕（保守），且不污染既有狀態。
TEST(AlphaSurface, RejectsEmptyAndDuplicateId) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);

    EXPECT_EQ(svc.create_alpha_surface("", SurfaceProfile{}), AlphaStatus::Invalid);
    EXPECT_EQ(svc.alpha_surface_count(), 0u);

    ASSERT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Ok);
    // 重複 id：服務層攔下（find 命中），回 Invalid。
    EXPECT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Invalid);
    EXPECT_EQ(svc.alpha_surface_count(), 1u);
    EXPECT_EQ(backend.surface_count(), 1u);
}

// 後端已存在同名 surface（非經本服務建立）→ 後端 create_surface 失敗 → Invalid，且不留半份狀態。
TEST(AlphaSurface, BackendConflictYieldsInvalidNoPartialState) {
    NullKernelBackend backend = MakeCapableBackend();
    ASSERT_TRUE(backend.create_surface("surface.dup", SurfaceProfile{}));
    AlphaSurfaceService svc(backend);

    EXPECT_EQ(svc.create_alpha_surface("surface.dup", SurfaceProfile{}),
              AlphaStatus::Invalid);
    // 服務層未留下記錄（未建成 → 無半份狀態）。
    EXPECT_FALSE(svc.has_alpha_surface("surface.dup"));
    EXPECT_EQ(svc.alpha_surface_count(), 0u);
}

// -----------------------------------------------------------------------------
// alpha 設定 / 查詢
// -----------------------------------------------------------------------------

// set_mode / set_opacity / set_alpha 更新記憶體狀態並可查詢。
TEST(AlphaSetGet, UpdatesAndQueries) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);
    ASSERT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Ok);

    EXPECT_EQ(svc.set_mode("surface.pet", AlphaMode::Opaque), AlphaStatus::Ok);
    EXPECT_FALSE(svc.is_per_pixel("surface.pet"));

    EXPECT_EQ(svc.set_opacity("surface.pet", 0.25f), AlphaStatus::Ok);
    EXPECT_FLOAT_EQ(svc.alpha_profile("surface.pet")->opacity, 0.25f);

    AlphaProfile ap;
    ap.mode = AlphaMode::PerPixel;
    ap.opacity = 0.5f;
    EXPECT_EQ(svc.set_alpha("surface.pet", ap), AlphaStatus::Ok);
    const AlphaProfile* got = svc.alpha_profile("surface.pet");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(got->opacity, 0.5f);
}

// opacity clamp 至 [0,1]：越界值飽和、非有限值（NaN/Inf）拒絕。
TEST(AlphaSetGet, OpacityClampAndFiniteGuard) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);
    ASSERT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Ok);

    EXPECT_EQ(svc.set_opacity("surface.pet", 5.0f), AlphaStatus::Ok);
    EXPECT_FLOAT_EQ(svc.alpha_profile("surface.pet")->opacity, 1.0f);
    EXPECT_EQ(svc.set_opacity("surface.pet", -3.0f), AlphaStatus::Ok);
    EXPECT_FLOAT_EQ(svc.alpha_profile("surface.pet")->opacity, 0.0f);

    // 非有限值拒絕，且不改先前狀態（維持 0.0）。
    const float nan_v = std::numeric_limits<float>::quiet_NaN();
    const float inf_v = std::numeric_limits<float>::infinity();
    EXPECT_EQ(svc.set_opacity("surface.pet", nan_v), AlphaStatus::Invalid);
    EXPECT_EQ(svc.set_opacity("surface.pet", inf_v), AlphaStatus::Invalid);
    EXPECT_FLOAT_EQ(svc.alpha_profile("surface.pet")->opacity, 0.0f);

    // 建立時的非有限 opacity 亦拒絕。
    AlphaProfile bad;
    bad.opacity = inf_v;
    EXPECT_EQ(svc.create_alpha_surface("surface.bad", SurfaceProfile{}, bad),
              AlphaStatus::Invalid);
    EXPECT_FALSE(svc.has_alpha_surface("surface.bad"));
}

// 未知 id 的設定 / 查詢：Invalid / nullptr / false，永不崩潰。
TEST(AlphaSetGet, UnknownIdIsStructured) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);
    EXPECT_EQ(svc.set_opacity("surface.ghost", 0.5f), AlphaStatus::Invalid);
    EXPECT_EQ(svc.set_mode("surface.ghost", AlphaMode::Opaque), AlphaStatus::Invalid);
    EXPECT_EQ(svc.set_alpha("surface.ghost", AlphaProfile{}), AlphaStatus::Invalid);
    EXPECT_EQ(svc.alpha_profile("surface.ghost"), nullptr);
    EXPECT_FALSE(svc.is_per_pixel("surface.ghost"));
}

// -----------------------------------------------------------------------------
// 生命週期
// -----------------------------------------------------------------------------

// 銷毀同步釋放服務記錄與後端實體 surface；未知 id 回 Invalid（不崩潰）。
TEST(AlphaLifecycle, DestroyReleasesBothLayers) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);
    ASSERT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Ok);
    ASSERT_TRUE(backend.has_surface("surface.pet"));

    EXPECT_EQ(svc.destroy_alpha_surface("surface.pet"), AlphaStatus::Ok);
    EXPECT_FALSE(svc.has_alpha_surface("surface.pet"));
    EXPECT_EQ(svc.alpha_surface_count(), 0u);
    EXPECT_FALSE(backend.has_surface("surface.pet"));  // 後端同步釋放

    // 未知 id 銷毀：不崩潰。
    EXPECT_EQ(svc.destroy_alpha_surface("surface.pet"), AlphaStatus::Invalid);
}

// 多個具名 alpha surface 共存，各自獨立（NFR-02：純具名指涉，無數字 index / 座標）。
TEST(AlphaLifecycle, MultipleNamedSurfacesCoexist) {
    NullKernelBackend backend = MakeCapableBackend();
    AlphaSurfaceService svc(backend);
    ASSERT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Ok);
    ASSERT_EQ(svc.create_alpha_surface("surface.overlay", SurfaceProfile{}),
              AlphaStatus::Ok);

    ASSERT_EQ(svc.set_opacity("surface.pet", 0.3f), AlphaStatus::Ok);
    ASSERT_EQ(svc.set_mode("surface.overlay", AlphaMode::Opaque), AlphaStatus::Ok);

    // 改一個不影響另一個。
    EXPECT_FLOAT_EQ(svc.alpha_profile("surface.pet")->opacity, 0.3f);
    EXPECT_TRUE(svc.is_per_pixel("surface.pet"));
    EXPECT_FLOAT_EQ(svc.alpha_profile("surface.overlay")->opacity, 1.0f);
    EXPECT_FALSE(svc.is_per_pixel("surface.overlay"));
    EXPECT_EQ(svc.alpha_surface_count(), 2u);
}

// 經抽象 KernelBackend 基底指標操作亦成立（呼叫端與具體後端解耦）。
TEST(AlphaLifecycle, WorksThroughAbstractBackend) {
    NullKernelBackend concrete = MakeCapableBackend();
    ds::kernel::KernelBackend& backend = concrete;
    AlphaSurfaceService svc(backend);
    EXPECT_TRUE(svc.supported());
    EXPECT_EQ(svc.create_alpha_surface("surface.pet", SurfaceProfile{}),
              AlphaStatus::Ok);
    EXPECT_TRUE(svc.has_alpha_surface("surface.pet"));
}

}  // namespace
