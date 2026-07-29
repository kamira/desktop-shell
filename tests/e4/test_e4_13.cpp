// E4-13 文字流內嵌物件 — 單元測試（gtest）
//
// 涵蓋：文字 + 內嵌物件混排、物件佔一個排版位、換行對齊（含物件參與換行判斷）、以 E4-01
// FontMetrics/LayoutConstraints 排版、以 E4-02 ImageRenderModel 顯示物件、多物件、無效物件
// （無來源）、無效尺寸（非正/非有限）、render_model 內容、NFR-02（相對佈局、無絕對座標/
// 無數字 z-order）、非法 UTF-8 不靜默。
#include "inline_flow_element.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

using ds::elements::FlowGlyph;
using ds::elements::FlowObject;
using ds::elements::ImageDimensions;
using ds::elements::ImageElement;
using ds::elements::ImageRenderModel;
using ds::elements::InlineFlowElement;
using ds::elements::InlineFlowResult;
using ds::elements::InlineFlowStatus;
using ds::elements::MemoryImageSource;
using ds::render::FixedFontMetrics;
using ds::render::LayoutConstraints;
using ds::render::Size;
using ds::render::TextAlign;
using ds::render::WrapMode;

namespace {

// 便利：等寬度量 advance=10、行高=20、ascent=16（同 E4-01 測試慣例）。
FixedFontMetrics mono() { return FixedFontMetrics(10.0, 20.0, 16.0); }

// 便利：一個「已成功載入來源」的 E4-02 ImageRenderModel（emoji/圖示常見尺寸）。
ImageRenderModel make_valid_image(const std::string& ref = "res://emoji_smile") {
    ImageElement el;
    MemoryImageSource src(ref, ImageDimensions{32, 32});
    EXPECT_EQ(el.set_source(src), ds::elements::ImageStatus::Ok);
    return el.render_model();
}

// 便利：一個「未載入來源」的無效 ImageRenderModel（has_source == false）。
ImageRenderModel make_invalid_image() {
    ImageElement el;  // 從未 set_source
    return el.render_model();
}

}  // namespace

// --- 文字 + 內嵌物件混排 -------------------------------------------------------

TEST(E4_13_MixedFlow, TextOnlyProducesGlyphsNoObjects) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("abc"), InlineFlowStatus::Ok);

    InlineFlowResult r = flow.render_model();
    EXPECT_EQ(r.glyphs.size(), 3u);
    EXPECT_EQ(r.objects.size(), 0u);
    EXPECT_EQ(r.lines.size(), 1u);
}

TEST(E4_13_MixedFlow, TextThenObjectThenTextInterleavesInOrder) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("hi "), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{16.0, 16.0}), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_text(" bye"), InlineFlowStatus::Ok);

    InlineFlowResult r = flow.render_model();
    // "hi" (2 chars) + 空白 + 物件 + 空白 + "bye" (3 chars) —— 全部在無界寬度下同一行。
    EXPECT_EQ(r.lines.size(), 1u);
    EXPECT_EQ(r.glyphs.size(), 2u + 1u + 1u + 3u);  // h,i,' ',' ',b,y,e = 7 個文字 token
    ASSERT_EQ(r.objects.size(), 1u);
    // 物件的 x 應落在 "hi " 之後、" bye" 之前（單調遞增於文字之間）。
    EXPECT_GT(r.objects[0].x, 0.0);
}

TEST(E4_13_MixedFlow, EmptyFlowProducesEmptyResult) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    EXPECT_TRUE(flow.empty());
    InlineFlowResult r = flow.render_model();
    EXPECT_EQ(r.lines.size(), 0u);
    EXPECT_EQ(r.glyphs.size(), 0u);
    EXPECT_EQ(r.objects.size(), 0u);
    EXPECT_NEAR(r.size.width, 0.0, 1e-9);
    EXPECT_NEAR(r.size.height, 0.0, 1e-9);
}

