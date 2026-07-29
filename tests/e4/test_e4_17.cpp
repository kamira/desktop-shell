// E4-17 直方圖 — 單元測試（gtest）
//
// 涵蓋：序列→bins、y 軸縮放、滾動視窗保留最新 N、閾值線、空序列、超範圍夾限、
// 合成描述（E1-03 AlphaProfile）、E7-03 段落變數驅動、render_model 內容、非法輸入不靜默。
// 全程無真實繪製、平台中立。
#include "histogram.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "document.hpp"      // E7-01：parse
#include "section_vars.hpp"  // E7-03：expand（間接經 configure_from_document）

using ds::elements::configure;
using ds::elements::configure_from_document;
using ds::elements::HistogramConfigError;
using ds::elements::HistogramElement;
using ds::elements::HistogramRenderModel;
using ds::elements::HistogramStatus;
using ds::elements::kHistogramUnbounded;

namespace {

constexpr float kEps = 1e-6f;

// --- 值域設定與 y 軸縮放 ------------------------------------------------------

// 序列→bins：每筆樣本一根長條，magnitude 為值域內的比例，順序即時間序。
TEST(Histogram, SeriesMapsToNormalizedBars) {
    HistogramElement h;
    ASSERT_EQ(h.set_range(0.0, 100.0), HistogramStatus::Ok);
    ASSERT_EQ(h.set_series({0.0, 25.0, 50.0, 100.0}), HistogramStatus::Ok);

    const HistogramRenderModel m = h.render_model();
    ASSERT_FALSE(m.empty);
    ASSERT_EQ(m.bars.size(), 4u);
    EXPECT_NEAR(m.bars[0].magnitude, 0.0f, kEps);
    EXPECT_NEAR(m.bars[1].magnitude, 0.25f, kEps);
    EXPECT_NEAR(m.bars[2].magnitude, 0.5f, kEps);
    EXPECT_NEAR(m.bars[3].magnitude, 1.0f, kEps);
    // 原始值保留、順序即時間序（最舊→最新）。
    EXPECT_DOUBLE_EQ(m.bars[0].value, 0.0);
    EXPECT_DOUBLE_EQ(m.bars[3].value, 100.0);
    // 未越界者不標記 clamped。
    for (const auto& b : m.bars) EXPECT_FALSE(b.clamped);
    EXPECT_EQ(m.range.min, 0.0);
    EXPECT_EQ(m.range.max, 100.0);
}

// 非原點值域：magnitude = (v - min) / (max - min)。
TEST(Histogram, ScalingRespectsNonZeroRangeMin) {
    HistogramElement h;
    ASSERT_EQ(h.set_range(20.0, 40.0), HistogramStatus::Ok);
    ASSERT_EQ(h.set_series({20.0, 30.0, 40.0}), HistogramStatus::Ok);
    const HistogramRenderModel m = h.render_model();
    EXPECT_NEAR(m.bars[0].magnitude, 0.0f, kEps);
    EXPECT_NEAR(m.bars[1].magnitude, 0.5f, kEps);
    EXPECT_NEAR(m.bars[2].magnitude, 1.0f, kEps);
}

// 無效值域一律 Invalid 且不套用（保留舊值域）。
TEST(Histogram, InvalidRangeRejected) {
    HistogramElement h;
    ASSERT_EQ(h.set_range(0.0, 10.0), HistogramStatus::Ok);
    EXPECT_EQ(h.set_range(5.0, 5.0), HistogramStatus::Invalid);   // min==max
    EXPECT_EQ(h.set_range(10.0, 0.0), HistogramStatus::Invalid);  // min>max
    EXPECT_EQ(h.set_range(std::nan(""), 10.0), HistogramStatus::Invalid);
    EXPECT_EQ(h.set_range(0.0, std::numeric_limits<double>::infinity()), HistogramStatus::Invalid);
    // 舊值域仍為 [0,10]。
    EXPECT_EQ(h.range().min, 0.0);
    EXPECT_EQ(h.range().max, 10.0);
}

// --- 超範圍夾限（明確、不靜默）----------------------------------------------

TEST(Histogram, OutOfRangeSamplesClampedAndFlagged) {
    HistogramElement h;
    ASSERT_EQ(h.set_range(0.0, 100.0), HistogramStatus::Ok);
    ASSERT_EQ(h.set_series({-30.0, 150.0, 50.0}), HistogramStatus::Ok);
    const HistogramRenderModel m = h.render_model();
    ASSERT_EQ(m.bars.size(), 3u);
    // 低於 min → 夾至 0 並標記。
    EXPECT_NEAR(m.bars[0].magnitude, 0.0f, kEps);
    EXPECT_TRUE(m.bars[0].clamped);
    EXPECT_DOUBLE_EQ(m.bars[0].value, -30.0);  // 原始值仍保留（不靜默改寫）。
    // 高於 max → 夾至 1 並標記。
    EXPECT_NEAR(m.bars[1].magnitude, 1.0f, kEps);
    EXPECT_TRUE(m.bars[1].clamped);
    // 範圍內 → 不標記。
    EXPECT_NEAR(m.bars[2].magnitude, 0.5f, kEps);
    EXPECT_FALSE(m.bars[2].clamped);
}

// --- 滾動視窗保留最新 N -------------------------------------------------------

// push 超過容量 → 丟棄最舊者只留最新 N。
TEST(Histogram, ScrollingWindowKeepsLatestNOnPush) {
    HistogramElement h;
    ASSERT_EQ(h.set_range(0.0, 10.0), HistogramStatus::Ok);
    ASSERT_EQ(h.set_capacity(3), HistogramStatus::Ok);
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) ASSERT_EQ(h.push_sample(v), HistogramStatus::Ok);
    EXPECT_EQ(h.sample_count(), 3u);
    const HistogramRenderModel m = h.render_model();
    ASSERT_EQ(m.bars.size(), 3u);
    EXPECT_DOUBLE_EQ(m.bars[0].value, 3.0);  // 最舊者 1,2 已被丟棄。
    EXPECT_DOUBLE_EQ(m.bars[1].value, 4.0);
    EXPECT_DOUBLE_EQ(m.bars[2].value, 5.0);
}

