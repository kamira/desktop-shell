// E4-16 輸入建議與補全呈現 — 實作
//
// 純邏輯：候選清單 + 選取索引狀態，候選項排版經注入的 E4-01 TextLayout，套用經綁定的
// E4-15 TextInputElement，鍵盤事件經（可選）注入的 E5-13 KeyboardInputSource。
// 無任何真實鍵盤 / IME / 平台分支；不做候選比對 / 過濾（相位 1 由呼叫端注入清單）。
#include "suggestion_popup_element.hpp"

#include <stdexcept>
#include <utility>

namespace ds::elements {

SuggestionPopupElement::SuggestionPopupElement(const ds::render::FontMetrics& metrics,
                                               TextInputElement& target)
    : metrics_(metrics), layout_(metrics), target_(target) {}

void SuggestionPopupElement::set_suggestions(std::vector<std::string> suggestions) {
    suggestions_ = std::move(suggestions);
    selected_index_ = 0;
}

std::size_t SuggestionPopupElement::selected_index() const {
    if (suggestions_.empty()) {
        throw std::logic_error(
            "SuggestionPopupElement::selected_index: 候選清單為空，無選取可言");
    }
    return selected_index_;
}

const std::string& SuggestionPopupElement::selected_text() const {
    // selected_index() 已對空清單擲例外，故 .at() 於此必為合法索引。
    return suggestions_.at(selected_index());
}

void SuggestionPopupElement::set_selected_index(std::size_t index) {
    if (index >= suggestions_.size()) {
        throw std::out_of_range(
            "SuggestionPopupElement::set_selected_index: 索引超出候選清單範圍");
    }
    selected_index_ = index;
}

void SuggestionPopupElement::move_selection(SelectionMove move) {
    if (suggestions_.empty()) {
        throw std::logic_error(
            "SuggestionPopupElement::move_selection: 候選清單為空，無法移動選取");
    }
    switch (move) {
        case SelectionMove::Up:
            if (selected_index_ > 0) {
                --selected_index_;
            }
            break;
        case SelectionMove::Down:
            if (selected_index_ + 1 < suggestions_.size()) {
                ++selected_index_;
            }
            break;
    }
}

void SuggestionPopupElement::accept() {
    if (suggestions_.empty()) {
        throw std::logic_error("SuggestionPopupElement::accept: 候選清單為空，無可套用項");
    }
    target_.set_text(suggestions_[selected_index_]);
    suggestions_.clear();
    selected_index_ = 0;
}

ds::events::SubscriptionId SuggestionPopupElement::attach(
    ds::events::KeyboardInputSource& source) {
    return source.subscribe_key(
        [this](const ds::events::KeyEvent& event) { this->handle_key(event); });
}

void SuggestionPopupElement::handle_key(const ds::events::KeyEvent& event) {
    if (event.action != ds::events::KeyAction::Press) {
        return;  // 相位 1 只在按下時動作；放開不觸發選取 / 套用。
    }
    if (!visible()) {
        return;  // 彈出未顯示（候選清單為空）：導覽 / 套用鍵安全略過（見標頭說明）。
    }
    using ds::events::Key;
    switch (event.key) {
        case Key::ArrowUp:
            move_selection(SelectionMove::Up);
            break;
        case Key::ArrowDown:
            move_selection(SelectionMove::Down);
            break;
        case Key::Tab:
        case Key::Enter:
            accept();
            break;
        default:
            break;  // 其餘鍵忽略（相位 1 僅導覽 + 套用鍵有轉譯語意）。
    }
}

SuggestionPopupRenderModel SuggestionPopupElement::render_model() const {
    SuggestionPopupRenderModel model;
    model.visible = visible();
    model.row_height = metrics_.line_height();
    if (!model.visible) {
        return model;  // 空清單：合法的「無建議可顯示」狀態，非例外（見標頭說明）。
    }
    model.selected_index = selected_index_;

    const ds::render::LayoutConstraints item_constraints = single_line_constraints();
    model.items.reserve(suggestions_.size());
    for (std::size_t i = 0; i < suggestions_.size(); ++i) {
        SuggestionItemRenderModel item;
        item.index = i;
        // 非法 UTF-8 候選文字 → 由上游 decode_utf8 擲 std::invalid_argument（不靜默；
        // 不重複驗證）。
        item.layout = layout_.layout(suggestions_[i], item_constraints);
        item.y = static_cast<double>(i) * model.row_height;
        item.selected = (i == selected_index_);
        model.items.push_back(std::move(item));
    }
    return model;
}

}  // namespace ds::elements
