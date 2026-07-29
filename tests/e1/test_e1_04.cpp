// E1-04 幾何命中測試 — 單元測試（gtest）
//
// 驗證 HitTester 於相位 1（Mac / null 期）的純幾何命中 + 注入式 alpha + 具名圖層優先：
//   - 點內判定：矩形 / 圓角矩形 / 圓 / 多邊形（凸 / 凹 / even-odd）/ path（NonZero），含邊界
//   - 無效形狀 / 座標報錯不靜默（負範圍 / 負半徑 / 非有限值 / 頂點數不足 / 非有限查詢點）
//   - alpha 命中：Opaque 忽略 alpha、PerPixel 透明處不命中、門檻 / 整體 opacity、oracle 回非有限值報錯
//   - HitPolicy::Transparent（命中穿透）永不命中本 surface
//   - topmost：多 surface 重疊依具名圖層 + 宣告順序決定命中者；無命中 / 任一無效
//   - NFR-02：座標本地 / 相對、優先序具名圖層 + 宣告順序（無畫面絕對座標 / 數字 z-order）
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗系統 / 繪圖 API。
#include "hit_test.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using ds::kernel::AlphaMode;
using ds::kernel::AlphaProfile;
using ds::kernel::FillRule;
using ds::kernel::HitStatus;
using ds::kernel::HitSurface;
using ds::kernel::HitTester;
using ds::kernel::LocalPoint;
using ds::kernel::make_circle;
using ds::kernel::make_path;
using ds::kernel::make_polygon;
using ds::kernel::make_rect;
using ds::kernel::make_rounded_rect;
using ds::kernel::Shape;
using ds::kernel::SurfaceLayer;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// -----------------------------------------------------------------------------
// 矩形
// -----------------------------------------------------------------------------

TEST(HitRect, InsideOutsideAndBoundary) {
    HitTester t;
    Shape r = make_rect(10.0f, 4.0f);

    auto in = t.hit_test(LocalPoint{5.0f, 2.0f}, r);
    EXPECT_EQ(in.status, HitStatus::Ok);
    EXPECT_TRUE(in.inside);

    EXPECT_FALSE(t.hit_test(LocalPoint{-0.1f, 2.0f}, r).inside);
    EXPECT_FALSE(t.hit_test(LocalPoint{10.1f, 2.0f}, r).inside);
    EXPECT_FALSE(t.hit_test(LocalPoint{5.0f, 4.1f}, r).inside);

    // 邊界（含）：四角與邊上皆命中。
    EXPECT_TRUE(t.hit_test(LocalPoint{0.0f, 0.0f}, r).inside);
    EXPECT_TRUE(t.hit_test(LocalPoint{10.0f, 4.0f}, r).inside);
    EXPECT_TRUE(t.hit_test(LocalPoint{0.0f, 2.0f}, r).inside);
}

// -----------------------------------------------------------------------------
// 圓角矩形
// -----------------------------------------------------------------------------

TEST(HitRoundedRect, CornersAreClipped) {
    HitTester t;
    Shape rr = make_rounded_rect(10.0f, 10.0f, 3.0f);

    // 幾何中心命中。
    EXPECT_TRUE(t.hit_test(LocalPoint{5.0f, 5.0f}, rr).inside);
    // 邊中點命中（非角落）。
    EXPECT_TRUE(t.hit_test(LocalPoint{5.0f, 0.0f}, rr).inside);
    // 極角 (0,0)：距角落圓心 (3,3) 為 sqrt(18) > 3 → 被裁掉，不命中。
    EXPECT_FALSE(t.hit_test(LocalPoint{0.0f, 0.0f}, rr).inside);
    // 角落圓弧內一點（圓心 (3,3)，點 (3.5,3.5) 距 <3）命中。
    EXPECT_TRUE(t.hit_test(LocalPoint{3.5f, 3.5f}, rr).inside);
    // corner_radius 超過 min(w,h)/2 被 clamp，不報錯。
    Shape big = make_rounded_rect(10.0f, 10.0f, 999.0f);
    EXPECT_TRUE(t.is_valid(big));
    EXPECT_TRUE(t.hit_test(LocalPoint{5.0f, 5.0f}, big).inside);
}

// -----------------------------------------------------------------------------
// 圓
// -----------------------------------------------------------------------------

