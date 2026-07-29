// E4-21 漸層填色 — 單元測試（gtest）
//
// 涵蓋：線性 / 放射狀漸層種類、多停靠點自動排序、sample(t) 內插（含邊界夾限）、
// 角度設定與正規化、停靠點位置越界報錯、顏色分量無效報錯、合成描述（E1-03 AlphaProfile）、
// render_model 內容。全程無真實繪製、平台中立。
#include "gradient_fill.hpp"

#include <gtest/gtest.h>

#include <limits>

using ds::elements::Color;
using ds::elements::GradientFill;
using ds::elements::GradientRenderModel;
using ds::elements::GradientStatus;
using ds::elements::GradientStop;
using ds::elements::GradientType;

namespace {

constexpr float kEps = 1e-6f;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

Color red() { return Color{1.0f, 0.0f, 0.0f, 1.0f}; }
Color blue() { return Color{0.0f, 0.0f, 1.0f, 1.0f}; }

}  // namespace

// --- 種類 -------------------------------------------------------------------

TEST(GradientFill, DefaultTypeIsLinear) {
    GradientFill g;
    EXPECT_TRUE(g.type() == GradientType::Linear);
}

TEST(GradientFill, RadialTypeConstructedExplicitly) {
    GradientFill g(GradientType::Radial);
    EXPECT_TRUE(g.type() == GradientType::Radial);
    const GradientRenderModel m = g.render_model();
    EXPECT_TRUE(m.type == GradientType::Radial);
}

// --- add_stop：有效輸入 -------------------------------------------------------

TEST(GradientFill, AddStopValidReturnsOkAndIncrementsCount) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    EXPECT_EQ(g.stop_count(), 1u);
    EXPECT_TRUE(g.add_stop(1.0f, blue()) == GradientStatus::Ok);
    EXPECT_EQ(g.stop_count(), 2u);
}

// --- add_stop：越界 / 非有限位置報錯 -----------------------------------------

TEST(GradientFill, AddStopPositionBelowZeroIsInvalidAndNotAdded) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(-0.1f, red()) == GradientStatus::InvalidPosition);
    EXPECT_EQ(g.stop_count(), 0u);
}

TEST(GradientFill, AddStopPositionAboveOneIsInvalidAndNotAdded) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(1.1f, red()) == GradientStatus::InvalidPosition);
    EXPECT_EQ(g.stop_count(), 0u);
}

TEST(GradientFill, AddStopNonFinitePositionIsInvalid) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(kNaN, red()) == GradientStatus::InvalidPosition);
    EXPECT_EQ(g.stop_count(), 0u);
}

// --- add_stop：顏色分量無效報錯 ----------------------------------------------

TEST(GradientFill, AddStopColorChannelOutOfRangeIsInvalidAndNotAdded) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(0.5f, Color{1.5f, 0.0f, 0.0f, 1.0f}) == GradientStatus::InvalidColor);
    EXPECT_EQ(g.stop_count(), 0u);
}

TEST(GradientFill, AddStopColorChannelNegativeIsInvalid) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(0.5f, Color{0.0f, -0.01f, 0.0f, 1.0f}) == GradientStatus::InvalidColor);
    EXPECT_EQ(g.stop_count(), 0u);
}

TEST(GradientFill, AddStopColorChannelNonFiniteIsInvalid) {
    GradientFill g;
    EXPECT_TRUE(g.add_stop(0.5f, Color{0.0f, 0.0f, 0.0f, kNaN}) == GradientStatus::InvalidColor);
    EXPECT_EQ(g.stop_count(), 0u);
}

// --- 多停靠點排序 -------------------------------------------------------------

TEST(GradientFill, StopsAreSortedByPositionRegardlessOfInsertionOrder) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.75f, blue()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(0.25f, Color{0.0f, 1.0f, 0.0f, 1.0f}) == GradientStatus::Ok);

    const auto& stops = g.stops();
    ASSERT_EQ(stops.size(), 3u);
    EXPECT_NEAR(stops[0].position, 0.0f, kEps);
    EXPECT_NEAR(stops[1].position, 0.25f, kEps);
    EXPECT_NEAR(stops[2].position, 0.75f, kEps);
}

TEST(GradientFill, ClearStopsRemovesAllButKeepsOtherSettings) {
    GradientFill g;
    ASSERT_TRUE(g.set_angle(45.0f) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    g.clear_stops();
    EXPECT_EQ(g.stop_count(), 0u);
    EXPECT_NEAR(g.angle(), 45.0f, kEps);
}

// --- 角度 ---------------------------------------------------------------------

TEST(GradientFill, SetAngleValidUpdatesAngle) {
    GradientFill g;
    EXPECT_TRUE(g.set_angle(90.0f) == GradientStatus::Ok);
    EXPECT_NEAR(g.angle(), 90.0f, kEps);
}

TEST(GradientFill, SetAngleNormalizesAboveRange) {
    GradientFill g;
    ASSERT_TRUE(g.set_angle(370.0f) == GradientStatus::Ok);
    EXPECT_NEAR(g.angle(), 10.0f, kEps);
}

TEST(GradientFill, SetAngleNormalizesNegative) {
    GradientFill g;
    ASSERT_TRUE(g.set_angle(-10.0f) == GradientStatus::Ok);
    EXPECT_NEAR(g.angle(), 350.0f, kEps);
}

TEST(GradientFill, SetAngleNonFiniteIsInvalid) {
    GradientFill g;
    ASSERT_TRUE(g.set_angle(180.0f) == GradientStatus::Ok);
    EXPECT_TRUE(g.set_angle(kNaN) == GradientStatus::InvalidAngle);
    // 非法輸入不套用，角度維持先前有效值。
    EXPECT_NEAR(g.angle(), 180.0f, kEps);
}

// --- sample() 內插 -------------------------------------------------------------

TEST(GradientFill, SampleWithNoStopsReturnsTransparentBlack) {
    GradientFill g;
    const Color c = g.sample(0.5f);
    EXPECT_NEAR(c.r, 0.0f, kEps);
    EXPECT_NEAR(c.g, 0.0f, kEps);
    EXPECT_NEAR(c.b, 0.0f, kEps);
    EXPECT_NEAR(c.a, 0.0f, kEps);
}

TEST(GradientFill, SampleWithSingleStopReturnsConstantColor) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.5f, red()) == GradientStatus::Ok);
    const Color c0 = g.sample(0.0f);
    const Color c1 = g.sample(1.0f);
    EXPECT_NEAR(c0.r, 1.0f, kEps);
    EXPECT_NEAR(c1.r, 1.0f, kEps);
}

