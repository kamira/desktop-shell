// E4-20 向量圖形 — 路徑 / 基本形狀 / 描邊填色的「向量渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：向量圖形元件——路徑(moveto/lineto/curveto)、基本形狀(矩形/圓/多邊形/線段)、
// 描邊/填色屬性，產出**向量渲染描述**(路徑指令序列 + 樣式)供後續相位的繪製層消費。
//
// 相位 1**不做真實繪製**：本單元只產出描述，不觸碰任何 OS / 繪圖 API、無 `#ifdef` / `win32` / `cocoa`。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 所有頂點 / 控制點皆為**元件本地座標系**（圖形資料本身的座標，如「這個圖示的路徑」），
//     不是螢幕像素位置；同一份路徑描述可放到畫面任何位置，位置換算是繪製層的事。
//   - 描邊寬度為**本地單位**，非螢幕像素。
//   - 指令 / 頂點的先後順序即是路徑的**連線順序**（語意順序），不是數字 z-order；本單元完全
//     不提供任何疊放層級欄位。
//   - 描邊 / 填色顏色的合成描述沿用上游 E1-03 `AlphaProfile`（opacity 為比例 [0,1]，非座標）。
//
// 不靜默失敗（NFR-04 精神）：
//   - 無效路徑（未 `move_to` 就 `line_to` / `curve_to` / `close`；重複 `close` 未先開新子路徑）
//     → 回 `VectorStatus::Invalid`，**不**追加該指令，路徑維持先前合法狀態。
//   - 退化形狀（零/負寬高矩形、零/負半徑圓、少於 3 頂點的多邊形、零長度線段）→ `build_shape_path`
//     回 `VectorStatus::Invalid`，且**不**寫入輸出路徑（不部分套用）。
//   - 非有限座標 / 寬高 / 半徑 / 不透明度（NaN / Inf）一律回 `Invalid`，不靜默改成預設值。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_20_VECTOR_GRAPHIC_HPP
#define DS_ELEMENTS_E4_20_VECTOR_GRAPHIC_HPP

#include <cstddef>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaProfile / AlphaMode（描邊/填色合成描述）

namespace ds::elements {

// 元件本地座標系中的一個點（NFR-02：圖形資料本身的座標，不是螢幕像素座標）。
struct Point {
    double x = 0.0;
    double y = 0.0;
};

// 操作結果碼 —— 與上游各 module 層單元同精神：明確、不靜默。
enum class VectorStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（無效路徑操作、退化形狀、非有限值等）；不部分套用
};

// 路徑指令種類。
enum class PathCommandKind {
    MoveTo,   // 開新子路徑於 to
    LineTo,   // 直線到 to
    CurveTo,  // 三次貝茲曲線，經 control1/control2 到 to
    Close,    // 封閉目前子路徑（連回最近一次 MoveTo 的起點）；不使用 to/control
};

// 一個路徑指令 —— 純資料，座標一律為元件本地座標系（NFR-02）。
struct PathCommand {
    PathCommandKind kind = PathCommandKind::MoveTo;
    Point to;        // MoveTo/LineTo 端點；CurveTo 終點。Close 不使用（保持預設值）。
    Point control1;  // 僅 CurveTo：第一控制點。
    Point control2;  // 僅 CurveTo：第二控制點。
};

// ---------------------------------------------------------------------------
// VectorPath —— 累積路徑指令（moveto/lineto/curveto/close），即時驗證合法性。
//
// 合法性規則（不靜默失敗；違反即回 Invalid 且不追加指令、內部狀態不變）：
//   - `line_to` / `curve_to` / `close` 前必須先有一次 `move_to` 開啟子路徑；否則 Invalid。
//   - 子路徑已 `close()` 後，`line_to` / `curve_to` / 再一次 `close()` 皆 Invalid，
//     除非先呼叫 `move_to()` 開新子路徑（`move_to` 恆可用，會重置「已 close」狀態）。
//   - 所有座標須為有限值（非 NaN / Inf），否則 Invalid。
// ---------------------------------------------------------------------------
class VectorPath {
public:
    VectorPath() = default;

    // 開新子路徑於 (x,y)。座標非有限 → Invalid。其餘情況恆成功（唯一無前置子路徑要求的指令）。
    VectorStatus move_to(double x, double y);
    // 直線到 (x,y)。尚未開放子路徑（未 move_to，或已 close 且未再 move_to）或座標非有限 → Invalid。
    VectorStatus line_to(double x, double y);
    // 三次貝茲曲線，經 (c1x,c1y) / (c2x,c2y) 控制點到 (x,y)。前置條件同 line_to。
    VectorStatus curve_to(double c1x, double c1y, double c2x, double c2y, double x, double y);
    // 封閉目前子路徑。尚未開放子路徑（未 move_to，或已 close 且未再 move_to）→ Invalid。
    VectorStatus close();

    // 清空所有指令，回到初始狀態。
    void clear() noexcept;

