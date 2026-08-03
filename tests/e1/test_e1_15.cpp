// E1-15 視窗級透明度與懸停淡變 — 單元測試（gtest）
//
// 驗證 WindowOpacity 於相位 1（Mac / null 期）的行為：
//   - 設定基準 / 懸停不透明度：未懸停時立即套用、懸停中僅記錄（反之亦然）
//   - 夾限：有限但超出 [0,1] 的值 clamp，不視為無效
//   - 無效值拒絕：非有限值（NaN/Inf）回 Invalid、不套用、不改設定值（報錯不靜默）
//   - E5-02 事件驅動：HoverTracker 的 Enter / Leave 觸發淡入 / 淡出起始
//   - advance(dt) 推進淡變：線性插值、跨越多次 advance 抵達目標、瞬時（fade_seconds<=0）切換
//   - 透過 E1-03 AlphaSurfaceService 實際套用：能力閘控（NFR-03）Unsupported 轉發、
//     未註冊 surface 的 Invalid 轉發
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實時鐘（時間全注入式）。
#include "window_opacity.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）
#include "hit_test.hpp"       // E1-04（上游，可讀不可改，供 HoverTracker 命中判定）
#include "hover_tracker.hpp"  // E5-02（上游，可讀不可改）
#include "null_backend.hpp"   // E1-24（上游，可讀不可改）

using ds::events::HoverTracker;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::alpha_incapable_matrix;
using ds::kernel::HitSurface;
using ds::kernel::LocalPoint;
using ds::kernel::make_rect;
using ds::kernel::NullKernelBackend;
using ds::kernel::SurfaceProfile;
using ds::kernel::WindowOpacity;
using ds::kernel::WindowOpacityStatus;

namespace {

constexpr const char* kSurfaceId = "surface.window";

// 一個具備 per-pixel alpha 能力、已建立好 kSurfaceId alpha surface 的後端 + 服務。
struct Fixture {
    NullKernelBackend backend{alpha_capable_matrix()};
    bool backend_initialized_ = backend.init();  // CHG-20260803-11：成員依宣告順序初始化，故此行在其後成員建構前完成（K-007 對齊）
    AlphaSurfaceService alpha_service{backend};

    Fixture() {
        backend.init();
        alpha_service.create_alpha_surface(kSurfaceId, SurfaceProfile{});
    }
};

// 供懸停測試使用：一個涵蓋 kSurfaceId 的矩形 HitSurface，供 HoverTracker 命中判定。
HitSurface MakeWindowHitSurface() {
    HitSurface s;
    s.id = kSurfaceId;
    s.shape = make_rect(10.0f, 10.0f);  // 本地 (0,0)-(10,10)
    return s;
}

// -----------------------------------------------------------------------------
// 設定透明度（基準 / 懸停）
// -----------------------------------------------------------------------------

// 未懸停時 set_base_opacity 立即生效（current_opacity() 立即反映，無需 advance）。
TEST(WindowOpacitySetters, SetBaseOpacityAppliesImmediatelyWhenNotHovering) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId);

    EXPECT_EQ(w.set_base_opacity(0.4f), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 0.4f, 1e-6f);
    EXPECT_NEAR(w.base_opacity(), 0.4f, 1e-6f);
    EXPECT_FALSE(w.is_hovering());
}

// 懸停中 set_hover_opacity 立即生效；未懸停中設定則僅記錄（不影響目前 current_opacity()）。
TEST(WindowOpacitySetters, SetHoverOpacityDefersUntilHoveringUnlessAlreadyHovering) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId, /*fade_seconds=*/0.5);
    w.set_base_opacity(0.3f);

    // 未懸停時設定 hover_opacity：僅記錄，current_opacity() 不受影響。
    EXPECT_EQ(w.set_hover_opacity(0.9f), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 0.3f, 1e-6f);
    EXPECT_NEAR(w.hover_opacity(), 0.9f, 1e-6f);
}

// -----------------------------------------------------------------------------
// 夾限（clamp）
// -----------------------------------------------------------------------------

