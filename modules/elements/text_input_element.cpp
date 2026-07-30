// E4-15 就地輸入框 — 實作
//
// 純邏輯：文字編輯狀態機（codepoint 序列 + 游標 + 選取），排版經注入的 E4-01 TextLayout，
// 事件經注入的 E5-13 KeyboardInputSource。無任何真實鍵盤 / IME / 平台分支。
#include "text_input_element.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ds::elements {

namespace {

using ds::render::CodePoint;

constexpr CodePoint kCR = 0x0D;  // '\r'
constexpr CodePoint kLF = 0x0A;  // '\n'

// 將單一合法 codepoint 編碼為 UTF-8 位元組序列，附加於 out。
// 前置：cp 為合法碼位（[0,0x10FFFF]、非代理區）——本函式只處理進入 codepoints_ 的值，
// 而 codepoints_ 只經由上游 `decode_utf8`（已驗證合法性）寫入，故此處不重覆驗證。
void append_utf8(std::string& out, CodePoint cp) {
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

std::string encode_utf8(const std::vector<CodePoint>& cps) {
    std::string out;
    out.reserve(cps.size());
    for (CodePoint cp : cps) {
        append_utf8(out, cp);
    }
    return out;
}

// E4-01 排版以 '\n' 分段、丟棄 '\r'，且兩者皆不產生字符(glyph)。故給定一個原始文字
// codepoint 索引，其「之前有幾個字符實際產生 glyph」= 掃過該範圍、跳過 CR/LF 後的計數
// ——此計數即該游標位置對應的**全域 glyph 索引**（在 WrapMode::None 下精確；見標頭「已知簡化」）。
std::size_t glyph_index_before(const std::vector<CodePoint>& cps, std::size_t index) {
    std::size_t count = 0;
    const std::size_t n = std::min(index, cps.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (cps[i] != kCR && cps[i] != kLF) {
            ++count;
        }
    }
    return count;
}

}  // namespace

ds::render::LayoutConstraints single_line_constraints() {
    ds::render::LayoutConstraints c;
    c.wrap = ds::render::WrapMode::None;
    return c;
}

TextInputElement::TextInputElement(const ds::render::FontMetrics& metrics,
                                   ds::render::LayoutConstraints constraints)
    : metrics_(metrics), layout_(metrics), constraints_(std::move(constraints)) {}

void TextInputElement::set_text(const std::string& utf8_text) {
    // 非法 UTF-8 → std::invalid_argument（不靜默）。
    codepoints_ = ds::render::decode_utf8(utf8_text);
    cursor_ = codepoints_.size();
    anchor_ = cursor_;
}

std::string TextInputElement::text() const { return encode_utf8(codepoints_); }

void TextInputElement::erase_range(std::size_t begin, std::size_t end) {
    // 前置：0 <= begin <= end <= codepoints_.size()（呼叫端已驗證）。
    codepoints_.erase(codepoints_.begin() + static_cast<std::ptrdiff_t>(begin),
                      codepoints_.begin() + static_cast<std::ptrdiff_t>(end));
    cursor_ = begin;
    anchor_ = begin;
}

void TextInputElement::replace_selection_if_any() {
    if (!has_selection()) {
        return;
    }
    const SelectionRange sel = selection();
    erase_range(sel.begin, sel.end);
}

void TextInputElement::insert(const std::string& utf8_text) {
    // 非法 UTF-8 → std::invalid_argument（不靜默）；先驗證再變動狀態。
    const std::vector<CodePoint> incoming = ds::render::decode_utf8(utf8_text);
    replace_selection_if_any();
    codepoints_.insert(codepoints_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                       incoming.begin(), incoming.end());
    cursor_ += incoming.size();
    anchor_ = cursor_;
}

void TextInputElement::backspace() {
    if (has_selection()) {
        replace_selection_if_any();
        return;
    }
    if (cursor_ == 0) {
        return;  // 邊界安全無動作（非法索引才是「不靜默」的對象；見標頭說明）
    }
    erase_range(cursor_ - 1, cursor_);
}

void TextInputElement::erase_forward() {
    if (has_selection()) {
        replace_selection_if_any();
        return;
    }
    if (cursor_ >= codepoints_.size()) {
        return;  // 邊界安全無動作
    }
    erase_range(cursor_, cursor_ + 1);
}

void TextInputElement::set_cursor(std::size_t index) {
    if (index > codepoints_.size()) {
        throw std::out_of_range("TextInputElement::set_cursor: 索引超出文字長度");
    }
    cursor_ = index;
    anchor_ = index;
}

void TextInputElement::move_cursor(CursorMove move, bool extend_selection) {
    switch (move) {
        case CursorMove::Left:
            if (cursor_ > 0) {
                --cursor_;
            }
            break;
        case CursorMove::Right:
            if (cursor_ < codepoints_.size()) {
                ++cursor_;
            }
            break;
        case CursorMove::Home:
            cursor_ = 0;
            break;
        case CursorMove::End:
            cursor_ = codepoints_.size();
            break;
    }
    if (!extend_selection) {
        anchor_ = cursor_;
    }
}

void TextInputElement::select(std::size_t begin, std::size_t end) {
    if (begin > codepoints_.size() || end > codepoints_.size()) {
        throw std::out_of_range("TextInputElement::select: 索引超出文字長度");
    }
    anchor_ = begin;
    cursor_ = end;
}

SelectionRange TextInputElement::selection() const noexcept {
    if (anchor_ <= cursor_) {
        return SelectionRange{anchor_, cursor_};
    }
    return SelectionRange{cursor_, anchor_};
}

TextInputElement::Subscriptions TextInputElement::attach(
    ds::events::KeyboardInputSource& source) {
    Subscriptions subs;
    subs.key_id = source.subscribe_key(
        [this](const ds::events::KeyEvent& event) { this->handle_key(event); });
    subs.text_id = source.subscribe_text(
        [this](const ds::events::TextInputEvent& event) { this->handle_text_input(event); });
    return subs;
}

void TextInputElement::handle_key(const ds::events::KeyEvent& event) {
    if (event.action != ds::events::KeyAction::Press) {
        return;  // 相位 1 只在按下時動作；放開不觸發編輯。
    }
    const bool extend = ds::events::has_modifier(event.modifiers, ds::events::Modifier::Shift);
    using ds::events::Key;
    switch (event.key) {
        case Key::Backspace:
            backspace();
            break;
        case Key::Delete:
            erase_forward();
            break;
        case Key::ArrowLeft:
            move_cursor(CursorMove::Left, extend);
            break;
        case Key::ArrowRight:
            move_cursor(CursorMove::Right, extend);
            break;
        case Key::Home:
            move_cursor(CursorMove::Home, extend);
            break;
        case Key::End:
            move_cursor(CursorMove::End, extend);
            break;
        default:
            break;  // 相位 1：字元輸入經 handle_text_input 到達，其餘鍵忽略。
    }
}

void TextInputElement::handle_text_input(const ds::events::TextInputEvent& event) {
    insert(event.text);
}

TextInputRenderModel TextInputElement::render_model() const {
    TextInputRenderModel model;
    model.layout = layout_.layout(text(), constraints_);

    const std::size_t total_glyphs = model.layout.glyphs.size();
    const std::size_t target = std::min(glyph_index_before(codepoints_, cursor_), total_glyphs);

    model.cursor.index = cursor_;
    if (model.layout.lines.empty()) {
        // 空文字（或退化排版）：無行可依附，直接以字型度量給出起點座標。
        model.cursor.line = 0;
        model.cursor.x = 0.0;
        model.cursor.y = 0.0;
        model.cursor.baseline = metrics_.ascent();
    } else {
        // 邊界（target 恰為某行終點亦為下一行起點）時的消歧義：若游標緊接在一個 '\n'
        // 之後，語意上屬於「新行的開頭」，故偏好較晚的匹配行；否則（一般折行 / 行中位置）
        // 偏好較早的匹配行（該行的末端）。
        const bool preceded_by_newline =
            cursor_ > 0 && cursor_ <= codepoints_.size() && codepoints_[cursor_ - 1] == kLF;

        long match_first = -1;
        long match_last = -1;
        for (std::size_t li = 0; li < model.layout.lines.size(); ++li) {
            const auto& lb = model.layout.lines[li];
            if (target >= lb.begin && target <= lb.begin + lb.count) {
                if (match_first < 0) {
                    match_first = static_cast<long>(li);
                }
                match_last = static_cast<long>(li);
            }
        }

        const long chosen = preceded_by_newline ? match_last : match_first;
        if (chosen >= 0) {
            const auto& lb = model.layout.lines[static_cast<std::size_t>(chosen)];
            model.cursor.line = static_cast<std::size_t>(chosen);
            if (target < lb.begin + lb.count) {
                model.cursor.x = model.layout.glyphs[target].x;
            } else {
                model.cursor.x = lb.x + lb.width;
            }
            model.cursor.y = lb.y;
            model.cursor.baseline = lb.baseline;
        } else {
            // 防禦性後備（理論上因行範圍連續覆蓋 [0,total_glyphs] 而不會發生）。
            const auto& last = model.layout.lines.back();
            model.cursor.line = model.layout.lines.size() - 1;
            model.cursor.x = last.x + last.width;
            model.cursor.y = last.y;
            model.cursor.baseline = last.baseline;
        }
    }

    model.has_selection = has_selection();
    if (model.has_selection) {
        model.selection = selection();
        const double line_height_value =
            constraints_.line_height > 0.0 ? constraints_.line_height : metrics_.line_height();
        const std::size_t gb =
            std::min(glyph_index_before(codepoints_, model.selection.begin), total_glyphs);
        const std::size_t ge =
            std::min(glyph_index_before(codepoints_, model.selection.end), total_glyphs);

        for (std::size_t li = 0; li < model.layout.lines.size(); ++li) {
            const auto& lb = model.layout.lines[li];
            const std::size_t line_begin = lb.begin;
            const std::size_t line_end = lb.begin + lb.count;
            const std::size_t clamped_b = std::min(std::max(gb, line_begin), line_end);
            const std::size_t clamped_e = std::min(std::max(ge, line_begin), line_end);
            if (clamped_b < clamped_e) {
                SelectionRect rect;
                rect.line = li;
                rect.x = model.layout.glyphs[clamped_b].x;
                rect.y = lb.y;
                const double x_end = (clamped_e < line_end) ? model.layout.glyphs[clamped_e].x
                                                             : (lb.x + lb.width);
                rect.width = x_end - rect.x;
                rect.height = line_height_value;
                model.selection_rects.push_back(rect);
            }
        }
    }

    return model;
}

}  // namespace ds::elements
