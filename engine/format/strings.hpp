// E7-10 字串處理 — 宣告式格式脈絡中的字串運算函式庫（平台中立 / engine 層）
//
// 本單元建構於 E7-01 的資料模型（`Value`）與 E7-05 的求值結果模型（`EvalResult` /
// `EvalError`）之上：提供一組可在**公式 / 格式脈絡**中使用的字串處理函式——
// concat、length、substring、upper/lower、trim、replace、split/join、
// contains/starts_with/ends_with、pad、format 等。屬「描述子系統」的一環：
// 設定 / profile / 版面常需以既有字串值推導新值（拼接路徑、切分標籤、補齊寬度、
// 依樣板組字），字串處理讓宣告可運算、可組合。
//
// 設計原則（延續 E7-01 / E7-02 / E7-05）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **錯誤可定位、不得靜默失敗**（NFR-04 精神）：型別誤用（非字串 / 非整數引數）、
//     索引越界、空填充字串、樣板佔位越界——一律回傳帶人類可讀訊息的 `EvalError`；
//     絕不安靜回預設值或截斷。
//   - **消費上游、不重造輪子**：型別 / 值模型沿用 E7-01 的 `Value`；結果 / 錯誤模型
//     沿用 E7-05 的 `EvalResult` / `EvalError`，使字串函式可被上層（含 E7-05 公式引擎的
//     函式呼叫擴充）以統一結果型別接入。
//
// E7-05 整合說明：
//   E7-05 當前的運算式文法僅含字面量 / 變數 / 運算子，**尚未**支援函式呼叫語法。
//   本單元因此以「獨立、可被上層接入的字串運算 API」形式提供：每個函式回傳 `EvalResult`，
//   並額外導出一張**函式表**（`function_table()` / `find_function()`，名稱 → 變參函式），
//   一旦 E7-05（或任一上層）加入函式呼叫，即可直接查表分派、無縫接入。
//
// Unicode 語意：
//   `length` / `substring` / `pad_left` / `pad_right` 以 **UTF-8 碼位（codepoint）** 為單位
//   計數與切分（非位元組），故對多位元組字元行為正確、且不會切在字元中間。
//   `upper` / `lower` 僅折疊 **ASCII** 字母（A–Z / a–z），非 ASCII 位元組原樣保留
//   （完整 Unicode 大小寫映射超出本單元範圍，明確不做而非做半套）。
//   `trim` 修去外緣 ASCII 空白（空白 / tab / CR / LF）。`contains` / `starts_with` /
//   `ends_with` / `replace` / `split` / `join` 以位元組子字串比對（對合法 UTF-8 等價於碼位比對）。
#ifndef DS_ENGINE_E7_10_STRINGS_HPP
#define DS_ENGINE_E7_10_STRINGS_HPP

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "document.hpp"  // E7-01：ds::format::Value
#include "formula.hpp"   // E7-05：ds::format::EvalResult / EvalError

namespace ds::format {
namespace strings {

// -----------------------------------------------------------------------------
// 字串運算（輸入 / 輸出以 E7-01 Value 表達；結果 / 錯誤沿用 E7-05 EvalResult / EvalError）
//
// 型別契約：標「字串引數」者其 Value 型別須為 String，否則回型別錯誤（不靜默）。
//           標「整數引數」者須為整數型 Number（`is_integer()`），否則回型別錯誤。
//           越界（substring 起點 / 長度、format 佔位索引）回明確錯誤。
// -----------------------------------------------------------------------------

// 字串長度（UTF-8 碼位數）。回整數 Number。s 非字串 → 型別錯誤。
EvalResult length(const Value& s);

// 轉大寫 / 小寫（僅折疊 ASCII 字母；非 ASCII 原樣）。回字串。s 非字串 → 型別錯誤。
EvalResult upper(const Value& s);
EvalResult lower(const Value& s);

// 修去外緣 ASCII 空白。回字串。s 非字串 → 型別錯誤。
EvalResult trim(const Value& s);

// 依序拼接所有引數（每個引數皆須為字串）。回字串。任一引數非字串 → 型別錯誤。
// 空清單 → 空字串。
EvalResult concat(const std::vector<Value>& parts);

// 子字串：自碼位索引 start 取到字串尾。start 為整數引數。
// start 非整數 → 型別錯誤；start < 0 或 start > 長度 → 索引越界錯誤。
EvalResult substring(const Value& s, const Value& start);
// 子字串：自碼位索引 start 取 count 個碼位。start / count 為整數引數。
// start / count 非整數或為負 → 錯誤；start + count > 長度 → 索引越界錯誤。
EvalResult substring(const Value& s, const Value& start, const Value& count);

// 將 s 中所有出現的 from（非空字串）替換為 to。回字串。
// s / from / to 非字串 → 型別錯誤；from 為空字串 → 引數錯誤（避免無限插入）。
EvalResult replace(const Value& s, const Value& from, const Value& to);

// 依分隔字串 sep 切分 s。回字串清單（List of String）。
// sep 為空字串 → 逐碼位切分。s / sep 非字串 → 型別錯誤。
EvalResult split(const Value& s, const Value& sep);

// 以 sep 連接清單 list 的各字串元素。回字串。
// list 非清單、或任一元素非字串 → 型別錯誤；sep 非字串 → 型別錯誤。空清單 → 空字串。
EvalResult join(const Value& list, const Value& sep);

// 子字串包含 / 前綴 / 後綴判斷。回 Bool。任一引數非字串 → 型別錯誤。
EvalResult contains(const Value& s, const Value& sub);
EvalResult starts_with(const Value& s, const Value& prefix);
EvalResult ends_with(const Value& s, const Value& suffix);

// 左 / 右填充至目標碼位寬度 width，以 pad（非空字串）循環填補。
// 已達 / 超過 width 則原樣回傳。回字串。
// s / pad 非字串 → 型別錯誤；width 非整數或為負 → 錯誤；pad 為空字串 → 引數錯誤。
EvalResult pad_left(const Value& s, const Value& width, const Value& pad);
EvalResult pad_right(const Value& s, const Value& width, const Value& pad);

// 樣板組字：tmpl 內 `{i}`（i 為 0-based 十進位索引）以 args[i] 的字串呈現替換。
// `{{` / `}}` 分別轉義為字面 `{` / `}`。回字串。
// tmpl 非字串 → 型別錯誤；`{...}` 內非合法索引、索引越界、未閉合 `{` → 明確錯誤；
// args[i] 為容器（List / Map）無法字串化 → 型別錯誤。
EvalResult format(const Value& tmpl, const std::vector<Value>& args);

// -----------------------------------------------------------------------------
// 函式表（供 E7-05 公式引擎 / 任一上層以名稱分派接入）
// -----------------------------------------------------------------------------

// 變參字串函式簽章：吃引數清單、回 EvalResult。表項內部做元數（arity）檢查——
// 引數個數不符 → 明確錯誤（不靜默）。
using StringFn = std::function<EvalResult(const std::vector<Value>&)>;

// 名稱 → 函式的有序表（供列舉 / 分派）。名稱如 "length"、"substring"、"concat" …。
const std::vector<std::pair<std::string, StringFn>>& function_table();

// 依名稱查函式：不存在回 nullptr。上層拿到後直接以引數清單呼叫即得 EvalResult。
const StringFn* find_function(const std::string& name);

}  // namespace strings
}  // namespace ds::format

#endif  // DS_ENGINE_E7_10_STRINGS_HPP