// set_series 超過容量 → 只保留最新 N。
TEST(Histogram, ScrollingWindowKeepsLatestNOnSetSeries) {
    HistogramElement h;
    ASSERT_EQ(h.set_capacity(2), HistogramStatus::Ok);
    ASSERT_EQ(h.set_series({1.0, 2.0, 3.0, 4.0}), HistogramStatus::Ok);
    EXPECT_EQ(h.sample_count(), 2u);
    EXPECT_DOUBLE_EQ(h.samples()[0], 3.0);
    EXPECT_DOUBLE_EQ(h.samples()[1], 4.0);
}

// 事後縮小容量 → 立即修剪既有樣本。
TEST(Histogram, ReducingCapacityTrimsExisting) {
    HistogramElement h;
    ASSERT_EQ(h.set_series({1.0, 2.0, 3.0, 4.0, 5.0}), HistogramStatus::Ok);
    EXPECT_EQ(h.sample_count(), 5u);
    ASSERT_EQ(h.set_capacity(2), HistogramStatus::Ok);
    EXPECT_EQ(h.sample_count(), 2u);
    EXPECT_DOUBLE_EQ(h.samples()[0], 4.0);
    EXPECT_DOUBLE_EQ(h.samples()[1], 5.0);
}

// 預設無上限（capacity 0）：不丟棄。
TEST(Histogram, UnboundedByDefault) {
    HistogramElement h;
    EXPECT_EQ(h.capacity(), kHistogramUnbounded);
    for (int i = 0; i < 50; ++i) ASSERT_EQ(h.push_sample(static_cast<double>(i)), HistogramStatus::Ok);
    EXPECT_EQ(h.sample_count(), 50u);
}

// --- 閾值線 -------------------------------------------------------------------

