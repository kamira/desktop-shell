// tests/c2/test_c2_01.cpp — C2-01 時鐘 widget（gtest）
//
// 涵蓋：configure（E7-01 宣告式定義：程式化 Value 與自文字 parse 兩路徑、format/seconds/align/
// width 欄位套用、空 map 用預設、基底未載入 → Unsupported）、tick（注入式 E2-10 TimeSource 更新
// 時間 / 無時間來源 → Unsupported / 未 configure → NotConfigured）、display_text 格式（24h / 12h
// 含 / 不含秒、午夜與正午邊界）、E4-01 排版（layout_result 綁定基底 surface id、對齊 / 寬度套用、
// 空字串排版）、refresh（純重排版、不重新取樣時間 / 未 configure → NotConfigured）、以及各類
// 無效設定（非 map / 型別錯 / 具名值無效 / 空 id）。
#include "clock_widget.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

using ds::format::Document;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::Value;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::alpha_capable_matrix;
using ds::profiles::SkinProfile;
using ds::profiles::SkinStatus;
using ds::render::FixedFontMetrics;
using ds::render::TextAlign;
using ds::sysinfo::FixedTimeSource;
using ds::sysinfo::TimeSource;
using ds::widgets::ClockHourFormat;
using ds::widgets::ClockState;
using ds::widgets::ClockStatus;
using ds::widgets::ClockWidget;

namespace {

// 令一個 C1-01 掛載基底成功載入（Loaded）。SkinProfile 不可複製 / 不可搬移
// （見 skin_profile.hpp），故呼叫端須自行就地建構，本輔助函式僅負責 load_skin 並在失敗時
// 以例外快速失敗（測試前置條件錯誤，非本單元邏輯錯誤，避免靜默誤判）。
void require_loaded(SkinProfile& base) {
    const SkinStatus st = base.load_skin(Value::map({}));
    if (st != SkinStatus::Ok) {
        throw std::runtime_error("require_loaded: load_skin 未成功");
    }
}

// 一個等寬 10.0 advance、行高 20.0 的固定字型度量，便於手算排版結果。
FixedFontMetrics wide_metrics() { return FixedFontMetrics{10.0, 20.0}; }

}  // namespace

// -----------------------------------------------------------------------------
// 建構預設
// -----------------------------------------------------------------------------

TEST(ClockWidget, ConstructedUnconfiguredWithDefaults) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();

    ClockWidget clock{"clock.desk", base, source, metrics};

    EXPECT_EQ(clock.state(), ClockState::Unconfigured);
    EXPECT_FALSE(clock.is_configured());
    EXPECT_EQ(clock.id(), "clock.desk");
    EXPECT_EQ(clock.hour_format(), ClockHourFormat::H24);
    EXPECT_TRUE(clock.show_seconds());
    EXPECT_EQ(clock.align(), TextAlign::Left);
    EXPECT_DOUBLE_EQ(clock.max_width(), 0.0);
    EXPECT_FALSE(clock.has_sampled());
    EXPECT_EQ(clock.display_text(), std::string(""));
}

// -----------------------------------------------------------------------------
// configure — E7-01 宣告式定義（程式化 Value）
// -----------------------------------------------------------------------------

TEST(ClockWidget, ConfigureEmptyMapUsesDefaultsAndSamplesOnce) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);  // 1970-01-01T00:00:00Z
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    EXPECT_EQ(clock.configure(Value::map({})), ClockStatus::Ok);
    EXPECT_EQ(clock.state(), ClockState::Configured);
    EXPECT_TRUE(clock.is_configured());
    EXPECT_TRUE(clock.has_sampled());  // configure 內建初次取樣
    EXPECT_EQ(clock.display_text(), std::string("00:00:00"));
}

TEST(ClockWidget, ConfigureAppliesFormatSecondsAlignWidth) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({
        {"format", Value::string("12h")},
        {"seconds", Value::boolean(false)},
        {"align", Value::string("center")},
        {"width", Value::number(200.0)},
    });
    EXPECT_EQ(clock.configure(def), ClockStatus::Ok);

    EXPECT_EQ(clock.hour_format(), ClockHourFormat::H12);
    EXPECT_FALSE(clock.show_seconds());
    EXPECT_EQ(clock.align(), TextAlign::Center);
    EXPECT_DOUBLE_EQ(clock.max_width(), 200.0);
    EXPECT_EQ(clock.display_text(), std::string("12:00 AM"));  // 午夜 → 12 AM，無秒
}

