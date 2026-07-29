// E4-01 文字排版與渲染 — gtest 測試
//
// 涵蓋：單行測量、多行換行、對齊(左/中/右)、省略、行高、空字串、可注入 FontMetrics、
// 無效輸入報錯、LayoutResult 內容、NFR-02 相對佈局、UTF-8 解碼。
#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "text_layout.hpp"

using namespace ds::render;

namespace {

// 便利：等寬度量 advance=10、行高=20、ascent=16。
FixedFontMetrics mono() { return FixedFontMetrics(10.0, 20.0, 16.0); }

// 可注入的「比例字型」stub —— 證明 FontMetrics 為抽象注入點：
// 'i' 窄(5)、空白(4)、其餘(10)。行高 20、ascent 依預設=行高。
class ProportionalMetrics : public FontMetrics {
public:
    double advance(CodePoint cp) const override {
        if (cp == U'i') return 5.0;
        if (cp == U' ') return 4.0;
        return 10.0;
    }
    double line_height() const override { return 20.0; }
};

// 會回傳非有限 advance 的壞度量 —— 用以驗證不靜默。
class BadMetrics : public FontMetrics {
public:
    double advance(CodePoint) const override {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double line_height() const override { return 20.0; }
};

}  // namespace

// --- 單行測量 ---
TEST(E4_01_Measure, SingleLineWidthIsSumOfAdvances) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    Size s = tl.measure("abcde");  // 5 字 × 10
    EXPECT_NEAR(s.width, 50.0, 1e-9);
    EXPECT_NEAR(s.height, 20.0, 1e-9);  // 一行 × 行高 20
}

TEST(E4_01_Measure, MeasureMatchesLayoutSize) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    LayoutResult r = tl.layout("hello", c);
    Size s = tl.measure("hello", c);
    EXPECT_NEAR(r.size.width, s.width, 1e-9);
    EXPECT_NEAR(r.size.height, s.height, 1e-9);
    EXPECT_NEAR(s.width, 50.0, 1e-9);
}

// --- LayoutResult 內容 ---
TEST(E4_01_Result, GlyphsAndLineBoxPopulated) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutResult r = tl.layout("ab", {});
    ASSERT_EQ(r.lines.size(), 1u);
    ASSERT_EQ(r.glyphs.size(), 2u);
    EXPECT_EQ(r.lines[0].begin, 0u);
    EXPECT_EQ(r.lines[0].count, 2u);
    EXPECT_NEAR(r.lines[0].width, 20.0, 1e-9);
    EXPECT_NEAR(r.lines[0].baseline, 16.0, 1e-9);  // y(0)+ascent(16)
    EXPECT_EQ(r.glyphs[0].codepoint, U'a');
    EXPECT_EQ(r.glyphs[1].codepoint, U'b');
    EXPECT_EQ(r.glyphs[0].line, 0u);
    EXPECT_FALSE(r.truncated);
}

// --- 多行換行（詞界 word-wrap）---
TEST(E4_01_Wrap, WordWrapBreaksAtSpaces) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_width = 100.0;  // 10 字/行
    c.wrap = WrapMode::Word;
    // "aaaa bbbb cccc"：'aaaa bbbb'(9) 放得下(90<=100)，加 ' cccc'(5) → 溢出換行。
    LayoutResult r = tl.layout("aaaa bbbb cccc", c);
    ASSERT_EQ(r.lines.size(), 2u);
    EXPECT_NEAR(r.lines[0].width, 90.0, 1e-9);   // "aaaa bbbb"
    EXPECT_NEAR(r.lines[1].width, 40.0, 1e-9);   // "cccc"
    EXPECT_NEAR(r.size.height, 40.0, 1e-9);      // 2 行 × 20
    EXPECT_NEAR(r.size.width, 100.0, 1e-9);      // 有界 → 盒寬
}

TEST(E4_01_Wrap, HardNewlineSplitsLines) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutResult r = tl.layout("ab\ncd", {});
    ASSERT_EQ(r.lines.size(), 2u);
    EXPECT_EQ(r.lines[0].count, 2u);
    EXPECT_EQ(r.lines[1].count, 2u);
    EXPECT_NEAR(r.lines[1].y, 20.0, 1e-9);  // 第二行頂 = 1×行高
    EXPECT_EQ(r.glyphs[2].line, 1u);
}

TEST(E4_01_Wrap, BlankLineFromDoubleNewline) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutResult r = tl.layout("a\n\nb", {});
    ASSERT_EQ(r.lines.size(), 3u);
    EXPECT_EQ(r.lines[1].count, 0u);          // 中間為空行
    EXPECT_NEAR(r.lines[1].width, 0.0, 1e-9);
    EXPECT_NEAR(r.size.height, 60.0, 1e-9);   // 3 行 × 20
}

TEST(E4_01_Wrap, OverlongWordOverflowsOwnLine) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_width = 30.0;  // 3 字/行，但單詞 5 字
    c.wrap = WrapMode::Word;
    LayoutResult r = tl.layout("aaaaa", c);
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_NEAR(r.lines[0].width, 50.0, 1e-9);  // 溢出、不硬斷字
}

