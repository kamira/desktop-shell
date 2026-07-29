// E4-29 折線圖 — 單元測試（gtest）
//
// 涵蓋：序列→折線點、y 軸縮放、滾動視窗保留最新 N、多序列、平滑（移動平均）、下方填充、
// 空序列、超範圍夾限、合成描述（E1-03 AlphaProfile）、render_model 內容、非法輸入不靜默。
// 全程無真實繪製、平台中立。
#include "line_chart.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

using ds::elements::kLineChartUnbounded;
using ds::elements::LineChartElement;
using ds::elements::LineChartRenderModel;
using ds::elements::LineChartStatus;

namespace {

constexpr float kEps = 1e-6f;

// --- 序列→折線點、正規化座標 -------------------------------------------------

// 序列→折線點：每筆樣本一個點，y 為值域內的比例，x 為序列內位置比例，順序即時間序。
TEST(LineChart, SeriesMapsToNormalizedPoints) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 100.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({0.0, 25.0, 50.0, 100.0}), LineChartStatus::Ok);

    const LineChartRenderModel m = lc.render_model();
    ASSERT_FALSE(m.empty);
    ASSERT_EQ(m.series.size(), 1u);
    const auto& s0 = m.series[0];
    EXPECT_EQ(s0.name, "");
    ASSERT_FALSE(s0.empty);
    ASSERT_EQ(s0.points.size(), 4u);

    EXPECT_NEAR(s0.points[0].y, 0.0f, kEps);
    EXPECT_NEAR(s0.points[1].y, 0.25f, kEps);
    EXPECT_NEAR(s0.points[2].y, 0.5f, kEps);
    EXPECT_NEAR(s0.points[3].y, 1.0f, kEps);

    // x 依 index/(count-1) 正規化：0, 1/3, 2/3, 1。
    EXPECT_NEAR(s0.points[0].x, 0.0f, kEps);
    EXPECT_NEAR(s0.points[1].x, 1.0f / 3.0f, kEps);
    EXPECT_NEAR(s0.points[2].x, 2.0f / 3.0f, kEps);
    EXPECT_NEAR(s0.points[3].x, 1.0f, kEps);

    // 原始值保留、順序即時間序（最舊→最新）。
    EXPECT_DOUBLE_EQ(s0.points[0].value, 0.0);
    EXPECT_DOUBLE_EQ(s0.points[3].value, 100.0);
    for (const auto& p : s0.points) EXPECT_FALSE(p.clamped);

    EXPECT_EQ(m.range.min, 0.0);
    EXPECT_EQ(m.range.max, 100.0);
}

// 單點序列：x 恆為 0（無「間距」可言，定義為序列起點）。
TEST(LineChart, SinglePointSeriesHasZeroX) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 10.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.push_sample(5.0), LineChartStatus::Ok);
    const LineChartRenderModel m = lc.render_model();
    ASSERT_EQ(m.series[0].points.size(), 1u);
    EXPECT_NEAR(m.series[0].points[0].x, 0.0f, kEps);
    EXPECT_NEAR(m.series[0].points[0].y, 0.5f, kEps);
}

// 非原點值域：y = (v - min) / (max - min)。
TEST(LineChart, ScalingRespectsNonZeroRangeMin) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(20.0, 40.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({20.0, 30.0, 40.0}), LineChartStatus::Ok);
    const LineChartRenderModel m = lc.render_model();
    EXPECT_NEAR(m.series[0].points[0].y, 0.0f, kEps);
    EXPECT_NEAR(m.series[0].points[1].y, 0.5f, kEps);
    EXPECT_NEAR(m.series[0].points[2].y, 1.0f, kEps);
}

// 無效值域一律 Invalid 且不套用（保留舊值域）。
TEST(LineChart, InvalidRangeRejected) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 10.0), LineChartStatus::Ok);
    EXPECT_EQ(lc.set_range(5.0, 5.0), LineChartStatus::Invalid);   // min==max
    EXPECT_EQ(lc.set_range(10.0, 0.0), LineChartStatus::Invalid);  // min>max
    EXPECT_EQ(lc.set_range(std::nan(""), 10.0), LineChartStatus::Invalid);
    EXPECT_EQ(lc.set_range(0.0, std::numeric_limits<double>::infinity()), LineChartStatus::Invalid);
    EXPECT_EQ(lc.range().min, 0.0);
    EXPECT_EQ(lc.range().max, 10.0);
}

// --- 超範圍夾限（明確、不靜默）----------------------------------------------

