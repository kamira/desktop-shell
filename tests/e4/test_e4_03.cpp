// E4-03 長條 / 進度 / 量表 — 單元測試（gtest）
//
// 涵蓋：
//   - Bar 值→長度比例、範圍映射、值夾限、render_model 內容。
//   - Progress 0–100%、百分比夾限、"%%" 標籤、render_model 內容。
//   - Gauge 值→角度、弧幾何、範圍映射、render_model 內容。
//   - 無效輸入報錯（非有限值、退化範圍、退化弧）不靜默。
//   - E7-03 宣告式變數段落驅動設定（含 ResolveError / NotAMap / InvalidField）。
//   - NFR-02：渲染描述以具名 surface + 具名 slot + 比例 / 角度表達，無絕對座標 / 數字 z-order。
#include "bar_gauge.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

using ds::elements::BarElement;
using ds::elements::ConfigStatus;
using ds::elements::ElementKind;
using ds::elements::GaugeElement;
using ds::elements::Orientation;
using ds::elements::ProgressElement;
using ds::elements::RenderModel;
using ds::format::Value;

namespace {

// ── Bar：值→比例、範圍映射、夾限 ──

TEST(BarElement, ValueMapsToRatioWithinRange) {
    BarElement bar(0.0, 200.0);
    bar.set_value(50.0);
    EXPECT_NEAR(bar.ratio(), 0.25, 1e-9);
    EXPECT_NEAR(bar.value(), 50.0, 1e-9);
}

TEST(BarElement, NonZeroMinRangeMapping) {
    BarElement bar(100.0, 300.0);
    bar.set_value(200.0);
    EXPECT_NEAR(bar.ratio(), 0.5, 1e-9);  // (200-100)/(300-100)
}

TEST(BarElement, ClampsValueAboveAndBelowRange) {
    BarElement bar(0.0, 10.0);
    bar.set_value(25.0);
    EXPECT_NEAR(bar.value(), 10.0, 1e-9);
    EXPECT_NEAR(bar.ratio(), 1.0, 1e-9);
    bar.set_value(-5.0);
    EXPECT_NEAR(bar.value(), 0.0, 1e-9);
    EXPECT_NEAR(bar.ratio(), 0.0, 1e-9);
    // 原樣值仍保留（未夾限），僅取用時夾限。
    EXPECT_NEAR(bar.raw_value(), -5.0, 1e-9);
}

TEST(BarElement, ChangingRangeReclampsValue) {
    BarElement bar(0.0, 100.0);
    bar.set_value(80.0);
    EXPECT_NEAR(bar.value(), 80.0, 1e-9);
    bar.set_range(0.0, 50.0);
    EXPECT_NEAR(bar.value(), 50.0, 1e-9);  // 80 夾至新上限 50
    EXPECT_NEAR(bar.ratio(), 1.0, 1e-9);
}

TEST(BarElement, RenderModelContent) {
    BarElement bar(0.0, 4.0);
    bar.set_value(1.0);
    bar.set_surface("desk.widget");
    bar.set_slot("content");
    bar.set_orientation(Orientation::Vertical);
    bar.set_fill_color("cpu");
    bar.set_track_color("bg");
    const RenderModel m = bar.render_model();
    EXPECT_TRUE(m.kind == ElementKind::Bar);
    EXPECT_FALSE(m.has_angle);
    EXPECT_NEAR(m.fill_ratio, 0.25, 1e-9);
    EXPECT_EQ(m.surface, std::string("desk.widget"));
    EXPECT_EQ(m.slot, std::string("content"));
    EXPECT_TRUE(m.orientation == Orientation::Vertical);
    EXPECT_EQ(m.fill_color, std::string("cpu"));
    EXPECT_EQ(m.track_color, std::string("bg"));
    EXPECT_EQ(m.label, std::string("1"));  // 整數值不帶小數
}

TEST(BarElement, LabelOverrideAndHide) {
    BarElement bar(0.0, 10.0);
    bar.set_value(3.0);
    bar.set_label("CPU");
    EXPECT_EQ(bar.render_model().label, std::string("CPU"));
    bar.set_show_label(false);
    EXPECT_EQ(bar.render_model().label, std::string(""));
    EXPECT_FALSE(bar.render_model().show_label);
}

// ── Progress：0–100% ──

TEST(ProgressElement, PercentMapsToRatio) {
    ProgressElement p;
    p.set_percent(63.0);
    EXPECT_NEAR(p.percent(), 63.0, 1e-9);
    EXPECT_NEAR(p.render_model().fill_ratio, 0.63, 1e-9);
}

TEST(ProgressElement, ClampsToZeroHundred) {
    ProgressElement p;
    p.set_percent(140.0);
    EXPECT_NEAR(p.percent(), 100.0, 1e-9);
    EXPECT_NEAR(p.render_model().fill_ratio, 1.0, 1e-9);
    p.set_percent(-20.0);
    EXPECT_NEAR(p.percent(), 0.0, 1e-9);
    EXPECT_NEAR(p.render_model().fill_ratio, 0.0, 1e-9);
}

TEST(ProgressElement, RenderModelPercentLabel) {
    ProgressElement p;
    p.set_percent(42.0);
    const RenderModel m = p.render_model();
    EXPECT_TRUE(m.kind == ElementKind::Progress);
    EXPECT_FALSE(m.has_angle);
    EXPECT_EQ(m.label, std::string("42%"));
}

TEST(ProgressElement, LabelOverrideSuppressesPercentFormat) {
    ProgressElement p;
    p.set_percent(42.0);
    p.set_label("loading");
    EXPECT_EQ(p.render_model().label, std::string("loading"));
}

// ── Gauge：值→角度 ──

TEST(GaugeElement, ValueMapsToAngle) {
    GaugeElement g;  // 範圍 [0,1]，起始 135°，跨距 270°
    g.set_value(0.5);
    EXPECT_NEAR(g.ratio(), 0.5, 1e-9);
    EXPECT_NEAR(g.angle(), 135.0 + 0.5 * 270.0, 1e-9);  // 270°
}

TEST(GaugeElement, CustomRangeAndArc) {
    GaugeElement g(0.0, 100.0);
    g.set_arc(-90.0, 180.0);
    g.set_value(25.0);
    EXPECT_NEAR(g.ratio(), 0.25, 1e-9);
    EXPECT_NEAR(g.angle(), -90.0 + 0.25 * 180.0, 1e-9);  // -45°
}

TEST(GaugeElement, ClampsValueForAngle) {
    GaugeElement g(0.0, 10.0);
    g.set_arc(0.0, 360.0);
    g.set_value(50.0);  // 夾至 10 → ratio 1
    EXPECT_NEAR(g.angle(), 360.0, 1e-9);
}

TEST(GaugeElement, RenderModelContent) {
    GaugeElement g(0.0, 200.0);
    g.set_arc(135.0, 270.0);
    g.set_value(100.0);
    g.set_surface("desk.gauge");
    g.set_slot("dial");
    const RenderModel m = g.render_model();
    EXPECT_TRUE(m.kind == ElementKind::Gauge);
    EXPECT_TRUE(m.has_angle);
    EXPECT_NEAR(m.fill_ratio, 0.5, 1e-9);
    EXPECT_NEAR(m.start_angle_degrees, 135.0, 1e-9);
    EXPECT_NEAR(m.sweep_degrees, 270.0, 1e-9);
    EXPECT_NEAR(m.angle_degrees, 270.0, 1e-9);
    EXPECT_EQ(m.surface, std::string("desk.gauge"));
    EXPECT_EQ(m.slot, std::string("dial"));
}

// ── 無效輸入報錯（不靜默）──

TEST(Invalid, NonFiniteValueThrows) {
    BarElement bar(0.0, 1.0);
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(bar.set_value(nan), std::invalid_argument);
    EXPECT_THROW(bar.set_value(inf), std::invalid_argument);
}

TEST(Invalid, DegenerateRangeThrows) {
    BarElement bar;
    EXPECT_THROW(bar.set_range(5.0, 5.0), std::invalid_argument);   // min == max
    EXPECT_THROW(bar.set_range(10.0, 1.0), std::invalid_argument);  // min > max
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(bar.set_range(0.0, inf), std::invalid_argument);   // 非有限
    // 建構子同樣驗證。
    EXPECT_THROW(BarElement(1.0, 1.0), std::invalid_argument);
}

TEST(Invalid, DegenerateArcThrows) {
    GaugeElement g;
    EXPECT_THROW(g.set_arc(0.0, 0.0), std::invalid_argument);  // sweep 0
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(g.set_arc(inf, 90.0), std::invalid_argument);  // 非有限
}

// ── E7-03 宣告式變數段落驅動設定 ──

// 以 vars 段落宣告 lo/hi，其餘欄位以 ${..} 引用；expand 後填入元件。
TEST(Config, BarDrivenBySectionVars) {
    // vars: { lo: 0, hi: 200 }
    // min: ${lo}  max: ${hi}  value: 50  orientation: vertical  surface: ${sfc}  slot: content
    Value cfg = Value::map({
        {"vars", Value::map({
                     {"lo", Value::integer(0)},
                     {"hi", Value::integer(200)},
                     {"sfc", Value::string("desk.cpu")},
                 })},
        {"min", Value::string("${lo}")},
        {"max", Value::string("${hi}")},
        {"value", Value::integer(50)},
        {"orientation", Value::string("vertical")},
        {"surface", Value::string("${sfc}")},
        {"slot", Value::string("content")},
        {"fill_color", Value::string("accent")},
    });

    BarElement bar;
    std::string err;
    const ConfigStatus st = ds::elements::load_bar_config(cfg, bar, err);
    ASSERT_TRUE(st == ConfigStatus::Ok);
    EXPECT_NEAR(bar.range().min, 0.0, 1e-9);
    EXPECT_NEAR(bar.range().max, 200.0, 1e-9);
    EXPECT_NEAR(bar.value(), 50.0, 1e-9);
    EXPECT_NEAR(bar.ratio(), 0.25, 1e-9);
    EXPECT_TRUE(bar.orientation() == Orientation::Vertical);
    EXPECT_EQ(bar.surface(), std::string("desk.cpu"));
    EXPECT_EQ(bar.slot(), std::string("content"));
}

TEST(Config, ProgressDrivenBySectionVars) {
    // vars: { p: 75 } ; percent: ${p}
    Value cfg = Value::map({
        {"vars", Value::map({{"p", Value::integer(75)}})},
        {"percent", Value::string("${p}")},
        {"surface", Value::string("desk.bar")},
    });
    ProgressElement prog;
    std::string err;
    const ConfigStatus st = ds::elements::load_progress_config(cfg, prog, err);
    ASSERT_TRUE(st == ConfigStatus::Ok);
    EXPECT_NEAR(prog.percent(), 75.0, 1e-9);
    EXPECT_EQ(prog.surface(), std::string("desk.bar"));
}

TEST(Config, GaugeDrivenBySectionVars) {
    Value cfg = Value::map({
        {"vars", Value::map({{"span", Value::integer(180)}})},
        {"min", Value::integer(0)},
        {"max", Value::integer(100)},
        {"value", Value::integer(75)},
        {"start_angle", Value::integer(-90)},
        {"sweep", Value::string("${span}")},
    });
    GaugeElement g;
    std::string err;
    const ConfigStatus st = ds::elements::load_gauge_config(cfg, g, err);
    ASSERT_TRUE(st == ConfigStatus::Ok);
    EXPECT_NEAR(g.range().max, 100.0, 1e-9);
    EXPECT_NEAR(g.start_angle(), -90.0, 1e-9);
    EXPECT_NEAR(g.sweep(), 180.0, 1e-9);
    EXPECT_NEAR(g.angle(), -90.0 + 0.75 * 180.0, 1e-9);  // 45°
}

TEST(Config, UndefinedVariableReportsResolveError) {
    Value cfg = Value::map({
        {"min", Value::string("${missing}")},
        {"max", Value::integer(10)},
    });
    BarElement bar;
    std::string err;
    const ConfigStatus st = ds::elements::load_bar_config(cfg, bar, err);
    EXPECT_TRUE(st == ConfigStatus::ResolveError);
    EXPECT_FALSE(err.empty());  // 不靜默：帶訊息
}

TEST(Config, NonMapRootReportsNotAMap) {
    Value cfg = Value::integer(5);
    BarElement bar;
    std::string err;
    const ConfigStatus st = ds::elements::load_bar_config(cfg, bar, err);
    EXPECT_TRUE(st == ConfigStatus::NotAMap);
    EXPECT_FALSE(err.empty());
}

TEST(Config, BadFieldTypeReportsInvalidField) {
    // orientation 非具名值 → InvalidField
    Value cfg = Value::map({
        {"orientation", Value::string("diagonal")},
    });
    BarElement bar;
    std::string err;
    const ConfigStatus st = ds::elements::load_bar_config(cfg, bar, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
    EXPECT_FALSE(err.empty());
}

TEST(Config, WrongTypeFieldReportsInvalidField) {
    // min 為字串（非引用、非數字）→ InvalidField
    Value cfg = Value::map({
        {"min", Value::string("lots")},
        {"max", Value::integer(10)},
    });
    BarElement bar;
    std::string err;
    const ConfigStatus st = ds::elements::load_bar_config(cfg, bar, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
}

TEST(Config, DegenerateRangeInConfigReportsInvalidField) {
    Value cfg = Value::map({
        {"min", Value::integer(10)},
        {"max", Value::integer(10)},  // min == max
    });
    BarElement bar;
    std::string err;
    const ConfigStatus st = ds::elements::load_bar_config(cfg, bar, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
    EXPECT_FALSE(err.empty());
}

// ── NFR-02：無絕對座標 / 數字 z-order；位置以具名表達、尺寸以比例 / 角度 ──

TEST(NFR02, PositionIsNamedNotNumeric) {
    // 佈局僅以具名 surface + 具名 slot 表達；填充僅以比例 [0,1] / 角度表達。
    // RenderModel 結構本身不含任何 x/y/寬/高像素或整數層級欄位（見標頭定義）。
    BarElement bar(0.0, 1.0);
    bar.set_value(0.5);
    bar.set_surface("named.surface");
    bar.set_slot("named.slot");
    const RenderModel m = bar.render_model();
    EXPECT_EQ(m.surface, std::string("named.surface"));
    EXPECT_EQ(m.slot, std::string("named.slot"));
    EXPECT_GE(m.fill_ratio, 0.0);
    EXPECT_LE(m.fill_ratio, 1.0);

    GaugeElement g;
    g.set_value(1.0);
    const RenderModel gm = g.render_model();
    // 角度落在宣告弧 [start, start+sweep] 內——語意輸出，非版面座標。
    EXPECT_GE(gm.fill_ratio, 0.0);
    EXPECT_LE(gm.fill_ratio, 1.0);
    EXPECT_NEAR(gm.angle_degrees, gm.start_angle_degrees + gm.sweep_degrees, 1e-9);
}

}  // namespace
