// E4-16 輸入建議與補全呈現 — 單元測試（gtest）
//
// 涵蓋：設定候選清單（含空清單使彈出隱藏）、以 E4-01 FixedFontMetrics 排版候選項
// 呈現、鍵盤上下選取（含邊界安全無動作與越界索引擲例外）、accept() 套用補全到綁定的
// E4-15 TextInputElement（並使彈出關閉）、空候選清單的選取 / 套用操作不靜默擲例外、
// 越界選取索引不靜默擲例外、render_model()（含 visible / row_height / 逐項 y 偏移 /
// selected 標記）、以 E5-13 KeyboardInputSource 注入事件驅動選取 / 套用。
// 全程不驅動任何真實鍵盤 / IME。
#include "suggestion_popup_element.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using ds::elements::SelectionMove;
using ds::elements::SuggestionPopupElement;
using ds::elements::SuggestionPopupRenderModel;
using ds::elements::TextInputElement;
using ds::events::Key;
using ds::events::KeyAction;
using ds::events::KeyboardInputSource;
using ds::events::KeyEvent;
using ds::events::Modifier;
using ds::render::FixedFontMetrics;

namespace {

// 等寬字型：每字元 advance=10、行高=20、ascent 預設等於行高(20)——與上游 E4-01/E4-15
// 測試同構。
FixedFontMetrics MakeMetrics() { return FixedFontMetrics(10.0, 20.0); }

std::vector<std::string> ThreeCandidates() { return {"alpha", "beta", "gamma"}; }

}  // namespace

// --- 候選清單 / visible ---

TEST(SuggestionPopupElement, DefaultStateNotVisibleEmptySuggestions) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    EXPECT_FALSE(popup.visible());
    EXPECT_EQ(popup.count(), 0u);
    EXPECT_TRUE(popup.suggestions().empty());
}

TEST(SuggestionPopupElement, SetSuggestionsMakesVisibleAndResetsSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    EXPECT_TRUE(popup.visible());
    EXPECT_EQ(popup.count(), 3u);
    EXPECT_EQ(popup.selected_index(), 0u);
    EXPECT_EQ(popup.selected_text(), "alpha");
}

TEST(SuggestionPopupElement, SetSuggestionsResetsPriorSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(2);
    ASSERT_EQ(popup.selected_index(), 2u);

    popup.set_suggestions({"only-one"});
    EXPECT_EQ(popup.selected_index(), 0u);
    EXPECT_EQ(popup.selected_text(), "only-one");
}

TEST(SuggestionPopupElement, SetSuggestionsEmptyHidesPopup) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    ASSERT_TRUE(popup.visible());

    popup.set_suggestions({});
    EXPECT_FALSE(popup.visible());
    EXPECT_EQ(popup.count(), 0u);
}

// --- 選取：空清單不靜默 ---

TEST(SuggestionPopupElement, SelectedIndexThrowsWhenEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    EXPECT_THROW(popup.selected_index(), std::logic_error);
}

TEST(SuggestionPopupElement, SelectedTextThrowsWhenEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    EXPECT_THROW(popup.selected_text(), std::logic_error);
}

TEST(SuggestionPopupElement, MoveSelectionThrowsWhenEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    EXPECT_THROW(popup.move_selection(SelectionMove::Down), std::logic_error);
    EXPECT_THROW(popup.move_selection(SelectionMove::Up), std::logic_error);
}

TEST(SuggestionPopupElement, AcceptThrowsWhenEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    EXPECT_THROW(popup.accept(), std::logic_error);
}

// --- 選取：越界索引不靜默 ---

TEST(SuggestionPopupElement, SetSelectedIndexOutOfRangeThrowsWhenEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    EXPECT_THROW(popup.set_selected_index(0), std::out_of_range);
}

TEST(SuggestionPopupElement, SetSelectedIndexOutOfRangeThrowsWhenNonEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    EXPECT_THROW(popup.set_selected_index(3), std::out_of_range);
    // 越界擲例外後，選取狀態不應被更動。
    EXPECT_EQ(popup.selected_index(), 0u);
}