TEST(LineChart, OutOfRangeSamplesClampedAndFlagged) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 100.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({-30.0, 150.0, 50.0}), LineChartStatus::Ok);
    const LineChartRenderModel m = lc.render_model();
    const auto& pts = m.series[0].points;
    ASSERT_EQ(pts.size(), 3u);
    EXPECT_NEAR(pts[0].y, 0.0f, kEps);
    EXPECT_TRUE(pts[0].clamped);
    EXPECT_DOUBLE_EQ(pts[0].value, -30.0);  // 原始值仍保留（不靜默改寫）。
    EXPECT_NEAR(pts[1].y, 1.0f, kEps);
    EXPECT_TRUE(pts[1].clamped);
    EXPECT_NEAR(pts[2].y, 0.5f, kEps);
    EXPECT_FALSE(pts[2].clamped);
}

// --- 滾動視窗保留最新 N -------------------------------------------------------

TEST(LineChart, ScrollingWindowKeepsLatestNOnPush) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 10.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_capacity(3), LineChartStatus::Ok);
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) ASSERT_EQ(lc.push_sample(v), LineChartStatus::Ok);
    EXPECT_EQ(lc.sample_count(), 3u);
    const LineChartRenderModel m = lc.render_model();
    const auto& pts = m.series[0].points;
    ASSERT_EQ(pts.size(), 3u);
    EXPECT_DOUBLE_EQ(pts[0].value, 3.0);  // 最舊者 1,2 已被丟棄。
    EXPECT_DOUBLE_EQ(pts[1].value, 4.0);
    EXPECT_DOUBLE_EQ(pts[2].value, 5.0);
}

TEST(LineChart, ScrollingWindowKeepsLatestNOnSetSeries) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_capacity(2), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({1.0, 2.0, 3.0, 4.0}), LineChartStatus::Ok);
    EXPECT_EQ(lc.sample_count(), 2u);
    EXPECT_DOUBLE_EQ(lc.samples()[0], 3.0);
    EXPECT_DOUBLE_EQ(lc.samples()[1], 4.0);
}

TEST(LineChart, ReducingCapacityTrimsExisting) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_series({1.0, 2.0, 3.0, 4.0, 5.0}), LineChartStatus::Ok);
    EXPECT_EQ(lc.sample_count(), 5u);
    ASSERT_EQ(lc.set_capacity(2), LineChartStatus::Ok);
    EXPECT_EQ(lc.sample_count(), 2u);
    EXPECT_DOUBLE_EQ(lc.samples()[0], 4.0);
    EXPECT_DOUBLE_EQ(lc.samples()[1], 5.0);
}

// 容量對所有序列一體適用（含具名序列）。
TEST(LineChart, CapacityAppliesAcrossAllSeries) {
    LineChartElement lc;
    ASSERT_EQ(lc.add_series("gpu"), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_capacity(2), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({1.0, 2.0, 3.0}), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series("gpu", {10.0, 20.0, 30.0, 40.0}), LineChartStatus::Ok);
    EXPECT_EQ(lc.sample_count(), 2u);
    EXPECT_EQ(lc.sample_count("gpu"), 2u);
}

TEST(LineChart, UnboundedByDefault) {
    LineChartElement lc;
    EXPECT_EQ(lc.capacity(), kLineChartUnbounded);
    for (int i = 0; i < 50; ++i) ASSERT_EQ(lc.push_sample(static_cast<double>(i)), LineChartStatus::Ok);
    EXPECT_EQ(lc.sample_count(), 50u);
}

// --- 多序列（可選）------------------------------------------------------------

TEST(LineChart, MultiSeriesAddAndIndependentData) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 100.0), LineChartStatus::Ok);
    EXPECT_EQ(lc.series_count(), 1u);  // 預設序列。

    ASSERT_EQ(lc.add_series("cpu"), LineChartStatus::Ok);
    ASSERT_EQ(lc.add_series("gpu"), LineChartStatus::Ok);
    EXPECT_EQ(lc.series_count(), 3u);

    ASSERT_EQ(lc.set_series("cpu", {10.0, 20.0}), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series("gpu", {80.0, 90.0, 100.0}), LineChartStatus::Ok);
    ASSERT_EQ(lc.push_sample(5.0), LineChartStatus::Ok);  // 預設序列。

    const LineChartRenderModel m = lc.render_model();
    ASSERT_EQ(m.series.size(), 3u);
    EXPECT_EQ(m.series[0].name, "");
    EXPECT_EQ(m.series[1].name, "cpu");
    EXPECT_EQ(m.series[2].name, "gpu");

    EXPECT_EQ(m.series[0].points.size(), 1u);
    EXPECT_EQ(m.series[1].points.size(), 2u);
    EXPECT_EQ(m.series[2].points.size(), 3u);

    // 疊加順序 = 清單順序（NFR-02：非數字 z-order）。
    EXPECT_NEAR(m.series[2].points[2].y, 1.0f, kEps);
}

