// E4-14 長文捲動與分頁 — 實作
//
// 純邏輯：全以行索引 / 比例運算，不觸碰任何座標系統或真實繪製。
#include "scroll_view.hpp"

#include <stdexcept>

namespace ds::elements {

void ScrollView::require_viewport() const {
    if (viewport_lines_ == 0) {
        throw std::invalid_argument(
            "ScrollView: 尚未設定有效 viewport（set_viewport_lines 必須 > 0）");
    }
}

std::size_t ScrollView::max_offset_lines_raw() const noexcept {
    if (content_lines_ > viewport_lines_) {
        return content_lines_ - viewport_lines_;
    }
    return 0;
}

void ScrollView::clamp_offset() {
    const std::size_t max_off = max_offset_lines_raw();
    if (offset_lines_ > max_off) {
        offset_lines_ = max_off;
    }
}

void ScrollView::set_content(const ds::render::LayoutResult& layout) {
    content_lines_ = layout.lines.size();
    offset_lines_ = 0;  // 新內容 → 重設捲動偏移（舊偏移對新內容無意義，可能越界）
}

void ScrollView::set_viewport_lines(std::size_t n) {
    if (n == 0) {
        throw std::invalid_argument("ScrollView: set_viewport_lines(0) 為無效視窗（不靜默）");
    }
    viewport_lines_ = n;
    clamp_offset();  // viewport 變更可能縮小 max_offset，需重新夾限（非重設為 0）
}

std::size_t ScrollView::max_offset_lines() const {
    require_viewport();
    return max_offset_lines_raw();
}

void ScrollView::scroll_by(long long delta_lines) {
    require_viewport();
    long long new_offset = static_cast<long long>(offset_lines_) + delta_lines;
    if (new_offset < 0) {
        new_offset = 0;
    }
    const long long max_off = static_cast<long long>(max_offset_lines_raw());
    if (new_offset > max_off) {
        new_offset = max_off;
    }
    offset_lines_ = static_cast<std::size_t>(new_offset);
}

void ScrollView::scroll_to(std::size_t line_offset) {
    require_viewport();
    const std::size_t max_off = max_offset_lines_raw();
    offset_lines_ = (line_offset > max_off) ? max_off : line_offset;
}

void ScrollView::page_next() {
    require_viewport();
    scroll_by(static_cast<long long>(viewport_lines_));
}

void ScrollView::page_prev() {
    require_viewport();
    scroll_by(-static_cast<long long>(viewport_lines_));
}

VisibleRange ScrollView::visible_range() const {
    require_viewport();
    VisibleRange r;
    r.begin = offset_lines_;
    std::size_t end = offset_lines_ + viewport_lines_;
    if (end > content_lines_) {
        end = content_lines_;
    }
    r.end = (end < r.begin) ? r.begin : end;  // content_lines_==0 时 begin=end=0，恆不小於 begin
    return r;
}

RenderModel ScrollView::render_model() const {
    require_viewport();

    RenderModel model;
    model.viewport_lines = viewport_lines_;
    model.content_lines = content_lines_;

    const VisibleRange range = visible_range();
    model.lines.reserve(range.end - range.begin);
    for (std::size_t i = range.begin; i < range.end; ++i) {
        VisibleLine vl;
        vl.content_line = i;
        vl.viewport_line = i - range.begin;
        model.lines.push_back(vl);
    }

    const std::size_t max_off = max_offset_lines_raw();
    ScrollPosition pos;
    pos.offset_lines = offset_lines_;
    pos.max_offset_lines = max_off;
    pos.ratio = (max_off == 0) ? 0.0
                                : static_cast<double>(offset_lines_) / static_cast<double>(max_off);
    pos.at_top = (offset_lines_ == 0);
    pos.at_bottom = (offset_lines_ == max_off);
    model.position = pos;

    // 分頁指示：以 viewport_lines 為一頁的近似頁碼。已在 max 偏移（含內容不足一頁、
    // 或 page_next() 觸底夾限）時回報最後一頁，避免因夾限未落在整除邊界而低估頁碼。
    if (content_lines_ > 0) {
        model.page_count = (content_lines_ + viewport_lines_ - 1) / viewport_lines_;
        model.page_index =
            (offset_lines_ >= max_off) ? (model.page_count - 1) : (offset_lines_ / viewport_lines_);
    } else {
        model.page_count = 0;
        model.page_index = 0;
    }

    return model;
}

}  // namespace ds::elements
