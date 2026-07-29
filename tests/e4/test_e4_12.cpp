// E4-12 可點文字區段 — gtest 測試
//
// 涵蓋：標記區段(add_span)、以 E4-01 取字符相對幾何(render_model)、以 E1-04 hit_span 命中回
// id、多區段各自獨立命中、跨行區段、越界(字元範圍/建構約束)明確報錯不靜默、重疊區段報錯、
// 無命中回空(nullopt)、set_text 重設清空舊區段。
#include "clickable_text.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "hit_test.hpp"
#include "text_layout.hpp"

using ds::elements::ClickableTextElement;
using ds::kernel::LocalPoint;
using ds::render::FixedFontMetrics;
using ds::render::LayoutConstraints;
using ds::render::WrapMode;

namespace {

// 等寬度量：advance=10、行高=20、ascent=16（承 E4-01/E4-11 測試慣例）。
FixedFontMetrics mono() { return FixedFontMetrics(10.0, 20.0, 16.0); }

}  // namespace

// --- 建構：約束驗證（wrap 必須 None、ellipsis 必須 false，否則破壞確定性映射）---
TEST(ClickableTextElement, ConstructorRejectsWordWrap) {
    FixedFontMetrics fm = mono();
    LayoutConstraints c;
    c.wrap = WrapMode::Word;
    c.max_width = 100.0;
    EXPECT_THROW(ClickableTextElement(fm, c), std::invalid_argument);
}

TEST(ClickableTextElement, ConstructorRejectsEllipsis) {
    FixedFontMetrics fm = mono();
    LayoutConstraints c;
    c.wrap = WrapMode::None;
    c.ellipsis = true;
    EXPECT_THROW(ClickableTextElement(fm, c), std::invalid_argument);
}

TEST(ClickableTextElement, ConstructorAcceptsDefaultConstraints) {
    FixedFontMetrics fm = mono();
    EXPECT_NO_THROW(ClickableTextElement el(fm));
}

// --- 標記區段 + 取字符相對幾何 ---
TEST(ClickableTextElement, AddSpanMarksRangeAndRenderModelExposesGlyphGeometry) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hello world");  // 11 個碼位

    EXPECT_EQ(el.text_length(), 11u);
    el.add_span(0, 5, "greet");  // "hello"
    EXPECT_EQ(el.span_count(), 1u);

    const auto& model = el.render_model();
    ASSERT_EQ(model.glyphs.size(), 11u);
    // 等寬 advance=10：字符 x 依序 0,10,20,...
    EXPECT_DOUBLE_EQ(model.glyphs[0].x, 0.0);
    EXPECT_DOUBLE_EQ(model.glyphs[1].x, 10.0);
    EXPECT_DOUBLE_EQ(model.glyphs[4].x, 40.0);
    EXPECT_DOUBLE_EQ(model.glyphs[0].advance, 10.0);
    EXPECT_EQ(model.glyphs[0].line, 0u);
}

// --- hit_span：命中回具名 id ---
TEST(ClickableTextElement, HitSpanReturnsIdWhenPointFallsInsideSpan) {
    FixedFontMetrics fm = mono();  // advance=10, line_height=20
    ClickableTextElement el(fm);
    el.set_text("hello world");
    el.add_span(0, 5, "greet");  // "hello" → 字符 x in [0,50), y in [0,20)

    // 第 3 個字元 'l'（index 2）：x in [20,30)。
    const auto id = el.hit_span(LocalPoint{25.0f, 10.0f});
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(*id, "greet");
}

// --- 多區段：各自獨立命中，未標記字元不命中 ---
TEST(ClickableTextElement, MultipleSpansHitIndependently) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hello world");  // index 0-4=hello, 5=space, 6-10=world
    el.add_span(0, 5, "greet");
    el.add_span(6, 11, "target");
    EXPECT_EQ(el.span_count(), 2u);

    // "world" 第 1 字元 'w'（index 6）：x in [60,70)。
    auto id_world = el.hit_span(LocalPoint{65.0f, 5.0f});
    ASSERT_TRUE(id_world.has_value());
    EXPECT_EQ(*id_world, "target");

    // "hello" 第 1 字元 'h'（index 0）：x in [0,10)。
    auto id_hello = el.hit_span(LocalPoint{5.0f, 5.0f});
    ASSERT_TRUE(id_hello.has_value());
    EXPECT_EQ(*id_hello, "greet");

    // 空白字元（index 5，x in [50,60)）未被任一區段涵蓋 → 無命中。
    auto id_gap = el.hit_span(LocalPoint{55.0f, 5.0f});
    EXPECT_FALSE(id_gap.has_value());
}

