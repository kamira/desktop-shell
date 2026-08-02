// H1-01 render_model → GDI 繪製橋接
//
// 把 C2-02 widget 產出的 `SystemStatusRenderModel`（純資料：標籤、fill_ratio、格式化文字）
// 畫成 Windows 桌面上的實際像素。
//
// **為什麼這一層在 host/ 而不是在 win32 後端裡**（兩個獨立理由，任一都足以決定）：
//   1. 分層：它吃 E4-03 的 `ds::elements::RenderModel`（engine / module 層型別）。
//      放進 `src/kernel/backend/win32/`（platform 層）會造成 platform 反向依賴 engine。
//   2. NFR-02：繪製必然涉及座標，而 kernel 契約硬性禁止絕對座標——
//      所以繪製**不可能**成為 kernel 契約的一部分，只能活在契約之外。
//
// 版面計算（`row_layout` / `fill_rect`）刻意抽成**不碰 GDI 的純函式**：
// 這樣它們可以被直接單元測試，而不必先有視窗。繪製本體則以記憶體 DC 測試。
#ifndef DS_HOST_PAINT_WIDGET_PAINTER_HPP
#define DS_HOST_PAINT_WIDGET_PAINTER_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "system_status_widget.hpp"  // C2-02（上游，可讀不可改）：SystemStatusRenderModel

namespace ds::host {

// 配色。桌面浮層的視覺慣例：深底、亮字、量表軌道比填充暗。
struct PaintTheme {
    COLORREF background;
    COLORREF title;
    COLORREF label;
    COLORREF value;
    COLORREF track;      // 量表未填滿的軌道
    COLORREF fill;       // 量表已填滿的部分
    COLORREF unavailable;  // 降級（available=false）條目的顏色
};

PaintTheme default_theme();

// 一列（一個指標）的版面：標籤區 / 量表軌道 / 數值文字區。
struct RowLayout {
    RECT label;
    RECT track;
    RECT value;
};

// 依「第 row 列，共 row_count 列」把 bounds 切出該列的三個區塊。
// 純函式，不碰 GDI。row 超出範圍或 row_count <= 0 時回傳全零矩形（保守，不崩潰）。
RowLayout row_layout(const RECT& bounds, int row, int row_count);

// 依 fill_ratio 算出量表已填滿的矩形。ratio 夾限於 [0,1]；
// ratio<=0 回寬度為 0 的矩形，ratio>=1 回滿軌道。純函式，不碰 GDI。
RECT fill_rect(const RECT& track, double fill_ratio);

// 把整個 widget 畫到 hdc 的 bounds 內（含背景）。
// `model` 的每個 entry 畫成一列；`available=false` 的條目以降級色顯示，不中斷其餘條目（NFR-03）。
void paint_widget(HDC hdc, const RECT& bounds,
                  const ds::widgets::SystemStatusRenderModel& model,
                  const std::wstring& title,
                  const PaintTheme& theme);

// 便利多載：使用 default_theme()。
void paint_widget(HDC hdc, const RECT& bounds,
                  const ds::widgets::SystemStatusRenderModel& model,
                  const std::wstring& title);

}  // namespace ds::host

#endif  // DS_HOST_PAINT_WIDGET_PAINTER_HPP