// --- 物件佔一個排版位 -----------------------------------------------------------

TEST(E4_13_ObjectSlot, ObjectAdvanceEqualsGivenSlotWidth) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("a"), InlineFlowStatus::Ok);  // 1 字, advance=10, x=0
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{24.0, 18.0}), InlineFlowStatus::Ok);

    InlineFlowResult r = flow.render_model();
    ASSERT_EQ(r.glyphs.size(), 1u);
    ASSERT_EQ(r.objects.size(), 1u);
    // 物件緊接在 "a" 之後：x = 10（"a" 的 advance），佔用寬度 = 給定的 24。
    EXPECT_NEAR(r.objects[0].x, 10.0, 1e-9);
    EXPECT_NEAR(r.objects[0].width, 24.0, 1e-9);
    EXPECT_NEAR(r.objects[0].height, 18.0, 1e-9);
}

TEST(E4_13_ObjectSlot, LineHeightGrowsToFitTallerObject) {
    FixedFontMetrics fm = mono();  // 基礎行高 20
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("x"), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{40.0, 40.0}), InlineFlowStatus::Ok);  // 高於行高

    InlineFlowResult r = flow.render_model();
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_NEAR(r.lines[0].height, 40.0, 1e-9);  // 行高被拉高至物件高度
    EXPECT_NEAR(r.size.height, 40.0, 1e-9);
}

TEST(E4_13_ObjectSlot, LineHeightStaysBaseWhenObjectShorter) {
    FixedFontMetrics fm = mono();  // 基礎行高 20
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{8.0, 8.0}), InlineFlowStatus::Ok);

    InlineFlowResult r = flow.render_model();
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_NEAR(r.lines[0].height, 20.0, 1e-9);  // 物件較矮，行高維持基礎值
}

// --- 換行對齊（含物件參與換行判斷）---------------------------------------------

TEST(E4_13_Wrap, ObjectWrapsToNextLineWithText) {
    FixedFontMetrics fm = mono();  // advance=10 每字
    InlineFlowElement flow(fm);
    // "abcd" (40) + 空白(10) + 物件(20) 累積 70 > max_width(50) → 物件連同前導空白換到下一行。
    ASSERT_EQ(flow.add_text("abcd "), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{20.0, 10.0}), InlineFlowStatus::Ok);

    LayoutConstraints c;
    c.max_width = 50.0;
    c.wrap = WrapMode::Word;
    InlineFlowResult r = flow.render_model(c);

    ASSERT_EQ(r.lines.size(), 2u);
    // 第一行只有文字（"abcd"），第二行只有物件。
    EXPECT_EQ(r.glyphs.size(), 4u);
    for (const FlowGlyph& g : r.glyphs) {
        EXPECT_EQ(g.line, 0u);
    }
    ASSERT_EQ(r.objects.size(), 1u);
    EXPECT_EQ(r.objects[0].line, 1u);
    EXPECT_NEAR(r.objects[0].x, 0.0, 1e-9);  // 換行後另起一行，行首偏移為 0
}

TEST(E4_13_Wrap, ObjectStaysOnSameLineWhenItFits) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("ab "), InlineFlowStatus::Ok);  // 20 + 10(space) = 30
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{20.0, 10.0}), InlineFlowStatus::Ok);  // +20 = 50

    LayoutConstraints c;
    c.max_width = 60.0;  // 剛好放得下
    c.wrap = WrapMode::Word;
    InlineFlowResult r = flow.render_model(c);

    ASSERT_EQ(r.lines.size(), 1u);
    ASSERT_EQ(r.objects.size(), 1u);
    EXPECT_EQ(r.objects[0].line, 0u);
}

