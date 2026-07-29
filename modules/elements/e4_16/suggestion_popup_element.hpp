// E4-16 輸入建議與補全呈現 — 建議清單彈出元件（module 層 / 子系統 elements）
//
// 語意：依上游 E4-15 就地輸入框（`TextInputElement`）綁定，呈現一份「輸入建議清單」
// (下拉/浮層) 的渲染描述——候選項文字以上游 E4-01 `TextLayout` 排版、標記目前選取項
// (供高亮)，並提供鍵盤上下選取 / Tab/Enter 套用補全的邏輯。
//
// **相位 1**：候選清單本身由呼叫端注入（`set_suggestions`）——本單元**不做**比對 / 過濾
// / 排序演算法，只負責「呈現 + 選取邏輯 + 套用」。真實模糊比對 / 排序留待呼叫端或後續
// 相位視需要處理，本單元介面不受影響。
//
// **NFR-02 鐵律**：渲染描述沿用 E4-01 的相對佈局：每個候選項各自獨立排版（單行），
// 項目之間僅以「索引 × 列高」表達垂直相對位移（承襲 `LineBox::y` 的語意，即「相對於
// 清單頂端」而非螢幕座標）；不含螢幕絕對座標、不含數字 z-order。目標 surface 以具名
// `SurfaceId` 指涉（承襲 E4-01）。
//
// 套用補全（`accept()`）：以目前選取候選項的文字**整段取代**綁定的 E4-15 輸入框內容
// （呼叫 `TextInputElement::set_text`）；套用後彈出視為關閉（候選清單清空、選取歸零）。
// 見下方「已知簡化」。
//
// 錯誤不靜默（NFR-04 精神）：
//   - `set_selected_index` 給出**越界索引**（`>= count()`；清單為空時 `count()==0`，
//     故任何索引皆越界）→ 擲 `std::out_of_range`（不夾限、不靜默忽略）。
//   - `move_selection()` / `accept()` / `selected_index()` / `selected_text()` 在
//     **候選清單為空**時呼叫 → 擲 `std::logic_error`（呼叫端明確的選取 / 套用 / 讀取
//     意圖，在無候選項下屬呼叫端狀態錯誤，不可靜默無動作或回傳未定義值）。
//   - 上述之外，方向式 `move_selection()` 於**非空清單**的邊界（已在首 / 末項）為安全
//     無動作(no-op)——屬正常導覽語意（與 E4-15 `move_cursor` 邊界處理同構的設計），
//     非「不靜默」規則的對象。
//   - `handle_key()` 是事件轉譯層：彈出**未顯示（候選清單為空）時**，導覽 / 套用鍵一律
//     安全略過（不呼叫 `move_selection`/`accept`，故不會觸發上述例外）——事件是否到達
//     由呼叫端狀態決定，屬正常事件分派語意，與「呼叫端顯式呼叫」的不靜默要求分屬不同
//     責任層級（同 E4-15 `handle_key` 忽略未知鍵的精神）。
//   - 候選字串本身若非合法 UTF-8，於 `render_model()` 排版時由上游 E4-01
//     `decode_utf8` 擲 `std::invalid_argument`（不重複驗證，天然繼承其不靜默保證）。
//
// 已知簡化（相位 1 / low risk 範圍內）：`accept()` 以「整段取代」套用補全，而非「僅
// 取代目前字詞 / 游標前綴」。就地輸入框相位 1 預設單行語意（`WrapMode::None`），呼叫端
// 若需要「部分詞取代」語意，可自行以 `TextInputElement` 既有 API（`select` + `insert`）
// 組合出更精細行為；本單元不越界猜測呼叫端的詞界規則。
#ifndef DS_ELEMENTS_E4_16_SUGGESTION_POPUP_ELEMENT_HPP
#define DS_ELEMENTS_E4_16_SUGGESTION_POPUP_ELEMENT_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "keyboard_input.hpp"      // E5-13（經 E4-15 PUBLIC 連結轉遞的上游，可讀不可改）：
                                    // KeyEvent / KeyAction / KeyboardInputSource / SubscriptionId
#include "text_input_element.hpp"  // E4-15（上游，可讀不可改）：TextInputElement / single_line_constraints
#include "text_layout.hpp"         // E4-01（上游，可讀不可改）：TextLayout / LayoutResult / FontMetrics

