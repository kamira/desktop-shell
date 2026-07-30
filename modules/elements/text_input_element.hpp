// E4-15 就地輸入框 — 可編輯文字欄位（module 層 / 子系統 elements）
//
// 語意：就地(inline)文字輸入框元件——維護一段可編輯文字：游標位置(codepoint 索引)、
// 插入/刪除字元、選取範圍；以上游 E4-01 `TextLayout` 排版顯示文字，以上游 E5-13
// `KeyboardInputSource` 接收**注入式**輸入事件驅動編輯狀態（相位 1 不做真實鍵盤 / IME）。
// 產出輸入框渲染描述（文字排版 + 游標 + 選取），供後續相位的繪製層消費。
//
// **NFR-02 鐵律**：渲染描述沿用 E4-01 的相對佈局（行內偏移 x、行頂偏移 y、基線）；
// 本單元自身不引入任何螢幕絕對座標或數字 z-order。
//
// 錯誤不靜默：`set_cursor` / `select` 給出**超出文字長度**的索引 → 擲 `std::out_of_range`
// （不夾限、不靜默忽略）。`insert` / `set_text` 給非法 UTF-8 → 由上游 `decode_utf8` 擲
// `std::invalid_argument`。方向式游標移動（`move_cursor`）在邊界為安全的無動作（no-op），
// 屬正常編輯語意而非非法操作，與顯式索引式操作（`set_cursor` / `select`）的越界檢查區分。
//
// 已知簡化（相位 1 / low risk 範圍內）：游標與選取的視覺座標映射對本元件預設約束
// （`WrapMode::None`，即不自動換行、僅顯式 '\n' 產生新行）精確。若呼叫端改用會實際觸發
// 自動換行的約束（`WrapMode::Word` 且 `max_width` 為正有限值），因排版層可能於折行處丟棄
// 行尾空白字符（不產生對應字符），游標 / 選取的字元索引→字符索引映射為近似（夾限至可用
// 字符範圍）；不影響文字內容本身的正確性，僅可能使極端情境下的游標視覺位置略有偏差。
#ifndef DS_ELEMENTS_E4_15_TEXT_INPUT_ELEMENT_HPP
#define DS_ELEMENTS_E4_15_TEXT_INPUT_ELEMENT_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "keyboard_input.hpp"  // E5-13（上游，可讀不可改）：KeyEvent / TextInputEvent / KeyboardInputSource
#include "text_layout.hpp"     // E4-01（上游，可讀不可改）：TextLayout / LayoutResult / FontMetrics

namespace ds::elements {

// 就地輸入框的預設排版約束：WrapMode::None（單行語意；換行僅由顯式 '\n' 產生）。
// 供建構子預設參數與呼叫端覆用（例如自訂約束後想恢復預設）。
ds::render::LayoutConstraints single_line_constraints();

// 游標方向式移動的粒度（具名，非座標）。
enum class CursorMove {
    Left,   // 左移一個字符（codepoint）；已在起點則無動作。
    Right,  // 右移一個字符；已在終點則無動作。
    Home,   // 移至文字最前（索引 0）。
    End,    // 移至文字最末（索引 = 字符數）。
};

// 選取範圍 —— 以 codepoint 索引表達 [begin, end)（非螢幕座標）。
// has_selection() 為 false 時 begin == end（收斂於游標）。
struct SelectionRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// 游標的渲染描述 —— 相對佈局（NFR-02）：x/y/baseline 皆承襲 E4-01 的相對偏移語意。
struct CursorRenderInfo {
    std::size_t index = 0;  // 游標的 codepoint 索引（原始文字，含換行）。
    std::size_t line = 0;   // 所屬排版行索引（承襲 E4-01 LineBox 索引）。
    double x = 0.0;         // 行內相對 x 偏移（游標應繪製處，緊接該字符之前）。
    double y = 0.0;         // 行頂相對 y 偏移。
    double baseline = 0.0;  // 基線相對 y。
};

// 一段選取高亮的相對矩形（每排版行至多一段；選取跨多行則每行各一段）。
// width/height 為相對量值（非座標）；height 取自排版行高。
struct SelectionRect {
    std::size_t line = 0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// 就地輸入框的完整渲染描述 —— 純資料（NFR-02：全相對偏移、無絕對座標 / z-order）。
struct TextInputRenderModel {
    ds::render::LayoutResult layout;  // E4-01 排版結果（文字本身）。
    CursorRenderInfo cursor;          // 游標位置。
    bool has_selection = false;
    SelectionRange selection;              // has_selection 為 false 時為空範圍（begin==end）。
    std::vector<SelectionRect> selection_rects;  // has_selection 為 false 時為空。
};

// -----------------------------------------------------------------------------
// TextInputElement —— 就地文字輸入框。
//
// 持有一份可編輯文字（以 codepoint 序列為內部表示）、游標與選取狀態，內部另持有一個
// E4-01 `TextLayout`（綁定呼叫端注入的 `FontMetrics`）用於 `render_model()` 排版顯示。
// 編輯操作可經公開 API 直接呼叫，亦可（可選）附掛一個 E5-13 `KeyboardInputSource`
// 以**注入式**事件驅動——相位 1 無真實鍵盤 / IME，`handle_key` / `handle_text_input`
// 純為事件→編輯操作的轉譯，完全可單元測試。
// -----------------------------------------------------------------------------
class TextInputElement {
public:
    // 綁定字型度量（不取得所有權；metrics 須存活於本物件之外的生命週期內）。
    explicit TextInputElement(
        const ds::render::FontMetrics& metrics,
        ds::render::LayoutConstraints constraints = single_line_constraints());

