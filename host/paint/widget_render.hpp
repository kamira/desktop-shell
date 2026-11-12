// H1-06 把 C2-02 的 SystemStatusRenderModel 畫出來 —— 平台中立的那一半
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含平台條件編譯**：ubuntu 的 G3
// （-DDS_WERROR=ON）與 gate_sanitizers 都真的建置並執行它的測試。GDI 在 `widget_painter.cpp`。
//
// 職責分兩件：
//   1. **投影**：`MetricRenderEntry`（內含 E4-03 `ds::elements::RenderModel`）→
//      `ElementDraw`（平台中立、kind 為具名字串鍵）。
//   2. **組版 + 分派**：背景 / 標題 / 每列（標籤文字 → 元件 → 數值文字），
//      元件怎麼畫**一律交給分派表**（`ElementRendererTable`），本層不再寫死任何一種畫法。
//      這就是「不再自訂固定版面」：H1-01 把三個指標都畫成同一個水平長條、
//      `entry.element.kind` 根本沒被讀過（見 CHG-20261112-h1_06 §現況）。
//
// ---------------------------------------------------------------------------
// 新增一個元件型別要改哪裡 —— 答案是「都不必改平台」
// ---------------------------------------------------------------------------
// 兩個擴充點都在呼叫端手上，`src/` / `engine/` / `modules/` 一行不動，連本檔也不必動：
//   - **畫法**：`renderer().register_kind("<新 kind>", ...)`
//   - **認領**：`add_projector(...)` —— 新元件型別有自己的 RenderModel 型別，投影成
//     `ElementDraw` 的那一步必須由認得它的人提供（E4-18 的範例見 `radial_draw.hpp`；
//     測試檔 `tests/h1/test_h1_06.cpp` 則在測試內自行定義一個前所未有的 kind）。
// 投影器依**註冊的反序**詢問（後註冊者先認領），第一個回 true 的勝出；都不認領則走內建
// 的 E4-03 投影。
#ifndef DS_HOST_PAINT_WIDGET_RENDER_HPP
#define DS_HOST_PAINT_WIDGET_RENDER_HPP

#include <functional>
#include <string>
#include <vector>

#include "render_dispatch.hpp"
#include "system_status_widget.hpp"  // C2-02（上游，可讀不可改）：SystemStatusRenderModel

namespace ds::host {

// 一次繪製的結果統計 —— 讓「有沒有真的畫出來」可被斷言，而不是只看畫面。
struct PaintSummary {
    int rows = 0;           // 收到幾筆指標
    int drawn = 0;          // 成功分派並畫出元件的筆數
    int degraded = 0;       // NFR-03 降級（available=false）的筆數
    int unknown_kind = 0;   // 分派表查不到 kind 的筆數（不靜默：這個數字一旦非 0 就是漏了畫法）
    int empty_area = 0;     // 格位無可畫面積（視窗被縮到極小）的筆數
};

// E4-03 `ElementKind` → 具名 kind 鍵（`kKindBar` / `kKindProgress` / `kKindGauge`）。
std::string element_kind_key(ds::elements::ElementKind kind);

// 內建投影：E4-03 `RenderModel` + 該筆的 available → `ElementDraw`。
//
// **上游限制（刻意不猜）**：C2-02 對 `available=false` 的條目**不填** element（維持預設
// 建構），所以降級列的 kind 一律是預設的 `bar`。繪製層沒有資訊可以還原它原本的 kind，
// 也不該自己發明一個 —— 降級列因此畫成「軌道 + 灰字」，與 H1-01 既有行為一致。
ElementDraw project_entry(const ds::widgets::MetricRenderEntry& entry);

// 投影器：認領一筆指標並填好它的 `ElementDraw`。回 true = 我認領了（`draw` 已填好）；
// 回 false = 不是我的，交給下一個 / 內建投影。
using ElementProjector =
    std::function<bool(const ds::widgets::MetricRenderEntry& entry, ElementDraw& draw)>;

// ---------------------------------------------------------------------------
// WidgetRenderer —— 組版 + 分派
// ---------------------------------------------------------------------------
class WidgetRenderer {
public:
    // 以指定的分派表建構（空表亦可：那會讓每一列都回報 unknown_kind，而非靜默空白）。
    explicit WidgetRenderer(ElementRendererTable table);

    // 內建 E4-03 三種 kind。
    static WidgetRenderer with_builtin_kinds();

    ElementRendererTable& renderer() noexcept { return table_; }
    const ElementRendererTable& renderer() const noexcept { return table_; }

    const RowMetrics& metrics() const noexcept { return metrics_; }
    void set_metrics(const RowMetrics& m) { metrics_ = m; }

    // 新增元件型別的認領入口（見檔頭）。
    void add_projector(ElementProjector projector);
    std::size_t projector_count() const noexcept { return projectors_.size(); }

    // 把整個 widget 畫到 sink 的 bounds 內（含背景與標題）。`title_utf8` 為 UTF-8。
    PaintSummary paint(const ds::widgets::SystemStatusRenderModel& model, const PaintBox& bounds,
                       const std::string& title_utf8, PaintSink& sink) const;

    // 單筆指標的投影（先問投影器，再退回內建），供呼叫端與測試檢查。
    ElementDraw project(const ds::widgets::MetricRenderEntry& entry) const;

private:
    ElementRendererTable table_;
    RowMetrics metrics_ = default_row_metrics();
    std::vector<ElementProjector> projectors_;
};

}  // namespace ds::host

#endif  // DS_HOST_PAINT_WIDGET_RENDER_HPP
