// H1-06 繪製層的平台中立分派核心 — 實作
#include "render_dispatch.hpp"

#include <algorithm>
#include <utility>

namespace ds::host {
namespace {

// 把比例換算成像素長度（四捨五入）。
int scaled(int length, double ratio) noexcept {
    const double v = static_cast<double>(length) * clamp_unit_ratio(ratio) + 0.5;
    return static_cast<int>(v);
}

}  // namespace

double clamp_unit_ratio(double ratio) noexcept {
    // 見標頭：`!(ratio > 0.0)` 同時擋掉 NaN（K-002）。H1-01 的 `fill_rect` 原本在
    // widget_painter.cpp 內各寫一次，現在統一由此處負責。
    if (!(ratio > 0.0)) return 0.0;
    if (ratio > 1.0) return 1.0;
    return ratio;
}

const char* to_string(PaintRole role) noexcept {
    switch (role) {
        case PaintRole::Background: return "background";
        case PaintRole::Title: return "title";
        case PaintRole::Label: return "label";
        case PaintRole::Value: return "value";
        case PaintRole::Track: return "track";
        case PaintRole::Fill: return "fill";
        case PaintRole::Unavailable: return "unavailable";
    }
    return "?";  // 不可達；不寫 default: 是為了讓日後新增角色時 -Wswitch 提醒這裡
}

const char* to_string(TextAlign align) noexcept {
    switch (align) {
        case TextAlign::Left: return "left";
        case TextAlign::Center: return "center";
        case TextAlign::Right: return "right";
    }
    return "?";
}

const char* to_string(DispatchResult result) noexcept {
    switch (result) {
        case DispatchResult::Drawn: return "drawn";
        case DispatchResult::UnknownKind: return "unknown-kind";
        case DispatchResult::EmptyArea: return "empty-area";
    }
    return "?";
}

bool operator==(const PaintBox& a, const PaintBox& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool operator!=(const PaintBox& a, const PaintBox& b) noexcept { return !(a == b); }

double ElementDraw::param(const std::string& name, double fallback) const {
    const auto it = params.find(name);
    return it == params.end() ? fallback : it->second;
}

// ---------------------------------------------------------------------------
// 版面幾何
// ---------------------------------------------------------------------------

RowMetrics default_row_metrics() noexcept { return RowMetrics{}; }

PaintBox title_box(const PaintBox& bounds, const RowMetrics& metrics) {
    return PaintBox{bounds.left + metrics.padding, bounds.top + metrics.padding,
                    bounds.right - metrics.padding,
                    bounds.top + metrics.padding + metrics.title_height};
}

PaintBox track_strip(const PaintBox& cell, int half_height) {
    const int mid = (cell.top + cell.bottom) / 2;
    return PaintBox{cell.left, mid - half_height, cell.right, mid + half_height};
}

PaintBox dial_box(const PaintBox& cell) {
    const int side = std::min(cell.width(), cell.height());
    if (side <= 0) return PaintBox{};
    const int cx = (cell.left + cell.right) / 2;
    const int cy = (cell.top + cell.bottom) / 2;
    const int half = side / 2;
    return PaintBox{cx - half, cy - half, cx - half + side, cy - half + side};
}

RowGeometry row_geometry(const PaintBox& bounds, int row, int row_count,
                         const RowMetrics& metrics) {
    RowGeometry out{};
    if (row_count <= 0 || row < 0 || row >= row_count) return out;  // 保守，不崩潰

    const int top = bounds.top + metrics.padding + metrics.title_height;
    const int usable = (bounds.bottom - metrics.padding) - top;
    if (usable <= 0) return out;

    const int row_h = usable / row_count;
    const int y = top + row * row_h;
    const int left = bounds.left + metrics.padding;
    const int right = bounds.right - metrics.padding;

    out.label = PaintBox{left, y, left + metrics.label_width, y + row_h};
    out.value = PaintBox{right - metrics.value_width, y, right, y + row_h};
    out.cell = PaintBox{out.label.right + metrics.gap, y, out.value.left - metrics.gap,
                        y + row_h};
    out.track = track_strip(out.cell, metrics.track_half_height);
    return out;
}

RowGeometry row_geometry(const PaintBox& bounds, int row, int row_count) {
    return row_geometry(bounds, row, row_count, default_row_metrics());
}

PaintBox fill_span(const PaintBox& track, double fill_ratio, bool vertical) {
    PaintBox out = track;
    if (vertical) {
        // 由下往上填：底邊不動，上緣往上長。
        out.top = track.bottom - scaled(track.height(), fill_ratio);
        if (out.top > out.bottom) out.top = out.bottom;
    } else {
        out.right = track.left + scaled(track.width(), fill_ratio);
        if (out.right < out.left) out.right = out.left;
    }
    return out;
}

PaintBox fill_span(const PaintBox& track, double fill_ratio) {
    return fill_span(track, fill_ratio, false);
}

// ---------------------------------------------------------------------------
// 內建 kind 的畫法
// ---------------------------------------------------------------------------
namespace {

// 線性填充（長條 / 進度共用的幾何）：軌道 + 依比例的填充。
// NFR-03：`available=false` 時**仍畫軌道**、**不畫填充** —— 只灰掉該筆，而不是整列消失。
void draw_linear(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    const PaintBox track = track_strip(area, default_row_metrics().track_half_height);
    if (track.empty()) return;
    sink.fill_box(track, PaintRole::Track);
    if (!draw.available) return;
    const PaintBox filled = fill_span(track, draw.fill_ratio, draw.vertical);
    if (!filled.empty()) sink.fill_box(filled, PaintRole::Fill);
}

// bar（E4-03 Bar）：值 → 長度比例。
void render_bar(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    draw_linear(draw, area, sink);
}

// progress（E4-03 Progress）：本質同為線性填充，但另把元件自帶的讀數（E4-03 自動格式化
// 的 "63%"）畫在軌道中央 —— 這是進度條的慣例外觀，也是它與 bar 在畫面上的差別。
// `show_label=false`（C2-02 目前即如此設定）時退化成與 bar 相同的外觀：那是上游的選擇，
// 不是分派沒有發生 —— 兩者仍各自走自己的 renderer（`DispatchResult` 與測試都看得到）。
void render_progress(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    draw_linear(draw, area, sink);
    if (!draw.show_label || draw.label.empty()) return;
    const PaintBox track = track_strip(area, default_row_metrics().track_half_height);
    if (track.empty()) return;
    sink.draw_text(track, draw.label, draw.available ? PaintRole::Value : PaintRole::Unavailable,
                   TextAlign::Center);
}

// gauge（E4-03 Gauge）：值 → 角度。錶盤 + 指針，與線性填充在畫面上完全不同。
void render_gauge(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    const PaintBox dial = dial_box(area);
    if (dial.empty()) return;
    sink.fill_box(dial, PaintRole::Track);
    if (!draw.available) return;  // NFR-03：錶盤仍在，指針不畫
    if (draw.has_angle) {
        sink.draw_needle(dial, draw.angle_degrees, draw.length_ratio, draw.thickness_ratio,
                         PaintRole::Fill);
        return;
    }
    // 角度缺失（上游沒填 has_angle）不得讓數值靜默消失 —— 退回線性填充，讓值仍然看得見。
    const PaintBox track = track_strip(dial, default_row_metrics().track_half_height);
    const PaintBox filled = fill_span(track, draw.fill_ratio, draw.vertical);
    if (!filled.empty()) sink.fill_box(filled, PaintRole::Fill);
}

}  // namespace

// ---------------------------------------------------------------------------
// ElementRendererTable
// ---------------------------------------------------------------------------

ElementRendererTable ElementRendererTable::with_builtin_kinds() {
    ElementRendererTable table;
    table.register_kind(kKindBar, &render_bar);
    table.register_kind(kKindProgress, &render_progress);
    table.register_kind(kKindGauge, &render_gauge);
    return table;
}

bool ElementRendererTable::register_kind(std::string key, ElementRenderer renderer) {
    if (key.empty() || !renderer) return false;
    if (renderers_.find(key) != renderers_.end()) return false;  // 不靜默覆寫
    renderers_.emplace(std::move(key), std::move(renderer));
    return true;
}

bool ElementRendererTable::replace_kind(std::string key, ElementRenderer renderer) {
    if (key.empty() || !renderer) return false;
    renderers_[std::move(key)] = std::move(renderer);
    return true;
}

bool ElementRendererTable::has_kind(const std::string& key) const {
    return renderers_.find(key) != renderers_.end();
}

std::vector<std::string> ElementRendererTable::kinds() const {
    std::vector<std::string> out;
    out.reserve(renderers_.size());
    for (const auto& kv : renderers_) out.push_back(kv.first);
    return out;  // std::map 已是字典序
}

DispatchResult ElementRendererTable::draw(const ElementDraw& draw, const PaintBox& area,
                                          PaintSink& sink) const {
    const auto it = renderers_.find(draw.kind);
    if (it == renderers_.end()) return DispatchResult::UnknownKind;
    if (area.empty()) return DispatchResult::EmptyArea;
    it->second(draw, area, sink);
    return DispatchResult::Drawn;
}

}  // namespace ds::host
