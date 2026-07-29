// E4-15 就地輸入框 — 單元測試（gtest）
//
// 涵蓋：插入 / 刪除、游標移動（含邊界 no-op 與延伸選取）、選取（含反向正規化 / 清除）、
// 以 E4-01 FixedFontMetrics 排版、以 E5-13 KeyboardInputSource 注入事件驅動編輯、
// 越界操作（set_cursor / select）不靜默、render_model()（文字 + 游標 + 選取矩形）、
// 換行邊界的游標消歧義。全程不驅動任何真實鍵盤 / IME。
#include "text_input_element.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

using ds::elements::CursorMove;
using ds::elements::SelectionRange;
using ds::elements::TextInputElement;
using ds::events::Key;
using ds::events::KeyAction;
using ds::events::KeyboardInputSource;
using ds::events::KeyEvent;
using ds::events::Modifier;
using ds::render::FixedFontMetrics;

namespace {

// 等寬字型：每字元 advance=10、行高=20、ascent 預設等於行高(20)——與上游 E4-01 測試同構。
FixedFontMetrics MakeMetrics() { return FixedFontMetrics(10.0, 20.0); }

// --- 文字內容 / 編輯操作 ---

TEST(TextInputElement, DefaultStateEmptyTextCursorAtZero) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    EXPECT_EQ(el.text(), "");
    EXPECT_EQ(el.cursor(), 0u);
    EXPECT_EQ(el.length(), 0u);
    EXPECT_FALSE(el.has_selection());
}

TEST(TextInputElement, SetTextMovesCursorToEndAndClearsSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("hello");
    el.select(1, 3);
    ASSERT_TRUE(el.has_selection());

    el.set_text("hi");
    EXPECT_EQ(el.text(), "hi");
    EXPECT_EQ(el.cursor(), 2u);
    EXPECT_FALSE(el.has_selection());
}

TEST(TextInputElement, InsertAtCursorAdvancesCursor) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("ac");
    el.set_cursor(1);
    el.insert("b");

    EXPECT_EQ(el.text(), "abc");
    EXPECT_EQ(el.cursor(), 2u);
    EXPECT_FALSE(el.has_selection());
}

TEST(TextInputElement, InsertReplacesSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("abcdef");
    el.select(1, 4);  // 選取 "bcd"
    el.insert("XY");

    EXPECT_EQ(el.text(), "aXYef");
    EXPECT_EQ(el.cursor(), 3u);
    EXPECT_FALSE(el.has_selection());
}

TEST(TextInputElement, BackspaceDeletesPrecedingChar) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("abc");  // 游標於末端(3)
    el.backspace();

    EXPECT_EQ(el.text(), "ab");
    EXPECT_EQ(el.cursor(), 2u);
}

TEST(TextInputElement, BackspaceAtStartIsNoOp) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("abc");
    el.set_cursor(0);
    el.backspace();

    EXPECT_EQ(el.text(), "abc");
    EXPECT_EQ(el.cursor(), 0u);
}

TEST(TextInputElement, EraseForwardDeletesCurrentChar) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("abc");
    el.set_cursor(1);
    el.erase_forward();

    EXPECT_EQ(el.text(), "ac");
    EXPECT_EQ(el.cursor(), 1u);
}

TEST(TextInputElement, EraseForwardAtEndIsNoOp) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("abc");  // 游標於末端(3)
    el.erase_forward();

    EXPECT_EQ(el.text(), "abc");
    EXPECT_EQ(el.cursor(), 3u);
}

TEST(TextInputElement, BackspaceWithSelectionDeletesSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    el.set_text("abcdef");
    el.select(1, 4);  // 選取 "bcd"
    el.backspace();

    EXPECT_EQ(el.text(), "aef");
    EXPECT_EQ(el.cursor(), 1u);
    EXPECT_FALSE(el.has_selection());
}

// --- 游標移動 ---