TEST(HitCircle, InsideOutsideAndBoundary) {
    HitTester t;
    Shape c = make_circle(LocalPoint{5.0f, 5.0f}, 2.0f);

    EXPECT_TRUE(t.hit_test(LocalPoint{5.0f, 5.0f}, c).inside);     // 圓心
    EXPECT_TRUE(t.hit_test(LocalPoint{7.0f, 5.0f}, c).inside);     // 邊界（含）
    EXPECT_FALSE(t.hit_test(LocalPoint{7.01f, 5.0f}, c).inside);   // 界外
    EXPECT_FALSE(t.hit_test(LocalPoint{5.0f, 8.0f}, c).inside);
}

// -----------------------------------------------------------------------------
// 多邊形（even-odd）與 path（NonZero）
// -----------------------------------------------------------------------------

TEST(HitPolygon, ConvexTriangle) {
    HitTester t;
    Shape tri = make_polygon({{0.0f, 0.0f}, {4.0f, 0.0f}, {0.0f, 4.0f}});

    EXPECT_TRUE(t.hit_test(LocalPoint{1.0f, 1.0f}, tri).inside);   // 內部
    EXPECT_FALSE(t.hit_test(LocalPoint{3.0f, 3.0f}, tri).inside);  // 斜邊外
    EXPECT_TRUE(t.hit_test(LocalPoint{2.0f, 2.0f}, tri).inside);   // 斜邊上（含邊界）
    EXPECT_TRUE(t.hit_test(LocalPoint{0.0f, 0.0f}, tri).inside);   // 頂點（含邊界）
}

TEST(HitPolygon, ConcaveEvenOdd) {
    HitTester t;
    // L 形凹多邊形：底條 x∈[0,4]×y∈[0,2] + 左柱 x∈[0,2]×y∈[2,4]，
    // 缺角（凹處）為 x∈[2,4]×y∈[2,4]。凹角在 (2,2)。
    Shape ell = make_polygon({{0.0f, 0.0f},
                              {4.0f, 0.0f},
                              {4.0f, 2.0f},
                              {2.0f, 2.0f},
                              {2.0f, 4.0f},
                              {0.0f, 4.0f}});
    EXPECT_TRUE(t.hit_test(LocalPoint{1.0f, 3.0f}, ell).inside);   // 左柱內
    EXPECT_TRUE(t.hit_test(LocalPoint{3.0f, 1.0f}, ell).inside);   // 底條內
    EXPECT_FALSE(t.hit_test(LocalPoint{3.0f, 3.0f}, ell).inside);  // 缺角（凹處）外
}

TEST(HitPath, NonZeroWindingFillsInterior) {
    HitTester t;
    // 正方形，以 NonZero 規則填滿。
    Shape sq = make_path(
        {{0.0f, 0.0f}, {4.0f, 0.0f}, {4.0f, 4.0f}, {0.0f, 4.0f}},
        FillRule::NonZero);
    EXPECT_TRUE(t.hit_test(LocalPoint{2.0f, 2.0f}, sq).inside);
    EXPECT_FALSE(t.hit_test(LocalPoint{5.0f, 2.0f}, sq).inside);
    // 邊界含。
    EXPECT_TRUE(t.hit_test(LocalPoint{4.0f, 2.0f}, sq).inside);
}

// -----------------------------------------------------------------------------
// 無效形狀 / 座標：報錯不靜默（HitStatus::Invalid）
// -----------------------------------------------------------------------------

TEST(HitInvalid, MalformedShapesReportInvalid) {
    HitTester t;
    const LocalPoint p{1.0f, 1.0f};

    EXPECT_FALSE(t.is_valid(make_rect(-1.0f, 4.0f)));
    EXPECT_EQ(t.hit_test(p, make_rect(-1.0f, 4.0f)).status, HitStatus::Invalid);

    EXPECT_EQ(t.hit_test(p, make_rect(kInf, 4.0f)).status, HitStatus::Invalid);
    EXPECT_EQ(t.hit_test(p, make_circle(LocalPoint{0, 0}, -2.0f)).status,
              HitStatus::Invalid);
    EXPECT_EQ(t.hit_test(p, make_rounded_rect(4.0f, 4.0f, -1.0f)).status,
              HitStatus::Invalid);

    // 頂點數不足（< 3）。
    EXPECT_EQ(t.hit_test(p, make_polygon({{0, 0}, {1, 1}})).status,
              HitStatus::Invalid);
    // 非有限頂點。
    EXPECT_EQ(t.hit_test(p, make_polygon({{0, 0}, {kNaN, 1}, {1, 1}})).status,
              HitStatus::Invalid);

    // 非有限查詢點（形狀有效但點無效）。
    EXPECT_EQ(t.hit_test(LocalPoint{kNaN, 1.0f}, make_rect(4.0f, 4.0f)).status,
              HitStatus::Invalid);
}