TEST(Histogram, ThresholdLineNormalizedAndOptional) {
    HistogramElement h;
    ASSERT_EQ(h.set_range(0.0, 100.0), HistogramStatus::Ok);
    // 預設無閾值。
    EXPECT_FALSE(h.has_threshold());
    EXPECT_FALSE(h.render_model().threshold.present);

    ASSERT_EQ(h.set_threshold(80.0), HistogramStatus::Ok);
    EXPECT_TRUE(h.has_threshold());
    HistogramRenderModel m = h.render_model();
    ASSERT_TRUE(m.threshold.present);
    EXPECT_DOUBLE_EQ(m.threshold.value, 80.0);
    EXPECT_NEAR(m.threshold.magnitude, 0.8f, kEps);
    EXPECT_FALSE(m.threshold.clamped);

    // 超範圍閾值 → 夾限並標記。
    ASSERT_EQ(h.set_threshold(200.0), HistogramStatus::Ok);
    m = h.render_model();
    EXPECT_NEAR(m.threshold.magnitude, 1.0f, kEps);
    EXPECT_TRUE(m.threshold.clamped);

    // 清除閾值。
    h.clear_threshold();
    EXPECT_FALSE(h.has_threshold());
    EXPECT_FALSE(h.render_model().threshold.present);
}

TEST(Histogram, ThresholdRejectsNonFinite) {
    HistogramElement h;
    EXPECT_EQ(h.set_threshold(std::nan("")), HistogramStatus::Invalid);
    EXPECT_FALSE(h.has_threshold());
}

// --- 空序列（明確、不靜默）---------------------------------------------------

TEST(Histogram, EmptySeriesReportedExplicitly) {
    HistogramElement h;
    const HistogramRenderModel m = h.render_model();
    EXPECT_TRUE(m.empty);
    EXPECT_TRUE(m.bars.empty());
    EXPECT_EQ(h.sample_count(), 0u);

    // clear 後回到空。
    HistogramElement h2;
    ASSERT_EQ(h2.set_series({1.0, 2.0}), HistogramStatus::Ok);
    h2.clear();
    EXPECT_TRUE(h2.render_model().empty);
}

// --- 非有限輸入不靜默 --------------------------------------------------------

TEST(Histogram, NonFiniteSampleRejected) {
    HistogramElement h;
    EXPECT_EQ(h.push_sample(std::nan("")), HistogramStatus::Invalid);
    EXPECT_EQ(h.push_sample(std::numeric_limits<double>::infinity()), HistogramStatus::Invalid);
    EXPECT_EQ(h.sample_count(), 0u);  // 均未追加。
}

// set_series 內含非有限值 → 整批拒絕、不部分寫入。
TEST(Histogram, SetSeriesRejectsAllOrNothing) {
    HistogramElement h;
    ASSERT_EQ(h.set_series({1.0, 2.0}), HistogramStatus::Ok);
    EXPECT_EQ(h.set_series({3.0, std::nan(""), 4.0}), HistogramStatus::Invalid);
    // 舊序列保留不變（未部分套用）。
    ASSERT_EQ(h.sample_count(), 2u);
    EXPECT_DOUBLE_EQ(h.samples()[0], 1.0);
    EXPECT_DOUBLE_EQ(h.samples()[1], 2.0);
}

// --- 合成描述（E1-03 AlphaProfile）------------------------------------------

TEST(Histogram, CompositeDefaultsAndClamps) {
    HistogramElement h;
    // 預設：PerPixel、opacity=1.0。
    EXPECT_EQ(h.composite().mode, ds::kernel::AlphaMode::PerPixel);
    EXPECT_NEAR(h.composite().opacity, 1.0f, kEps);

    ds::kernel::AlphaProfile p;
    p.mode = ds::kernel::AlphaMode::Opaque;
    p.opacity = 1.5f;  // 越界 → 夾至 1。
    ASSERT_EQ(h.set_composite(p), HistogramStatus::Ok);
    EXPECT_EQ(h.composite().mode, ds::kernel::AlphaMode::Opaque);
    EXPECT_NEAR(h.composite().opacity, 1.0f, kEps);

    p.opacity = -0.2f;  // 越界 → 夾至 0。
    ASSERT_EQ(h.set_composite(p), HistogramStatus::Ok);
    EXPECT_NEAR(h.composite().opacity, 0.0f, kEps);

    // render_model 帶出合成描述。
    EXPECT_EQ(h.render_model().composite.mode, ds::kernel::AlphaMode::Opaque);

    // 非有限 opacity → Invalid。
    p.opacity = std::nan("");
    EXPECT_EQ(h.set_composite(p), HistogramStatus::Invalid);
}

// --- 宣告式設定驅動：configure(Value) ---------------------------------------