TEST(TextInputElement, MoveCursorLeftRightClampsAtBoundaries) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");  // 游標於末端(2)

    el.move_cursor(CursorMove::Right);  // 已在終點：無動作
    EXPECT_EQ(el.cursor(), 2u);

    el.move_cursor(CursorMove::Left);
    EXPECT_EQ(el.cursor(), 1u);
    el.move_cursor(CursorMove::Left);
    EXPECT_EQ(el.cursor(), 0u);
    el.move_cursor(CursorMove::Left);  // 已在起點：無動作
    EXPECT_EQ(el.cursor(), 0u);
}

TEST(TextInputElement, MoveCursorHomeEnd) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("hello");
    el.set_cursor(2);

    el.move_cursor(CursorMove::Home);
    EXPECT_EQ(el.cursor(), 0u);

    el.move_cursor(CursorMove::End);
    EXPECT_EQ(el.cursor(), 5u);
}

TEST(TextInputElement, MoveCursorWithShiftExtendsSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcdef");
    el.set_cursor(2);

    el.move_cursor(CursorMove::Right, /*extend_selection=*/true);
    EXPECT_TRUE(el.has_selection());
    SelectionRange sel = el.selection();
    EXPECT_EQ(sel.begin, 2u);
    EXPECT_EQ(sel.end, 3u);

    el.move_cursor(CursorMove::Right, /*extend_selection=*/true);
    sel = el.selection();
    EXPECT_EQ(sel.begin, 2u);
    EXPECT_EQ(sel.end, 4u);
}

TEST(TextInputElement, MoveCursorWithoutShiftClearsSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcdef");
    el.set_cursor(2);
    el.move_cursor(CursorMove::Right, /*extend_selection=*/true);
    ASSERT_TRUE(el.has_selection());

    el.move_cursor(CursorMove::Left, /*extend_selection=*/false);
    EXPECT_FALSE(el.has_selection());
    EXPECT_EQ(el.cursor(), 2u);
}

// --- 選取 ---

TEST(TextInputElement, SelectSetsExplicitRangeAndHasSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcdef");

    el.select(1, 4);
    EXPECT_TRUE(el.has_selection());
    SelectionRange sel = el.selection();
    EXPECT_EQ(sel.begin, 1u);
    EXPECT_EQ(sel.end, 4u);
    EXPECT_EQ(el.cursor(), 4u);  // 游標 = 選取活動端(end)
}

TEST(TextInputElement, SelectionNormalizesReversedRange) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcdef");

    el.select(4, 2);  // 反向選取（錨點在活動端之後）
    EXPECT_TRUE(el.has_selection());
    EXPECT_EQ(el.cursor(), 2u);  // 游標 = 活動端(end) = 2

    SelectionRange sel = el.selection();
    EXPECT_EQ(sel.begin, 2u);  // 正規化後 begin<=end
    EXPECT_EQ(sel.end, 4u);
}

TEST(TextInputElement, ClearSelectionCollapsesToCursor) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcdef");
    el.select(1, 4);
    ASSERT_TRUE(el.has_selection());

    el.clear_selection();
    EXPECT_FALSE(el.has_selection());
    SelectionRange sel = el.selection();
    EXPECT_EQ(sel.begin, el.cursor());
    EXPECT_EQ(sel.end, el.cursor());
}

// --- 越界操作：不靜默 ---

TEST(TextInputElement, SetCursorOutOfRangeThrows) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");  // length()==2，合法索引 [0,2]

    EXPECT_THROW(el.set_cursor(3), std::out_of_range);
    // 擲例外後狀態不變（游標仍在插入前的末端位置）。
    EXPECT_EQ(el.cursor(), 2u);
}

TEST(TextInputElement, SelectOutOfRangeThrows) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");

    EXPECT_THROW(el.select(0, 3), std::out_of_range);
    EXPECT_THROW(el.select(5, 1), std::out_of_range);
}

TEST(TextInputElement, InsertInvalidUtf8Throws) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");

    const std::string bad(1, static_cast<char>(0xFF));
    EXPECT_THROW(el.insert(bad), std::invalid_argument);
    EXPECT_EQ(el.text(), "ab");  // 驗證失敗未變動狀態
}