TEST(GradientFill, SampleInterpolatesMidpointBetweenTwoStops) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(1.0f, blue()) == GradientStatus::Ok);

    const Color mid = g.sample(0.5f);
    EXPECT_NEAR(mid.r, 0.5f, kEps);
    EXPECT_NEAR(mid.g, 0.0f, kEps);
    EXPECT_NEAR(mid.b, 0.5f, kEps);
    EXPECT_NEAR(mid.a, 1.0f, kEps);
}

TEST(GradientFill, SampleQuarterPointWeightsTowardNearerStop) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(1.0f, blue()) == GradientStatus::Ok);

    const Color q = g.sample(0.25f);
    EXPECT_NEAR(q.r, 0.75f, kEps);
    EXPECT_NEAR(q.b, 0.25f, kEps);
}

TEST(GradientFill, SampleBeforeFirstStopClampsToFirstStopColor) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.3f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(0.7f, blue()) == GradientStatus::Ok);

    const Color c = g.sample(0.0f);
    EXPECT_NEAR(c.r, 1.0f, kEps);
    EXPECT_NEAR(c.b, 0.0f, kEps);
}

TEST(GradientFill, SampleAfterLastStopClampsToLastStopColor) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.3f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(0.7f, blue()) == GradientStatus::Ok);

    const Color c = g.sample(1.0f);
    EXPECT_NEAR(c.r, 0.0f, kEps);
    EXPECT_NEAR(c.b, 1.0f, kEps);
}

TEST(GradientFill, SampleClampsOutOfRangeTArgument) {
    GradientFill g;
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(1.0f, blue()) == GradientStatus::Ok);

    const Color below = g.sample(-5.0f);
    const Color above = g.sample(5.0f);
    EXPECT_NEAR(below.r, 1.0f, kEps);  // 夾限至 0 → 第一停靠點（紅）
    EXPECT_NEAR(above.b, 1.0f, kEps);  // 夾限至 1 → 最末停靠點（藍）
}

// --- 合成描述（E1-03 AlphaProfile）--------------------------------------------

TEST(GradientFill, SetCompositeValidUpdatesAndClampsOpacity) {
    GradientFill g;
    ds::kernel::AlphaProfile profile;
    profile.mode = ds::kernel::AlphaMode::Opaque;
    profile.opacity = 2.0f;  // 越界，應被夾限至 1.0
    EXPECT_TRUE(g.set_composite(profile) == GradientStatus::Ok);
    EXPECT_TRUE(g.composite().mode == ds::kernel::AlphaMode::Opaque);
    EXPECT_NEAR(g.composite().opacity, 1.0f, kEps);
}

TEST(GradientFill, SetCompositeNonFiniteOpacityIsInvalid) {
    GradientFill g;
    ds::kernel::AlphaProfile profile;
    profile.opacity = kNaN;
    EXPECT_TRUE(g.set_composite(profile) == GradientStatus::InvalidComposite);
    // 未套用：維持預設 opacity=1.0。
    EXPECT_NEAR(g.composite().opacity, 1.0f, kEps);
}

// --- render_model --------------------------------------------------------------

TEST(GradientFill, RenderModelReflectsTypeAngleStopsAndComposite) {
    GradientFill g(GradientType::Linear);
    ASSERT_TRUE(g.set_angle(45.0f) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(1.0f, blue()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    ds::kernel::AlphaProfile profile;
    profile.opacity = 0.5f;
    ASSERT_TRUE(g.set_composite(profile) == GradientStatus::Ok);

    const GradientRenderModel m = g.render_model();
    EXPECT_TRUE(m.type == GradientType::Linear);
    EXPECT_NEAR(m.angle_degrees, 45.0f, kEps);
    ASSERT_EQ(m.stops.size(), 2u);
    EXPECT_NEAR(m.stops[0].position, 0.0f, kEps);  // 已排序
    EXPECT_NEAR(m.stops[1].position, 1.0f, kEps);
    EXPECT_NEAR(m.composite.opacity, 0.5f, kEps);
}

TEST(GradientFill, RenderModelForRadialCarriesRadialType) {
    GradientFill g(GradientType::Radial);
    ASSERT_TRUE(g.add_stop(0.0f, red()) == GradientStatus::Ok);
    ASSERT_TRUE(g.add_stop(1.0f, blue()) == GradientStatus::Ok);

    const GradientRenderModel m = g.render_model();
    EXPECT_TRUE(m.type == GradientType::Radial);
    EXPECT_EQ(m.stops.size(), 2u);
}
