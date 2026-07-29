// E4-18 徑向線 / 指針 — 單元測試（gtest）
//
// 涵蓋：
//   - 值→角度映射、自訂弧幾何、範圍映射、值夾限。
//   - 長度比例 / 粗細比例設定與夾限。
//   - render_model 內容（含具名佈局）。
//   - 無效輸入報錯（非有限值、退化範圍、退化角度跨距、非有限長度/粗細比例）不靜默。
//   - E7-03 宣告式變數段落驅動設定（含 ResolveError / NotAMap / InvalidField）。
//   - NFR-02：渲染描述以具名 surface + 具名 slot + 比例 / 角度表達，無絕對座標 / 數字 z-order。
#include "radial_pointer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

using ds::elements::ConfigStatus;
using ds::elements::RadialPointerElement;
using ds::elements::RenderModel;
using ds::format::Value;

namespace {

// ── 值→角度映射、弧幾何、範圍映射、夾限 ──

TEST(RadialPointerElement, DefaultsAndValueMapsToAngle) {
    RadialPointerElement p;  // 範圍 [0,1]，起始 135°，跨距 270°
    p.set_value(0.5);
    EXPECT_NEAR(p.ratio(), 0.5, 1e-9);
    EXPECT_NEAR(p.angle(), 135.0 + 0.5 * 270.0, 1e-9);  // 270°
}

TEST(RadialPointerElement, CustomRangeAndAngleSpan) {
    RadialPointerElement p(0.0, 100.0);
    p.set_angle_span(-90.0, 180.0);
    p.set_value(25.0);
    EXPECT_NEAR(p.ratio(), 0.25, 1e-9);
    EXPECT_NEAR(p.angle(), -90.0 + 0.25 * 180.0, 1e-9);  // -45°
}

TEST(RadialPointerElement, NonZeroMinRangeMapping) {
    RadialPointerElement p(100.0, 300.0);
    p.set_value(200.0);
    EXPECT_NEAR(p.ratio(), 0.5, 1e-9);  // (200-100)/(300-100)
}

TEST(RadialPointerElement, ClampsValueAboveAndBelowRange) {
    RadialPointerElement p(0.0, 10.0);
    p.set_value(25.0);
    EXPECT_NEAR(p.value(), 10.0, 1e-9);
    EXPECT_NEAR(p.ratio(), 1.0, 1e-9);
    p.set_value(-5.0);
    EXPECT_NEAR(p.value(), 0.0, 1e-9);
    EXPECT_NEAR(p.ratio(), 0.0, 1e-9);
    // 原樣值仍保留（未夾限），僅取用時夾限。
    EXPECT_NEAR(p.raw_value(), -5.0, 1e-9);
}

TEST(RadialPointerElement, ChangingRangeReclampsValue) {
    RadialPointerElement p(0.0, 100.0);
    p.set_value(80.0);
    EXPECT_NEAR(p.value(), 80.0, 1e-9);
    p.set_range(0.0, 50.0);
    EXPECT_NEAR(p.value(), 50.0, 1e-9);  // 80 夾至新上限 50
    EXPECT_NEAR(p.ratio(), 1.0, 1e-9);
}

TEST(RadialPointerElement, ClampsValueForAngleAtFullSweep) {
    RadialPointerElement p(0.0, 10.0);
    p.set_angle_span(0.0, 360.0);
    p.set_value(50.0);  // 夾至 10 → ratio 1
    EXPECT_NEAR(p.angle(), 360.0, 1e-9);
}

// ── 長度 / 粗細比例 ──

TEST(RadialPointerElement, LengthAndThicknessRatioDefaultsAndSet) {
    RadialPointerElement p;
    EXPECT_NEAR(p.length_ratio(), 1.0, 1e-9);
    EXPECT_NEAR(p.thickness_ratio(), 0.05, 1e-9);

    p.set_length_ratio(0.6);
    p.set_thickness_ratio(0.1);
    EXPECT_NEAR(p.length_ratio(), 0.6, 1e-9);
    EXPECT_NEAR(p.thickness_ratio(), 0.1, 1e-9);
}

TEST(RadialPointerElement, LengthAndThicknessRatioClampToUnitRange) {
    RadialPointerElement p;
    p.set_length_ratio(1.5);
    EXPECT_NEAR(p.length_ratio(), 1.0, 1e-9);
    p.set_length_ratio(-0.5);
    EXPECT_NEAR(p.length_ratio(), 0.0, 1e-9);

    p.set_thickness_ratio(2.0);
    EXPECT_NEAR(p.thickness_ratio(), 1.0, 1e-9);
    p.set_thickness_ratio(-1.0);
    EXPECT_NEAR(p.thickness_ratio(), 0.0, 1e-9);
}

// ── render_model 內容 ──

TEST(RadialPointerElement, RenderModelContent) {
    RadialPointerElement p(0.0, 200.0);
    p.set_angle_span(135.0, 270.0);
    p.set_value(100.0);
    p.set_length_ratio(0.8);
    p.set_thickness_ratio(0.12);
    p.set_color("needle");
    p.set_surface("desk.gauge");
    p.set_slot("needle");
    const RenderModel m = p.render_model();
    EXPECT_NEAR(m.value_ratio, 0.5, 1e-9);
    EXPECT_NEAR(m.start_angle_degrees, 135.0, 1e-9);
    EXPECT_NEAR(m.sweep_degrees, 270.0, 1e-9);
    EXPECT_NEAR(m.angle_degrees, 270.0, 1e-9);
    EXPECT_NEAR(m.length_ratio, 0.8, 1e-9);
    EXPECT_NEAR(m.thickness_ratio, 0.12, 1e-9);
    EXPECT_EQ(m.color, std::string("needle"));
    EXPECT_EQ(m.surface, std::string("desk.gauge"));
    EXPECT_EQ(m.slot, std::string("needle"));
}

TEST(RadialPointerElement, RenderModelDefaultsWhenUnconfigured) {
    RadialPointerElement p;
    const RenderModel m = p.render_model();
    EXPECT_EQ(m.surface, std::string(""));
    EXPECT_EQ(m.slot, std::string(""));
    EXPECT_EQ(m.color, std::string("accent"));
    EXPECT_NEAR(m.value_ratio, 0.0, 1e-9);
}

// ── 無效輸入報錯（不靜默）──

TEST(Invalid, NonFiniteValueThrows) {
    RadialPointerElement p(0.0, 1.0);
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(p.set_value(nan), std::invalid_argument);
    EXPECT_THROW(p.set_value(inf), std::invalid_argument);
}

TEST(Invalid, DegenerateRangeThrows) {
    RadialPointerElement p;
    EXPECT_THROW(p.set_range(5.0, 5.0), std::invalid_argument);   // min == max
    EXPECT_THROW(p.set_range(10.0, 1.0), std::invalid_argument);  // min > max
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(p.set_range(0.0, inf), std::invalid_argument);   // 非有限
    // 建構子同樣驗證。
    EXPECT_THROW(RadialPointerElement(1.0, 1.0), std::invalid_argument);
}

TEST(Invalid, DegenerateAngleSpanThrows) {
    RadialPointerElement p;
    EXPECT_THROW(p.set_angle_span(0.0, 0.0), std::invalid_argument);  // sweep 0
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(p.set_angle_span(inf, 90.0), std::invalid_argument);  // 非有限
}

TEST(Invalid, NonFiniteLengthOrThicknessRatioThrows) {
    RadialPointerElement p;
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(p.set_length_ratio(nan), std::invalid_argument);
    EXPECT_THROW(p.set_length_ratio(inf), std::invalid_argument);
    EXPECT_THROW(p.set_thickness_ratio(nan), std::invalid_argument);
    EXPECT_THROW(p.set_thickness_ratio(inf), std::invalid_argument);
}

// ── E7-03 宣告式變數段落驅動設定 ──

// 以 vars 段落宣告 lo/hi/sfc，其餘欄位以 ${..} 引用；expand 後填入元件。
TEST(Config, DrivenBySectionVars) {
    // vars: { lo: 0, hi: 200, sfc: desk.cpu }
    // min: ${lo}  max: ${hi}  value: 150  start_angle: -90  sweep: 180
    // length_ratio: 0.9  thickness_ratio: 0.08  color: cpu  surface: ${sfc}  slot: needle
    Value cfg = Value::map({
        {"vars", Value::map({
                     {"lo", Value::integer(0)},
                     {"hi", Value::integer(200)},
                     {"sfc", Value::string("desk.cpu")},
                 })},
        {"min", Value::string("${lo}")},
        {"max", Value::string("${hi}")},
        {"value", Value::integer(150)},
        {"start_angle", Value::integer(-90)},
        {"sweep", Value::integer(180)},
        {"length_ratio", Value::number(0.9)},
        {"thickness_ratio", Value::number(0.08)},
        {"color", Value::string("cpu")},
        {"surface", Value::string("${sfc}")},
        {"slot", Value::string("needle")},
    });

    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    ASSERT_TRUE(st == ConfigStatus::Ok);
    EXPECT_NEAR(p.range_min(), 0.0, 1e-9);
    EXPECT_NEAR(p.range_max(), 200.0, 1e-9);
    EXPECT_NEAR(p.value(), 150.0, 1e-9);
    EXPECT_NEAR(p.ratio(), 0.75, 1e-9);
    EXPECT_NEAR(p.start_angle(), -90.0, 1e-9);
    EXPECT_NEAR(p.sweep(), 180.0, 1e-9);
    EXPECT_NEAR(p.angle(), -90.0 + 0.75 * 180.0, 1e-9);  // 45°
    EXPECT_NEAR(p.length_ratio(), 0.9, 1e-9);
    EXPECT_NEAR(p.thickness_ratio(), 0.08, 1e-9);
    EXPECT_EQ(p.color(), std::string("cpu"));
    EXPECT_EQ(p.surface(), std::string("desk.cpu"));
    EXPECT_EQ(p.slot(), std::string("needle"));
}

TEST(Config, PartialFieldsPreserveExistingValues) {
    // 只給 value，其餘欄位缺席 → 沿用元件現值（缺欄非錯誤）。
    Value cfg = Value::map({
        {"value", Value::number(0.3)},
    });
    RadialPointerElement p;
    p.set_color("preset");
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    ASSERT_TRUE(st == ConfigStatus::Ok);
    EXPECT_NEAR(p.value(), 0.3, 1e-9);
    EXPECT_EQ(p.color(), std::string("preset"));  // 未被覆寫
    EXPECT_NEAR(p.start_angle(), 135.0, 1e-9);      // 沿用預設弧
}

TEST(Config, UndefinedVariableReportsResolveError) {
    Value cfg = Value::map({
        {"min", Value::string("${missing}")},
        {"max", Value::integer(10)},
    });
    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    EXPECT_TRUE(st == ConfigStatus::ResolveError);
    EXPECT_FALSE(err.empty());  // 不靜默：帶訊息
}

TEST(Config, NonMapRootReportsNotAMap) {
    Value cfg = Value::integer(5);
    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    EXPECT_TRUE(st == ConfigStatus::NotAMap);
    EXPECT_FALSE(err.empty());
}

TEST(Config, DegenerateRangeInConfigReportsInvalidField) {
    Value cfg = Value::map({
        {"min", Value::integer(10)},
        {"max", Value::integer(10)},  // min == max
    });
    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
    EXPECT_FALSE(err.empty());
}

TEST(Config, DegenerateSweepInConfigReportsInvalidField) {
    Value cfg = Value::map({
        {"start_angle", Value::integer(0)},
        {"sweep", Value::integer(0)},  // 退化跨距
    });
    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
    EXPECT_FALSE(err.empty());
}

TEST(Config, WrongTypeFieldReportsInvalidField) {
    // min 為字串（非引用、非數字）→ InvalidField
    Value cfg = Value::map({
        {"min", Value::string("lots")},
        {"max", Value::integer(10)},
    });
    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
    EXPECT_FALSE(err.empty());
}

TEST(Config, WrongTypeColorFieldReportsInvalidField) {
    // color 為數字（非字串）→ InvalidField
    Value cfg = Value::map({
        {"color", Value::integer(5)},
    });
    RadialPointerElement p;
    std::string err;
    const ConfigStatus st = ds::elements::load_radial_pointer_config(cfg, p, err);
    EXPECT_TRUE(st == ConfigStatus::InvalidField);
    EXPECT_FALSE(err.empty());
}

// ── NFR-02：無絕對座標 / 數字 z-order；位置以具名表達、尺寸以比例 / 角度 ──

TEST(NFR02, PositionIsNamedNotNumeric) {
    // 佈局僅以具名 surface + 具名 slot 表達；幾何僅以比例 [0,1] / 角度表達。
    // RenderModel 結構本身不含任何 x/y/寬/高像素或整數層級欄位（見標頭定義）。
    RadialPointerElement p(0.0, 1.0);
    p.set_value(0.5);
    p.set_surface("named.surface");
    p.set_slot("named.slot");
    const RenderModel m = p.render_model();
    EXPECT_EQ(m.surface, std::string("named.surface"));
    EXPECT_EQ(m.slot, std::string("named.slot"));
    EXPECT_GE(m.value_ratio, 0.0);
    EXPECT_LE(m.value_ratio, 1.0);
    EXPECT_GE(m.length_ratio, 0.0);
    EXPECT_LE(m.length_ratio, 1.0);
    EXPECT_GE(m.thickness_ratio, 0.0);
    EXPECT_LE(m.thickness_ratio, 1.0);
    // 角度落在宣告弧 [start, start+sweep] 內——語意輸出，非版面座標。
    p.set_value(1.0);
    const RenderModel m2 = p.render_model();
    EXPECT_NEAR(m2.angle_degrees, m2.start_angle_degrees + m2.sweep_degrees, 1e-9);
}

}  // namespace
