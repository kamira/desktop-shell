// H1-06 E4-18 徑向線 / 指針 → 繪製層（新增一個元件型別的實例示範）
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含平台條件編譯**。
//
// ---------------------------------------------------------------------------
// 這份檔案存在的意義
// ---------------------------------------------------------------------------
// H1-06 的驗收標準是平台承諾本身：「新增一個元件型別後，不改 `src/` 與 `engine/` 任何一行
// 即可被畫出」。E4-18（徑向線 / 指針）正是一個**分派核心原本不認得**的元件型別：
//   - 它有**自己的** `RenderModel`（與 E4-03 的同名但不同型別，見下方警告）；
//   - 它的 kind（`kKindNeedle`）**不在** `ElementRendererTable::with_builtin_kinds()` 裡。
// 接上它只用到兩個對外擴充點 —— 一個投影函式 + 一次 `register_kind()`，
// `src/` / `engine/` / `modules/` 皆一行未動（連 `render_dispatch.*` 也沒動）。
//
// ---------------------------------------------------------------------------
// 【警告】`ds::elements::RenderModel` 有兩個同名不同型別的定義
// ---------------------------------------------------------------------------
//   - `modules/elements/bar_gauge.hpp`（E4-03）：長條 / 進度 / 量表的渲染描述。
//   - `modules/elements/radial_pointer.hpp`（E4-18）：徑向線 / 指針的渲染描述。
// 兩者同在 `namespace ds::elements`、同名 `RenderModel`、**成員不同**。後果：
//   1. 同一個翻譯單元同時 include 兩者 → 重定義，編譯失敗；
//   2. 同一個**程式**連入兩份不同定義 → ODR 違反（UB，任何警告旗標與 sanitizer 都抓不到，
//      正是 K-013 那一族「綠燈不代表正確」的形狀）。
// 故本檔**刻意不 include** `widget_render.hpp` / `system_status_widget.hpp`（它們帶入 E4-03），
// 且其 target `h1_06_radial` 與其測試 `test_h1_06_radial` 是**獨立的程式**，不與
// `h1_06_paint_model`（E4-03 側）連在一起 —— 兩個程式各自只有一份 `RenderModel` 定義。
// repo 內 C1-06 / C2-07 的橋接（pimpl）範式處理的是同一族問題（`ds::kernel::HitResult`）。
#ifndef DS_HOST_PAINT_RADIAL_DRAW_HPP
#define DS_HOST_PAINT_RADIAL_DRAW_HPP

#include "radial_pointer.hpp"  // E4-18（上游，可讀不可改）：RenderModel / RadialPointerElement
#include "render_dispatch.hpp"

namespace ds::host {

// E4-18 的具名 kind 鍵。**不是**內建 kind（見檔頭）。
inline constexpr const char* kKindNeedle = "needle";

// E4-18 `RenderModel` → 平台中立的 `ElementDraw`（kind = `kKindNeedle`）。
// `available` 預設 true：該筆指標可不可用是繪製層（NFR-03）的政策，不是元件描述的一部分，
// 由呼叫端在投影後設定。
ElementDraw project_radial(const ds::elements::RenderModel& model);

// 把 `kKindNeedle` 的畫法註冊進分派表。
// 已存在同名 kind → 回 false（`register_kind` 不靜默覆寫）。
//
// 畫法：自錶盤中心、依 `angle_degrees` 畫一根長度 / 粗細為比例的指針。
// NFR-03：`available=false` 時**不畫指針**——錶盤底盤屬於另一個元件的職責，
// 本 kind 不發明一個上游沒說過的形狀（該列的標籤 / 數值仍由組版層以降級色畫出）。
bool register_radial_kind(ElementRendererTable& table);

}  // namespace ds::host

#endif  // DS_HOST_PAINT_RADIAL_DRAW_HPP