TEST(WindowOpacityClamp, OutOfRangeFiniteValuesClampTo01) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId);

    EXPECT_EQ(w.set_base_opacity(1.5f), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.base_opacity(), 1.0f, 1e-6f);
    EXPECT_NEAR(w.current_opacity(), 1.0f, 1e-6f);

    EXPECT_EQ(w.set_base_opacity(-0.3f), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.base_opacity(), 0.0f, 1e-6f);
    EXPECT_NEAR(w.current_opacity(), 0.0f, 1e-6f);
}

// -----------------------------------------------------------------------------
// 無效值拒絕（不靜默）
// -----------------------------------------------------------------------------

TEST(WindowOpacityInvalid, NonFiniteBaseOpacityRejectedAndUnchanged) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId);
    w.set_base_opacity(0.6f);

    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    const float inf_value = std::numeric_limits<float>::infinity();

    EXPECT_EQ(w.set_base_opacity(nan_value), WindowOpacityStatus::Invalid);
    EXPECT_NEAR(w.base_opacity(), 0.6f, 1e-6f);  // 未被非有限值覆蓋

    EXPECT_EQ(w.set_base_opacity(inf_value), WindowOpacityStatus::Invalid);
    EXPECT_NEAR(w.base_opacity(), 0.6f, 1e-6f);
}

TEST(WindowOpacityInvalid, NonFiniteHoverOpacityRejectedAndUnchanged) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId);
    w.set_hover_opacity(0.8f);

    const float nan_value = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(w.set_hover_opacity(nan_value), WindowOpacityStatus::Invalid);
    EXPECT_NEAR(w.hover_opacity(), 0.8f, 1e-6f);
}

// -----------------------------------------------------------------------------
// E5-02 事件驅動 + 懸停淡入 / 離開淡出 + advance 推進
// -----------------------------------------------------------------------------

// Enter 事件觸發淡入：is_hovering() 立即為 true，current_opacity() 隨 advance 逐步逼近
// hover_opacity()；抵達 fade_seconds 後精確等於目標。
TEST(WindowOpacityHoverFade, EnterTriggersFadeInTowardHoverOpacity) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId, /*fade_seconds=*/1.0);
    w.set_base_opacity(0.3f);
    w.set_hover_opacity(1.0f);

    HoverTracker hover;
    hover.add_surface(MakeWindowHitSurface());
    w.attach(hover);

    ASSERT_TRUE(hover.inject_move(LocalPoint{5.0f, 5.0f}));  // 移入本 surface → Enter
    EXPECT_TRUE(w.is_hovering());

    // 淡變起點為進入當下的 current_opacity()（0.3），中途（0.5s / 1.0s）應約在中點。
    EXPECT_EQ(w.advance(0.5), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 0.65f, 1e-4f);  // 0.3 + (1.0-0.3)*0.5

    // 推進至滿 duration：精確抵達 hover_opacity()。
    EXPECT_EQ(w.advance(0.5), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 1.0f, 1e-6f);

    // 超過 duration 不越界（夾在終點）。
    EXPECT_EQ(w.advance(10.0), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 1.0f, 1e-6f);
}

// Leave 事件觸發淡出：從懸停目標往 base_opacity() 過場。
TEST(WindowOpacityHoverFade, LeaveTriggersFadeOutTowardBaseOpacity) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId, /*fade_seconds=*/1.0);
    w.set_base_opacity(0.2f);
    w.set_hover_opacity(1.0f);

    HoverTracker hover;
    hover.add_surface(MakeWindowHitSurface());
    w.attach(hover);

    ASSERT_TRUE(hover.inject_move(LocalPoint{5.0f, 5.0f}));  // Enter
    w.advance(1.0);                                          // 抵達 hover_opacity（1.0）
    EXPECT_NEAR(w.current_opacity(), 1.0f, 1e-6f);

    ASSERT_TRUE(hover.inject_move(LocalPoint{50.0f, 50.0f}));  // 移出 → Leave
    EXPECT_FALSE(w.is_hovering());

    EXPECT_EQ(w.advance(0.5), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 0.6f, 1e-4f);  // 1.0 + (0.2-1.0)*0.5

    EXPECT_EQ(w.advance(0.5), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 0.2f, 1e-6f);
}