TEST(SuggestionPopupElement, SetSelectedIndexValidUpdatesSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(2);
    EXPECT_EQ(popup.selected_index(), 2u);
    EXPECT_EQ(popup.selected_text(), "gamma");
}

// --- 選取：方向移動（含邊界安全無動作） ---

TEST(SuggestionPopupElement, MoveSelectionDownAdvances) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.move_selection(SelectionMove::Down);
    EXPECT_EQ(popup.selected_index(), 1u);
    popup.move_selection(SelectionMove::Down);
    EXPECT_EQ(popup.selected_index(), 2u);
}

TEST(SuggestionPopupElement, MoveSelectionDownClampsAtLastItem) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(2);
    popup.move_selection(SelectionMove::Down);  // 已在末項：安全無動作
    EXPECT_EQ(popup.selected_index(), 2u);
}

TEST(SuggestionPopupElement, MoveSelectionUpClampsAtFirstItem) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.move_selection(SelectionMove::Up);  // 已在首項(0)：安全無動作
    EXPECT_EQ(popup.selected_index(), 0u);
}

TEST(SuggestionPopupElement, MoveSelectionUpRetreats) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(2);
    popup.move_selection(SelectionMove::Up);
    EXPECT_EQ(popup.selected_index(), 1u);
}

// --- 套用補全（accept）---

TEST(SuggestionPopupElement, AcceptAppliesSelectedTextToBoundInputAndClosesPopup) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    input.set_text("orig");
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.move_selection(SelectionMove::Down);  // 選到 "beta"

    popup.accept();

    EXPECT_EQ(input.text(), "beta");
    EXPECT_FALSE(popup.visible());
    EXPECT_EQ(popup.count(), 0u);
}

TEST(SuggestionPopupElement, AcceptMovesBoundInputCursorToEnd) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions({"hello"});
    popup.accept();

    // TextInputElement::set_text 語意：套用後游標移至文字末端、清除選取。
    EXPECT_EQ(input.cursor(), input.length());
    EXPECT_FALSE(input.has_selection());
}

// --- render_model() ---

TEST(SuggestionPopupElement, RenderModelNotVisibleWhenEmpty) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    const SuggestionPopupRenderModel model = popup.render_model();
    EXPECT_FALSE(model.visible);
    EXPECT_TRUE(model.items.empty());
}

TEST(SuggestionPopupElement, RenderModelLayoutsEachCandidateAndMarksSelected) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(1);

    const SuggestionPopupRenderModel model = popup.render_model();
    ASSERT_TRUE(model.visible);
    ASSERT_EQ(model.items.size(), 3u);
    EXPECT_EQ(model.selected_index, 1u);
    EXPECT_DOUBLE_EQ(model.row_height, 20.0);

    for (std::size_t i = 0; i < model.items.size(); ++i) {
        EXPECT_EQ(model.items[i].index, i);
        EXPECT_EQ(model.items[i].selected, i == 1u);
    }

    // "alpha" → 5 字元 × advance 10 = 寬度 50；排版本身無 z-order / 絕對座標。
    EXPECT_DOUBLE_EQ(model.items[0].layout.size.width, 50.0);
    EXPECT_EQ(model.items[1].layout.glyphs.size(), 4u);  // "beta"
}

TEST(SuggestionPopupElement, RenderModelItemYOffsetsIncrementByRowHeight) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    popup.set_suggestions(ThreeCandidates());
    const SuggestionPopupRenderModel model = popup.render_model();

    ASSERT_EQ(model.items.size(), 3u);
    EXPECT_DOUBLE_EQ(model.items[0].y, 0.0);
    EXPECT_DOUBLE_EQ(model.items[1].y, 20.0);
    EXPECT_DOUBLE_EQ(model.items[2].y, 40.0);
}

TEST(SuggestionPopupElement, RenderModelPropagatesInvalidUtf8FromLayout) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    // 單一孤立延續位元組（0x80 開頭但非合法序列起點）：非法 UTF-8。
    popup.set_suggestions({std::string(1, static_cast<char>(0x80))});
    EXPECT_THROW(popup.render_model(), std::invalid_argument);
}