namespace ds::elements {

// 選取移動方向（具名，非座標）。
enum class SelectionMove {
    Up,
    Down,
};

// 單一候選項的渲染描述 —— 相對佈局（NFR-02）。
struct SuggestionItemRenderModel {
    std::size_t index = 0;            // 候選項索引（對應 set_suggestions 給定順序，0-based）。
    ds::render::LayoutResult layout;  // 候選文字排版結果（E4-01；各項獨立單行排版）。
    double y = 0.0;                   // 自清單頂端的相對 y 偏移 = index × row_height（NFR-02）。
    bool selected = false;            // 是否為目前選取項（呈現高亮的依據；實際視覺樣式留待繪製層）。
};

// 建議清單彈出的完整渲染描述 —— 純資料（NFR-02：全相對偏移、無絕對座標 / z-order）。
struct SuggestionPopupRenderModel {
    bool visible = false;                          // 候選清單非空 = true（呼叫端據此決定是否繪製浮層）。
    std::size_t selected_index = 0;                // 僅 visible 為 true 時有意義。
    double row_height = 0.0;                       // 每列高度（供呼叫端計算浮層整體尺寸）。
    std::vector<SuggestionItemRenderModel> items;  // 依序，含 selected 標記；visible 為 false 時為空。
};

// -----------------------------------------------------------------------------
// SuggestionPopupElement —— 輸入建議 / 自動補全的呈現與選取邏輯。
//
// 持有一份呼叫端注入的候選字串清單與目前選取索引；綁定一個上游 E4-01 `FontMetrics`
// （供內部 `TextLayout` 排版候選項文字；不取得所有權）與一個上游 E4-15
// `TextInputElement`（`accept()` 套用補全的目標；不取得所有權）——兩者皆須存活於
// 本物件之外的生命週期內。
// -----------------------------------------------------------------------------
class SuggestionPopupElement {
public:
    // 綁定字型度量（候選項排版用）與目標就地輸入框（accept() 套用補全的目標）。
    explicit SuggestionPopupElement(const ds::render::FontMetrics& metrics,
                                    TextInputElement& target);

    // --- 候選清單 ---
    // 設定候選清單（整段取代現有清單）；選取索引重置為 0（清單非空時）。
    // 可為空（清空 = 彈出隱藏，`visible()` 回傳 false）。
    void set_suggestions(std::vector<std::string> suggestions);
    // 目前候選清單（原始注入順序）。
    const std::vector<std::string>& suggestions() const noexcept { return suggestions_; }
    std::size_t count() const noexcept { return suggestions_.size(); }
    // 候選清單非空 = 彈出應顯示。
    bool visible() const noexcept { return !suggestions_.empty(); }

    // --- 選取 ---
    // 目前選取索引，範圍 [0, count())。清單為空時讀取 → std::logic_error（不靜默）。
    std::size_t selected_index() const;
    // 目前選取候選項文字（等同 suggestions()[selected_index()]）。
    // 清單為空 → std::logic_error（不靜默）。
    const std::string& selected_text() const;
    // 明確設定選取索引。index >= count()（含清單為空時的任何索引）→ std::out_of_range
    // （越界不靜默）。
    void set_selected_index(std::size_t index);
    // 方向式移動一格。清單為空 → std::logic_error（不靜默）。非空清單中已在首 / 末項的
    // 方向移動為安全無動作（正常導覽語意，見標頭上方說明）。
    void move_selection(SelectionMove move);

    // --- 套用補全 ---
    // 以目前選取候選項文字整段取代綁定輸入框內容（TextInputElement::set_text）；
    // 套用後彈出視為關閉（候選清單清空、選取歸零）。清單為空 → std::logic_error（不靜默）。
    void accept();

    // --- 鍵盤事件整合（注入式；相位 1 無真實鍵盤）---
    // 向一個 KeyboardInputSource 訂閱按鍵頻道，轉譯為選取 / 套用操作。回傳訂閱代號供
    // 呼叫端之後 unsubscribe()；本物件不管理來源生命週期（source 須存活於訂閱期間）。
    ds::events::SubscriptionId attach(ds::events::KeyboardInputSource& source);
    // 直接處理一個按鍵事件（供測試 / 手動驅動，無需透過 KeyboardInputSource）。
    // 僅 Press 動作生效；彈出未顯示（候選清單為空）時，導覽 / 套用鍵安全略過（不觸發
    // move_selection/accept，故不會擲出例外——事件到達與否由呼叫端狀態決定，屬正常
    // 事件分派語意，見標頭上方說明）：
    //   - ArrowUp   → move_selection(SelectionMove::Up)
    //   - ArrowDown → move_selection(SelectionMove::Down)
    //   - Tab / Enter → accept()
    //   - 其餘鍵 / Release 動作 → 忽略。
    void handle_key(const ds::events::KeyEvent& event);

    // --- 排版 / 渲染（E4-01）---
    void set_surface(ds::kernel::SurfaceId id) { layout_.set_surface(std::move(id)); }
    const ds::kernel::SurfaceId& surface() const noexcept { return layout_.surface(); }

    // 排版目前候選清單並回傳完整渲染描述（各項排版 + 選取標記；全相對佈局，NFR-02）。
    // 候選清單為空時回傳 visible=false、items 為空的渲染描述（非例外——「無建議可顯示」
    // 本身是合法且常見的呈現狀態；不靜默規則作用於選取 / 套用操作，見標頭上方說明）。
    SuggestionPopupRenderModel render_model() const;

private:
    const ds::render::FontMetrics& metrics_;
    ds::render::TextLayout layout_;
    TextInputElement& target_;
    std::vector<std::string> suggestions_;
    std::size_t selected_index_ = 0;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_16_SUGGESTION_POPUP_ELEMENT_HPP