    const std::vector<PathCommand>& commands() const noexcept { return commands_; }
    bool empty() const noexcept { return commands_.empty(); }
    // 目前是否有「已開放尚未 close」的子路徑。
    bool is_open_subpath() const noexcept { return started_ && !closed_; }

private:
    std::vector<PathCommand> commands_;
    bool started_ = false;  // 是否曾經 move_to 過。
    bool closed_ = false;   // 最近一次子路徑是否已 close。
};

// ---------------------------------------------------------------------------
// 描邊（stroke）/ 填色（fill）樣式 —— 純資料。粗細為元件本地單位（非螢幕像素；NFR-02）；
// 顏色 / 合成不透明度沿用上游 E1-03 `AlphaProfile`（mode + opacity 比例 [0,1]）。
// ---------------------------------------------------------------------------
struct StrokeStyle {
    bool enabled = false;
    double width = 1.0;                // 本地單位；`enabled=true` 時須 > 0。
    ds::kernel::AlphaProfile paint{};  // 描邊顏色的合成描述（opacity 為比例，符合 NFR-02）。
};

struct FillStyle {
    bool enabled = false;
    ds::kernel::AlphaProfile paint{};  // 填色的合成描述。
};

// ---------------------------------------------------------------------------
// 基本形狀 —— `Shape{Rect,Circle,Polygon,Line}`：以 `kind` 標記使用哪一組欄位。
// 全部座標 / 尺寸皆為元件本地座標系（NFR-02，非螢幕像素）。
// ---------------------------------------------------------------------------
enum class ShapeKind {
    Rect,
    Circle,
    Polygon,
    Line,
};

struct Shape {
    ShapeKind kind = ShapeKind::Rect;

    // Rect：origin 為左上角（本地座標系），width x height。
    Point origin;
    double width = 0.0;
    double height = 0.0;

    // Circle：圓心 + 半徑。
    Point center;
    double radius = 0.0;

    // Polygon：頂點序列（依序連線；至少 3 點才成形，順序即連線順序而非 z-order）。
    std::vector<Point> vertices;

    // Line：兩端點（線段無面積，恆為開放路徑，不可 close / 不可填色；零長度視為退化）。
    Point from;
    Point to;

    static Shape make_rect(Point origin, double width, double height);
    static Shape make_circle(Point center, double radius);
    static Shape make_polygon(std::vector<Point> vertices);
    static Shape make_line(Point from, Point to);
};

// 把基本形狀轉換為路徑指令序列，寫入 `out`（成功時覆蓋 `out` 既有內容；失敗時 `out` 維持
// 呼叫前狀態不變，不部分套用）。
//   - Rect  → MoveTo(origin) + 3×LineTo（矩形四角）+ Close。
//   - Circle → MoveTo + 4×CurveTo（三次貝茲圓弧近似）+ Close。
//   - Polygon → MoveTo(vertices[0]) + LineTo(其餘頂點，依序) + Close。
//   - Line  → MoveTo(from) + LineTo(to)（不 close：線段無面積）。
// 退化形狀（零/負寬高矩形、零/負半徑圓、<3 頂點多邊形、零長度線段）或任一座標 / 尺寸非有限值
// → `VectorStatus::Invalid`，不寫入 `out`（不靜默）。
VectorStatus build_shape_path(const Shape& shape, VectorPath& out);

// 向量圖形的**渲染描述模型**——供後續相位的繪製層消費。相位 1 不含任何真實繪製。
struct VectorRenderModel {
    std::vector<PathCommand> commands;  // 路徑指令序列（本地座標系，NFR-02）。
    StrokeStyle stroke;
    FillStyle fill;
    bool empty = true;  // 路徑無任何指令時為 true（明確、不靜默假裝有資料）。
};

// ---------------------------------------------------------------------------
// VectorGraphic —— 向量圖形元件：持有一條路徑 + 描邊 / 填色樣式，產出渲染描述模型。
// 純邏輯、平台中立、無真實繪製。
// ---------------------------------------------------------------------------
class VectorGraphic {
public:
    VectorGraphic() = default;

    // 設定路徑（複製一份；`VectorPath` 本身已在指令累積時保證合法性，故此處為單純設值)。
    void set_path(const VectorPath& path) { path_ = path; }
    const VectorPath& path() const noexcept { return path_; }

    // 設定描邊樣式。`width` / `paint.opacity` 非有限值，或 `enabled=true` 卻 `width<=0`
    // → Invalid（不套用）。成功 → Ok（`paint.opacity` 會被 clamp 至 [0,1]）。
    VectorStatus set_stroke(const StrokeStyle& stroke);
    // 設定填色樣式。`paint.opacity` 非有限值 → Invalid（不套用）。成功 → Ok（opacity clamp）。
    VectorStatus set_fill(const FillStyle& fill);
    const StrokeStyle& stroke() const noexcept { return stroke_; }
    const FillStyle& fill() const noexcept { return fill_; }

    // 產出目前狀態的渲染描述模型：路徑指令序列 + 描邊 / 填色樣式。空路徑 → `empty=true`
    // 且 `commands` 為空（明確，不靜默假裝有資料）。
    VectorRenderModel render_model() const;

private:
    VectorPath path_;
    StrokeStyle stroke_;
    FillStyle fill_;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_20_VECTOR_GRAPHIC_HPP
