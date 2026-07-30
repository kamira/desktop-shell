// E4-12 可點文字區段 — 超連結風格的可點擊文字區段（module 層 / 子系統 elements）
//
// 語意：把一段文字中的「某個字元範圍」標記為一個具名可點擊區段（hyperlink span）——
// 例如一段文字「請見 [說明文件] 與 [聯絡我們]」中 `[說明文件]` 與 `[聯絡我們]` 各自
// 對應一個具名 id。本單元：
//   - 以上游 **E4-01**（`ds::render::TextLayout`）對整段文字排版，取得每個字元的相對幾何
//     （行內 x 偏移、所屬行、advance 寬度）。
//   - 以上游 **E1-04**（`ds::kernel::HitTester`）對「點擊落在哪個字元格」做幾何命中判定
//     （每個字元視為一個本地矩形：寬 = 該字元 advance、高 = 行高）。
//   - 命中時回傳該字元所屬區段的具名 id（若該字元不屬於任何已標記區段 → 未命中）。
//
// **NFR-02 鐵律**：本單元不新增任何絕對座標或數字 z-order —— 區段以「字元範圍」（碼位索引）
// 標記，幾何一律沿用 E4-01 的相對佈局（行內偏移 / 行索引），命中判定的輸入點為 E1-04 的
// 本地 / 相對座標 `LocalPoint`；`render_model()` 直接回傳 E4-01 的渲染描述（相對佈局，
// 目標 surface 以具名 `SurfaceId` 指涉）。
//
// **確定性字元→字符幾何映射（本單元的關鍵設計限制）**：E4-01 的 `WrapMode::Word`（詞界換行）
// 與 `ellipsis`（省略號裁切）會使排版輸出的字符序列與原始文字**不再一一對應**（換行處丟棄
// 待落地空白、裁切處移除字元）。可點區段依賴「原始字元索引 → 字符幾何」的**確定**映射，故本
// 單元的排版約束**固定要求** `wrap == WrapMode::None` 且 `ellipsis == false`——建構 /
// `set_constraints` 時違反者 → `std::invalid_argument`（不靜默）。在此限制下，原始文字的
// 每個碼位（除 `'\r'` 與硬換行 `'\n'` 本身不產生字符外）恰對應排版輸出中**依序**一個字符，
// 換行完全由顯式 `'\n'` 決定（可測「跨行」區段）。
//
// 錯誤不靜默：非法 UTF-8（沿用 E4-01 `decode_utf8`）、非法排版約束（沿用 E4-01）、
// 越界 / 反轉的字元範圍、空 id、與既有區段重疊的字元範圍 → 一律擲 `std::invalid_argument`。
#ifndef DS_ELEMENTS_E4_12_CLICKABLE_TEXT_HPP
#define DS_ELEMENTS_E4_12_CLICKABLE_TEXT_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "hit_test.hpp"     // E1-04（上游，可讀不可改）：HitTester / LocalPoint / Shape / make_rect
#include "text_layout.hpp"  // E4-01（上游，可讀不可改）：TextLayout / LayoutConstraints / LayoutResult

namespace ds::elements {

namespace detail {

// 預設排版約束：E4-01 之 `LayoutConstraints{}` 預設 `wrap = WrapMode::Word`，與本單元「確定性
// 映射」要求（wrap 必須 None）不相容，故本單元的預設值改為 `WrapMode::None`（其餘欄位沿用
// E4-01 預設：不換行不裁切、靠左、無最大行數）。
inline ds::render::LayoutConstraints default_clickable_constraints() {
    ds::render::LayoutConstraints c;
    c.wrap = ds::render::WrapMode::None;
    return c;
}

}  // namespace detail

// -----------------------------------------------------------------------------
// ClickableTextElement —— 可點文字區段元件。
//
// 持有一段完整文字（UTF-8）與其 E4-01 排版渲染描述，並維護一組「字元範圍 → 具名 id」的
// 可點區段標記。`hit_span(point)` 以 E1-04 幾何命中測試判定本地點落在哪個已標記區段。
//
// 不取得 FontMetrics 的所有權（承 E4-01 慣例）：其生命週期須涵蓋本物件。
// -----------------------------------------------------------------------------
class ClickableTextElement {
public:
    // metrics：字型度量（不取得所有權，生命週期須涵蓋本物件）。
    // constraints：排版約束（轉交 E4-01）；`wrap` 必須為 `WrapMode::None` 且 `ellipsis` 必須為
    //   false（見檔頭「確定性映射」說明），否則 std::invalid_argument。預設值見
    //   `detail::default_clickable_constraints()`（非 E4-01 的 `LayoutConstraints{}`，因其預設
    //   `wrap == WrapMode::Word` 與本單元要求不相容）。
    // surface：可選目標具名 surface（NFR-02，轉交 E4-01 渲染描述）。
    explicit ClickableTextElement(
        const ds::render::FontMetrics& metrics,
        ds::render::LayoutConstraints constraints = detail::default_clickable_constraints(),
        ds::kernel::SurfaceId surface = {});