// --- 跨行區段：字元範圍橫跨硬換行 '\n' 前後兩行，兩行的字元都能命中同一 id ---
TEST(ClickableTextElement, SpanAcrossHardNewlineHitsOnBothLines) {
    FixedFontMetrics fm = mono();  // advance=10, line_height=20
    ClickableTextElement el(fm);
    el.set_text("ab\ncd");  // 碼位：a,b,\n,c,d（index 2 為 '\n'，不產生字符）
    EXPECT_EQ(el.text_length(), 5u);

    // 區段涵蓋 index [1,4)：'b'（第0行）、'\n'（無字符）、'c'（第1行）。
    el.add_span(1, 4, "cross");

    const auto& model = el.render_model();
    ASSERT_EQ(model.glyphs.size(), 4u);  // a,b,c,d（'\n' 不產生字符）
    ASSERT_EQ(model.lines.size(), 2u);

    // 'b'：第0行，x in [10,20)，y in [0,20)。
    auto id_line0 = el.hit_span(LocalPoint{15.0f, 5.0f});
    ASSERT_TRUE(id_line0.has_value());
    EXPECT_EQ(*id_line0, "cross");

    // 'c'：第1行，x in [0,10)，y in [20,40)。
    auto id_line1 = el.hit_span(LocalPoint{5.0f, 25.0f});
    ASSERT_TRUE(id_line1.has_value());
    EXPECT_EQ(*id_line1, "cross");

    // 'a'：第0行首字元，不在區段 [1,4) 內 → 無命中。
    auto id_outside_span = el.hit_span(LocalPoint{5.0f, 5.0f});
    EXPECT_FALSE(id_outside_span.has_value());
}

// --- 越界：字元範圍超過目前文字長度 → 明確報錯，不靜默 ---
TEST(ClickableTextElement, AddSpanOutOfRangeThrows) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hi");  // 2 個碼位
    EXPECT_THROW(el.add_span(0, 3, "oops"), std::invalid_argument);   // end 越界
    EXPECT_THROW(el.add_span(2, 5, "oops2"), std::invalid_argument);  // start 已在界外
    EXPECT_EQ(el.span_count(), 0u);  // 失敗不改變狀態
}

// --- 反轉 / 空範圍、空 id → 明確報錯 ---
TEST(ClickableTextElement, AddSpanInvalidRangeOrEmptyIdThrows) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hello");
    EXPECT_THROW(el.add_span(3, 3, "empty-range"), std::invalid_argument);  // start == end
    EXPECT_THROW(el.add_span(4, 2, "reversed"), std::invalid_argument);     // start > end
    EXPECT_THROW(el.add_span(0, 2, ""), std::invalid_argument);             // 空 id
    EXPECT_EQ(el.span_count(), 0u);
}

// --- 重疊區段 → 明確報錯，不靜默允許 / 覆蓋 ---
TEST(ClickableTextElement, OverlappingSpansThrow) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hello world");
    el.add_span(0, 5, "a");
    EXPECT_THROW(el.add_span(3, 8, "b"), std::invalid_argument);  // 與 [0,5) 重疊於 [3,5)
    EXPECT_EQ(el.span_count(), 1u);                               // 拒絕的呼叫不落地

    // 恰相鄰（不重疊）合法。
    EXPECT_NO_THROW(el.add_span(5, 11, "b2"));
    EXPECT_EQ(el.span_count(), 2u);
}

// --- 無命中回空：點落在無任何區段的文字上、或完全落在文字幾何外 ---
TEST(ClickableTextElement, HitSpanReturnsNulloptWhenNoSpanMatches) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hello world");
    el.add_span(0, 5, "greet");

    // 遠離所有字符幾何的點。
    EXPECT_FALSE(el.hit_span(LocalPoint{1000.0f, 1000.0f}).has_value());
    // 落在 "world" 範圍內但未標記任何區段。
    EXPECT_FALSE(el.hit_span(LocalPoint{65.0f, 5.0f}).has_value());
}

TEST(ClickableTextElement, HitSpanOnEmptyTextReturnsNullopt) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    EXPECT_EQ(el.text_length(), 0u);
    EXPECT_FALSE(el.hit_span(LocalPoint{0.0f, 0.0f}).has_value());
}

// --- set_text 重設會清空舊區段（避免舊字元範圍對應新文字語意不明）---
TEST(ClickableTextElement, SetTextClearsExistingSpans) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    el.set_text("hello world");
    el.add_span(0, 5, "greet");
    EXPECT_EQ(el.span_count(), 1u);

    el.set_text("goodbye");
    EXPECT_EQ(el.span_count(), 0u);
    EXPECT_EQ(el.text_length(), 7u);
    EXPECT_FALSE(el.hit_span(LocalPoint{5.0f, 5.0f}).has_value());
}

// --- 非法 UTF-8（沿用 E4-01 decode_utf8 驗證）不靜默 ---
TEST(ClickableTextElement, SetTextRejectsInvalidUtf8) {
    FixedFontMetrics fm = mono();
    ClickableTextElement el(fm);
    const std::string bad = "\xFF\xFE";
    EXPECT_THROW(el.set_text(bad), std::invalid_argument);
}
