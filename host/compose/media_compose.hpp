// H1-07 第二案例 —— 把 C2-07 媒體控制當成**一個組件**，擺進 widget 內的相對像素版面
//
// 驗收標準指定的第二案例：「同一組件須能同時出現在兩個 widget 且呈現型態可不同
// （以 C2-07 媒體控制為第二案例）」。本檔提供那個組件與它的兩種呈現：
//
//   - **組件**（獨立組合單位）＝ 一個 E9-01 `ds::package::Package`
//     （`make_media_control_component()`，走 E9-01 的**文字格式解析**產生，格式與驗證都由
//     上游負責，本層不手捏結構）。它可以被綁進**任意數量**的 `WidgetAssembly` 插槽。
//   - **呈現型態**＝ `ComponentPresentation` 的具名變體，由本檔的
//     `media_part_boxes()` 解讀成 widget 內相對的像素部件框：
//       `expanded` → 封面（左）＋ 曲名 / 演出者（右上）＋ **四顆**按鈕（右下一列）
//       `compact`  → 無封面、單行曲名 ＋ **一顆**按鈕（依播放狀態選 play 或 pause）
//     兩個變體的**部件集合不同、像素框也不同** —— 這就是「呈現型態可不同」的實體。
//
// 內容來自 C2-07 的 `MediaControlRenderModel`（可讀不可改的上游）：
// `has_media` / `playback` / `title` / `artist` / `artwork.has_source` 都會改變產出的部件集合。
// **沒有的東西不產出框**（無封面來源就沒有 artwork 框、無媒體就沒有文字框）——
// 不假造一個空框讓繪製層畫出一塊空白（那是「壞掉的狀態偽裝成正常」的形狀，K-002 同族）。
//
// ---------------------------------------------------------------------------
// K-014（同名不同型別的 ODR 陷阱）——本檔刻意踩不到
// ---------------------------------------------------------------------------
// `ds::elements::RenderModel` 在 `modules/elements/` 下有**三個**同名不同型別的定義
// （`bar_gauge.hpp:68` / `radial_pointer.hpp:50` / `scroll_view.hpp:54`）。兩個 TU 各含一份
// 不同定義、連進同一程式 = ODR 違反（UB），而 `-Wall -Wextra -Werror` 與 ASan + UBSan
// **同時零診斷**（知識庫 K-014）。
// 本檔的 E4 相依只經 `c2_07` 一條路徑，而 `c2_07` 連的是 `e4_02`（`ImageRenderModel`）與
// `e4_04`（`ButtonRenderModel`）—— **兩者都不是** `RenderModel` 這個名字，且 `c2_07` 早已
// 在 main 上以單一 target 同時連入這兩者。本單元**不**連入 `c2_02` / `e4_03` / `e4_18` /
// `e4_14` 任何一個，故全程式內 `ds::elements::RenderModel` 的定義數為 **0**。
//
// 同 TU 內 `ds::kernel::HitResult` 的碰撞（E1-02 的 `enum class` vs E1-04 的 `struct`）
// 由 C2-07 自己的橋接層（`media_control_button_bridge`）隔離；本檔只 include
// `media_control_widget.hpp`，不直接 include `button_element.hpp`。
#ifndef DS_HOST_COMPOSE_MEDIA_COMPOSE_HPP
#define DS_HOST_COMPOSE_MEDIA_COMPOSE_HPP

#include <string>
#include <vector>

#include "media_control_widget.hpp"  // C2-07（上游，可讀不可改）：MediaControlRenderModel /
                                      //   MediaButtonRenderModel / MediaControlWidget
#include "widget_compose.hpp"        // H1-07（本單元）

namespace ds::host {

// --- 組件的內容清單 kind（E9-01 的 `<kind>: <logical_path>`）---
inline constexpr const char* kMediaArtworkKind = "media.artwork";
inline constexpr const char* kMediaTransportKind = "media.transport";

// --- 具名呈現變體（字串鍵而非 enum —— 新增變體不必改本層的型別）---
inline constexpr const char* kMediaVariantExpanded = "expanded";
inline constexpr const char* kMediaVariantCompact = "compact";

// --- 具名部件 id（`MediaPartBox::part`）---
inline constexpr const char* kMediaPartArtwork = "artwork";
inline constexpr const char* kMediaPartTitle = "title";
inline constexpr const char* kMediaPartArtist = "artist";
inline constexpr const char* kMediaPartButtonPlay = "button.play";
inline constexpr const char* kMediaPartButtonPause = "button.pause";
inline constexpr const char* kMediaPartButtonPrev = "button.prev";
inline constexpr const char* kMediaPartButtonNext = "button.next";

// 媒體控制插槽：要求組件的內容清單同時提供封面與傳輸控制兩種資源
// （相容性由 E9-03 `check_compatibility` 判定，本層不重造）。
ds::package::ComponentSlot media_control_slot(std::string slot_id);

// 只要求封面的插槽 —— 用來證明「插槽介面」真的在篩：媒體控制組件對它相容，
// 而一個只有封面的組件對 `media_control_slot()` **不**相容（E9-03 會帶原因拒絕）。
ds::package::ComponentSlot artwork_only_slot(std::string slot_id);

// 把 C2-07 媒體控制組裝成一個 E9-01 組件。走 E9-01 的文字格式解析（`parse_package`）。
// 失敗（名稱為空 / 上游解析錯）回 false 並填 `error`（含上游給的行號），**不回半套的 Package**。
bool make_media_control_component(const std::string& name, ds::package::Package& out,
                                  std::string& error);

// 只含封面資源的組件（用於上述插槽介面篩選的反例）。
bool make_artwork_only_component(const std::string& name, ds::package::Package& out,
                                 std::string& error);

// 一個部件在 widget 內的相對像素框。
struct MediaPartBox {
    std::string part;  // 具名部件 id（見上方 kMediaPart*）
    PaintBox box;      // widget 內相對、已套 DPI 的實際像素框（半開區間）
};

// 依呈現型態把媒體控制組件切成部件框。
//
// `placed` 是 `WidgetAssembly::resolve()` 交出來的該組件的框（已套 DPI）；本函式只在
// **那個框之內**切，不會畫到框外（每個部件框都滿足 `placed.box` 的包含關係，有測試釘住）。
//
//   `kMediaVariantExpanded`（或未知 / 空變體 → 視為 expanded）：
//     封面佔左側正方形（邊長取框高）、曲名與演出者在右上兩行、四顆按鈕在右下等寬一列。
//   `kMediaVariantCompact`：
//     無封面；曲名佔左側剩餘寬度、右側一顆按鈕（`playback == Playing` → pause，否則 play）。
//
// 呈現參數（`presentation.params`，皆為可選，具名、有預設）：
//   "button_px"  按鈕邊長（邏輯像素，預設 24）—— 已於 `placed` 的實際像素尺度下夾限
//   "pad_px"     內距（預設 4）
//
// `presentation.visible == false` → 回空清單（佔著插槽但本 widget 內不顯示）。
// 框退化（無可畫面積）→ 回空清單（不產出面積為零的部件框）。
std::vector<MediaPartBox> media_part_boxes(const ds::widgets::MediaControlRenderModel& model,
                                           const ResolvedComponent& placed);

// 診斷 / 斷言用：取某個具名部件的框；不存在回 nullptr。
const MediaPartBox* find_part(const std::vector<MediaPartBox>& parts, const std::string& part);

}  // namespace ds::host

#endif  // DS_HOST_COMPOSE_MEDIA_COMPOSE_HPP
