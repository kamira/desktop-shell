// H1-06 繪製層的平台中立分派核心 —— kind → 畫法
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含平台條件編譯**，因此 ubuntu 的
// G3（含 -DDS_WERROR=ON）與 gate_sanitizers（ASan + UBSan）都真的建置並執行它的測試。
// 真實 GDI 呼叫留在 `widget_painter.cpp`（整份 if(WIN32)），由 `gate_windows` 承擔。
//
// 為什麼要這樣拆（照 W1-08 的先例，CHG-20261111-w1_08：政策層抽出守衛外；
// 更早的先例是 H1-01 自己——版面計算抽成不碰 GDI 的純函式）：
// 本單元的驗收標準**是平台承諾本身**——「新增一個元件型別後，不改 src/ 與 engine/
// 任何一行即可被畫出」。那是純邏輯命題，不需要 Windows。若把它留在 if(WIN32) 裡，
// ubuntu 的 G3 與 gate_sanitizers 一行都看不到，而 gate_windows 的第二道防線是一份
// 硬編碼名單（backend_win32 / tray_win32 / h1_01_paint / h1_02_controls），
// 不含本單元 —— 連「有沒有被建進來」都沒人查。
//
// ---------------------------------------------------------------------------
// 三個概念
// ---------------------------------------------------------------------------
//   1. `PaintSink`  —— **可注入的抽象繪製介面**。只有三個原語（填矩形 / 畫文字 / 畫指針），
//      顏色以**具名角色**（`PaintRole`）表達而非 RGB 數字：Windows 那側提供 GDI 實作，
//      測試提供記錄型假實作（錄下指令序列即可斷言）。
//   2. `ElementDraw` —— 平台中立的「一個元件要怎麼畫」描述，由各 E4 元件的 `RenderModel`
//      **投影**而來（E4-03 的投影見 `widget_render.hpp`；E4-18 見 `radial_draw.hpp`）。
//      `kind` 刻意是**具名字串鍵**而不是 enum —— enum 一加值就得改本層，字串鍵不必：
//      這正是「新增元件型別不改平台」得以成立的機制。
//   3. `ElementRendererTable` —— kind → renderer 的**分派表**。內建 E4-03 的三種
//      （bar / progress / gauge）；`register_kind()` 讓呼叫端在**不改本層任何一行**的
//      前提下加入新 kind。查不到 kind 時回報 `UnknownKind`，**不靜默**（NFR-04 精神）。
//
// NFR-02 說明：本層出現像素矩形（`PaintBox`）是刻意且必要的——繪製必然涉及座標，故繪製
// **不可能**成為 kernel 契約的一部分（見 `widget_painter.hpp` 檔頭第 2 點）。NFR-02 禁的是
// **kernel API** 的絕對座標與數字 z-order；`PaintBox` 不在任何 kernel 契約上，且上游送來的
// 渲染描述仍然只有具名 surface / 具名 slot / 比例值。
#ifndef DS_HOST_PAINT_RENDER_DISPATCH_HPP
#define DS_HOST_PAINT_RENDER_DISPATCH_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ds::host {

// 具名顏色角色（非 RGB 數字）。GDI 側以 `PaintTheme` 對映到 COLORREF，測試側直接斷言角色。
enum class PaintRole {
    Background,   // widget 底色
    Title,        // widget 標題文字
    Label,        // 指標標籤文字
    Value,        // 指標數值文字
    Track,        // 量表 / 長條未填滿的軌道
    Fill,         // 量表 / 長條已填滿的部分、指針本體
    Unavailable,  // NFR-03 降級條目的文字顏色
};

const char* to_string(PaintRole role) noexcept;

// 文字對齊（具名）。
enum class TextAlign { Left, Center, Right };

const char* to_string(TextAlign align) noexcept;

// 繪製層的像素矩形（半開區間 [left,right) x [top,bottom)，與 win32 RECT 同慣例）。
struct PaintBox {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const noexcept { return right - left; }
    int height() const noexcept { return bottom - top; }
    // 無可畫面積（寬或高非正）。退化尺寸是正常輸入（視窗被縮到極小），不是錯誤。
    bool empty() const noexcept { return width() <= 0 || height() <= 0; }
};