    // --- 文字內容 ---
    // 以合法 UTF-8 取代整段內容；游標移至文字末端、清除選取。非法 UTF-8 → std::invalid_argument。
    void set_text(const std::string& utf8_text);
    // 目前內容（重新編碼自內部 codepoint 序列的 UTF-8 字串）。
    std::string text() const;
    // 文字長度（codepoint 數，含換行字元）。
    std::size_t length() const noexcept { return codepoints_.size(); }

    // --- 編輯操作 ---
    // 於游標處插入合法 UTF-8 文字（若有選取，先取代選取範圍）；游標移至插入內容之後、清除選取。
    // 非法 UTF-8 → std::invalid_argument（不靜默）。
    void insert(const std::string& utf8_text);
    // 刪除游標前一個字符（若有選取，改為刪除選取範圍）。游標在起點且無選取 → 無動作。
    void backspace();
    // 刪除游標所在位置的字符（若有選取，改為刪除選取範圍）。游標在終點且無選取 → 無動作。
    void erase_forward();

    // --- 游標 ---
    // 目前游標 codepoint 索引，範圍 [0, length()]。
    std::size_t cursor() const noexcept { return cursor_; }
    // 絕對設定游標位置。index > length() → std::out_of_range（越界不靜默）。設定後清除選取。
    void set_cursor(std::size_t index);
    // 方向式移動一格。extend_selection=true 時延伸（或建立）選取，錨點固定於移動前的游標；
    // false 則移動並清除選取。邊界處為安全無動作（非法索引才擲例外，見標頭說明）。
    void move_cursor(CursorMove move, bool extend_selection = false);

    // --- 選取 ---
    // 明確設定選取範圍：錨點(anchor)=begin（固定端）、游標(cursor)=end（活動端）；
    // begin 可大於 end（表達反向選取，即 shift+左移造成的選取方向）。
    // begin 或 end > length() → std::out_of_range（越界不靜默）。
    void select(std::size_t begin, std::size_t end);
    // 清除選取（錨點收斂至目前游標）。
    void clear_selection() noexcept { anchor_ = cursor_; }
    bool has_selection() const noexcept { return anchor_ != cursor_; }
    // 正規化選取範圍（begin<=end）；無選取時 begin==end==cursor()。
    SelectionRange selection() const noexcept;

    // --- E5-13 事件整合（注入式；相位 1 無真實鍵盤 / IME）---
    struct Subscriptions {
        ds::events::SubscriptionId key_id = 0;
        ds::events::SubscriptionId text_id = 0;
    };
    // 向一個 KeyboardInputSource 訂閱按鍵 + 文字提交頻道，轉譯為編輯操作。
    // 回傳訂閱代號供呼叫端之後 unsubscribe()；本物件不管理來源生命週期（source 須存活
    // 於訂閱期間）。
    Subscriptions attach(ds::events::KeyboardInputSource& source);
    // 直接處理一個按鍵事件（供測試 / 手動驅動，無需透過 KeyboardInputSource）。
    // 僅 Press 動作生效；已知導覽 / 編輯鍵（方向鍵、Home/End、Backspace、Delete）之外
    // 的鍵一律忽略（字元輸入經 handle_text_input 到達，相位 1 不做鍵→字元映射）。
    void handle_key(const ds::events::KeyEvent& event);
    // 直接處理一個文字提交事件（等同 insert(event.text)）。
    void handle_text_input(const ds::events::TextInputEvent& event);

    // --- 排版 / 渲染（E4-01）---
    void set_surface(ds::kernel::SurfaceId id) { layout_.set_surface(std::move(id)); }
    const ds::kernel::SurfaceId& surface() const noexcept { return layout_.surface(); }
    void set_constraints(ds::render::LayoutConstraints constraints) {
        constraints_ = std::move(constraints);
    }
    const ds::render::LayoutConstraints& constraints() const noexcept { return constraints_; }

    // 排版目前內容並回傳完整渲染描述（文字 + 游標 + 選取；全相對佈局，NFR-02）。
    TextInputRenderModel render_model() const;

private:
    void erase_range(std::size_t begin, std::size_t end);  // [begin,end) codepoint 範圍
    void replace_selection_if_any();  // 若有選取，刪除之並將游標設為範圍起點、清除選取

    const ds::render::FontMetrics& metrics_;
    ds::render::TextLayout layout_;
    ds::render::LayoutConstraints constraints_;
    std::vector<ds::render::CodePoint> codepoints_;
    std::size_t cursor_ = 0;
    std::size_t anchor_ = 0;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_15_TEXT_INPUT_ELEMENT_HPP
