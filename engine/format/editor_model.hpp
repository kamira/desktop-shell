// E7-14 圖形化設定與就地編輯 — 把宣告式設定描述成「可編輯欄位模型」並就地套用變更
// （engine 層 / 平台中立 / 描述子系統 / 相依 E7-12 設定值寫回）。
//
// 本單元是 E7-12（設定值寫回）與 E7-01（宣告式格式核心）的**應用**，不自造格式模型：
//   1. `build_editor_model(Value/Document)`：走訪宣告式設定的 `Value` 樹，把每個**可編輯的
//      純量葉節點**描述成一個 `EditableField`——帶「控制項型別」（文字 / 數字 / 整數 / 布林 /
//      列舉 / 顏色）、當前值、標籤、以及約束（數值範圍 / 列舉選項）。這是 GUI 設定面板背後的
//      **資料模型**，而非真實 GUI：不綁任何視窗 / 繪圖 / 平台框架。
//   2. `validate_edit(field, new_value)`：把使用者編輯後的候選值對欄位的控制項型別 / 約束驗證。
//   3. `apply_edit(root, field/path, new_value)`：驗證通過後，經 **E7-12 `set_value`** 就地
//      套用變更，回傳**新的** Value 樹（不就地改寫，承 E7-01 不可變 Value 值語意）；呼叫端再以
//      E7-12 `serialize(...)` 序列化回 E7-01 文字格式即保留格式（round-trip 一致）。
//
// 設計原則（承 E7-01 / E7-12）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 系統呼叫 / 真實 GUI 後端。engine 層換平台一行不動。
//   - **不靜默失敗**（NFR-04 精神）：無效編輯——型別不符（對布林欄位給字串等）、數值越界
//     （超出 min / max）、非列舉選項——一律 throw `std::runtime_error`（訊息可定位到欄位路徑），
//     絕不安靜接受可疑輸入。
//
// 控制項型別推斷（`build_editor_model` 無 schema 時）：
//   Bool → Boolean；整數 Number → Integer；非整數 Number → Number；
//   字串 → 形如 `#RGB` / `#RRGGBB` 者為 Color，其餘為 Text；Null → Text（可自由改型）。
//   列舉（Enum）與數值範圍無法由 Value 本身推得，須由 `EditorSchema` 提示（見下）。
#ifndef DS_ENGINE_E7_14_EDITOR_MODEL_HPP
#define DS_ENGINE_E7_14_EDITOR_MODEL_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "document.hpp"   // E7-01：Value / Document（相依 target e7_01，經 e7_12 傳遞）
#include "writeback.hpp"  // E7-12：Path / PathSegment / parse_path / set_value（相依 target e7_12）

namespace ds::format {

// -----------------------------------------------------------------------------
// 控制項型別：一個可編輯欄位在 GUI 上會呈現的編輯器種類
// -----------------------------------------------------------------------------

enum class EditKind {
    Text,     // 自由文字（字串）
    Integer,  // 整數（帶整數旗標的 Number）
    Number,   // 實數（Number）
    Boolean,  // 布林開關
    Enum,     // 由固定選項集合擇一（字串）
    Color,    // 顏色（`#RGB` / `#RRGGBB` 十六進位字串）
};

// 控制項型別的人類可讀名（供訊息 / 測試；不會回傳 nullptr）。
const char* to_string(EditKind kind) noexcept;

// -----------------------------------------------------------------------------
// 約束：某欄位對候選值的限制（數值範圍 / 列舉選項）
// -----------------------------------------------------------------------------

struct EditConstraints {
    // 數值範圍（含端點）；has_min / has_max 為 false 時該端點不限制。
    bool has_min = false;
    bool has_max = false;
    double min_value = 0.0;
    double max_value = 0.0;

    // 列舉允許的選項（僅 EditKind::Enum 有意義）。
    std::vector<std::string> enum_options;

    // 便捷建構子（供呼叫端 / 測試）。
    static EditConstraints range(double lo, double hi);
    static EditConstraints min_of(double lo);
    static EditConstraints max_of(double hi);
    static EditConstraints enum_of(std::vector<std::string> options);

