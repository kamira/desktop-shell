// E1-22 建置期能力閘控 lint — 契約測試（gtest）
//
// 驗證：偵測絕對座標違規、偵測數字 z-order 違規、偵測無 has() 保護的能力呼叫、
// 合規碼零違規、診斷帶行號、多違規彙整。相位 1：純文字規則分析，無平台分支。
#include "capability_lint.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using ds::kernel::CapabilityLint;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LintDiagnostic;
using ds::kernel::LintRule;

namespace {

// 計數指定規則的診斷數。
std::size_t count_rule(const std::vector<LintDiagnostic>& ds, LintRule rule) {
    return static_cast<std::size_t>(
        std::count_if(ds.begin(), ds.end(),
                      [rule](const LintDiagnostic& d) { return d.rule == rule; }));
}

bool has_line(const std::vector<LintDiagnostic>& ds, LintRule rule, std::size_t line) {
    return std::any_of(ds.begin(), ds.end(), [&](const LintDiagnostic& d) {
        return d.rule == rule && d.line == line;
    });
}

// 固定閘控名單，讓 NFR-03 測試不依賴矩陣導出，結果決定性。
CapabilityLint make_lint() {
    return CapabilityLint(std::vector<std::string>{"tray_icon", "set_brightness"});
}

// --- NFR-02：數字 z-order ---
TEST(CapabilityLint, DetectsNumericZOrder) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("widget.z_order = 3;\n");
    EXPECT_EQ(count_rule(ds, LintRule::NumericZOrder), 1u);
    ASSERT_FALSE(ds.empty());
    EXPECT_EQ(ds.front().rule_id, "NFR-02");
}

// z-index / 冒號指派變體也應偵測。
TEST(CapabilityLint, DetectsZIndexVariant) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("layer: { z-index: 100 }\n");
    EXPECT_EQ(count_rule(ds, LintRule::NumericZOrder), 1u);
}

// --- NFR-02：絕對 / 像素座標 ---
TEST(CapabilityLint, DetectsAbsoluteCoordinateAssignment) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("win.pos_x = 640;\n");
    EXPECT_EQ(count_rule(ds, LintRule::AbsoluteCoordinate), 1u);
    EXPECT_EQ(ds.front().rule_id, "NFR-02");
}

TEST(CapabilityLint, DetectsAbsoluteCoordinateCall) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("set_position(100, 200);\n");
    EXPECT_EQ(count_rule(ds, LintRule::AbsoluteCoordinate), 1u);
}

// --- NFR-03：能力呼叫缺 has() 保護 ---
TEST(CapabilityLint, DetectsUnguardedCapabilityCall) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("tray_icon();\n");
    EXPECT_EQ(count_rule(ds, LintRule::UnguardedCapability), 1u);
    ASSERT_FALSE(ds.empty());
    EXPECT_EQ(ds.front().rule_id, "NFR-03");
    EXPECT_EQ(ds.front().line, 1u);
}

// 同行先 has() 閘控 → 不算違規。
TEST(CapabilityLint, GuardedOnSameLineIsClean) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("if (caps.has(\"host.tray_icon\")) tray_icon();\n");
    EXPECT_EQ(count_rule(ds, LintRule::UnguardedCapability), 0u);
}

// 前面行有 has() 閘控 → 後續能力呼叫視為受保護。
TEST(CapabilityLint, GuardedByEarlierHasIsClean) {
    const CapabilityLint lint = make_lint();
    const std::string src =
        "if (caps.has(\"host.tray_icon\")) {\n"
        "    tray_icon();\n"
        "}\n";
    EXPECT_EQ(count_rule(lint.lint(src), LintRule::UnguardedCapability), 0u);
}

// --- 合規碼零違規 ---
TEST(CapabilityLint, CompliantSourceHasZeroViolations) {
    const CapabilityLint lint = make_lint();
    const std::string src =
        "// 具名錨點 + 具名層級，能力呼叫先閘控\n"
        "window.anchor = ScreenAnchor::Center;\n"
        "window.layer = ScreenRole::Primary;\n"
        "if (caps.has(\"host.tray_icon\")) {\n"
        "    tray_icon();\n"
        "}\n";
    const auto ds = lint.lint(src);
    EXPECT_TRUE(ds.empty()) << "unexpected diagnostics: " << ds.size();
}

