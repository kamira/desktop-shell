// E4-14 長文捲動與分頁 — gtest 測試
//
// 涵蓋：捲動 offset、可見行範圍、分頁 next/prev、捲動越界夾限、內容短於視窗、
// visible_range、render_model（含捲動位置指示 / 分頁指示）、無效視窗處理不靜默。
#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "scroll_view.hpp"
#include "text_layout.hpp"

using ds::elements::RenderModel;
using ds::elements::ScrollView;
using ds::elements::VisibleRange;

namespace {

// 便利：以真實 E4-01 TextLayout 產生剛好 n 行的排版結果（每行一段文字，無自動換行）。
// n == 0 → 空字串 → 空結果（0 行）。
ds::render::LayoutResult make_content(std::size_t n) {
    ds::render::FixedFontMetrics fm(10.0, 20.0, 16.0);
    ds::render::TextLayout tl(fm);
    std::string text;
    for (std::size_t i = 0; i < n; ++i) {
        if (i != 0) {
            text += "\n";
        }
        text += "line";
    }
    return tl.layout(text, ds::render::LayoutConstraints{});
}

}  // namespace

// --- 捲動 offset ---
TEST(E4_14_Scroll, ScrollByAdvancesOffset) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    EXPECT_EQ(sv.offset_lines(), 0u);
    sv.scroll_by(2);
    EXPECT_EQ(sv.offset_lines(), 2u);
    sv.scroll_by(1);
    EXPECT_EQ(sv.offset_lines(), 3u);
}

TEST(E4_14_Scroll, ScrollByNegativeMovesBack) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(5);
    sv.scroll_by(-2);
    EXPECT_EQ(sv.offset_lines(), 3u);
}

TEST(E4_14_Scroll, ScrollToSetsAbsoluteOffset) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(4);
    EXPECT_EQ(sv.offset_lines(), 4u);
}

TEST(E4_14_Scroll, MaxOffsetLinesIsContentMinusViewport) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    EXPECT_EQ(sv.max_offset_lines(), 7u);  // 10 - 3
}

// --- 捲動越界夾限（非錯誤）---
TEST(E4_14_ClampScroll, ScrollByPastBottomClampsToMaxOffset) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_by(1000);
    EXPECT_EQ(sv.offset_lines(), sv.max_offset_lines());
    EXPECT_EQ(sv.offset_lines(), 7u);
}

TEST(E4_14_ClampScroll, ScrollByPastTopClampsToZero) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(2);
    sv.scroll_by(-1000);
    EXPECT_EQ(sv.offset_lines(), 0u);
}

TEST(E4_14_ClampScroll, ScrollToPastBottomClampsToMaxOffset) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(999);
    EXPECT_EQ(sv.offset_lines(), 7u);
}

TEST(E4_14_ClampScroll, ScrollToHugeValueDoesNotUnderflow) {
    // 以極大 scroll_by 負值測試不會整數下溢：夾限於 0，而非環繞成巨大正值。
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_by(-1000000);
    EXPECT_EQ(sv.offset_lines(), 0u);
}

// --- 分頁 next / prev ---
TEST(E4_14_Page, PageNextAdvancesByViewportLines) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.page_next();
    EXPECT_EQ(sv.offset_lines(), 3u);
    sv.page_next();
    EXPECT_EQ(sv.offset_lines(), 6u);
}

TEST(E4_14_Page, PageNextClampsAtLastPage) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.page_next();  // 3
    sv.page_next();  // 6
    sv.page_next();  // 9 -> clamp 7 (max_offset)
    EXPECT_EQ(sv.offset_lines(), 7u);
    sv.page_next();  // 已在底部，仍保持 7
    EXPECT_EQ(sv.offset_lines(), 7u);
}

TEST(E4_14_Page, PagePrevMovesBackByViewportLines) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(6);
    sv.page_prev();
    EXPECT_EQ(sv.offset_lines(), 3u);
}

TEST(E4_14_Page, PagePrevClampsAtFirstPage) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.page_prev();
    EXPECT_EQ(sv.offset_lines(), 0u);
}

