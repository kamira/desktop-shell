// H1-01 render_model → GDI 繪製橋接 — 實作
#include "widget_painter.hpp"

#include <string>

namespace ds::host {
namespace {

constexpr int kPadding = 12;
constexpr int kTitleHeight = 24;
constexpr int kLabelWidth = 44;
constexpr int kValueWidth = 66;
constexpr int kGap = 8;

// UTF-8 → UTF-16。widget 產出的 display_text 是 std::string（UTF-8），GDI 要 wide。
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          &out[0], need);
    return out;
}

void fill(HDC hdc, const RECT& r, COLORREF c) {
    HBRUSH brush = ::CreateSolidBrush(c);
    ::FillRect(hdc, &r, brush);
    ::DeleteObject(brush);
}

void draw_text(HDC hdc, const RECT& r, const std::wstring& text, COLORREF c, UINT align) {
    ::SetTextColor(hdc, c);
    ::SetBkMode(hdc, TRANSPARENT);
    RECT box = r;
    ::DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &box,
                align | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

}  // namespace

PaintTheme default_theme() {
    PaintTheme t;
    t.background = RGB(18, 20, 26);
    t.title = RGB(150, 160, 180);
    t.label = RGB(220, 226, 236);
    t.value = RGB(255, 255, 255);
    t.track = RGB(44, 48, 58);
    t.fill = RGB(88, 176, 255);
    t.unavailable = RGB(110, 116, 128);
    return t;
}

RowLayout row_layout(const RECT& bounds, int row, int row_count) {
    RowLayout out = {};
    if (row_count <= 0 || row < 0 || row >= row_count) return out;  // 保守，不崩潰

    const int top = bounds.top + kPadding + kTitleHeight;
    const int usable = (bounds.bottom - kPadding) - top;
    if (usable <= 0) return out;

    const int row_h = usable / row_count;
    const int y = top + row * row_h;
    const int left = bounds.left + kPadding;
    const int right = bounds.right - kPadding;

    out.label = {left, y, left + kLabelWidth, y + row_h};
    out.value = {right - kValueWidth, y, right, y + row_h};
    out.track = {out.label.right + kGap, y + row_h / 2 - 5,
                 out.value.left - kGap, y + row_h / 2 + 5};
    return out;
}

RECT fill_rect(const RECT& track, double fill_ratio) {
    double r = fill_ratio;
    if (!(r > 0.0)) r = 0.0;   // 同時擋掉 NaN：NaN 的任何比較都為 false
    if (r > 1.0) r = 1.0;
    RECT out = track;
    const LONG width = track.right - track.left;
    out.right = track.left + static_cast<LONG>(static_cast<double>(width) * r + 0.5);
    if (out.right < out.left) out.right = out.left;
    return out;
}

void paint_widget(HDC hdc, const RECT& bounds,
                  const ds::widgets::SystemStatusRenderModel& model,
                  const std::wstring& title,
                  const PaintTheme& theme) {
    fill(hdc, bounds, theme.background);

    RECT title_box = {bounds.left + kPadding, bounds.top + kPadding,
                      bounds.right - kPadding, bounds.top + kPadding + kTitleHeight};
    draw_text(hdc, title_box, title, theme.title, DT_LEFT);

    const int count = static_cast<int>(model.entries.size());
    for (int i = 0; i < count; ++i) {
        const auto& e = model.entries[static_cast<std::size_t>(i)];
        const RowLayout row = row_layout(bounds, i, count);

        // NFR-03 優雅降級：某筆指標不可用時只把**該筆**畫成灰的，其餘照常。
        const bool ok = e.available;
        const COLORREF label_c = ok ? theme.label : theme.unavailable;
        const COLORREF value_c = ok ? theme.value : theme.unavailable;

        draw_text(hdc, row.label, widen(e.label.empty() ? std::string("?") : e.label),
                  label_c, DT_LEFT);

        fill(hdc, row.track, theme.track);
        if (ok) {
            const RECT filled = fill_rect(row.track, e.element.fill_ratio);
            if (filled.right > filled.left) fill(hdc, filled, theme.fill);
        }

        draw_text(hdc, row.value,
                  widen(e.display_text.empty() ? std::string("--") : e.display_text),
                  value_c, DT_RIGHT);
    }
}

void paint_widget(HDC hdc, const RECT& bounds,
                  const ds::widgets::SystemStatusRenderModel& model,
                  const std::wstring& title) {
    paint_widget(hdc, bounds, model, title, default_theme());
}

}  // namespace ds::host
