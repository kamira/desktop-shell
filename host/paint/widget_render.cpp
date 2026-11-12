// H1-06 把 C2-02 的 SystemStatusRenderModel 畫出來 — 平台中立實作
#include "widget_render.hpp"

#include <utility>

namespace ds::host {
namespace {

// H1-01 既有的佔位語意，一字不改：標籤空 → "?"，數值空 → "--"。
// （空字串直接送進繪製 = 該列在畫面上消失，使用者無從得知有這個指標。）
const char* kMissingLabel = "?";
const char* kMissingValue = "--";

}  // namespace

std::string element_kind_key(ds::elements::ElementKind kind) {
    switch (kind) {
        case ds::elements::ElementKind::Bar: return kKindBar;
        case ds::elements::ElementKind::Progress: return kKindProgress;
        case ds::elements::ElementKind::Gauge: return kKindGauge;
    }
    return std::string();  // 不可達；空鍵會讓分派回 UnknownKind（不靜默），不會畫錯東西
}

ElementDraw project_entry(const ds::widgets::MetricRenderEntry& entry) {
    const ds::elements::RenderModel& m = entry.element;
    ElementDraw draw;
    draw.kind = element_kind_key(m.kind);
    draw.available = entry.available;
    draw.vertical = m.orientation == ds::elements::Orientation::Vertical;
    draw.fill_ratio = m.fill_ratio;
    draw.has_angle = m.has_angle;
    draw.start_angle_degrees = m.start_angle_degrees;
    draw.sweep_degrees = m.sweep_degrees;
    draw.angle_degrees = m.angle_degrees;
    draw.show_label = m.show_label;
    draw.label = m.label;
    // length_ratio / thickness_ratio：E4-03 的量表描述沒有指針幾何（那是 E4-18 的語意），
    // 故留在 ElementDraw 的預設值，不假造一個上游沒說過的數字。
    return draw;
}

WidgetRenderer::WidgetRenderer(ElementRendererTable table) : table_(std::move(table)) {}

WidgetRenderer WidgetRenderer::with_builtin_kinds() {
    return WidgetRenderer(ElementRendererTable::with_builtin_kinds());
}

void WidgetRenderer::add_projector(ElementProjector projector) {
    if (!projector) return;  // 空的 std::function 塞進來只會在呼叫時炸，寧可不收
    projectors_.push_back(std::move(projector));
}

ElementDraw WidgetRenderer::project(const ds::widgets::MetricRenderEntry& entry) const {
    // 後註冊者先認領（反序）：呼叫端加的新元件型別優先於內建投影。
    for (auto it = projectors_.rbegin(); it != projectors_.rend(); ++it) {
        ElementDraw draw;
        draw.available = entry.available;
        if ((*it)(entry, draw)) {
            // available 由繪製層的 NFR-03 政策決定，不交給投影器改寫。
            draw.available = entry.available;
            return draw;
        }
    }
    return project_entry(entry);
}

PaintSummary WidgetRenderer::paint(const ds::widgets::SystemStatusRenderModel& model,
                                   const PaintBox& bounds, const std::string& title_utf8,
                                   PaintSink& sink) const {
    PaintSummary summary;
    sink.fill_box(bounds, PaintRole::Background);
    sink.draw_text(title_box(bounds, metrics_), title_utf8, PaintRole::Title, TextAlign::Left);

    const int count = static_cast<int>(model.entries.size());
    summary.rows = count;
    for (int i = 0; i < count; ++i) {
        const ds::widgets::MetricRenderEntry& entry = model.entries[static_cast<std::size_t>(i)];
        const RowGeometry geo = row_geometry(bounds, i, count, metrics_);

        // NFR-03 優雅降級：該筆不可用時只把**該筆**的文字換成降級色、元件不畫填充 / 指針，
        // 其餘筆完全不受影響（迴圈不中斷、不丟例外）。
        const bool ok = entry.available;
        if (!ok) ++summary.degraded;
        const PaintRole text_role = ok ? PaintRole::Label : PaintRole::Unavailable;
        const PaintRole value_role = ok ? PaintRole::Value : PaintRole::Unavailable;

        sink.draw_text(geo.label, entry.label.empty() ? kMissingLabel : entry.label, text_role,
                       TextAlign::Left);

        switch (table_.draw(project(entry), geo.cell, sink)) {
            case DispatchResult::Drawn: ++summary.drawn; break;
            case DispatchResult::UnknownKind: ++summary.unknown_kind; break;
            case DispatchResult::EmptyArea: ++summary.empty_area; break;
        }

        sink.draw_text(geo.value, entry.display_text.empty() ? kMissingValue : entry.display_text,
                       value_role, TextAlign::Right);
    }
    return summary;
}

}  // namespace ds::host