// 退化但有效：零面積矩形（線 / 點）不算無效，只是幾乎無內部。
TEST(HitInvalid, DegenerateZeroExtentIsValid) {
    HitTester t;
    Shape line = make_rect(0.0f, 4.0f);
    EXPECT_TRUE(t.is_valid(line));
    EXPECT_EQ(t.hit_test(LocalPoint{0.0f, 2.0f}, line).status, HitStatus::Ok);
    EXPECT_TRUE(t.hit_test(LocalPoint{0.0f, 2.0f}, line).inside);   // 落在退化線上
    EXPECT_FALSE(t.hit_test(LocalPoint{0.1f, 2.0f}, line).inside);
}

// -----------------------------------------------------------------------------
// alpha 命中（用 E1-03 的模式 / 不透明度 + 注入式 alpha oracle）
// -----------------------------------------------------------------------------

// 幾何外：不論 alpha 一律不命中。
TEST(HitAlpha, GeometryMissNeverHits) {
    HitTester t;
    HitSurface s;
    s.id = "surface.pet";
    s.shape = make_rect(4.0f, 4.0f);
    s.alpha.mode = AlphaMode::PerPixel;
    s.alpha_query = [](const LocalPoint&) { return 1.0f; };
    auto r = t.hit_test_alpha(LocalPoint{9.0f, 9.0f}, s);
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.inside);
}

// Opaque 模式忽略 alpha：幾何命中即命中。
TEST(HitAlpha, OpaqueIgnoresAlpha) {
    HitTester t;
    HitSurface s;
    s.shape = make_rect(4.0f, 4.0f);
    s.alpha.mode = AlphaMode::Opaque;
    s.alpha_query = [](const LocalPoint&) { return 0.0f; };  // 即使全透明
    EXPECT_TRUE(t.hit_test_alpha(LocalPoint{2.0f, 2.0f}, s).inside);
}

// PerPixel：透明處（alpha 低於門檻）不命中，不透明處命中。
TEST(HitAlpha, PerPixelTransparentDoesNotHit) {
    HitTester t;
    HitSurface s;
    s.shape = make_rect(10.0f, 10.0f);
    s.alpha.mode = AlphaMode::PerPixel;
    s.alpha_threshold = 0.5f;
    // 左半透明（alpha 0）、右半不透明（alpha 1）。
    s.alpha_query = [](const LocalPoint& p) { return p.x < 5.0f ? 0.0f : 1.0f; };

    EXPECT_FALSE(t.hit_test_alpha(LocalPoint{2.0f, 5.0f}, s).inside);  // 透明處
    EXPECT_TRUE(t.hit_test_alpha(LocalPoint{7.0f, 5.0f}, s).inside);   // 不透明處
}

// 有效 alpha = per-pixel × 整體 opacity；整體不透明度壓過門檻則不命中。
TEST(HitAlpha, OverallOpacityModulates) {
    HitTester t;
    HitSurface s;
    s.shape = make_rect(4.0f, 4.0f);
    s.alpha.mode = AlphaMode::PerPixel;
    s.alpha.opacity = 0.4f;  // 整體 0.4
    s.alpha_threshold = 0.5f;
    s.alpha_query = [](const LocalPoint&) { return 1.0f; };  // per-pixel 全滿
    // 有效 = 1.0 × 0.4 = 0.4 < 0.5 → 不命中。
    EXPECT_FALSE(t.hit_test_alpha(LocalPoint{2.0f, 2.0f}, s).inside);

    s.alpha.opacity = 0.9f;  // 0.9 ≥ 0.5 → 命中。
    EXPECT_TRUE(t.hit_test_alpha(LocalPoint{2.0f, 2.0f}, s).inside);
}

// 未注入 oracle 的 PerPixel：視為 alpha 1.0（受整體 opacity 調變）。
TEST(HitAlpha, PerPixelWithoutOracleTreatsAsPresent) {
    HitTester t;
    HitSurface s;
    s.shape = make_rect(4.0f, 4.0f);
    s.alpha.mode = AlphaMode::PerPixel;  // 無 alpha_query
    EXPECT_TRUE(t.hit_test_alpha(LocalPoint{2.0f, 2.0f}, s).inside);
}

