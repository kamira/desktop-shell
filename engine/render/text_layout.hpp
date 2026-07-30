// E4-01 文字排版與渲染 — 排版(layout) + 渲染描述（engine 層 / 子系統 render）
//
// 語意：把一段文字字串，依「可注入的字型度量介面」(`FontMetrics`：字元→advance 寬度、行高)
// 與一組「排版約束」(`LayoutConstraints`：最大寬度 / 換行 / 對齊 / 行高 / 最大行數 / 省略)，
// 排版成一份**渲染描述** (`LayoutResult`)——一組**行盒(LineBox)**與**字符(Glyph)**，各自帶
// **相對佈局**（行內偏移 x、行頂偏移 y、基線 baseline、advance 寬度）。供後續相位的繪製層消費。
//
// **相位 1 不做真實字型光柵化**：不觸碰 FreeType / CoreText / 任何 OS 字型 API；字型度量一律經
// **注入式 `FontMetrics`** 取得（相位 1 以 `FixedFontMetrics` 等固定度量或測試 stub 提供，真實
// 字型引擎於相位 2 實作同一介面）。本層純邏輯、平台中立：無 `#ifdef` / win32 / cocoa。
//
// **NFR-02 鐵律**：渲染描述**不含畫面絕對座標、不含數字 z-order**。所有位置皆為**相對偏移**：
//   - 字符 `x` 為「行內／版面盒內」相對偏移（自版面盒左緣起算，已含對齊位移），非螢幕座標。
//   - 行 `y` 為「自版面盒頂緣」相對偏移 = 行索引 × 行高，非螢幕座標。
//   - 目標 surface 以上游 E1-03 具名 `SurfaceId` 指涉（非數字 handle / z-order）。
//
// 錯誤不靜默（NFR-04 精神）：非法 UTF-8、非有限 / 負值的度量、非正行高、NaN 的約束寬度等
//   → 一律擲 `std::invalid_argument`（絕不回傳半份或靜默夾帶錯誤結果）。內容超出約束（過寬 /
//   超過最大行數）非錯誤——依 `ellipsis` 設定裁切並以 `truncated` 標記。
#ifndef DS_RENDER_E4_01_TEXT_LAYOUT_HPP
#define DS_RENDER_E4_01_TEXT_LAYOUT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：ds::kernel::SurfaceId（具名 surface）

namespace ds::render {

// 一個 Unicode 碼位（相位 1 以 char32_t 表達；排版只需碼位 → advance 映射）。
using CodePoint = char32_t;

// 水平對齊（具名，非座標）。
enum class TextAlign {
    Left,    // 靠左（行起點對齊盒左緣）
    Center,  // 置中
    Right,   // 靠右（行終點對齊盒右緣）
};

// 換行策略（具名）。
enum class WrapMode {
    None,  // 不自動換行：僅在硬換行 '\n' 斷行；過寬內容溢出（可配合 ellipsis 裁切）。
    Word,  // 詞界換行(word-wrap)：於空白處斷行使各行不超過最大寬度（過長單詞自成一行溢出）。
};

// -----------------------------------------------------------------------------
// FontMetrics —— 可注入的字型度量介面（NFR：相位 1 的字型抽象）。
//
// 排版只透過本介面取得度量，**完全不觸碰真實字型引擎**。相位 2 的 FreeType / CoreText
// 後端實作同一介面即可，排版邏輯一行不動。
//   - `advance(cp)`：該碼位的水平前進寬度（>= 0、有限）。
//   - `line_height()`：一行的高度（> 0、有限）。
//   - `ascent()`：基線以上高度（用於行盒 baseline）；預設等於行高，子類可精緻化。
// -----------------------------------------------------------------------------
class FontMetrics {
public:
    virtual ~FontMetrics() = default;
    virtual double advance(CodePoint cp) const = 0;
    virtual double line_height() const = 0;
    virtual double ascent() const { return line_height(); }
};

// 固定度量：等寬 —— 每個碼位同一 advance、固定行高。相位 1 的預設 / 測試度量。
//
// 建構參數非有限 / advance<0 / line_height<=0 / ascent<0 → std::invalid_argument。
class FixedFontMetrics : public FontMetrics {
public:
    // ascent < 0 表示「未指定」→ 採用 line_height 作為 ascent。
    FixedFontMetrics(double advance_per_char, double line_height, double ascent = -1.0);

    double advance(CodePoint /*cp*/) const override { return advance_; }
    double line_height() const override { return line_height_; }
    double ascent() const override { return ascent_; }

private:
    double advance_;
    double line_height_;
    double ascent_;
};

// -----------------------------------------------------------------------------
// 排版約束 —— layout(text, constraints) 的第二參數。純資料。
// -----------------------------------------------------------------------------
struct LayoutConstraints {
    // 版面盒最大寬度。<= 0 或非有限（inf）視為**無界**（不依寬度換行）。負值以外的無界一律合法；
    // NaN → std::invalid_argument（非法輸入不靜默）。詞界換行僅在此為正有限值時生效。
    double max_width = 0.0;