TEST(TextInputElement, SetTextInvalidUtf8Throws) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    const std::string bad(1, static_cast<char>(0xFF));
    EXPECT_THROW(el.set_text(bad), std::invalid_argument);
}

// --- render_model()：以 E4-01 排版顯示文字 + 游標 + 選取 ---

TEST(TextInputElement, RenderModelProducesLayoutMatchingText) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");

    const auto model = el.render_model();
    ASSERT_EQ(model.layout.lines.size(), 1u);
    ASSERT_EQ(model.layout.glyphs.size(), 2u);
    EXPECT_EQ(model.layout.glyphs[0].codepoint, static_cast<ds::render::CodePoint>('a'));
    EXPECT_EQ(model.layout.glyphs[1].codepoint, static_cast<ds::render::CodePoint>('b'));
    EXPECT_DOUBLE_EQ(model.layout.glyphs[0].x, 0.0);
    EXPECT_DOUBLE_EQ(model.layout.glyphs[1].x, 10.0);
}

TEST(TextInputElement, RenderModelCursorPositionAfterInsert) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("hello");
    el.set_cursor(2);  // 游標於 "he|llo"

    const auto model = el.render_model();
    EXPECT_EQ(model.cursor.index, 2u);
    EXPECT_EQ(model.cursor.line, 0u);
    EXPECT_DOUBLE_EQ(model.cursor.x, 20.0);  // 兩字元 * advance(10)
    EXPECT_DOUBLE_EQ(model.cursor.y, 0.0);
    EXPECT_DOUBLE_EQ(model.cursor.baseline, 20.0);  // ascent == line_height == 20
}

TEST(TextInputElement, RenderModelCursorAtEndOfText) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");  // 游標於末端(2)

    const auto model = el.render_model();
    EXPECT_EQ(model.cursor.line, 0u);
    EXPECT_DOUBLE_EQ(model.cursor.x, 20.0);  // 行末（行寬 = 2*10）
}

TEST(TextInputElement, RenderModelEmptyTextCursorFallback) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);

    const auto model = el.render_model();
    EXPECT_TRUE(model.layout.lines.empty());
    EXPECT_EQ(model.cursor.line, 0u);
    EXPECT_DOUBLE_EQ(model.cursor.x, 0.0);
    EXPECT_DOUBLE_EQ(model.cursor.y, 0.0);
    EXPECT_DOUBLE_EQ(model.cursor.baseline, 20.0);  // 退化情形直接取 metrics.ascent()
}

TEST(TextInputElement, RenderModelSelectionRectForSimpleRange) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcd");
    el.select(1, 3);  // 選取 "bc"

    const auto model = el.render_model();
    EXPECT_TRUE(model.has_selection);
    EXPECT_EQ(model.selection.begin, 1u);
    EXPECT_EQ(model.selection.end, 3u);
    ASSERT_EQ(model.selection_rects.size(), 1u);
    const auto& rect = model.selection_rects[0];
    EXPECT_EQ(rect.line, 0u);
    EXPECT_DOUBLE_EQ(rect.x, 10.0);
    EXPECT_DOUBLE_EQ(rect.y, 0.0);
    EXPECT_DOUBLE_EQ(rect.width, 20.0);  // 2 字元 * advance(10)
    EXPECT_DOUBLE_EQ(rect.height, 20.0);  // line_height
}

TEST(TextInputElement, RenderModelNoSelectionRectsWhenCollapsed) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcd");

    const auto model = el.render_model();
    EXPECT_FALSE(model.has_selection);
    EXPECT_TRUE(model.selection_rects.empty());
}