    // 設定完整文字內容（UTF-8）。非法 UTF-8 序列 → std::invalid_argument（沿用 E4-01
    // decode_utf8 驗證，不靜默）。**成功設定新文字會清空所有已標記區段**（舊區段的字元範圍
    // 可能不再對應新文字的內容，保留將造成語意不明；呼叫端須於 set_text 後重新 add_span）。
    void set_text(const std::string& utf8_text);

    // 目前文字的碼位（字元）總數（= add_span 的 [start, end) 範圍上界）。
    std::size_t text_length() const noexcept { return char_to_glyph_.size(); }

    // 標記字元範圍 [start, end)（以 set_text 設定之文字的碼位索引計）為一個具名可點區段。
    //   - id 為空 → std::invalid_argument。
    //   - start >= end（空或反轉範圍）→ std::invalid_argument。
    //   - end > text_length()（越界）→ std::invalid_argument。
    //   - 與既有任一已標記區段的字元範圍重疊（不論 id 是否相同）→ std::invalid_argument
    //     （重疊區段語意不明，不靜默允許 / 覆蓋）。
    // 驗證失敗時不改變既有已標記區段狀態（strong exception guarantee）。
    void add_span(std::size_t start, std::size_t end, std::string id);

    // 清空所有已標記區段（文字與排版不變）。
    void clear_spans() noexcept;

    // 目前已標記的區段數。
    std::size_t span_count() const noexcept { return spans_.size(); }

    // 目前文字的排版渲染描述（供繪製消費；相對佈局，NFR-02，直接沿用 E4-01 輸出）。
    const ds::render::LayoutResult& render_model() const noexcept { return model_; }

    // 命中判定：本地點（E1-04 `LocalPoint`，元件本地相對座標）若落在任一已標記區段所涵蓋
    // 字元的字符幾何內（以 E1-04 `HitTester` 對「字符矩形：寬=advance、高=行高」做點內判定，
    // 含邊界）→ 回該區段 id；否則（含空文字 / 無任何區段 / 點落在字符間隙或無區段字元上）
    // 回 std::nullopt（無命中不是錯誤，不擲例外）。
    std::optional<std::string> hit_span(const ds::kernel::LocalPoint& point) const;

private:
    // 標記的可點區段：字元範圍（半開區間） + 具名 id。
    struct Span {
        std::size_t start = 0;
        std::size_t end = 0;
        std::string id;
    };

    // 依目前 model_ 的行高（排版時的有效行高，等同 E4-01 內部算法：constraints_.line_height
    // > 0 則用之，否則用 metrics_.line_height()）計算，供 hit_span 建構字符矩形高度。
    double effective_line_height() const;

    ds::render::TextLayout layout_;             // 轉交 E4-01 排版（含度量與 surface 綁定）
    ds::render::LayoutConstraints constraints_;  // 排版約束（固定 wrap=None、ellipsis=false）
    const ds::render::FontMetrics& metrics_;     // 不取得所有權；供 effective_line_height() 查詢

    ds::render::LayoutResult model_;  // 目前文字的排版渲染描述

    // 原始文字碼位索引 → model_.glyphs 索引的映射（見檔頭「確定性映射」）。
    // kNoGlyph：該碼位為 '\r' 或硬換行 '\n' 本身，不產生字符幾何（範圍涵蓋此類碼位不算越界，
    // 只是該碼位永遠不參與命中判定）。
    static constexpr std::size_t kNoGlyph = static_cast<std::size_t>(-1);
    std::vector<std::size_t> char_to_glyph_;

    std::vector<Span> spans_;

    ds::kernel::HitTester hit_tester_;  // 無狀態，供 hit_span 重複使用
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_12_CLICKABLE_TEXT_HPP
