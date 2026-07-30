// content/widgets/c2_08/note_widget.hpp — C2-08 筆記 widget（artifact 層 / widgets, 相位 1）
//
// 語意：桌面「筆記」widget——一張可編輯的便條，掛在 C1-01 skin profile 基底上（具名圖層歸屬 /
// 可互動 / 具透明外形 / 自由拖曳 + 位置記憶），內容以上游 E4-15 就地輸入框編輯（E4-15 又以
// E4-01 排版顯示），並以上游 E7-12（消費 E7-01 `Value`）把筆記內容序列化 / 還原，供持久化。
// 本單元不新增引擎邏輯，是**組裝型 artifact 單元**：把四個已合併的擴充點組成一個具體 widget。
//
//   - C1-01（`ds::profiles::SkinProfile`）：本 widget 的桌面存在基底——載入 / 卸載、具名圖層 /
//     輸入策略 / 透明外形、九宮 anchor 定位 + 自由拖曳狀態機。內容與基底**分離持久化**：
//     基底位置經 C1-01 自身的 `save_position` / `load_position`；筆記**內容**經本單元的
//     `save()` / `load()`（各自序列化為獨立的 E7-01 文字）。
//   - E4-15（`ds::elements::TextInputElement`）：筆記內容的編輯狀態機（游標 / 選取 / 插入 /
//     刪除），本 widget 的 `set_text` / `insert` / `backspace` / `erase_forward` 直接委派之。
//   - E4-01（經由 E4-15 內部持有的 `TextLayout`）：筆記文字的排版顯示，透過 `render_model()`
//     取得（`TextInputRenderModel` 含排版 + 游標 + 選取，全相對佈局，NFR-02）。
//   - E7-12（`ds::format::set_value` / `serialize`，消費 E7-01 `Value`）：把筆記內容包成
//     `{ text: "..." }` 的宣告式 Map，序列化為 E7-01 文字（`save()`）；`load()` 反向解析
//     並取回 `text` 欄位還原內容。round-trip：`save()` 之後 `load()` 應還原相同文字內容
//     （含多行 / 特殊字元，經 E7-12 字串引號 / 轉義規則保證，見 E7-12 標頭）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02，承 C1-01 / E4-01 / E4-15）。
//
// 錯誤不靜默：`load()` 對「無法解析的序列化文字」/「`text` 欄位存在但型別非字串」/
// 「`text` 欄位為合法字串但非合法 UTF-8」一律回具名 `NoteStatus::Invalid`，**不改動**現有筆記
// 內容（全有或全無，與 C1-01 `load_skin` 同風格）；`text` 欄位缺席視為空筆記（`NoteStatus::Ok`），
// 非錯誤。基底 / 編輯 / 顯示的既有精確結果碼（`SkinStatus` / `DragStatus` / `std::out_of_range` /
// `std::invalid_argument`）皆透傳上游語意，不吞不改寫。
#ifndef DS_CONTENT_WIDGETS_C2_08_NOTE_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_08_NOTE_WIDGET_HPP

#include <cstddef>
#include <string>

#include "skin_profile.hpp"        // C1-01（上游，可讀不可改）：SkinProfile / SkinState / SkinStatus
                                    //   （透傳 E1-01/E1-02/E1-03/E1-07/E1-08/E7-01 KernelBackend /
                                    //   LayerStack / AnchorSpec / DragStatus / AnchorStatus）
#include "text_input_element.hpp"  // E4-15（上游，可讀不可改）：TextInputElement /
                                    //   TextInputRenderModel（透傳 E4-01 FontMetrics /
                                    //   LayoutConstraints / E5-13 KeyboardInputSource）
#include "writeback.hpp"           // E7-12（上游，可讀不可改）：set_value / serialize（透傳
                                    //   E7-01 Value / Document / FormatVersion / parse）

namespace ds::widgets {

// 筆記內容持久化操作的具名結果（NFR-02：具名，非布林 / 例外掩蓋語意）。
enum class NoteStatus {
    Ok,       // 操作成功（含「無 text 欄位 → 視為空筆記」的成功情形）。
    Invalid,  // 序列化文字無法解析 / text 欄位型別非字串 / text 欄位非合法 UTF-8；內容不變動。
};

const char* to_string(NoteStatus s) noexcept;

// -----------------------------------------------------------------------------
// NoteWidget —— 筆記 widget：組裝 C1-01（基底）+ E4-15（編輯）+ E4-01（顯示，經 E4-15）+
// E7-12（內容序列化，經 E7-01）。
//
// 每個實例代表**一張**具名筆記（如 "note.todo"）。內部自持一個以注入後端 / 圖層堆疊建構的
// C1-01 `SkinProfile`（桌面存在）與一個以注入字型度量建構的 E4-15 `TextInputElement`
// （筆記內容）。兩者生命週期由本物件擁有；`backend` / `layers` / `metrics` 皆為注入式共用
// 相依（不取得所有權，須比本物件活得久，語意承 C1-01 / E4-15）。
// -----------------------------------------------------------------------------
class NoteWidget {
public:
    // 建構一張具名筆記。id 即其基底 surface 的具名 SurfaceId（NFR-02，透傳 C1-01）。
    // constraints 預設沿用 E4-15 的 `single_line_constraints()`（WrapMode::None：僅顯式 '\n'
    // 產生新行，適合筆記——不自動換行、以使用者輸入的斷行為準）。
    NoteWidget(std::string id, ds::kernel::KernelBackend& backend, ds::kernel::LayerStack& layers,
              const ds::render::FontMetrics& metrics,
              ds::render::LayoutConstraints constraints = ds::elements::single_line_constraints());

