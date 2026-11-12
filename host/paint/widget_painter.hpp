// H1-01 / H1-06 render_model → GDI 繪製橋接
//
// 把 C2-02 widget 產出的 `SystemStatusRenderModel`（純資料：標籤、E4 元件渲染描述、
// 格式化文字）畫成 Windows 桌面上的實際像素。
//
// **為什麼這一層在 host/ 而不是在 win32 後端裡**（兩個獨立理由，任一都足以決定）：
//   1. 分層：它吃 E4-03 的 `ds::elements::RenderModel`（engine / module 層型別）。
//      放進 `src/kernel/backend/win32/`（platform 層）會造成 platform 反向依賴 engine。
//   2. NFR-02：繪製必然涉及座標，而 kernel 契約硬性禁止絕對座標——
//      所以繪製**不可能**成為 kernel 契約的一部分，只能活在契約之外。
//
// ---------------------------------------------------------------------------
// H1-06 之後：本檔只剩「GDI 怎麼下筆」
// ---------------------------------------------------------------------------
// 「依 kind 分派」「組版」「NFR-03 降級」全部搬到**平台中立**的
// `render_dispatch.{hpp,cpp}` + `widget_render.{hpp,cpp}`（守衛外的自有 target，
// ubuntu 的 G3 與 gate_sanitizers 真的建置並執行它們的測試）。本檔只提供一個
// `PaintSink` 的 GDI 實作：具名顏色角色 → `COLORREF`、UTF-8 → UTF-16 → `DrawTextW`、
// 指針 → `CreatePen` + `LineTo`。
//
// 版面計算（`row_layout` / `fill_rect`）仍是**不碰 GDI 的純函式**（H1-01 自
// CHG-20260803-05 起的做法），現在轉為對中立層的薄轉接（`RECT` ↔ `PaintBox`）：
// `tests/h1/test_h1_01.cpp` 直接斷言它們的幾何，而該檔不在 H1-06 的 write_scope 內，
// 故對外行為一字不改。
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

#include "widget_render.hpp"  // H1-06（同單元，平台中立）：WidgetRenderer / PaintSink / PaintSummary

namespace ds::host {

// 配色。桌面浮層的視覺慣例：深底、亮字、量表軌道比填充暗。
// 每個欄位對應一個具名 `PaintRole` —— 中立層只說角色，顏色的決定權留在平台這側。
struct PaintTheme {
    COLORREF background;
    COLORREF title;
    COLORREF label;
    COLORREF value;
    COLORREF track;      // 量表未填滿的軌道
    COLORREF fill;       // 量表已填滿的部分 / 指針
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
// （H1-06 起為 `row_geometry()` 的轉接；`track` 即其格位內的細長軌道。）
RowLayout row_layout(const RECT& bounds, int row, int row_count);

// 依 fill_ratio 算出量表已填滿的矩形。ratio 夾限於 [0,1]（NaN 視為 0）；
// ratio<=0 回寬度為 0 的矩形，ratio>=1 回滿軌道。純函式，不碰 GDI。
// （H1-06 起為 `fill_span()` 的轉接。）
RECT fill_rect(const RECT& track, double fill_ratio);

// 把整個 widget 畫到 hdc 的 bounds 內（含背景）。
// `model` 的每個 entry 畫成一列，**畫法依該筆 E4 元件描述的 kind 分派**；
// `available=false` 的條目以降級色顯示，不中斷其餘條目（NFR-03）。
//
// 回傳值（H1-06 新增）讓呼叫端看得到「有幾筆被降級 / 有幾筆的 kind 查不到畫法」——
// 不回傳就只能從畫面猜，而 `unknown_kind` 非 0 正是最該被看到的那種失敗。
// 舊呼叫端忽略回傳值即可（刻意不加 `[[nodiscard]]`）。
PaintSummary paint_widget(HDC hdc, const RECT& bounds,
                          const ds::widgets::SystemStatusRenderModel& model,
                          const std::wstring& title,
                          const PaintTheme& theme);

// 便利多載：使用 default_theme()。
PaintSummary paint_widget(HDC hdc, const RECT& bounds,
                          const ds::widgets::SystemStatusRenderModel& model,
                          const std::wstring& title);

// 以呼叫端自備的 `WidgetRenderer` 繪製 —— **新增元件型別的入口**：
// 呼叫端 `register_kind()` + `add_projector()` 後直接生效，本檔與 `src/` / `engine/`
// 皆一行不動（H1-06 的驗收標準）。
PaintSummary paint_widget(HDC hdc, const RECT& bounds,
                          const ds::widgets::SystemStatusRenderModel& model,
                          const std::wstring& title,
                          const PaintTheme& theme,
                          const WidgetRenderer& renderer);

// `RECT` ↔ `PaintBox` 轉接（繪製層與中立層的唯一邊界）。
PaintBox to_paint_box(const RECT& r);
RECT to_rect(const PaintBox& b);

}  // namespace ds::host

#endif  // DS_HOST_PAINT_WIDGET_PAINTER_HPP
