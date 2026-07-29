// E4-13 文字流內嵌物件 — 文字排版流中的內嵌非文字物件（module 層 / 子系統 elements）
//
// 語意：在文字排版流中插入**非文字物件**（如表情符號 / 小圖示 / 行內圖片）——該物件**佔一個
// 排版位**，與文字一起換行、對齊，不是疊加在文字上方的獨立圖層。排版（換行寬度預留、行內
// 相對位置、行高/基線計算）重用上游 **E4-01**（`FontMetrics` 度量介面、`LayoutConstraints`
// 換行/對齊約束、`decode_utf8` UTF-8 解碼），內嵌物件本身的顯示描述（來源參照/縮放模式/裁切/
// 透明度/目標 surface）重用上游 **E4-02** 的 `ImageRenderModel`（呼叫端以 `ImageElement`
// 產出後傳入 `add_inline_object`）。本單元本身**不做**文字光柵化、不做影像解碼/繪製，只產出
// 一份「文字 + 內嵌物件」混排的**渲染描述**（`InlineFlowResult`）供後續相位的繪製層消費。
//
// 換行演算法：把累積的文字（拆成字符/空白）與內嵌物件（視為不可拆的單一排版單位，寬度即
// `add_inline_object` 給定的 `size.width`）攤平成一個 token 序列，重用與 E4-01 相同的貪婪
// 詞界換行規則——內嵌物件在換行判斷上等價於「一個字」：只在其前後為空白/換行處可斷行，物件
// 本身不可再拆。這讓內嵌物件能與文字混合出現在同一行、也能被自然換到下一行。
//
// 相位 1 平台中立：無 `#ifdef` / win32 / cocoa，不觸碰任何 OS 字型或影像 API。
//
// **NFR-02 鐵律**：渲染描述**不含畫面絕對座標、不含數字 z-order**（同 E4-01 精神）：
//   - 字符 / 內嵌物件的 `x` 為「行內相對偏移」（自版面盒左緣起算，已含對齊位移）。
//   - 行 `y` 為「自版面盒頂緣」相對偏移（逐行累加各行有效行高，非螢幕座標）。
//   - 目標 surface 以具名 `ds::kernel::SurfaceId` 指涉；內嵌物件的顯示描述沿用 E4-02
//     `ImageRenderModel`（其目標 / 裁切皆已是具名 / 比例式，無數字座標）。
//
// 錯誤不靜默：
//   - 文字內容非法 UTF-8 → `std::invalid_argument`（重用 E4-01 `decode_utf8`，與其同精神：
//     結構性壞資料一律擲例外，不以替代字元靜默吞掉）。
//   - 內嵌物件無效（`ImageRenderModel::has_source == false`，即尚未成功 `set_source` 的
//     E4-02 元件）或尺寸非法（非正 / 非有限）→ 回 `InlineFlowStatus::Invalid`，**不**加入
//     （與 E4-02 `ImageElement` 對無效輸入的處理同精神：明確拒絕，不部分套用）。
//   - 排版約束非法（`NaN` 寬度、非有限行高等）→ `std::invalid_argument`（同 E4-01）。
//
// 命名空間 `ds::elements`（與 E4-02 同子系統命名空間）。
#ifndef DS_ELEMENTS_E4_13_INLINE_FLOW_ELEMENT_HPP
#define DS_ELEMENTS_E4_13_INLINE_FLOW_ELEMENT_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "image_element.hpp"  // E4-02（上游，可讀不可改）：ImageRenderModel（內嵌物件顯示描述）
#include "text_layout.hpp"    // E4-01（上游，可讀不可改）：FontMetrics / LayoutConstraints /
                              // TextAlign / WrapMode / CodePoint / Size / decode_utf8