TEST(E4_13_Wrap, TextAlignCenterOffsetsLineWithObject) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("a"), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{10.0, 10.0}), InlineFlowStatus::Ok);

    LayoutConstraints c;
    c.max_width = 100.0;
    c.align = TextAlign::Center;
    InlineFlowResult r = flow.render_model(c);

    ASSERT_EQ(r.lines.size(), 1u);
    // 行寬 = 10(字) + 10(物件) = 20；置中留白 = (100-20)/2 = 40。
    EXPECT_NEAR(r.lines[0].x, 40.0, 1e-9);
    ASSERT_EQ(r.glyphs.size(), 1u);
    EXPECT_NEAR(r.glyphs[0].x, 40.0, 1e-9);
    ASSERT_EQ(r.objects.size(), 1u);
    EXPECT_NEAR(r.objects[0].x, 50.0, 1e-9);
}

// --- 以 E4-01 排版：重用 FontMetrics / LayoutConstraints ------------------------

TEST(E4_13_UsesE401, MeasureUsesInjectedFontMetricsAdvance) {
    class ProportionalMetrics : public ds::render::FontMetrics {
    public:
        double advance(ds::render::CodePoint cp) const override {
            return cp == U'i' ? 5.0 : 10.0;
        }
        double line_height() const override { return 20.0; }
    };
    ProportionalMetrics pm;
    InlineFlowElement flow(pm);
    ASSERT_EQ(flow.add_text("ii"), InlineFlowStatus::Ok);  // 2 個窄字元
    InlineFlowResult r = flow.render_model();
    ASSERT_EQ(r.glyphs.size(), 2u);
    EXPECT_NEAR(r.glyphs[0].advance, 5.0, 1e-9);
    EXPECT_NEAR(r.glyphs[1].x, 5.0, 1e-9);
    EXPECT_NEAR(r.size.width, 10.0, 1e-9);
}

TEST(E4_13_UsesE401, RespectsMaxLinesTruncation) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("line1\nline2\nline3"), InlineFlowStatus::Ok);

    LayoutConstraints c;
    c.max_lines = 2;
    InlineFlowResult r = flow.render_model(c);
    EXPECT_EQ(r.lines.size(), 2u);
    EXPECT_TRUE(r.truncated);
}

TEST(E4_13_UsesE401, InvalidConstraintsThrow) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("a"), InlineFlowStatus::Ok);

    LayoutConstraints c;
    c.max_width = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(flow.render_model(c), std::invalid_argument);
}

// --- 以 E4-02 顯示內嵌圖：render_model 攜帶完整 ImageRenderModel -----------------

TEST(E4_13_UsesE402, ObjectCarriesFullImageRenderModel) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ImageRenderModel img = make_valid_image("res://icon_star");
    ASSERT_EQ(flow.add_inline_object(img, Size{12.0, 12.0}), InlineFlowStatus::Ok);

    InlineFlowResult r = flow.render_model();
    ASSERT_EQ(r.objects.size(), 1u);
    EXPECT_TRUE(r.objects[0].image.has_source);
    EXPECT_EQ(r.objects[0].image.source_reference, "res://icon_star");
    EXPECT_EQ(r.objects[0].image.source_dimensions.width, 32);
    EXPECT_EQ(r.objects[0].image.source_dimensions.height, 32);
}

// --- 多物件 ---------------------------------------------------------------------

TEST(E4_13_MultipleObjects, SeveralObjectsAllAppearInOrder) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_inline_object(make_valid_image("res://a"), Size{10.0, 10.0}),
              InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_text(" "), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image("res://b"), Size{10.0, 10.0}),
              InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_text(" "), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image("res://c"), Size{10.0, 10.0}),
              InlineFlowStatus::Ok);

    EXPECT_EQ(flow.object_count(), 3u);
    InlineFlowResult r = flow.render_model();
    ASSERT_EQ(r.objects.size(), 3u);
    EXPECT_EQ(r.objects[0].image.source_reference, "res://a");
    EXPECT_EQ(r.objects[1].image.source_reference, "res://b");
    EXPECT_EQ(r.objects[2].image.source_reference, "res://c");
    EXPECT_LT(r.objects[0].x, r.objects[1].x);
    EXPECT_LT(r.objects[1].x, r.objects[2].x);
}