// --- 對齊 ---
TEST(E4_01_Align, LeftCenterRightOffsets) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_width = 100.0;
    c.wrap = WrapMode::None;

    c.align = TextAlign::Left;
    LayoutResult l = tl.layout("ab", c);  // 寬 20，盒 100
    EXPECT_NEAR(l.lines[0].x, 0.0, 1e-9);
    EXPECT_NEAR(l.glyphs[0].x, 0.0, 1e-9);

    c.align = TextAlign::Center;
    LayoutResult m = tl.layout("ab", c);
    EXPECT_NEAR(m.lines[0].x, 40.0, 1e-9);        // (100-20)/2
    EXPECT_NEAR(m.glyphs[0].x, 40.0, 1e-9);
    EXPECT_NEAR(m.glyphs[1].x, 50.0, 1e-9);

    c.align = TextAlign::Right;
    LayoutResult rr = tl.layout("ab", c);
    EXPECT_NEAR(rr.lines[0].x, 80.0, 1e-9);       // 100-20
    EXPECT_NEAR(rr.glyphs[0].x, 80.0, 1e-9);
}

TEST(E4_01_Align, CenterUnboundedUsesWidestLine) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.align = TextAlign::Center;  // 無界 → 對齊寬 = 最寬行
    LayoutResult r = tl.layout("a\nabcd", c);  // 行寬 10 與 40
    EXPECT_NEAR(r.size.width, 40.0, 1e-9);
    EXPECT_NEAR(r.lines[0].x, 15.0, 1e-9);  // (40-10)/2
    EXPECT_NEAR(r.lines[1].x, 0.0, 1e-9);
}

// --- 省略 ellipsis ---
TEST(E4_01_Ellipsis, NoWrapOverflowGetsEllipsis) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_width = 50.0;   // 5 字寬
    c.wrap = WrapMode::None;
    c.ellipsis = true;    // 省略字元 advance=10
    LayoutResult r = tl.layout("abcdefgh", c);  // 80 寬 > 50
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_TRUE(r.truncated);
    EXPECT_TRUE(r.lines[0].ellipsized);
    EXPECT_LE(r.lines[0].width, 50.0 + 1e-9);   // 裁切後不超過盒寬
    // 末字符應為省略字元。
    ASSERT_GT(r.glyphs.size(), 0u);
    EXPECT_EQ(r.glyphs.back().codepoint, static_cast<CodePoint>(0x2026));
}

TEST(E4_01_Ellipsis, MaxLinesTruncationAppendsEllipsis) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_lines = 2;
    c.ellipsis = true;
    LayoutResult r = tl.layout("a\nb\nc\nd", c);  // 4 行 → 裁成 2
    ASSERT_EQ(r.lines.size(), 2u);
    EXPECT_TRUE(r.truncated);
    EXPECT_TRUE(r.lines.back().ellipsized);
    EXPECT_EQ(r.glyphs.back().codepoint, static_cast<CodePoint>(0x2026));
}

TEST(E4_01_Ellipsis, NoEllipsisWhenFits) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_width = 100.0;
    c.wrap = WrapMode::None;
    c.ellipsis = true;
    LayoutResult r = tl.layout("abc", c);  // 30 <= 100
    EXPECT_FALSE(r.truncated);
    EXPECT_FALSE(r.lines[0].ellipsized);
}

// --- 行高 ---
TEST(E4_01_LineHeight, OverrideChangesLineY) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.line_height = 30.0;  // 覆寫
    LayoutResult r = tl.layout("a\nb", c);
    EXPECT_NEAR(r.lines[1].y, 30.0, 1e-9);
    EXPECT_NEAR(r.size.height, 60.0, 1e-9);
}

TEST(E4_01_LineHeight, DefaultUsesMetrics) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutResult r = tl.layout("a\nb", {});
    EXPECT_NEAR(r.lines[1].y, 20.0, 1e-9);  // 度量行高
}

// --- 空字串 ---
TEST(E4_01_Empty, EmptyStringYieldsEmptyResult) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutResult r = tl.layout("", {});
    EXPECT_TRUE(r.lines.empty());
    EXPECT_TRUE(r.glyphs.empty());
    EXPECT_NEAR(r.size.width, 0.0, 1e-9);
    EXPECT_NEAR(r.size.height, 0.0, 1e-9);
    EXPECT_FALSE(r.truncated);
}