TEST(LineChart, AddSeriesRejectsEmptyOrDuplicateName) {
    LineChartElement lc;
    EXPECT_EQ(lc.add_series(""), LineChartStatus::Invalid);       // "" 保留給預設序列。
    ASSERT_EQ(lc.add_series("cpu"), LineChartStatus::Ok);
    EXPECT_EQ(lc.add_series("cpu"), LineChartStatus::Invalid);    // 重複名稱。
    EXPECT_EQ(lc.series_count(), 2u);                              // 未新增。
}

TEST(LineChart, OperationsOnUnknownSeriesRejected) {
    LineChartElement lc;
    EXPECT_EQ(lc.push_sample("missing", 1.0), LineChartStatus::Invalid);
    EXPECT_EQ(lc.set_series("missing", {1.0, 2.0}), LineChartStatus::Invalid);
    EXPECT_EQ(lc.sample_count("missing"), 0u);  // 找不到回 0（與存在但空需另以 series_count 分辨）。
}

// set_series（具名）內含非有限值 → 整批拒絕、不部分寫入。
TEST(LineChart, NamedSetSeriesRejectsAllOrNothing) {
    LineChartElement lc;
    ASSERT_EQ(lc.add_series("cpu"), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series("cpu", {1.0, 2.0}), LineChartStatus::Ok);
    EXPECT_EQ(lc.set_series("cpu", {3.0, std::nan(""), 4.0}), LineChartStatus::Invalid);
    ASSERT_EQ(lc.sample_count("cpu"), 2u);  // 舊序列保留不變。
}

// --- 平滑（可選；簡單移動平均）------------------------------------------------

TEST(LineChart, SmoothingDisabledMatchesRawY) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 100.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({0.0, 100.0, 0.0, 100.0}), LineChartStatus::Ok);
    EXPECT_FALSE(lc.smoothing_enabled());
    const LineChartRenderModel m = lc.render_model();
    EXPECT_FALSE(m.smoothed);
    for (const auto& p : m.series[0].points) EXPECT_NEAR(p.smoothed_y, p.y, kEps);
}

TEST(LineChart, SmoothingAppliesMovingAverage) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_range(0.0, 100.0), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series({0.0, 100.0, 0.0}), LineChartStatus::Ok);
    lc.set_smoothing(true);
    EXPECT_TRUE(lc.smoothing_enabled());

    const LineChartRenderModel m = lc.render_model();
    EXPECT_TRUE(m.smoothed);
    const auto& pts = m.series[0].points;
    ASSERT_EQ(pts.size(), 3u);
    // 原始 y 不受影響（0, 1, 0）。
    EXPECT_NEAR(pts[0].y, 0.0f, kEps);
    EXPECT_NEAR(pts[1].y, 1.0f, kEps);
    EXPECT_NEAR(pts[2].y, 0.0f, kEps);
    // 移動平均（窗口 3，邊界取可得鄰居）：
    //   point0 = avg(0,100) = 50 → y=0.5；point1 = avg(0,100,0) = 33.33 → y≈0.3333；
    //   point2 = avg(100,0) = 50 → y=0.5。
    EXPECT_NEAR(pts[0].smoothed_y, 0.5f, kEps);
    EXPECT_NEAR(pts[1].smoothed_y, 1.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(pts[2].smoothed_y, 0.5f, kEps);
}

// --- 下方填充（可選）----------------------------------------------------------

TEST(LineChart, FillDisabledByDefault) {
    LineChartElement lc;
    EXPECT_FALSE(lc.fill_enabled());
    ASSERT_EQ(lc.set_series({1.0, 2.0}), LineChartStatus::Ok);
    const LineChartRenderModel m = lc.render_model();
    EXPECT_FALSE(m.series[0].fill.enabled);
}