// --- 鍵盤事件（直接呼叫 handle_key）---

TEST(SuggestionPopupElement, HandleKeyArrowDownMovesSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());

    popup.handle_key(KeyEvent{Key::ArrowDown, Modifier::None, KeyAction::Press});
    EXPECT_EQ(popup.selected_index(), 1u);
}

TEST(SuggestionPopupElement, HandleKeyArrowUpMovesSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(2);

    popup.handle_key(KeyEvent{Key::ArrowUp, Modifier::None, KeyAction::Press});
    EXPECT_EQ(popup.selected_index(), 1u);
}

TEST(SuggestionPopupElement, HandleKeyTabAcceptsSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());

    popup.handle_key(KeyEvent{Key::Tab, Modifier::None, KeyAction::Press});
    EXPECT_EQ(input.text(), "alpha");
    EXPECT_FALSE(popup.visible());
}

TEST(SuggestionPopupElement, HandleKeyEnterAcceptsSelection) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());
    popup.set_selected_index(2);

    popup.handle_key(KeyEvent{Key::Enter, Modifier::None, KeyAction::Press});
    EXPECT_EQ(input.text(), "gamma");
    EXPECT_FALSE(popup.visible());
}

TEST(SuggestionPopupElement, HandleKeyIgnoredWhenNotVisibleDoesNotThrow) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    // 候選清單為空（未顯示）：導覽 / 套用鍵安全略過，不擲例外。
    EXPECT_NO_THROW(
        popup.handle_key(KeyEvent{Key::ArrowDown, Modifier::None, KeyAction::Press}));
    EXPECT_NO_THROW(popup.handle_key(KeyEvent{Key::Enter, Modifier::None, KeyAction::Press}));
    EXPECT_FALSE(popup.visible());
}

TEST(SuggestionPopupElement, HandleKeyIgnoresReleaseAction) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());

    popup.handle_key(KeyEvent{Key::ArrowDown, Modifier::None, KeyAction::Release});
    EXPECT_EQ(popup.selected_index(), 0u);
}

TEST(SuggestionPopupElement, HandleKeyIgnoresUnknownKey) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());

    popup.handle_key(KeyEvent{Key::A, Modifier::None, KeyAction::Press});
    EXPECT_EQ(popup.selected_index(), 0u);
    EXPECT_TRUE(popup.visible());
}

// --- 鍵盤事件（透過 E5-13 KeyboardInputSource 注入）---

TEST(SuggestionPopupElement, AttachSubscribesAndRespondsToInjectedKeys) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());

    KeyboardInputSource source;
    const auto sub_id = popup.attach(source);
    EXPECT_NE(sub_id, 0u);

    source.inject_key_press(Key::ArrowDown);
    EXPECT_EQ(popup.selected_index(), 1u);

    source.inject_key_press(Key::Tab);
    EXPECT_EQ(input.text(), "beta");
    EXPECT_FALSE(popup.visible());
}

TEST(SuggestionPopupElement, UnsubscribeStopsFurtherDispatch) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);
    popup.set_suggestions(ThreeCandidates());

    KeyboardInputSource source;
    const auto sub_id = popup.attach(source);
    ASSERT_TRUE(source.unsubscribe(sub_id));

    source.inject_key_press(Key::ArrowDown);
    EXPECT_EQ(popup.selected_index(), 0u);  // 已取消訂閱：不再收到事件。
}

// --- surface 透傳（E4-01）---

TEST(SuggestionPopupElement, SetSurfacePropagatesToItemLayouts) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement input(metrics);
    SuggestionPopupElement popup(metrics, input);

    ds::kernel::SurfaceId surface_id("popup-surface");
    popup.set_surface(surface_id);
    EXPECT_EQ(popup.surface(), surface_id);

    popup.set_suggestions({"x"});
    const SuggestionPopupRenderModel model = popup.render_model();
    ASSERT_EQ(model.items.size(), 1u);
    EXPECT_EQ(model.items[0].layout.surface, surface_id);
}