// oracle 回非有限 alpha：報錯不靜默。
TEST(HitAlpha, NonFiniteOracleReportsInvalid) {
    HitTester t;
    HitSurface s;
    s.shape = make_rect(4.0f, 4.0f);
    s.alpha.mode = AlphaMode::PerPixel;
    s.alpha_query = [](const LocalPoint&) {
        return std::numeric_limits<float>::infinity();
    };
    EXPECT_EQ(t.hit_test_alpha(LocalPoint{2.0f, 2.0f}, s).status,
              HitStatus::Invalid);
}

// HitPolicy::Transparent 之 surface 命中穿透，永不命中本 surface。
TEST(HitAlpha, TransparentHitPolicyPassesThrough) {
    HitTester t;
    HitSurface s;
    s.shape = make_rect(4.0f, 4.0f);
    s.hit = ds::kernel::HitPolicy::Transparent;
    s.alpha.mode = AlphaMode::Opaque;
    auto r = t.hit_test_alpha(LocalPoint{2.0f, 2.0f}, s);
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.inside);  // 穿透
}

// -----------------------------------------------------------------------------
// topmost：命中優先（具名圖層 + 宣告順序，NFR-02 無數字 z-order）
// -----------------------------------------------------------------------------

HitSurface MakeOpaqueRect(const char* id, SurfaceLayer layer) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(10.0f, 10.0f);
    s.layer = layer;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

// 多 surface 重疊：較高具名圖層者命中（不論宣告順序）。
TEST(HitTopmost, HigherNamedLayerWins) {
    HitTester t;
    std::vector<HitSurface> surfaces = {
        MakeOpaqueRect("surface.wallpaper", SurfaceLayer::Wallpaper),
        MakeOpaqueRect("surface.panel", SurfaceLayer::Overlay),
        MakeOpaqueRect("surface.window", SurfaceLayer::Normal),
    };
    auto r = t.topmost_hit(LocalPoint{5.0f, 5.0f}, surfaces);
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.id, "surface.panel");  // Overlay 為最高
}

// 同圖層：宣告順序後者為上。
TEST(HitTopmost, SameLayerLaterDeclaredWins) {
    HitTester t;
    std::vector<HitSurface> surfaces = {
        MakeOpaqueRect("surface.a", SurfaceLayer::Normal),
        MakeOpaqueRect("surface.b", SurfaceLayer::Normal),
    };
    auto r = t.topmost_hit(LocalPoint{5.0f, 5.0f}, surfaces);
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.id, "surface.b");  // 後宣告者在上
}

// 最高層但該點透明（PerPixel）→ 落到其下的實心 surface。
TEST(HitTopmost, TransparentTopFallsToLower) {
    HitTester t;
    HitSurface top = MakeOpaqueRect("surface.glass", SurfaceLayer::Topmost);
    top.alpha.mode = AlphaMode::PerPixel;
    top.alpha_query = [](const LocalPoint&) { return 0.0f; };  // 該點全透明
    std::vector<HitSurface> surfaces = {
        MakeOpaqueRect("surface.base", SurfaceLayer::Normal),
        top,
    };
    auto r = t.topmost_hit(LocalPoint{5.0f, 5.0f}, surfaces);
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.id, "surface.base");  // 玻璃層透明處穿過，命中底層
}

// 無任一命中：hit=false、id 空、status=Ok。
TEST(HitTopmost, NoHitIsStructured) {
    HitTester t;
    std::vector<HitSurface> surfaces = {
        MakeOpaqueRect("surface.a", SurfaceLayer::Normal),
    };
    auto r = t.topmost_hit(LocalPoint{99.0f, 99.0f}, surfaces);
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.id.empty());
}

// 任一 surface 形狀無效 → 整體 Invalid（報錯不靜默）。
TEST(HitTopmost, AnyInvalidSurfaceReportsInvalid) {
    HitTester t;
    HitSurface bad;
    bad.id = "surface.bad";
    bad.shape = make_rect(-1.0f, 4.0f);  // 無效
    std::vector<HitSurface> surfaces = {
        MakeOpaqueRect("surface.ok", SurfaceLayer::Normal),
        bad,
    };
    auto r = t.topmost_hit(LocalPoint{5.0f, 5.0f}, surfaces);
    EXPECT_EQ(r.status, HitStatus::Invalid);
    EXPECT_FALSE(r.hit);
}

// 空 surface 清單：無命中，不崩潰。
TEST(HitTopmost, EmptyListIsNoHit) {
    HitTester t;
    auto r = t.topmost_hit(LocalPoint{5.0f, 5.0f}, {});
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.hit);
}

}  // namespace