// --- 可注入 FontMetrics ---
TEST(E4_01_Injectable, ProportionalMetricsAffectWidth) {
    ProportionalMetrics pm;
    TextLayout tl(pm);
    // "iii" = 3×5 = 15；"aaa" = 3×10 = 30。
    EXPECT_NEAR(tl.measure("iii").width, 15.0, 1e-9);
    EXPECT_NEAR(tl.measure("aaa").width, 30.0, 1e-9);
    // 個別 glyph advance 反映注入度量。
    LayoutResult r = tl.layout("ai", {});
    ASSERT_EQ(r.glyphs.size(), 2u);
    EXPECT_NEAR(r.glyphs[0].advance, 10.0, 1e-9);
    EXPECT_NEAR(r.glyphs[1].advance, 5.0, 1e-9);
    EXPECT_NEAR(r.glyphs[1].x, 10.0, 1e-9);  // 第二字 x = 第一字 advance
}

// --- 無效輸入報錯（不靜默）---
TEST(E4_01_Invalid, BadUtf8Throws) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    std::string bad;
    bad.push_back(static_cast<char>(0xFF));  // 非法起始位元組
    EXPECT_THROW(tl.layout(bad, {}), std::invalid_argument);
}

TEST(E4_01_Invalid, TruncatedUtf8Throws) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    std::string bad;
    bad.push_back(static_cast<char>(0xE4));  // 3-byte 起始但缺續接
    bad.push_back(static_cast<char>(0xB8));
    EXPECT_THROW(tl.layout(bad, {}), std::invalid_argument);
}

TEST(E4_01_Invalid, NanMaxWidthThrows) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.max_width = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(tl.layout("a", c), std::invalid_argument);
}

TEST(E4_01_Invalid, NonFiniteLineHeightThrows) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    LayoutConstraints c;
    c.line_height = std::numeric_limits<double>::infinity();
    EXPECT_THROW(tl.layout("a", c), std::invalid_argument);
}

TEST(E4_01_Invalid, BadMetricAdvanceThrows) {
    BadMetrics bm;
    TextLayout tl(bm);
    EXPECT_THROW(tl.layout("a", {}), std::invalid_argument);
}

TEST(E4_01_Invalid, FixedFontMetricsRejectsBadArgs) {
    EXPECT_THROW(FixedFontMetrics(-1.0, 20.0), std::invalid_argument);   // 負 advance
    EXPECT_THROW(FixedFontMetrics(10.0, 0.0), std::invalid_argument);    // 非正行高
    EXPECT_THROW(FixedFontMetrics(
                     std::numeric_limits<double>::quiet_NaN(), 20.0),
                 std::invalid_argument);  // NaN advance
}

// --- UTF-8 解碼直接測試 ---
TEST(E4_01_Utf8, DecodesMultibyte) {
    // U+4E2D（中，3-byte: E4 B8 AD）+ 'x'
    std::string s = "\xE4\xB8\xAD" "x";
    std::vector<CodePoint> cps = decode_utf8(s);
    ASSERT_EQ(cps.size(), 2u);
    EXPECT_EQ(cps[0], static_cast<CodePoint>(0x4E2D));
    EXPECT_EQ(cps[1], U'x');
}

TEST(E4_01_Utf8, RejectsOverlongAndSurrogate) {
    // 過長編碼 C0 80（本應為 U+0000 的 1-byte）
    std::string overlong;
    overlong.push_back(static_cast<char>(0xC0));
    overlong.push_back(static_cast<char>(0x80));
    EXPECT_THROW(decode_utf8(overlong), std::invalid_argument);
    // 代理區 U+D800（ED A0 80）
    std::string surrogate;
    surrogate.push_back(static_cast<char>(0xED));
    surrogate.push_back(static_cast<char>(0xA0));
    surrogate.push_back(static_cast<char>(0x80));
    EXPECT_THROW(decode_utf8(surrogate), std::invalid_argument);
}

// --- NFR-02：相對佈局，無絕對座標 / z-order ---
TEST(E4_01_NFR02, PositionsAreRelativeOffsets) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm, "surface.panel");  // 具名 surface 綁定（非數字 handle）
    LayoutConstraints c;
    c.max_width = 100.0;
    LayoutResult r = tl.layout("ab\ncd", c);
    // 目標 surface 為具名字串，回傳於結果。
    EXPECT_EQ(r.surface, "surface.panel");
    // 首行頂 y=0（相對盒頂，非螢幕座標）；次行 y = 行高（相對累加）。
    EXPECT_NEAR(r.lines[0].y, 0.0, 1e-9);
    EXPECT_NEAR(r.lines[1].y, 20.0, 1e-9);
    // 每行首 glyph x 自其行起點累加，屬相對偏移。
    EXPECT_NEAR(r.glyphs[0].x, 0.0, 1e-9);
    EXPECT_NEAR(r.glyphs[1].x, 10.0, 1e-9);
    // 無任何數字 z-order 欄位存在於渲染描述（型別上即不提供，見 LineBox/Glyph 定義）。
}

TEST(E4_01_NFR02, SurfaceDefaultsEmptyAndSettable) {
    FixedFontMetrics fm = mono();
    TextLayout tl(fm);
    EXPECT_TRUE(tl.surface().empty());
    tl.set_surface("surface.overlay");
    EXPECT_EQ(tl.layout("x", {}).surface, "surface.overlay");
}