TEST(HistogramConfig, ConfigureFromValueMap) {
    // 直接以解析出的文件 root（Map）設定。
    const std::string text =
        "format_version: 1.0\n"
        "range:\n"
        "  min: 0\n"
        "  max: 100\n"
        "capacity: 3\n"
        "threshold: 90\n"
        "series:\n"
        "  - 10\n"
        "  - 20\n"
        "  - 30\n"
        "  - 40\n";
    ds::format::ParseResult pr = ds::format::parse(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    HistogramElement h;
    HistogramConfigError err;
    ASSERT_TRUE(configure(pr.document().root, h, err)) << err.message;

    EXPECT_EQ(h.range().min, 0.0);
    EXPECT_EQ(h.range().max, 100.0);
    EXPECT_EQ(h.capacity(), 3u);
    ASSERT_TRUE(h.has_threshold());
    // capacity=3 → 4 筆序列只留最新 3 筆（20,30,40）。
    ASSERT_EQ(h.sample_count(), 3u);
    const HistogramRenderModel m = h.render_model();
    ASSERT_TRUE(m.threshold.present);
    EXPECT_NEAR(m.threshold.magnitude, 0.9f, kEps);
    EXPECT_DOUBLE_EQ(m.bars.front().value, 20.0);
    EXPECT_DOUBLE_EQ(m.bars.back().value, 40.0);
}

// 錯誤設定不靜默：非數字 series 元素。
TEST(HistogramConfig, MalformedConfigReported) {
    const std::string text =
        "format_version: 1.0\n"
        "series:\n"
        "  - 10\n"
        "  - hello\n";  // 裸字串，非數字。
    ds::format::ParseResult pr = ds::format::parse(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    HistogramElement h;
    HistogramConfigError err;
    EXPECT_FALSE(configure(pr.document().root, h, err));
    EXPECT_FALSE(err.message.empty());
}

// 無效值域經 config 亦回報。
TEST(HistogramConfig, InvalidRangeInConfigReported) {
    const std::string text =
        "format_version: 1.0\n"
        "range:\n"
        "  min: 50\n"
        "  max: 50\n";  // min==max 無效。
    ds::format::ParseResult pr = ds::format::parse(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    HistogramElement h;
    HistogramConfigError err;
    EXPECT_FALSE(configure(pr.document().root, h, err));
    EXPECT_FALSE(err.message.empty());
}

// --- E7-03 段落變數驅動：configure_from_document ----------------------------

TEST(HistogramConfig, ConfigureFromDocumentExpandsSectionVars) {
    // vars: 段落宣告變數，其餘欄位以 ${...} 引用——由 E7-03 expand() 展開後才設定。
    const std::string text =
        "format_version: 1.0\n"
        "vars:\n"
        "  hi: 100\n"
        "  warn: 80\n"
        "range:\n"
        "  min: 0\n"
        "  max: ${hi}\n"
        "threshold: ${warn}\n"
        "series:\n"
        "  - 50\n"
        "  - 100\n";
    ds::format::ParseResult pr = ds::format::parse(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    HistogramElement h;
    HistogramConfigError err;
    ASSERT_TRUE(configure_from_document(pr.document(), h, err)) << err.message;

    // ${hi} → 100（保留原生數字型別），${warn} → 80。
    EXPECT_EQ(h.range().max, 100.0);
    ASSERT_TRUE(h.has_threshold());
    const HistogramRenderModel m = h.render_model();
    EXPECT_NEAR(m.threshold.magnitude, 0.8f, kEps);
    ASSERT_EQ(m.bars.size(), 2u);
    EXPECT_NEAR(m.bars[0].magnitude, 0.5f, kEps);
    EXPECT_NEAR(m.bars[1].magnitude, 1.0f, kEps);
}

// 未定義變數 → 展開失敗、明確回報（不靜默）。
TEST(HistogramConfig, ConfigureFromDocumentReportsUndefinedVar) {
    const std::string text =
        "format_version: 1.0\n"
        "threshold: ${missing}\n";
    ds::format::ParseResult pr = ds::format::parse(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    HistogramElement h;
    HistogramConfigError err;
    EXPECT_FALSE(configure_from_document(pr.document(), h, err));
    EXPECT_FALSE(err.message.empty());
}

}  // namespace
