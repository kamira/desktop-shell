// E7-05 公式運算引擎 — 宣告式格式值中的運算式求值（平台中立 / engine 層）
//
// 本單元建構於 E7-01 的資料模型（`Value`）與 E7-02 的變數作用域（`VariableScope`）
// 之上：把宣告式格式值裡的**公式 / 運算式字串**解析並求值為一個 `Value`。
// 屬「描述子系統」的一環——設定 / profile / 版面常需以既有值推導新值（如
// `width = base + 8`、`visible = count > 0`），公式引擎讓宣告可運算、可組合。
//
// 設計原則（延續 E7-01 / E7-02）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **錯誤可定位、不得靜默失敗**（NFR-04 精神）：任何語法錯誤、未定義變數、除零、
//     型別誤用——一律回傳帶「位置（欄）＋ 訊息」的 `EvalError`；絕不安靜回預設值。
//   - **消費上游、不重造輪子**：數值 / 布林 / 型別模型沿用 E7-01 的 `Value`；變數查找
//     沿用 E7-02 的 `VariableScope`（含父鏈上溯、子層遮蔽）。
//
// 運算式語法（單行、極簡、自足）：
//     - 數值字面量：整數 `42`、浮點 `3.14`、指數 `1e3`。整數 / 浮點型別在結果中保留。
//     - 四則運算 `+ - * /` 與取模 `%`，括號 `(` `)` 改變優先序。
//     - 一元 `+` `-`（正負號）與 `!`（邏輯非）。
//     - 比較 `< <= > >=`（數值）與 `== !=`（數值 / 布林），結果為 Bool。
//     - 邏輯 `&&` `||`（以真值判斷：Bool 原樣、Number 非零為真），結果為 Bool。
//     - 變數引用：裸識別字（`[A-Za-z_][A-Za-z0-9_]*`）自 `VariableScope` 取值；
//       `true` / `false` 為布林字面量（保留字）。
//
// 優先序（低 → 高）：`||` < `&&` < `== !=` < `< <= > >=` < `+ -` < `* / %` < 一元 < 括號 / 字面 / 變數。
//
// 型別規則：
//   - `+ - *`：兩運算元皆整數 → 結果整數；否則浮點。
//   - `/`：除數為 0 → **除零錯誤**（不靜默）；兩整數且整除 → 整數，否則浮點。
//   - `%`：兩運算元須為整數，除數為 0 → 錯誤。
//   - 比較 / 邏輯運算元須可轉為對應型別，否則型別錯誤（帶位置）。
//   - 裸變數引用原樣回傳其 `Value`（可為字串 / 清單 / 映射）；但一旦參與運算而非
//     數值 / 布林 → 明確型別錯誤。
//
// 公式標記（讓引擎可直接吃宣告式格式的字串值）：
//     "= 2 * (width + 8)"   前導 '='（可含空白）標記其後為公式。
//     "${ a + b }"          `${ ... }` 包裹標記其內為公式。
//   `is_formula()` 判斷、`formula_body()` 取出運算式本體（非公式則原樣回傳）。
#ifndef DS_ENGINE_E7_05_FORMULA_HPP
#define DS_ENGINE_E7_05_FORMULA_HPP

#include <cstddef>
#include <string>

#include "document.hpp"    // E7-01：ds::format::Value
#include "variables.hpp"   // E7-02：ds::format::VariableScope

namespace ds::format {

// -----------------------------------------------------------------------------
// 求值錯誤（不靜默失敗，可定位）
// -----------------------------------------------------------------------------

// 公式求值 / 解析錯誤——語法錯誤、未定義變數、除零、型別誤用等，一律帶人類可讀
// 訊息與**位置**（`position` 為 0-based 欄索引，指向運算式字串中肇因 token 起點；
// 對「非特定位置」的錯誤〔如空運算式〕位置為 0）。
struct EvalError {
    std::size_t position = 0;  // 0-based 欄索引；指向肇因 token 起點。
    std::string message;       // 人類可讀原因。
};

// 求值結果：成功持 `Value`，失敗持 `EvalError`。二者互斥（對齊 E7-01 `ParseResult` 風格）。
class EvalResult {
public:
    static EvalResult success(Value v);
    static EvalResult failure(EvalError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Value& value() const { return value_; }       // 僅 ok() 為 true 時有效。
    const EvalError& error() const { return error_; }     // 僅 ok() 為 false 時有效。

private:
    EvalResult() = default;
    bool ok_ = false;
    Value value_;
    EvalError error_;
};

// -----------------------------------------------------------------------------
// 公式標記辨識
// -----------------------------------------------------------------------------

// 該字串是否為公式（前導 '=' 或 `${ ... }` 包裹）。
bool is_formula(const std::string& s);

// 取出公式本體（去除前導 '=' 或 `${ }` 包裹並修去外緣空白）。
// 非公式者原樣回傳（僅修去外緣空白）。
std::string formula_body(const std::string& s);

// -----------------------------------------------------------------------------
// 求值引擎
// -----------------------------------------------------------------------------

// 公式求值器。可選綁定一個變數作用域（不取得所有權；須於本物件存活期間有效）。
// 求值器本身無狀態、可重入；同一實例可對多條運算式求值。
class Evaluator {
public:
    // 無作用域（任何變數引用 → 未定義錯誤）。
    Evaluator() = default;
    // 綁定作用域；`scope` 須於本物件存活期間有效。
    explicit Evaluator(const VariableScope& scope) : scope_(&scope) {}

    // 解析並求值一條運算式。運算式可含公式標記（會自動以 `formula_body` 去除）。
    // 空運算式 / 語法錯誤 / 未定義變數 / 除零 / 型別誤用 → failure（帶位置）。
    EvalResult evaluate(const std::string& expr) const;

    const VariableScope* scope() const noexcept { return scope_; }

private:
    const VariableScope* scope_ = nullptr;
};

// 便捷入口：以給定作用域求值一條運算式。
EvalResult evaluate(const std::string& expr, const VariableScope& scope);
// 便捷入口：無變數作用域求值一條運算式。
EvalResult evaluate(const std::string& expr);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_05_FORMULA_HPP