TEST(LineChart, FillFlagPropagatesToEverySeries) {
    LineChartElement lc;
    ASSERT_EQ(lc.add_series("cpu"), LineChartStatus::Ok);
    lc.set_fill(true);
    EXPECT_TRUE(lc.fill_enabled());
    ASSERT_EQ(lc.set_series({1.0}), LineChartStatus::Ok);
    ASSERT_EQ(lc.set_series("cpu", {2.0}), LineChartStatus::Ok);

    const LineChartRenderModel m = lc.render_model();
    ASSERT_EQ(m.series.size(), 2u);
    for (const auto& sm : m.series) {
        EXPECT_TRUE(sm.fill.enabled);
        EXPECT_NEAR(sm.fill.baseline, 0.0f, kEps);  // 底線恆為 range.min（正規化幅值 0）。
    }
}

// --- 空序列（明確、不靜默）---------------------------------------------------

TEST(LineChart, EmptySeriesReportedExplicitly) {
    LineChartElement lc;
    const LineChartRenderModel m = lc.render_model();
    EXPECT_TRUE(m.empty);
    ASSERT_EQ(m.series.size(), 1u);
    EXPECT_TRUE(m.series[0].empty);
    EXPECT_TRUE(m.series[0].points.empty());
    EXPECT_EQ(lc.sample_count(), 0u);

    // clear() 後回到空（含具名序列）。
    LineChartElement lc2;
    ASSERT_EQ(lc2.add_series("cpu"), LineChartStatus::Ok);
    ASSERT_EQ(lc2.set_series({1.0, 2.0}), LineChartStatus::Ok);
    ASSERT_EQ(lc2.set_series("cpu", {3.0}), LineChartStatus::Ok);
    lc2.clear();
    const LineChartRenderModel m2 = lc2.render_model();
    EXPECT_TRUE(m2.empty);
    for (const auto& sm : m2.series) EXPECT_TRUE(sm.empty);
}

// model.empty 僅當「所有」序列皆空才為 true——只要有一序列非空即為 false。
TEST(LineChart, ModelEmptyOnlyWhenAllSeriesEmpty) {
    LineChartElement lc;
    ASSERT_EQ(lc.add_series("cpu"), LineChartStatus::Ok);
    // 預設序列空、cpu 序列有資料。
    ASSERT_EQ(lc.set_series("cpu", {1.0}), LineChartStatus::Ok);
    const LineChartRenderModel m = lc.render_model();
    EXPECT_FALSE(m.empty);
    EXPECT_TRUE(m.series[0].empty);
    EXPECT_FALSE(m.series[1].empty);
}

// --- 非有限輸入不靜默 --------------------------------------------------------

TEST(LineChart, NonFiniteSampleRejected) {
    LineChartElement lc;
    EXPECT_EQ(lc.push_sample(std::nan("")), LineChartStatus::Invalid);
    EXPECT_EQ(lc.push_sample(std::numeric_limits<double>::infinity()), LineChartStatus::Invalid);
    EXPECT_EQ(lc.sample_count(), 0u);  // 均未追加。
}

TEST(LineChart, SetSeriesRejectsAllOrNothing) {
    LineChartElement lc;
    ASSERT_EQ(lc.set_series({1.0, 2.0}), LineChartStatus::Ok);
    EXPECT_EQ(lc.set_series({3.0, std::nan(""), 4.0}), LineChartStatus::Invalid);
    ASSERT_EQ(lc.sample_count(), 2u);
    EXPECT_DOUBLE_EQ(lc.samples()[0], 1.0);
    EXPECT_DOUBLE_EQ(lc.samples()[1], 2.0);
}

// --- 合成描述（E1-03 AlphaProfile，經 E4-17 傳遞）----------------------------

TEST(LineChart, CompositeDefaultsAndClamps) {
    LineChartElement lc;
    EXPECT_EQ(lc.composite().mode, ds::kernel::AlphaMode::PerPixel);
    EXPECT_NEAR(lc.composite().opacity, 1.0f, kEps);

    ds::kernel::AlphaProfile p;
    p.mode = ds::kernel::AlphaMode::Opaque;
    p.opacity = 1.5f;  // 越界 → 夾至 1。
    ASSERT_EQ(lc.set_composite(p), LineChartStatus::Ok);
    EXPECT_EQ(lc.composite().mode, ds::kernel::AlphaMode::Opaque);
    EXPECT_NEAR(lc.composite().opacity, 1.0f, kEps);

    p.opacity = -0.2f;  // 越界 → 夾至 0。
    ASSERT_EQ(lc.set_composite(p), LineChartStatus::Ok);
    EXPECT_NEAR(lc.composite().opacity, 0.0f, kEps);

    EXPECT_EQ(lc.render_model().composite.mode, ds::kernel::AlphaMode::Opaque);

    p.opacity = std::nan("");
    EXPECT_EQ(lc.set_composite(p), LineChartStatus::Invalid);
}

}  // namespace