// --- 無效物件 / 尺寸：不靜默 ------------------------------------------------------

TEST(E4_13_InvalidInput, RejectsObjectWithoutSource) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    EXPECT_EQ(flow.add_inline_object(make_invalid_image(), Size{10.0, 10.0}),
              InlineFlowStatus::Invalid);
    EXPECT_EQ(flow.object_count(), 0u);
    EXPECT_TRUE(flow.empty());  // 未套用，流仍為空
}

TEST(E4_13_InvalidInput, RejectsNonPositiveSize) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    EXPECT_EQ(flow.add_inline_object(make_valid_image(), Size{0.0, 10.0}),
              InlineFlowStatus::Invalid);
    EXPECT_EQ(flow.add_inline_object(make_valid_image(), Size{10.0, 0.0}),
              InlineFlowStatus::Invalid);
    EXPECT_EQ(flow.add_inline_object(make_valid_image(), Size{-5.0, 10.0}),
              InlineFlowStatus::Invalid);
    EXPECT_EQ(flow.object_count(), 0u);
}

TEST(E4_13_InvalidInput, RejectsNonFiniteSize) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(flow.add_inline_object(make_valid_image(), Size{inf, 10.0}),
              InlineFlowStatus::Invalid);
    EXPECT_EQ(flow.add_inline_object(make_valid_image(), Size{10.0, nan}),
              InlineFlowStatus::Invalid);
    EXPECT_EQ(flow.object_count(), 0u);
}

TEST(E4_13_InvalidInput, InvalidUtf8TextThrows) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    const std::string bad = "\xFF\xFE";
    EXPECT_THROW(flow.add_text(bad), std::invalid_argument);
}

// --- render_model 一般行為 --------------------------------------------------------

TEST(E4_13_RenderModel, IsPureQueryDoesNotMutateState) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("abc"), InlineFlowStatus::Ok);

    InlineFlowResult r1 = flow.render_model();
    InlineFlowResult r2 = flow.render_model();
    EXPECT_EQ(r1.glyphs.size(), r2.glyphs.size());
    EXPECT_EQ(flow.object_count(), 0u);
    EXPECT_FALSE(flow.empty());
}

TEST(E4_13_RenderModel, ClearResetsFlowToEmpty) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("abc"), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{10.0, 10.0}), InlineFlowStatus::Ok);
    flow.clear();
    EXPECT_TRUE(flow.empty());
    EXPECT_EQ(flow.object_count(), 0u);
    InlineFlowResult r = flow.render_model();
    EXPECT_EQ(r.lines.size(), 0u);
}

// --- NFR-02：相對佈局，無絕對座標 / 無數字 z-order -------------------------------

TEST(E4_13_NFR02, SurfaceIsNamedNotNumeric) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm, ds::kernel::SurfaceId("panel://chat_log"));
    ASSERT_EQ(flow.add_text("hi"), InlineFlowStatus::Ok);
    InlineFlowResult r = flow.render_model();
    EXPECT_EQ(r.surface, ds::kernel::SurfaceId("panel://chat_log"));
}

TEST(E4_13_NFR02, PositionsAreRelativeOffsetsFromBoxOrigin) {
    FixedFontMetrics fm = mono();
    InlineFlowElement flow(fm);
    ASSERT_EQ(flow.add_text("a"), InlineFlowStatus::Ok);
    ASSERT_EQ(flow.add_inline_object(make_valid_image(), Size{10.0, 10.0}), InlineFlowStatus::Ok);

    InlineFlowResult r = flow.render_model();
    // 第一個字符固定於行首相對偏移 0（左對齊），非任何螢幕絕對座標。
    ASSERT_EQ(r.glyphs.size(), 1u);
    EXPECT_NEAR(r.glyphs[0].x, 0.0, 1e-9);
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_NEAR(r.lines[0].y, 0.0, 1e-9);  // 第一行頂端相對偏移 = 0
}