bool operator==(const PaintBox& a, const PaintBox& b) noexcept;
bool operator!=(const PaintBox& a, const PaintBox& b) noexcept;

// ---------------------------------------------------------------------------
// PaintSink —— 可注入的抽象繪製介面
// ---------------------------------------------------------------------------
class PaintSink {
public:
    virtual ~PaintSink() = default;

    // 以具名角色的顏色填滿一個矩形。
    virtual void fill_box(const PaintBox& box, PaintRole role) = 0;

    // 在矩形內單行、垂直居中地畫一段 **UTF-8** 文字（寬字元轉換是 GDI 側的事）。
    virtual void draw_text(const PaintBox& box, const std::string& utf8, PaintRole role,
                           TextAlign align) = 0;

    // 自 `dial` 的中心畫一根指針：角度為度、數學慣例（0° 指向右、逆時針為正；
    // y 軸朝下的翻轉由實作負責）。長度 / 粗細為相對於錶盤半徑的比例 [0,1]。
    virtual void draw_needle(const PaintBox& dial, double angle_degrees, double length_ratio,
                             double thickness_ratio, PaintRole role) = 0;

protected:
    PaintSink() = default;
    PaintSink(const PaintSink&) = default;
    PaintSink& operator=(const PaintSink&) = default;
};

// ---------------------------------------------------------------------------
// 內建 kind 鍵（具名字串，非數字）
// ---------------------------------------------------------------------------
// E4-03 `ElementKind` 的三種。E4-18（徑向線 / 指針）的鍵見 `radial_draw.hpp`——
// 它**不是**內建的：E4-18 是「後來才接上的元件型別」，正好用來證明擴充機制真的成立。
inline constexpr const char* kKindBar = "bar";
inline constexpr const char* kKindProgress = "progress";
inline constexpr const char* kKindGauge = "gauge";

// ---------------------------------------------------------------------------
// ElementDraw —— 一個元件的平台中立繪製描述
// ---------------------------------------------------------------------------
struct ElementDraw {
    std::string kind;  // 具名 kind 鍵；空字串 = 未指定（分派時視為 UnknownKind，不靜默）

    // NFR-03：該筆指標是否可用。false 時 renderer 仍畫軌道（使用者得知道這條量表存在），
    // 但**不畫**填充 / 指針 —— 「只灰掉該筆」的降級語意。
    bool available = true;

    bool vertical = false;     // E4-03 Orientation 的投影（Vertical = 由下往上填）
    double fill_ratio = 0.0;   // [0,1]，已由上游夾限；本層另自行防 NaN

    // --- 角度型（量表 / 指針）---
    bool has_angle = false;
    double start_angle_degrees = 0.0;
    double sweep_degrees = 0.0;
    double angle_degrees = 0.0;
    double length_ratio = 1.0;
    double thickness_ratio = 0.05;

    // --- 標籤（E4-03 的 show_label / label；進度讀數即走這裡）---
    bool show_label = false;
    std::string label;

    // 擴充 kind 自帶的具名參數（平台不認得其語意，只負責原樣遞給該 kind 的 renderer）。
    // 新增元件型別時不必、也不得修改本結構 —— 這個袋子就是為此存在的。
    std::map<std::string, double> params;

    // 取一個具名參數；不存在則回 fallback（不靜默新增鍵）。
    double param(const std::string& name, double fallback) const;
};

// 一個 kind 的畫法：對 `area`（該指標在版面上的格位）下繪製指令。
using ElementRenderer = std::function<void(const ElementDraw& draw, const PaintBox& area,
                                           PaintSink& sink)>;

// 分派結果（不靜默：查不到 / 沒面積都要說出來）。
enum class DispatchResult {
    Drawn,        // 找到 renderer 並已下指令
    UnknownKind,  // 分派表沒有這個 kind（含 kind 為空字串）
    EmptyArea,    // 格位沒有可畫面積（退化尺寸）；kind 本身是認得的
};

const char* to_string(DispatchResult result) noexcept;

// ---------------------------------------------------------------------------
// ElementRendererTable —— kind → renderer 的分派表
// ---------------------------------------------------------------------------
class ElementRendererTable {
public:
    // 空表（不含任何 kind）。任何分派皆回 UnknownKind。
    ElementRendererTable() = default;

    // 內建 E4-03 三種 kind 的表。
    static ElementRendererTable with_builtin_kinds();