TEST(E4_14_Page, RenderModelPageIndexAndCount) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    // content=10, viewport=3 -> page_count = ceil(10/3) = 4
    RenderModel m0 = sv.render_model();
    EXPECT_EQ(m0.page_count, 4u);
    EXPECT_EQ(m0.page_index, 0u);

    sv.page_next();  // offset=3
    RenderModel m1 = sv.render_model();
    EXPECT_EQ(m1.page_index, 1u);

    sv.page_next();  // offset=6
    RenderModel m2 = sv.render_model();
    EXPECT_EQ(m2.page_index, 2u);

    sv.page_next();  // offset=9 clamp -> 7 (max_offset, 觸底 -> 最後一頁)
    RenderModel m3 = sv.render_model();
    EXPECT_EQ(sv.offset_lines(), 7u);
    EXPECT_EQ(m3.page_index, 3u);  // page_count - 1
}

// --- 內容短於視窗 ---
TEST(E4_14_ShortContent, MaxOffsetIsZeroWhenContentFitsViewport) {
    ScrollView sv;
    sv.set_content(make_content(2));
    sv.set_viewport_lines(5);
    EXPECT_EQ(sv.max_offset_lines(), 0u);
}

TEST(E4_14_ShortContent, ScrollByStaysAtZero) {
    ScrollView sv;
    sv.set_content(make_content(2));
    sv.set_viewport_lines(5);
    sv.scroll_by(10);
    EXPECT_EQ(sv.offset_lines(), 0u);
}

TEST(E4_14_ShortContent, VisibleRangeCoversAllContentLines) {
    ScrollView sv;
    sv.set_content(make_content(2));
    sv.set_viewport_lines(5);
    VisibleRange r = sv.visible_range();
    EXPECT_EQ(r.begin, 0u);
    EXPECT_EQ(r.end, 2u);
}

TEST(E4_14_ShortContent, RenderModelSinglePage) {
    ScrollView sv;
    sv.set_content(make_content(2));
    sv.set_viewport_lines(5);
    RenderModel m = sv.render_model();
    EXPECT_EQ(m.page_count, 1u);
    EXPECT_EQ(m.page_index, 0u);
    EXPECT_TRUE(m.position.at_top);
    EXPECT_TRUE(m.position.at_bottom);
}

// --- 空內容 ---
TEST(E4_14_EmptyContent, VisibleRangeIsEmpty) {
    ScrollView sv;
    sv.set_content(make_content(0));
    sv.set_viewport_lines(4);
    VisibleRange r = sv.visible_range();
    EXPECT_EQ(r.begin, 0u);
    EXPECT_EQ(r.end, 0u);
}

TEST(E4_14_EmptyContent, RenderModelHasNoLinesAndNoPages) {
    ScrollView sv;
    sv.set_content(make_content(0));
    sv.set_viewport_lines(4);
    RenderModel m = sv.render_model();
    EXPECT_TRUE(m.lines.empty());
    EXPECT_EQ(m.page_count, 0u);
    EXPECT_EQ(m.page_index, 0u);
    EXPECT_EQ(m.content_lines, 0u);
}

// --- visible_range() ---
TEST(E4_14_VisibleRange, MidScrollRangeIsOffsetPlusViewport) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(4);
    VisibleRange r = sv.visible_range();
    EXPECT_EQ(r.begin, 4u);
    EXPECT_EQ(r.end, 7u);
}

TEST(E4_14_VisibleRange, RangeClampsToContentLinesAtBottom) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(7);  // max offset
    VisibleRange r = sv.visible_range();
    EXPECT_EQ(r.begin, 7u);
    EXPECT_EQ(r.end, 10u);  // 不超過 content_lines
}