TEST(ClockWidget, ConfigureFromParsedTextDocument) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(3661);  // 1970-01-01T01:01:01Z
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    const std::string text =
        "format_version: 1.0\n"
        "format: 24h\n"
        "seconds: true\n"
        "align: right\n"
        "width: 150\n";
    ParseResult r = parse(text);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(clock.configure(r.document().root), ClockStatus::Ok);

    EXPECT_EQ(clock.align(), TextAlign::Right);
    EXPECT_DOUBLE_EQ(clock.max_width(), 150.0);
    EXPECT_EQ(clock.display_text(), std::string("01:01:01"));
}

TEST(ClockWidget, ConfigureWithoutTimeSourceLeavesDisplayEmptyButConfigured) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, /*time_source=*/nullptr, metrics};

    EXPECT_EQ(clock.configure(Value::map({})), ClockStatus::Ok);
    EXPECT_TRUE(clock.is_configured());
    EXPECT_FALSE(clock.has_sampled());
    EXPECT_EQ(clock.display_text(), std::string(""));
    EXPECT_TRUE(clock.layout_result().lines.empty());
}

TEST(ClockWidget, ConfigureUnsupportedWhenBaseNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.unloaded", backend, layers};  // 未 load_skin
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    EXPECT_EQ(clock.configure(Value::map({})), ClockStatus::Unsupported);
    EXPECT_EQ(clock.state(), ClockState::Unconfigured);  // 未提交任何設定
    EXPECT_FALSE(clock.has_sampled());
}

// -----------------------------------------------------------------------------
// tick — E2-10 注入式時間來源更新
// -----------------------------------------------------------------------------

TEST(ClockWidget, TickBeforeConfigureReturnsNotConfigured) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    EXPECT_EQ(clock.tick(), ClockStatus::NotConfigured);
    EXPECT_FALSE(clock.has_sampled());
}

TEST(ClockWidget, TickResamplesInjectedTimeSource) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({})), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("00:00:00"));

    // 「時間前進」：呼叫端推進注入的固定時間來源（模擬時鐘走動，非真實 wall-clock）。
    source->set_epoch_seconds(3725);  // 1970-01-01T01:02:05Z
    EXPECT_EQ(clock.tick(), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("01:02:05"));
}

TEST(ClockWidget, TickWithoutTimeSourceReturnsUnsupportedAndKeepsPriorDisplay) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, /*time_source=*/nullptr, metrics};

    ASSERT_EQ(clock.configure(Value::map({})), ClockStatus::Ok);
    EXPECT_EQ(clock.tick(), ClockStatus::Unsupported);
    EXPECT_FALSE(clock.has_sampled());
    EXPECT_EQ(clock.display_text(), std::string(""));  // 不變
}

// -----------------------------------------------------------------------------
// display_text — 12h / 24h 格式正確性（含邊界）
// -----------------------------------------------------------------------------

TEST(ClockWidget, DisplayText24HourWithSeconds) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(14 * 3600 + 23 * 60 + 5);  // 14:23:05
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({{"format", Value::string("24h")}})), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("14:23:05"));
}

TEST(ClockWidget, DisplayText24HourWithoutSeconds) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(14 * 3600 + 23 * 60 + 5);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"format", Value::string("24h")}, {"seconds", Value::boolean(false)}});
    ASSERT_EQ(clock.configure(def), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("14:23"));
}

TEST(ClockWidget, DisplayText12HourAfternoonWithSeconds) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(14 * 3600 + 23 * 60 + 5);  // 14:23:05 → 02:23:05 PM
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({{"format", Value::string("12h")}})), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("02:23:05 PM"));
}