// 其他 surface 的懸停事件不影響本 WindowOpacity（僅回應同 id 的 Enter/Leave）。
TEST(WindowOpacityHoverFade, OtherSurfaceEventsIgnored) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId, /*fade_seconds=*/1.0);
    w.set_base_opacity(0.3f);
    w.set_hover_opacity(1.0f);

    // 只登記另一個 surface（不同 id），本物件訂閱其懸停事件；命中它應完全不影響本物件。
    HoverTracker hover;
    HitSurface other;
    other.id = "surface.other";
    other.shape = make_rect(10.0f, 10.0f);
    hover.add_surface(other);
    w.attach(hover);

    ASSERT_TRUE(hover.inject_move(LocalPoint{1.0f, 1.0f}));  // 命中 other，非本 surface
    EXPECT_FALSE(w.is_hovering());
    EXPECT_NEAR(w.current_opacity(), 0.3f, 1e-6f);  // 未被觸發淡變
}

// 瞬時切換（fade_seconds<=0）：Enter 後不需 advance 亦可由下一次 advance 立即抵達目標。
TEST(WindowOpacityHoverFade, ZeroFadeSecondsSnapsImmediatelyOnNextAdvance) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId, /*fade_seconds=*/0.0);
    w.set_base_opacity(0.4f);
    w.set_hover_opacity(1.0f);

    HoverTracker hover;
    hover.add_surface(MakeWindowHitSurface());
    w.attach(hover);

    ASSERT_TRUE(hover.inject_move(LocalPoint{5.0f, 5.0f}));  // Enter
    // start_fade() 於 fade_seconds<=0 時立即設定 current_opacity_，advance(0) 亦回報一致值。
    EXPECT_NEAR(w.current_opacity(), 1.0f, 1e-6f);
    EXPECT_EQ(w.advance(0.0), WindowOpacityStatus::Ok);
    EXPECT_NEAR(w.current_opacity(), 1.0f, 1e-6f);
}

// -----------------------------------------------------------------------------
// 透過 E1-03 AlphaSurfaceService 實際套用：能力 / 註冊狀態轉發
// -----------------------------------------------------------------------------

// 能力不可用（NFR-03）：advance() 轉發 Unsupported，但內部 current_opacity() 仍持續追蹤
// （降級路徑：呼叫端仍可查詢計算值，即使無法實際套用到後端）。
TEST(WindowOpacityBackend, AdvanceForwardsUnsupportedWhenCapabilityUnavailable) {
    NullKernelBackend backend(alpha_incapable_matrix());
    backend.init();
    AlphaSurfaceService svc(backend);  // 能力不可用：create_alpha_surface 也會是 Unsupported

    WindowOpacity w(svc, kSurfaceId, /*fade_seconds=*/1.0);
    EXPECT_EQ(w.set_base_opacity(0.5f), WindowOpacityStatus::Unsupported);
    EXPECT_NEAR(w.current_opacity(), 0.5f, 1e-6f);  // 內部值仍照常計算/追蹤

    EXPECT_EQ(w.advance(0.1), WindowOpacityStatus::Unsupported);
}

// surface 未於 E1-03 註冊為 alpha surface：套用回 Invalid。
TEST(WindowOpacityBackend, AdvanceForwardsInvalidWhenSurfaceUnregistered) {
    NullKernelBackend backend(alpha_capable_matrix());
    backend.init();
    AlphaSurfaceService svc(backend);  // 有能力，但從未 create_alpha_surface("surface.ghost", ...)

    WindowOpacity w(svc, "surface.ghost", /*fade_seconds=*/1.0);
    EXPECT_EQ(w.set_base_opacity(0.5f), WindowOpacityStatus::Invalid);
    EXPECT_EQ(w.advance(0.1), WindowOpacityStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 當前透明度查詢
// -----------------------------------------------------------------------------

// current_opacity() 為純查詢：連續呼叫不改變狀態、不重複套用。
TEST(WindowOpacityQuery, CurrentOpacityIsPureQuery) {
    Fixture f;
    WindowOpacity w(f.alpha_service, kSurfaceId);
    w.set_base_opacity(0.55f);

    const float first = w.current_opacity();
    const float second = w.current_opacity();
    EXPECT_NEAR(first, 0.55f, 1e-6f);
    EXPECT_NEAR(second, first, 1e-6f);
    EXPECT_FALSE(w.is_hovering());
}

}  // namespace