    // 最大行數。0 = 無限制。> 0 時超出的行被裁掉並標記 truncated（配合 ellipsis 於末行加省略號）。
    std::size_t max_lines = 0;

    WrapMode wrap = WrapMode::Word;  // 換行策略。
    TextAlign align = TextAlign::Left;  // 水平對齊。

    // 行高覆寫。<= 0 表示採用 FontMetrics::line_height()；> 0 則以此為每行高度（非有限 → 擲例外）。
    double line_height = 0.0;

    // 省略：內容因過寬（no-wrap 溢出）或超過 max_lines 被裁切時，於末端加省略字元。
    bool ellipsis = false;
    CodePoint ellipsis_char = 0x2026;  // '…' HORIZONTAL ELLIPSIS。
};

// 尺寸（相對量，非螢幕座標）。
struct Size {
    double width = 0.0;
    double height = 0.0;
};

// 單一字符的渲染描述 —— 相對佈局（NFR-02）。
struct Glyph {
    CodePoint codepoint = 0;
    double x = 0.0;        // 版面盒內相對 x 偏移（自盒左緣，已含對齊位移）。
    double advance = 0.0;  // 前進寬度。
    std::size_t line = 0;  // 所屬行索引（0-based）。
};

// 單一行盒的渲染描述 —— 相對佈局（NFR-02）。
struct LineBox {
    std::size_t begin = 0;   // 此行第一個 glyph 於 LayoutResult::glyphs 的索引。
    std::size_t count = 0;   // 此行 glyph 數（可為 0 = 空行）。
    double x = 0.0;          // 對齊造成的行起點相對偏移（Left=0；Center/Right > 0）。
    double y = 0.0;          // 行頂相對 y 偏移 = 行索引 × 行高。
    double width = 0.0;      // 行內容寬度（此行 glyph advance 總和；不含左側對齊留白）。
    double baseline = 0.0;   // 基線相對 y = y + ascent。
    bool ellipsized = false; // 此行是否因裁切而以省略字元結尾。
};

// 完整排版結果 —— 純資料渲染描述（NFR-02：全相對偏移、無絕對座標 / z-order）。
struct LayoutResult {
    std::vector<LineBox> lines;
    std::vector<Glyph> glyphs;
    Size size;                    // 版面盒尺寸：寬 = 對齊寬度、高 = 行數 × 行高。
    bool truncated = false;       // 是否有內容因裁切（過寬 / 超過 max_lines）被移除。
    ds::kernel::SurfaceId surface;  // 目標具名 surface（空字串 = 尚未綁定）。NFR-02 具名指涉。
};

// -----------------------------------------------------------------------------
// TextLayout —— 排版引擎。持有一個 FontMetrics 參考（不取得所有權；須存活於本物件之外的
// 生命週期內）與可選的目標 surface 綁定。對外提供 `layout()`（完整排版）與 `measure()`
// （只求尺寸）。純邏輯、可完全單元測試、平台中立。
// -----------------------------------------------------------------------------
class TextLayout {
public:
    // 綁定字型度量（不取得所有權）。可選給定目標具名 surface（NFR-02）。
    explicit TextLayout(const FontMetrics& metrics, ds::kernel::SurfaceId surface = {});

    // 目標具名 surface（可於建構後變更 / 清空）。空字串 = 未綁定。
    void set_surface(ds::kernel::SurfaceId id) { surface_ = std::move(id); }
    const ds::kernel::SurfaceId& surface() const noexcept { return surface_; }

    // 排版 UTF-8 文字為渲染描述。
    //   - text 必須為合法 UTF-8；非法序列 → std::invalid_argument（不靜默）。
    //   - constraints.line_height / max_width 非有限（含 NaN）、或度量回非有限 / 負值 →
    //     std::invalid_argument。
    //   - 空字串 → 空結果（無行、size{0,0}）。含 '\n' → 對應多行（空段落為 count=0 的空行）。
    LayoutResult layout(const std::string& text, const LayoutConstraints& constraints) const;

    // 只求版面盒尺寸（等價於 layout(text, constraints).size，但不建構 glyph 明細）。
    Size measure(const std::string& text, const LayoutConstraints& constraints = {}) const;

private:
    const FontMetrics& metrics_;
    ds::kernel::SurfaceId surface_;
};

// -----------------------------------------------------------------------------
// UTF-8 解碼工具（供排版；亦對外公開便於測試與其他 render 單元覆用）。
//
// 將 UTF-8 位元組序列解碼為碼位序列。遇非法序列（過短、非法續位元、過長編碼、越界碼位、
// 代理區碼位）→ std::invalid_argument（不靜默；不以 U+FFFD 取代）。
// -----------------------------------------------------------------------------
std::vector<CodePoint> decode_utf8(const std::string& text);

}  // namespace ds::render

#endif  // DS_RENDER_E4_01_TEXT_LAYOUT_HPP