    NoteWidget(const NoteWidget&) = delete;
    NoteWidget& operator=(const NoteWidget&) = delete;

    // --- 基底（C1-01）：桌面存在 —— 全數透傳 SkinProfile，保留其精確結果碼 ---

    // 從宣告式定義（E7-01 Value，見 C1-01 標頭 schema）載入基底：具名圖層 / 輸入策略 /
    // 透明外形 / 初始位置。委派 `SkinProfile::load_skin`（全有或全無、能力閘控降級）。
    ds::profiles::SkinStatus load_base(const ds::format::Value& definition);
    // 卸載基底（銷毀後端 surface、移除圖層 / 輸入 / 位置登錄）。未載入 → false（no-op）。
    bool unload_base();
    bool is_base_loaded() const;
    ds::profiles::SkinState base_state() const;
    const std::string& id() const noexcept { return id_; }

    // 定位 / 拖曳（E1-07 anchor + E1-08 狀態機，經 C1-01 透傳）。
    ds::kernel::DragStatus place(const ds::kernel::AnchorSpec& spec);
    ds::kernel::DragStatus begin_drag();
    ds::kernel::DragStatus drag_to(const ds::kernel::AnchorSpec& spec);
    ds::kernel::DragStatus end_drag();
    ds::kernel::DragStatus cancel_drag();
    bool is_dragging() const;

    // --- 編輯（E4-15）：筆記內容的可編輯文字 ---

    // 以合法 UTF-8 取代整段筆記內容；游標移至末端、清除選取。非法 UTF-8 → std::invalid_argument
    // （透傳 E4-15，不靜默）。
    void set_text(const std::string& utf8_text);
    // 於游標處插入文字（若有選取，先取代選取範圍）。非法 UTF-8 → std::invalid_argument。
    void insert(const std::string& utf8_text);
    void backspace();      // 刪除游標前一個字符（或選取範圍）；邊界安全無動作。
    void erase_forward();  // 刪除游標所在字符（或選取範圍）；邊界安全無動作。

    // 目前筆記內容（UTF-8）；長度（codepoint 數，含換行字元）。
    std::string text() const;
    std::size_t length() const noexcept;

    // --- 內容持久化（E7-12，經 E7-01）：save() ↔ load() round-trip ---

    // 從序列化文字載入筆記內容：解析為 E7-01 文件，取根 Map 的 `text` 欄位（字串）還原內容
    // （`set_text`）。`text` 欄位缺席 → 視為空筆記（Ok，內容清空）。
    //   - 文字無法解析（E7-01 語法錯誤 / 版本不相容）→ Invalid，內容不變。
    //   - `text` 欄位存在但非字串（Number / Bool / List / Map / Null）→ Invalid，內容不變。
    //   - `text` 欄位為字串但非合法 UTF-8 → Invalid，內容不變（不靜默吞下 E4-15 的例外）。
    NoteStatus load(const std::string& serialized_text);

    // 把目前筆記內容序列化為 E7-01 宣告式文字：`{ text: "<內容，含引號/轉義> " }`
    // （首行 `format_version: 1.0`）。空筆記序列化為 `text: ""`（合法、可再解析為空字串，
    // 而非落入 E7-12 空容器不 round-trip 的限制——`text` 恆為 String，非空容器）。
    std::string save() const;

    // --- 顯示（E4-01，經 E4-15）---

    // 排版目前筆記內容並回傳完整渲染描述（文字 + 游標 + 選取；全相對佈局，NFR-02）。
    ds::elements::TextInputRenderModel render_model() const;

private:
    std::string id_;
    ds::profiles::SkinProfile base_;      // C1-01：桌面存在（圖層 / 輸入 / alpha / 位置）。
    ds::elements::TextInputElement content_;  // E4-15：筆記內容（經其內部 E4-01 排版顯示）。
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_08_NOTE_WIDGET_HPP
