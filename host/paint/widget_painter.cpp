// H1-01 / H1-06 render_model → GDI 繪製橋接 — 實作（本檔是唯一碰 GDI 的地方）
#include "widget_painter.hpp"

#include <cmath>
#include <string>

namespace ds::host {
namespace {

// 圓周率。MSVC 不定義 M_PI（除非 _USE_MATH_DEFINES），故自備一個常數而不依賴平台巧合。
constexpr double kPi = 3.14159265358979323846;

// UTF-8 → UTF-16。中立層的文字一律 UTF-8，GDI 要 wide。
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

// UTF-16 → UTF-8。`paint_widget` 的 title 是 `std::wstring`（對外 API 不改），而中立的
// 組版層只認 UTF-8，故在此邊界轉一次、再由 `GdiPaintSink::draw_text` 轉回去。
// 看似多繞一圈，但換來的是「標題也走同一條文字指令路徑」——標題若自己偷一條捷徑直接
// 呼叫 GDI，中立層的測試就看不到它畫了什麼。UTF-16 ↔ UTF-8 往返無損。
std::string narrow(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(),
                                           static_cast<int>(s.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out(static_cast<std::size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          &out[0], need, nullptr, nullptr);
    return out;
}

UINT align_flag(TextAlign align) {
    switch (align) {
        case TextAlign::Left: return DT_LEFT;
        case TextAlign::Center: return DT_CENTER;
        case TextAlign::Right: return DT_RIGHT;
    }
    return DT_LEFT;  // 不可達；不寫 default: 以保留 -Wswitch 的提醒
}

// ---------------------------------------------------------------------------
// GdiPaintSink —— `PaintSink` 的 GDI 實作
// ---------------------------------------------------------------------------
class GdiPaintSink final : public PaintSink {
public:
    // **theme 按值持有，不是 `const PaintTheme&`** —— 知識庫 K-013：
    // 「以 const 參考持有的建構參數 + 按值回傳的工廠函式 = 懸空參考，而測試是綠的」。
    // 本檔的 `paint_widget(hdc, bounds, model, title)` 正是把 `default_theme()`
    // （按值回傳）直接餵進來的呼叫端，剛好是那個形狀。PaintTheme 是 7 個 COLORREF，
    // 複製成本可忽略，沒有任何理由去冒那個 UB。
    GdiPaintSink(HDC hdc, PaintTheme theme) : hdc_(hdc), theme_(theme) {}

    void fill_box(const PaintBox& box, PaintRole role) override {
        if (box.empty()) return;
        const RECT r = to_rect(box);
        HBRUSH brush = ::CreateSolidBrush(color_of(role));
        ::FillRect(hdc_, &r, brush);
        ::DeleteObject(brush);
    }

    void draw_text(const PaintBox& box, const std::string& utf8, PaintRole role,
                   TextAlign align) override {
        if (box.empty()) return;
        const std::wstring text = widen(utf8);
        ::SetTextColor(hdc_, color_of(role));
        ::SetBkMode(hdc_, TRANSPARENT);
        RECT r = to_rect(box);
        ::DrawTextW(hdc_, text.c_str(), static_cast<int>(text.size()), &r,
                    align_flag(align) | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    void draw_needle(const PaintBox& dial, double angle_degrees, double length_ratio,
                     double thickness_ratio, PaintRole role) override {
        if (dial.empty()) return;
        const int side = dial.width() < dial.height() ? dial.width() : dial.height();
        const int cx = (dial.left + dial.right) / 2;
        const int cy = (dial.top + dial.bottom) / 2;
        const double radius = static_cast<double>(side) / 2.0;
        const double length = radius * clamp_unit_ratio(length_ratio);
        // 角度為數學慣例（0° 指向右、逆時針為正）；GDI 的 y 軸朝下，故 sin 取負號。
        const double rad = angle_degrees * kPi / 180.0;
        const int x = cx + static_cast<int>(std::lround(length * std::cos(rad)));
        const int y = cy - static_cast<int>(std::lround(length * std::sin(rad)));
        int width = static_cast<int>(std::lround(static_cast<double>(side) *
                                                 clamp_unit_ratio(thickness_ratio)));
        if (width < 1) width = 1;  // 0 寬的筆在 GDI 下仍會畫 1px，寫明白免得像是巧合
        HPEN pen = ::CreatePen(PS_SOLID, width, color_of(role));
        HGDIOBJ old_pen = ::SelectObject(hdc_, pen);
        ::MoveToEx(hdc_, cx, cy, nullptr);
        ::LineTo(hdc_, x, y);
        ::SelectObject(hdc_, old_pen);
        ::DeleteObject(pen);
    }

private:
    COLORREF color_of(PaintRole role) const {
        switch (role) {
            case PaintRole::Background: return theme_.background;
            case PaintRole::Title: return theme_.title;
            case PaintRole::Label: return theme_.label;
            case PaintRole::Value: return theme_.value;
            case PaintRole::Track: return theme_.track;
            case PaintRole::Fill: return theme_.fill;
            case PaintRole::Unavailable: return theme_.unavailable;
        }
        return theme_.label;  // 不可達；不寫 default: 以保留 -Wswitch 的提醒
    }

    HDC hdc_;
    PaintTheme theme_;
};

// 內建 kind 的預設繪製器。函式區域 static：只建一次（每幀重建分派表是白做工），
// 生命週期到程式結束，不會有 K-013 那種懸空問題。
const WidgetRenderer& default_widget_renderer() {
    static const WidgetRenderer kRenderer = WidgetRenderer::with_builtin_kinds();
    return kRenderer;
}

}  // namespace

PaintBox to_paint_box(const RECT& r) {
    return PaintBox{static_cast<int>(r.left), static_cast<int>(r.top),
                    static_cast<int>(r.right), static_cast<int>(r.bottom)};
}

RECT to_rect(const PaintBox& b) {
    RECT r;
    r.left = static_cast<LONG>(b.left);
    r.top = static_cast<LONG>(b.top);
    r.right = static_cast<LONG>(b.right);
    r.bottom = static_cast<LONG>(b.bottom);
    return r;
}

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
    const RowGeometry geo = row_geometry(to_paint_box(bounds), row, row_count);
    RowLayout out;
    out.label = to_rect(geo.label);
    out.track = to_rect(geo.track);
    out.value = to_rect(geo.value);
    return out;
}

RECT fill_rect(const RECT& track, double fill_ratio) {
    return to_rect(fill_span(to_paint_box(track), fill_ratio));
}

PaintSummary paint_widget(HDC hdc, const RECT& bounds,
                          const ds::widgets::SystemStatusRenderModel& model,
                          const std::wstring& title,
                          const PaintTheme& theme,
                          const WidgetRenderer& renderer) {
    GdiPaintSink sink(hdc, theme);
    return renderer.paint(model, to_paint_box(bounds), narrow(title), sink);
}

PaintSummary paint_widget(HDC hdc, const RECT& bounds,
                          const ds::widgets::SystemStatusRenderModel& model,
                          const std::wstring& title,
                          const PaintTheme& theme) {
    return paint_widget(hdc, bounds, model, title, theme, default_widget_renderer());
}

PaintSummary paint_widget(HDC hdc, const RECT& bounds,
                          const ds::widgets::SystemStatusRenderModel& model,
                          const std::wstring& title) {
    return paint_widget(hdc, bounds, model, title, default_theme());
}

}  // namespace ds::host
