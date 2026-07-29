// E4-14 長文捲動與分頁 — 捲動 / 分頁核心（module 層 / 子系統 elements）
//
// 語意：把 E4-01 排版出的多行文字（`ds::render::LayoutResult::lines`），依「可視區高度」
// （以**行數**表達的 viewport）做**捲動**（scroll offset，行索引為單位）或**分頁**（page，
// 以 viewport 行數為一頁），產出「目前可見行範圍」的渲染描述、捲動 / 翻頁操作、捲動位置指示。
//
// **本單元不重繪 / 不複製 glyph 內容**：`RenderModel` 只回報「哪些內容行（`content_line` 索引，
// 指向上游 `LayoutResult::lines`）目前可見、對應到 viewport 內第幾行（`viewport_line`）」，實際
// 繪製資料（glyph / x / baseline...）由呼叫端依 `content_line` 回頭查 `LayoutResult` 取得——本層
// 只負責「可視區窗口」的邏輯，不越界重複上游職責。
//
// **NFR-02 鐵律**：捲動 / 分頁位置一律以**行索引**（`std::size_t`）與**比例**（`ratio` ∈ [0,1]）
// 表達，**不含任何螢幕絕對像素座標、不含數字 z-order**。`ScrollPosition::ratio` 是「捲動進度」的
// 比例值（0 = 頂端、1 = 底端），非座標。
//
// 錯誤不靜默：`set_viewport_lines(0)` 或尚未設定 viewport 即呼叫捲動 / 分頁 / 產出渲染描述的
// 方法 → 一律擲 `std::invalid_argument`（無效視窗不可靜默略過）。**捲動越界則夾限**（非錯誤）：
// `scroll_by` / `scroll_to` / `page_next` / `page_prev` 一律把結果夾限於 `[0, max_offset_lines()]`。
#ifndef DS_ELEMENTS_E4_14_SCROLL_VIEW_HPP
#define DS_ELEMENTS_E4_14_SCROLL_VIEW_HPP

#include <cstddef>
#include <vector>

#include "text_layout.hpp"  // E4-01（上游，可讀不可改）：ds::render::LayoutResult（多行排版結果）

namespace ds::elements {

// 目前可見的內容行範圍 —— 半開區間 [begin, end)，0-based 行索引，指向上游 LayoutResult::lines。
// begin == end 表示無可見行（例如內容為空）。
struct VisibleRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// 單一可見行的對應關係 —— viewport 內的相對行位置（NFR-02：行索引，非像素 y）。
struct VisibleLine {
    std::size_t content_line = 0;   // 指向上游 LayoutResult::lines 的索引
    std::size_t viewport_line = 0;  // 於 viewport 內的 0-based 相對偏移（0 = 目前可見的第一行）
};

// 捲動位置指示 —— 全部以行索引 / 比例表達（NFR-02：無絕對像素座標）。
struct ScrollPosition {
    std::size_t offset_lines = 0;      // 目前捲動偏移（自內容頂端起算之行數）
    std::size_t max_offset_lines = 0;  // 可捲動的最大偏移（content_lines 不足一頁時為 0）
    double ratio = 0.0;                // offset_lines / max_offset_lines，夾限於 [0,1]；
                                        // max_offset_lines==0（內容不足一頁）時恆為 0
    bool at_top = true;                // offset_lines == 0
    bool at_bottom = true;             // offset_lines == max_offset_lines
};

// 「目前可見內容」的渲染描述 —— 純資料，供繪製層依 content_line 回查 LayoutResult 取用實際
// glyph / 佈局細節；本結構本身不重複儲存像素幾何（NFR-02）。
struct RenderModel {
    std::vector<VisibleLine> lines;  // 目前可見的每一行（viewport_line 遞增排列）
    ScrollPosition position;         // 捲動位置指示
    std::size_t viewport_lines = 0;  // 目前 viewport 高度（行數）
    std::size_t content_lines = 0;   // 目前綁定內容的總行數

    // 分頁指示（以 viewport_lines 為一頁的近似頁碼；page_next()/page_prev() 產生的偏移恆對齊此
    // 分頁邏輯）。content_lines==0 時 page_count=0、page_index=0。
    std::size_t page_index = 0;  // 目前頁（0-based）
    std::size_t page_count = 0;  // 總頁數 = ceil(content_lines / viewport_lines)
};

// -----------------------------------------------------------------------------
// ScrollView —— 長文捲動 / 分頁核心。綁定一份 E4-01 排版結果的「行數」，以此為捲動範圍，
// 對外提供捲動（逐行）與分頁（逐 viewport）兩種操作，並產出目前可見範圍的渲染描述與捲動位置
// 指示。純邏輯、平台中立，不做真實繪製 / 不觸碰任何座標系統。
// -----------------------------------------------------------------------------
class ScrollView {
public:
    ScrollView() = default;

    // 綁定新內容（E4-01 排版結果）。只取用 `layout.lines.size()` 作為捲動範圍（NFR-02：捲動邏輯
    // 只認行索引，不重複儲存上游的像素幾何）。綁定新內容會把捲動偏移重設為 0（避免舊偏移對新
    // 內容失去意義而越界殘留）。
    void set_content(const ds::render::LayoutResult& layout);

    // 設定可視區高度（行數）。0 為無效視窗 → 擲 std::invalid_argument（不靜默）。
    // 變更 viewport 高度後，目前偏移會依新的 max_offset_lines() 重新夾限（不重設為 0）。
    void set_viewport_lines(std::size_t n);

    std::size_t viewport_lines() const noexcept { return viewport_lines_; }
    std::size_t content_lines() const noexcept { return content_lines_; }

    // 目前捲動偏移（行）。
    std::size_t offset_lines() const noexcept { return offset_lines_; }
    // 可捲動的最大偏移；內容不足一頁（content_lines <= viewport_lines）時為 0。
    // 尚未設定 viewport（viewport_lines()==0）時呼叫 → 擲 std::invalid_argument。
    std::size_t max_offset_lines() const;

    // --- 捲動（逐行）：越界夾限於 [0, max_offset_lines()]，非錯誤 ---
    // 尚未設定 viewport（viewport_lines()==0）時呼叫任一者 → 擲 std::invalid_argument（無效視窗
    // 不可靜默）。
    void scroll_by(long long delta_lines);       // 相對位移（可負）
    void scroll_to(std::size_t line_offset);     // 絕對定位

    // --- 分頁（以 viewport_lines 為一頁）：等價於 scroll_by(±viewport_lines())，同樣越界夾限 ---
    void page_next();
    void page_prev();

    // 目前可見的內容行範圍（半開區間）。尚未設定 viewport → 擲 std::invalid_argument。
    VisibleRange visible_range() const;

    // 目前可見內容的完整渲染描述（含捲動位置 / 分頁指示）。尚未設定 viewport →
    // 擲 std::invalid_argument。
    RenderModel render_model() const;

private:
    void require_viewport() const;  // viewport_lines_==0 → 擲例外
    void clamp_offset();            // 把 offset_lines_ 夾限於 [0, max_offset_lines_raw()]
    std::size_t max_offset_lines_raw() const noexcept;  // 不做 viewport 檢查的內部版本

    std::size_t viewport_lines_ = 0;  // 0 = 尚未設定（無效視窗）
    std::size_t content_lines_ = 0;
    std::size_t offset_lines_ = 0;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_14_SCROLL_VIEW_HPP