    bool operator==(const EditConstraints& o) const;
    bool operator!=(const EditConstraints& o) const { return !(*this == o); }
};

// -----------------------------------------------------------------------------
// 可編輯欄位：對單一設定葉節點的完整描述
// -----------------------------------------------------------------------------

struct EditableField {
    Path path;                   // 該欄位在 Value 樹中的路徑（E7-12 的 Path；空路徑 = 根純量）。
    std::string label;           // 人類可讀標籤（預設取路徑末段鍵；list 元素為 "[i]"）。
    EditKind kind = EditKind::Text;
    Value current;               // 該路徑當前的值。
    EditConstraints constraints; // 約束（數值範圍 / 列舉選項）。

    // 路徑的字串表示（如 "window.width"、"layers[0].name"）；供標籤 / 訊息 / 尋址。
    std::string path_string() const;
};

// -----------------------------------------------------------------------------
// Schema 提示：覆寫特定路徑的推斷（指定控制項型別 / 標籤 / 約束）
// -----------------------------------------------------------------------------

// 針對某一路徑的欄位提示。Enum / 數值範圍 / 自訂標籤無法由 Value 推得，故經此提供。
struct FieldHint {
    Path path;                   // 目標欄位路徑。
    EditKind kind = EditKind::Text;
    std::string label;           // has_label 為 true 時覆寫預設標籤。
    bool has_label = false;
    EditConstraints constraints;

    // 便捷建構：以字串路徑 + 控制項型別建提示（路徑語法錯 → throw std::runtime_error）。
    static FieldHint at(const std::string& path_text, EditKind kind);
    FieldHint& with_label(std::string text);
    FieldHint& with_constraints(EditConstraints c);
};

// 一組欄位提示（依路徑對應；未命中的欄位走預設推斷）。
using EditorSchema = std::vector<FieldHint>;

// -----------------------------------------------------------------------------
// 建模：Value / Document → 可編輯欄位清單
// -----------------------------------------------------------------------------

// 走訪 root，把每個純量葉節點描述成一個 EditableField（前序、保序）。容器（Map / List）遞迴，
// 空容器不產生欄位。控制項型別以推斷規則決定（見檔首）。
std::vector<EditableField> build_editor_model(const Value& root);

// 同上，但以 schema 覆寫命中路徑的控制項型別 / 標籤 / 約束（例如把某字串欄位標為 Enum / Color、
// 為某數值欄位加範圍）。未命中的欄位走預設推斷。
std::vector<EditableField> build_editor_model(const Value& root, const EditorSchema& schema);

// 便捷多載：自整份文件建模（使用 doc.root；format_version 不在 root 內，故不會被列為欄位）。
std::vector<EditableField> build_editor_model(const Document& doc);
std::vector<EditableField> build_editor_model(const Document& doc, const EditorSchema& schema);

// 於模型中依路徑尋找欄位（未命中回 nullptr）。便於呼叫端由路徑取回欄位描述再套用編輯。
const EditableField* find_field(const std::vector<EditableField>& model, const Path& path);
const EditableField* find_field(const std::vector<EditableField>& model, const std::string& path_text);

// -----------------------------------------------------------------------------
// 驗證與就地套用
// -----------------------------------------------------------------------------

// 對欄位的控制項型別 / 約束驗證候選新值。契約違反一律 throw std::runtime_error（訊息含欄位路徑）：
//   - Boolean：new_value 須為 Bool。
//   - Integer：須為整數 Number，且落在 [min, max]（若有）。
//   - Number ：須為 Number，且落在 [min, max]（若有）。
//   - Text   ：須為 String。
//   - Color  ：須為形如 `#RGB` / `#RRGGBB` 的 String。
//   - Enum   ：須為 String，且屬 constraints.enum_options 之一。
void validate_edit(const EditableField& field, const Value& new_value);

// 判斷字串是否為合法顏色字面值（`#RGB` 或 `#RRGGBB`，十六進位）。
bool is_color_literal(const std::string& text) noexcept;

// 就地套用編輯：先 validate_edit，通過後經 **E7-12 `set_value`** 把 new_value 寫回 field.path，
// 回傳一棵**新的** Value 樹（root 不被就地改寫）。呼叫端再以 E7-12 serialize 序列化即保留格式。
Value apply_edit(const Value& root, const EditableField& field, Value new_value);

// 便捷多載：以路徑就地套用——由該路徑「當前值」推斷控制項型別後**型別驗證**（無範圍 / 列舉約束），
// 再經 E7-12 set_value 寫回。路徑不存在於 root → throw std::runtime_error（不靜默）。
Value apply_edit(const Value& root, const Path& path, Value new_value);
Value apply_edit(const Value& root, const std::string& path_text, Value new_value);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_14_EDITOR_MODEL_HPP