// 換行邊界的游標消歧義：游標緊接在 '\n' 之後應歸屬新行的開頭，而非前一行的末端。
TEST(TextInputElement, RenderModelMultilineCursorPlacementAfterNewline) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab\ncd");
    el.set_cursor(3);  // 緊接在 '\n' 之後、'c' 之前

    const auto model = el.render_model();
    ASSERT_EQ(model.layout.lines.size(), 2u);
    EXPECT_EQ(model.cursor.line, 1u);
    EXPECT_DOUBLE_EQ(model.cursor.x, 0.0);      // 第二行行首
    EXPECT_DOUBLE_EQ(model.cursor.y, 20.0);     // 第二行行頂 = 1 * line_height
    EXPECT_DOUBLE_EQ(model.cursor.baseline, 40.0);  // 20 (y) + 20 (ascent)
}

TEST(TextInputElement, RenderModelMultilineCursorPlacementBeforeNewline) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab\ncd");
    el.set_cursor(2);  // 緊接在 'b' 之後、'\n' 之前 → 屬第一行末端

    const auto model = el.render_model();
    EXPECT_EQ(model.cursor.line, 0u);
    EXPECT_DOUBLE_EQ(model.cursor.x, 20.0);  // 第一行末端（行寬 = 2*10）
    EXPECT_DOUBLE_EQ(model.cursor.y, 0.0);
}

// --- E5-13 事件整合：注入式輸入驅動編輯狀態 ---

TEST(TextInputElement, AttachReceivesKeyEventsForNavigationAndEditing) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("hello");
    KeyboardInputSource source;
    el.attach(source);

    source.inject_key_press(Key::Home);
    EXPECT_EQ(el.cursor(), 0u);

    source.inject_key_press(Key::ArrowRight);
    EXPECT_EQ(el.cursor(), 1u);

    source.inject_key_press(Key::End);
    EXPECT_EQ(el.cursor(), 5u);

    source.inject_key_press(Key::Backspace);
    EXPECT_EQ(el.text(), "hell");
    EXPECT_EQ(el.cursor(), 4u);

    source.inject_key_press(Key::ArrowLeft);
    source.inject_key_press(Key::ArrowLeft);
    EXPECT_EQ(el.cursor(), 2u);

    source.inject_key_press(Key::Delete);  // "hell" 索引2為 'l'
    EXPECT_EQ(el.text(), "hel");
    EXPECT_EQ(el.cursor(), 2u);
}

TEST(TextInputElement, AttachReceivesTextInputEventsForInsertion) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    KeyboardInputSource source;
    el.attach(source);

    source.submit_text("hi");
    EXPECT_EQ(el.text(), "hi");
    EXPECT_EQ(el.cursor(), 2u);
}

TEST(TextInputElement, AttachShiftArrowExtendsSelectionViaEvents) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abcdef");
    el.set_cursor(0);
    KeyboardInputSource source;
    el.attach(source);

    source.inject_key_press(Key::ArrowRight, Modifier::Shift);
    source.inject_key_press(Key::ArrowRight, Modifier::Shift);
    EXPECT_TRUE(el.has_selection());
    SelectionRange sel = el.selection();
    EXPECT_EQ(sel.begin, 0u);
    EXPECT_EQ(sel.end, 2u);
}

TEST(TextInputElement, HandleKeyIgnoresReleaseAction) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abc");
    el.set_cursor(1);

    const KeyEvent release{Key::ArrowRight, Modifier::None, KeyAction::Release};
    el.handle_key(release);

    EXPECT_EQ(el.cursor(), 1u);  // 放開不觸發移動
}

TEST(TextInputElement, HandleKeyUnknownKeyIsIgnored) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("abc");
    el.set_cursor(1);

    const KeyEvent unrelated{Key::F1, Modifier::None, KeyAction::Press};
    el.handle_key(unrelated);

    EXPECT_EQ(el.text(), "abc");
    EXPECT_EQ(el.cursor(), 1u);
}

// --- Surface 綁定（透傳 E4-01 TextLayout） ---

TEST(TextInputElement, SurfaceSetAndGetPassesThroughToLayout) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement el(metrics);
    el.set_text("ab");

    el.set_surface("surface-1");
    EXPECT_EQ(el.surface(), "surface-1");

    const auto model = el.render_model();
    EXPECT_EQ(model.layout.surface, "surface-1");
}

}  // namespace