namespace ds::elements {

// 操作結果碼 —— 與 E4-02 `ImageStatus` 同精神：明確、不靜默。
enum class InlineFlowStatus {
    Ok,       // 操作成功
    Invalid,  // 無效內嵌物件（無來源）或非法尺寸（非正 / 非有限）；不套用
};

// 內嵌流中的單一文字字符 —— 相對佈局（NFR-02），欄位語意同 E4-01 `Glyph`。
struct FlowGlyph {
    ds::render::CodePoint codepoint = 0;
    double x = 0.0;        // 版面盒內相對 x 偏移（自盒左緣，已含對齊位移）。
    double advance = 0.0;  // 前進寬度。
    std::size_t line = 0;  // 所屬行索引（0-based）。
};

// 內嵌流中的單一內嵌物件放置 —— 相對佈局（NFR-02）。物件佔一個排版位，與文字並列同一行序。
struct FlowObject {
    ImageRenderModel image;  // E4-02 顯示描述（來源參照/縮放模式/裁切/透明度/目標 surface）。
    double x = 0.0;          // 版面盒內相對 x 偏移（同 FlowGlyph::x 語意）。
    double width = 0.0;      // 佔用寬度（即 `add_inline_object` 給定的 `size.width`）。
    double height = 0.0;     // 佔用高度（即 `add_inline_object` 給定的 `size.height`）。
    std::size_t line = 0;    // 所屬行索引（0-based）。
};

// 單一行盒的渲染描述 —— 相對佈局（NFR-02）。與 E4-01 `LineBox` 同精神，但行高可因本行內
// 內嵌物件較高而放大（`height = max(基礎行高, 本行內嵌物件最大 height)`），故逐行可能不等高。
struct FlowLine {
    double x = 0.0;          // 對齊造成的行起點相對偏移（Left=0；Center/Right > 0）。
    double y = 0.0;          // 行頂相對 y 偏移（前面各行有效行高的累加，非螢幕座標）。
    double width = 0.0;      // 行內容寬度（文字 + 物件 advance 總和；不含左側對齊留白）。
    double height = 0.0;     // 本行有效行高（見上）。
    double baseline = 0.0;   // 基線相對 y = y + ascent（ascent 取自 FontMetrics，不受物件影響）。
    bool ellipsized = false; // 此行是否因裁切而以省略字元結尾。
};

// 完整內嵌流排版結果 —— 純資料渲染描述（NFR-02：全相對偏移、無絕對座標 / z-order）。
struct InlineFlowResult {
    std::vector<FlowLine> lines;
    std::vector<FlowGlyph> glyphs;    // 純文字部分（依 line 分組，可用 FlowGlyph::line 篩選）。
    std::vector<FlowObject> objects;  // 內嵌物件部分（依 line 分組，可用 FlowObject::line 篩選）。
    ds::render::Size size;            // 版面盒尺寸：寬 = 對齊寬度，高 = 各行有效行高總和。
    bool truncated = false;           // 是否有內容因裁切（過寬 / 超過 max_lines）被移除。
    ds::kernel::SurfaceId surface;    // 目標具名 surface（空字串 = 尚未綁定）。NFR-02 具名指涉。
};

// -----------------------------------------------------------------------------
// InlineFlowElement —— 文字流內嵌物件元件：累積文字 + 內嵌物件，產出混排渲染描述。
//
// 持有一個 `FontMetrics` 參考（不取得所有權；同 E4-01 `TextLayout` 慣例，須存活於本物件
// 之外的生命週期內）。文字以 `add_text` 逐段附加（依呼叫順序串接），內嵌物件以
// `add_inline_object` 附加（佔一個排版位，插入於目前流的末端）。`render_model` 對目前累積
// 的完整內容做一次排版，純查詢、不修改狀態，可重複呼叫（例如以不同 `LayoutConstraints`
// 重新排版同一份內容）。
// -----------------------------------------------------------------------------
class InlineFlowElement {
public:
    // 綁定字型度量（不取得所有權，重用 E4-01 `FontMetrics`）。可選給定目標具名 surface。
    explicit InlineFlowElement(const ds::render::FontMetrics& metrics,
                                ds::kernel::SurfaceId surface = {});

    // 目標具名 surface（可於建構後變更 / 清空）。空字串 = 未綁定。
    void set_surface(ds::kernel::SurfaceId id) { surface_ = std::move(id); }
    const ds::kernel::SurfaceId& surface() const noexcept { return surface_; }

    // 附加一段文字（UTF-8）至流末端。
    //   - 非法 UTF-8 → `std::invalid_argument`（重用 E4-01 `decode_utf8`，不靜默）。
    //   - 空字串 → 合法無操作，回 `Ok`。
    //   - `\n` 視為硬換行（同 E4-01）；`\r` 於排版時正規化略去。
    InlineFlowStatus add_text(const std::string& text);

    // 附加一個內嵌物件至流末端 —— 佔一個排版位，與文字一起換行對齊（不可再拆）。
    //   - `image`：E4-02 `ImageElement::render_model()` 的輸出；`image.has_source == false`
    //     （無效 / 未載入來源）→ `Invalid`，**不**加入。
    //   - `size`：此物件在文字流中佔用的排版尺寸（與來源影像固有像素尺寸無關，NFR-02：
    //     相對量，非螢幕座標）。`width`/`height` 非正或非有限 → `Invalid`，**不**加入。
    InlineFlowStatus add_inline_object(const ImageRenderModel& image, ds::render::Size size);

    // 清空目前累積的文字 + 內嵌物件內容（回到空流）。恆成功。
    void clear() noexcept;
    // 目前流是否為空（未曾成功 `add_text`/`add_inline_object` 過任何內容）。
    bool empty() const noexcept { return tokens_.empty(); }
    // 目前流中已成功加入的內嵌物件數。
    std::size_t object_count() const noexcept { return object_count_; }

    // 對目前累積內容排版，產出文字 + 內嵌物件混排的渲染描述。
    //   - `constraints`：重用 E4-01 `LayoutConstraints`（換行寬度 / 最大行數 / 換行策略 /
    //     對齊 / 行高覆寫 / 省略）。非法（`NaN` 寬度、非有限行高等）→ `std::invalid_argument`
    //     （同 E4-01）。
    //   - 空流 → 空結果（無行、`size{0,0}`）。
    //   - 純查詢，不修改本物件狀態；可重複以不同約束呼叫對同一份內容重新排版。
    InlineFlowResult render_model(
        const ds::render::LayoutConstraints& constraints = {}) const;

private:
    // 排版期間的內部 token（文字字符 / 空白 / 硬換行 / 內嵌物件）。
    enum class TokenKind { Char, Space, NewLine, Object };
    struct FlowToken {
        TokenKind kind = TokenKind::Char;
        ds::render::CodePoint cp = 0;    // 有效於 Char / Space（含省略字元）。
        double advance = 0.0;             // Char/Space：字型度量前進寬度；Object：物件 size.width。
        std::size_t object_index = 0;     // 有效於 Object：objects_ / object_sizes_ 的索引。
    };

    const ds::render::FontMetrics& metrics_;
    ds::kernel::SurfaceId surface_;

    std::vector<FlowToken> tokens_;          // 依附加順序的完整 token 序列。
    std::vector<ImageRenderModel> objects_;  // 已加入的內嵌物件顯示描述（依加入順序）。
    std::vector<ds::render::Size> object_sizes_;  // 對應 objects_ 的排版尺寸。
    std::size_t object_count_ = 0;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_13_INLINE_FLOW_ELEMENT_HPP