TEST(ClockWidget, DisplayText12HourMidnightAndNoonBoundaries) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"format", Value::string("12h")}, {"seconds", Value::boolean(false)}});
    ASSERT_EQ(clock.configure(def), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("12:00 AM"));  // 午夜 0 時 → 12 AM

    source->set_epoch_seconds(12 * 3600);  // 正午
    ASSERT_EQ(clock.tick(), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("12:00 PM"));  // 正午 12 時 → 12 PM

    source->set_epoch_seconds(23 * 3600 + 59 * 60);  // 23:59
    ASSERT_EQ(clock.tick(), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("11:59 PM"));
}

// -----------------------------------------------------------------------------
// E4-01 排版
// -----------------------------------------------------------------------------

TEST(ClockWidget, LayoutResultBoundToBaseSurfaceId) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.for-clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({{"seconds", Value::boolean(false)}})), ClockStatus::Ok);
    // display_text "00:00" = 5 字元 * advance 10.0 = 寬度 50，行高 20（見 FixedFontMetrics）。
    ASSERT_EQ(clock.layout_result().lines.size(), 1u);
    EXPECT_EQ(clock.layout_result().glyphs.size(), 5u);
    EXPECT_DOUBLE_EQ(clock.layout_result().size.width, 50.0);
    EXPECT_DOUBLE_EQ(clock.layout_result().size.height, 20.0);
    EXPECT_EQ(clock.layout_result().surface, base.id());
    EXPECT_FALSE(clock.layout_result().truncated);
}

TEST(ClockWidget, LayoutResultAppliesAlignmentWithinWidth) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    // "00:00" 寬 50；盒寬 100、置中 → 行起點偏移 (100-50)/2 = 25。
    Value def = Value::map({
        {"seconds", Value::boolean(false)},
        {"align", Value::string("center")},
        {"width", Value::number(100.0)},
    });
    ASSERT_EQ(clock.configure(def), ClockStatus::Ok);
    ASSERT_EQ(clock.layout_result().lines.size(), 1u);
    EXPECT_DOUBLE_EQ(clock.layout_result().lines[0].x, 25.0);
    EXPECT_DOUBLE_EQ(clock.layout_result().size.width, 100.0);  // 有界寬度 = 盒寬
}

TEST(ClockWidget, LayoutResultEmptyWhenNoTimeSampled) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, /*time_source=*/nullptr, metrics};

    ASSERT_EQ(clock.configure(Value::map({})), ClockStatus::Ok);
    EXPECT_TRUE(clock.layout_result().lines.empty());
    EXPECT_TRUE(clock.layout_result().glyphs.empty());
    EXPECT_DOUBLE_EQ(clock.layout_result().size.width, 0.0);
    EXPECT_DOUBLE_EQ(clock.layout_result().size.height, 0.0);
}

// -----------------------------------------------------------------------------
// refresh — 純重排版，不重新取樣時間
// -----------------------------------------------------------------------------

TEST(ClockWidget, RefreshBeforeConfigureReturnsNotConfigured) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    EXPECT_EQ(clock.refresh(), ClockStatus::NotConfigured);
}

TEST(ClockWidget, RefreshRecomputesLayoutWithoutResamplingTime) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({{"seconds", Value::boolean(false)}})), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("00:00"));

    // 推進時間來源但**不** tick：refresh 不應反映新時間（僅重排版舊文字）。
    source->set_epoch_seconds(3661);
    EXPECT_EQ(clock.refresh(), ClockStatus::Ok);
    EXPECT_EQ(clock.display_text(), std::string("00:00"));  // 未變
    EXPECT_EQ(clock.layout_result().glyphs.size(), 5u);      // "00:00" 排版仍在
}

TEST(ClockWidget, RefreshAfterAlignmentEffectivelyManualReapply) {
    // refresh() 本身不重新解讀樣式（樣式變更須經 configure）；此測試驗證 refresh 對「目前樣式 +
    // 目前 display_text」的重排版是冪等且正確的。
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({{"seconds", Value::boolean(false)}})), ClockStatus::Ok);
    const auto first = clock.layout_result();
    EXPECT_EQ(clock.refresh(), ClockStatus::Ok);
    EXPECT_EQ(clock.layout_result().size.width, first.size.width);
    EXPECT_EQ(clock.layout_result().size.height, first.size.height);
    EXPECT_EQ(clock.layout_result().glyphs.size(), first.glyphs.size());
}