// --- render_model() ---
TEST(E4_14_RenderModel, VisibleLinesMapContentToViewportOffsets) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(4);
    RenderModel m = sv.render_model();
    ASSERT_EQ(m.lines.size(), 3u);
    EXPECT_EQ(m.lines[0].content_line, 4u);
    EXPECT_EQ(m.lines[0].viewport_line, 0u);
    EXPECT_EQ(m.lines[1].content_line, 5u);
    EXPECT_EQ(m.lines[1].viewport_line, 1u);
    EXPECT_EQ(m.lines[2].content_line, 6u);
    EXPECT_EQ(m.lines[2].viewport_line, 2u);
}

TEST(E4_14_RenderModel, ScrollPositionRatioAndFlags) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    RenderModel m0 = sv.render_model();
    EXPECT_DOUBLE_EQ(m0.position.ratio, 0.0);
    EXPECT_TRUE(m0.position.at_top);
    EXPECT_FALSE(m0.position.at_bottom);

    sv.scroll_to(7);  // max_offset
    RenderModel m1 = sv.render_model();
    EXPECT_DOUBLE_EQ(m1.position.ratio, 1.0);
    EXPECT_FALSE(m1.position.at_top);
    EXPECT_TRUE(m1.position.at_bottom);

    sv.scroll_to(3);  // 中間：3/7
    RenderModel m2 = sv.render_model();
    EXPECT_NEAR(m2.position.ratio, 3.0 / 7.0, 1e-12);
    EXPECT_FALSE(m2.position.at_top);
    EXPECT_FALSE(m2.position.at_bottom);
}

TEST(E4_14_RenderModel, ReportsViewportAndContentLineCounts) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    RenderModel m = sv.render_model();
    EXPECT_EQ(m.viewport_lines, 3u);
    EXPECT_EQ(m.content_lines, 10u);
}

// --- set_content 重設捲動偏移 ---
TEST(E4_14_SetContent, RebindingContentResetsOffset) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(5);
    EXPECT_EQ(sv.offset_lines(), 5u);
    sv.set_content(make_content(4));  // 換內容
    EXPECT_EQ(sv.offset_lines(), 0u);
    EXPECT_EQ(sv.content_lines(), 4u);
}

// --- 變更 viewport 重新夾限（非重設為 0）---
TEST(E4_14_SetViewport, ShrinkingContentRangeReclampsOffset) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(7);  // max offset for viewport=3
    sv.set_viewport_lines(6);  // max_offset now 10-6=4 < 7 -> 需重新夾限
    EXPECT_EQ(sv.offset_lines(), 4u);
}

TEST(E4_14_SetViewport, GrowingViewportKeepsOffsetIfStillValid) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    sv.scroll_to(2);
    sv.set_viewport_lines(4);  // max_offset now 6, 2 仍合法
    EXPECT_EQ(sv.offset_lines(), 2u);
}

// --- 無效視窗處理不靜默 ---
TEST(E4_14_InvalidViewport, SetViewportZeroThrows) {
    ScrollView sv;
    sv.set_content(make_content(10));
    EXPECT_THROW(sv.set_viewport_lines(0), std::invalid_argument);
}

TEST(E4_14_InvalidViewport, ScrollBeforeViewportSetThrows) {
    ScrollView sv;
    sv.set_content(make_content(10));
    EXPECT_THROW(sv.scroll_by(1), std::invalid_argument);
    EXPECT_THROW(sv.scroll_to(1), std::invalid_argument);
    EXPECT_THROW(sv.page_next(), std::invalid_argument);
    EXPECT_THROW(sv.page_prev(), std::invalid_argument);
    EXPECT_THROW(sv.visible_range(), std::invalid_argument);
    EXPECT_THROW(sv.render_model(), std::invalid_argument);
    EXPECT_THROW(sv.max_offset_lines(), std::invalid_argument);
}

TEST(E4_14_InvalidViewport, ValidAfterSettingPositiveViewport) {
    ScrollView sv;
    sv.set_content(make_content(10));
    sv.set_viewport_lines(3);
    EXPECT_NO_THROW(sv.scroll_by(1));
    EXPECT_NO_THROW(sv.visible_range());
    EXPECT_NO_THROW(sv.render_model());
}