    // 註冊一個 kind。key 為空、renderer 為空、或該 key 已存在 → 回 false 且**不覆寫**
    // （靜默覆寫等於讓兩個元件型別互相吃掉對方）。成功 → true。
    bool register_kind(std::string key, ElementRenderer renderer);

    // 明確取代既有 kind（例如主題換掉內建長條畫法）。key / renderer 合法即 true。
    // 與 register_kind 分成兩個名字，是為了讓「我知道我在覆寫」寫在呼叫端而非註解裡。
    bool replace_kind(std::string key, ElementRenderer renderer);

    bool has_kind(const std::string& key) const;
    std::size_t size() const noexcept { return renderers_.size(); }
    // 目前認得的 kind 鍵（字典序）。
    std::vector<std::string> kinds() const;

    // 依 `draw.kind` 查表並下繪製指令。
    DispatchResult draw(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) const;

private:
    std::map<std::string, ElementRenderer> renderers_;
};

// ---------------------------------------------------------------------------
// 版面幾何（純函式，不碰 GDI、不碰元件型別）
// ---------------------------------------------------------------------------
// 一列（一個指標）的版面。`cell` 是交給該 kind 的 renderer 的格位（整列高度）——
// **格位內怎麼畫由 kind 自己決定**，這正是「不再自訂固定版面」的落點。
// `track` / `dial` 是兩個常用的格位內子幾何，抽出來讓 renderer 共用、也讓 H1-01 的
// `RowLayout.track` 有單一來源。
struct RowGeometry {
    PaintBox label;  // 標籤文字區
    PaintBox cell;   // 元件格位（整列高度，標籤與數值之間）
    PaintBox track;  // 格位內垂直居中的細長軌道（線性 kind 用）
    PaintBox value;  // 數值文字區
};

// 版面尺度（像素）。預設值即 H1-01 自 CHG-20260803-05 起使用的那組，**不得隨意改動**：
// `tests/h1/test_h1_01.cpp` 直接斷言其幾何關係，而該檔不在本單元的 write_scope 內。
struct RowMetrics {
    int padding = 12;
    int title_height = 24;
    int label_width = 44;
    int value_width = 66;
    int gap = 8;
    int track_half_height = 5;
};

RowMetrics default_row_metrics() noexcept;

// 依「第 row 列，共 row_count 列」切出該列版面。
// row 超出範圍、row_count <= 0、或可用高度 <= 0 → 回傳全零（保守，不崩潰）。
RowGeometry row_geometry(const PaintBox& bounds, int row, int row_count,
                         const RowMetrics& metrics);
RowGeometry row_geometry(const PaintBox& bounds, int row, int row_count);

// 標題列（`bounds` 頂端、內縮 padding 的一條）。
PaintBox title_box(const PaintBox& bounds, const RowMetrics& metrics);

// 格位內垂直居中、半高為 half_height 的細長軌道。
PaintBox track_strip(const PaintBox& cell, int half_height);

// 格位內置中的正方形錶盤（邊長取寬高較小者）。
PaintBox dial_box(const PaintBox& cell);

// 把一個比例夾限至 [0,1]，並把 **NaN 當成 0**。
//
// `!(r > 0.0)` 而非 `r <= 0.0`：NaN 的任何比較都為 false，於是 `r <= 0.0` 對 NaN 不成立，
// NaN 會一路落到「比例正常」的分支、最後畫成滿的 —— 壞掉的讀數偽裝成 100%，正是知識庫
// K-002 的形狀。本函式是繪製層所有比例夾限的**唯一來源**（GDI 側的指針長度 / 粗細也用它），
// 免得同一條判斷在兩個平台上各寫一次、各錯一次。
double clamp_unit_ratio(double ratio) noexcept;

// 依 fill_ratio 算出軌道已填滿的部分。ratio 夾限於 [0,1]（NaN 視為 0 —— K-002：
// 壞掉的值不得偽裝成一個看起來正常的讀數）；vertical 時由**下往上**填。
PaintBox fill_span(const PaintBox& track, double fill_ratio, bool vertical);
PaintBox fill_span(const PaintBox& track, double fill_ratio);

}  // namespace ds::host

#endif  // DS_HOST_PAINT_RENDER_DISPATCH_HPP