// 註解內的示例文字不應誤判。
TEST(CapabilityLint, CommentsAreIgnored) {
    const CapabilityLint lint = make_lint();
    const std::string src =
        "// 反例：z_order = 5 或 pos_x = 10 都不可\n"
        "int safe = 1;\n";
    EXPECT_TRUE(lint.lint(src).empty());
}

// --- 診斷帶行號（可定位）---
TEST(CapabilityLint, DiagnosticsCarryLineNumbers) {
    const CapabilityLint lint = make_lint();
    const std::string src =
        "int a = 0;\n"        // 1
        "panel.z_order = 7;\n"  // 2  NFR-02
        "int b = 1;\n"        // 3
        "set_brightness(80);\n";  // 4  NFR-03（也含 NFR-02 座標呼叫? 不，非座標 API）
    const auto ds = lint.lint(src);
    EXPECT_TRUE(has_line(ds, LintRule::NumericZOrder, 2u));
    EXPECT_TRUE(has_line(ds, LintRule::UnguardedCapability, 4u));
}

// --- 多違規彙整 ---
TEST(CapabilityLint, AggregatesMultipleViolations) {
    const CapabilityLint lint = make_lint();
    const std::string src =
        "frame.z_order = 2;\n"   // 1 NFR-02 zorder
        "frame.pos_y = 480;\n"   // 2 NFR-02 coord
        "tray_icon();\n"         // 3 NFR-03
        "set_brightness(50);\n"; // 4 NFR-03
    const auto ds = lint.lint(src);
    EXPECT_EQ(count_rule(ds, LintRule::NumericZOrder), 1u);
    EXPECT_EQ(count_rule(ds, LintRule::AbsoluteCoordinate), 1u);
    EXPECT_EQ(count_rule(ds, LintRule::UnguardedCapability), 2u);
    EXPECT_EQ(ds.size(), 4u);
    // 依行號遞增彙整。
    for (std::size_t i = 1; i < ds.size(); ++i) {
        EXPECT_LE(ds[i - 1].line, ds[i].line);
    }
}

// 同一行多違規：z-order 與座標同時觸發。
TEST(CapabilityLint, MultipleViolationsOnSameLine) {
    const CapabilityLint lint = make_lint();
    const auto ds = lint.lint("cfg.z_order = 1; cfg.left = 3;\n");
    EXPECT_EQ(count_rule(ds, LintRule::NumericZOrder), 1u);
    EXPECT_EQ(count_rule(ds, LintRule::AbsoluteCoordinate), 1u);
    for (const auto& d : ds) EXPECT_EQ(d.line, 1u);
}

// --- 預設建構：閘控名單由 E1-21 能力矩陣 optional 能力導出 ---
TEST(CapabilityLint, DefaultGatedApisDerivedFromMatrix) {
    const CapabilityLint lint;  // 預設
    const auto& apis = lint.gated_apis();
    EXPECT_FALSE(apis.empty());
    // "host.tray_icon" -> 尾段 "tray_icon"。
    EXPECT_NE(std::find(apis.begin(), apis.end(), "tray_icon"), apis.end());
    // 未閘控呼叫該能力應被 NFR-03 抓到。
    EXPECT_EQ(count_rule(lint.lint("tray_icon();\n"), LintRule::UnguardedCapability), 1u);
}

// 導出僅取 optional 能力：mandatory 能力（如 render.paint）不進閘控名單。
TEST(CapabilityLint, MatrixDerivationTakesOnlyOptional) {
    const auto apis = CapabilityLint::gated_apis_from_matrix(CapabilityMatrix::defaults());
    EXPECT_EQ(std::find(apis.begin(), apis.end(), "paint"), apis.end());
}

// 空來源：零違規、不崩。
TEST(CapabilityLint, EmptySourceIsClean) {
    const CapabilityLint lint = make_lint();
    EXPECT_TRUE(lint.lint("").empty());
}

}  // namespace