// -----------------------------------------------------------------------------
// 無效設定
// -----------------------------------------------------------------------------

TEST(ClockWidget, ConfigureRejectsNonMapDefinition) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    EXPECT_EQ(clock.configure(Value::string("not-a-map")), ClockStatus::Invalid);
    EXPECT_EQ(clock.state(), ClockState::Unconfigured);
}

TEST(ClockWidget, ConfigureRejectsUnknownFormatValue) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"format", Value::string("36h")}});
    EXPECT_EQ(clock.configure(def), ClockStatus::Invalid);
    EXPECT_EQ(clock.state(), ClockState::Unconfigured);
}

TEST(ClockWidget, ConfigureRejectsWrongTypeForFormat) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"format", Value::number(24)}});  // 應為字串
    EXPECT_EQ(clock.configure(def), ClockStatus::Invalid);
}

TEST(ClockWidget, ConfigureRejectsWrongTypeForSeconds) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"seconds", Value::string("yes")}});  // 應為 bool
    EXPECT_EQ(clock.configure(def), ClockStatus::Invalid);
}

TEST(ClockWidget, ConfigureRejectsUnknownAlignValue) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"align", Value::string("justify")}});
    EXPECT_EQ(clock.configure(def), ClockStatus::Invalid);
}

TEST(ClockWidget, ConfigureRejectsNegativeWidth) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"width", Value::number(-1.0)}});
    EXPECT_EQ(clock.configure(def), ClockStatus::Invalid);
}

TEST(ClockWidget, ConfigureRejectsNonFiniteWidth) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    Value def = Value::map({{"width", Value::number(std::numeric_limits<double>::quiet_NaN())}});
    EXPECT_EQ(clock.configure(def), ClockStatus::Invalid);
}

TEST(ClockWidget, ConfigureRejectsEmptyId) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{/*id=*/"", base, source, metrics};

    EXPECT_EQ(clock.configure(Value::map({})), ClockStatus::Invalid);
    EXPECT_EQ(clock.state(), ClockState::Unconfigured);
}

TEST(ClockWidget, ConfigureInvalidDoesNotMutateExistingConfiguration) {
    NullKernelBackend backend{alpha_capable_matrix()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile base{"base.clock", backend, layers};
    require_loaded(base);
    auto source = std::make_shared<FixedTimeSource>(0);
    FixedFontMetrics metrics = wide_metrics();
    ClockWidget clock{"clock.desk", base, source, metrics};

    ASSERT_EQ(clock.configure(Value::map({{"format", Value::string("12h")}})), ClockStatus::Ok);
    ASSERT_EQ(clock.hour_format(), ClockHourFormat::H12);

    // 後續一次無效 configure 不應更動既有已提交樣式。
    Value bad = Value::map({{"format", Value::string("bogus")}});
    EXPECT_EQ(clock.configure(bad), ClockStatus::Invalid);
    EXPECT_EQ(clock.hour_format(), ClockHourFormat::H12);  // 仍是先前成功套用的值
}

// -----------------------------------------------------------------------------
// 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(ClockWidget, ToStringForNamedEnums) {
    EXPECT_STREQ(ds::widgets::to_string(ClockState::Unconfigured), "Unconfigured");
    EXPECT_STREQ(ds::widgets::to_string(ClockState::Configured), "Configured");
    EXPECT_STREQ(ds::widgets::to_string(ClockStatus::Ok), "Ok");
    EXPECT_STREQ(ds::widgets::to_string(ClockStatus::Invalid), "Invalid");
    EXPECT_STREQ(ds::widgets::to_string(ClockStatus::Unsupported), "Unsupported");
    EXPECT_STREQ(ds::widgets::to_string(ClockStatus::NotConfigured), "NotConfigured");
    EXPECT_STREQ(ds::widgets::to_string(ClockHourFormat::H24), "H24");
    EXPECT_STREQ(ds::widgets::to_string(ClockHourFormat::H12), "H12");
}
