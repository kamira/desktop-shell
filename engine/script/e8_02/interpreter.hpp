// E8-02 對話腳本直譯器 — 逐步執行以宣告式格式撰寫的對話 / 腳本（平台中立 / engine 層）
//
// 本單元建構於 E7-05 的公式運算引擎（`Evaluator` / `evaluate()` / `EvalResult`）之上，
// 傳遞取得 E7-02 的 `VariableScope` 與 E7-01 的 `Value`。它把一段**步驟序列**當成可
// 逐步（`step()`）或一次跑完（`run()`）的腳本來直譯：
//   - 以 E7-05 公式引擎求值**運算式**（`set`）與**條件**（`if`）；
//   - 變數狀態在步驟間**延續**（存於 `ExecutionContext` 的 `VariableScope`）；
//   - **條件分支 / 跳轉**（`if <cond> goto <label>` / `goto <label>` / `label`）；
//   - **輸出 / 動作步驟**（`say`）以抽象、可注入的 `OutputSink` 表達——**不綁任何真實副作用**。
//
// 設計原則（延續 E7-01 / E7-02 / E7-05）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//     副作用（輸出 / 動作）一律走注入式 `OutputSink`，由呼叫端決定真正落地行為。
//   - **錯誤可定位、不得靜默失敗**（NFR-04 精神）：未知指令、求值 / 條件失敗、跳轉到未知
//     標籤、條件非布林 / 數值、重複標籤、疑似無窮迴圈——一律回傳帶「步驟索引 + 訊息」的
//     `ScriptError`；絕不安靜略過或回預設值。
//   - **消費上游、不重造輪子**：運算式 / 條件求值沿用 E7-05 `evaluate`；輸出文字的
//     `${var}` 內插沿用 E7-02 `resolve`；型別 / 數值模型沿用 E7-01 `Value`。
//
// 指令集（每個 `Step` 以 `command` 字串為 opcode；未知 opcode → 執行期明確報錯）：
//     set   <name> = <expr>          以 E7-05 求值 <expr>，定義 / 覆寫變數 <name>（跨步延續）。
//     say   <text>                    對 <text> 做 E7-02 `${var}` 內插後 emit 至注入的 sink。
//     if    <cond> goto <label>       以 E7-05 求值 <cond>；真 → 跳到 <label>，否則落到下一步。
//     goto  <label>                   無條件跳到 <label>。
//     label <name>                    標記一個跳轉目標（no-op）。
//
// 真值規則（與 E7-05 一致）：Bool 原樣；Number 非零為真；其餘型別 → 條件型別錯誤（不靜默）。
#ifndef DS_ENGINE_E8_02_INTERPRETER_HPP
#define DS_ENGINE_E8_02_INTERPRETER_HPP

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "formula.hpp"  // E7-05：ds::format::evaluate / EvalResult（傳遞取得 E7-02 / E7-01）

namespace ds::script {

// 沿用上游型別（不重造）。
using ds::format::Value;
using ds::format::VariableScope;

// -----------------------------------------------------------------------------
// 步驟 / 指令
// -----------------------------------------------------------------------------

// 單一腳本步驟 / 指令。`command` 為 opcode 字串；未知 opcode 於執行期明確報錯（非靜默）。
// 已知 opcode 與各欄位語意見檔首。以工廠函式建構可讀性最佳；直接建構亦可（供未知指令測試）。
struct Step {
    std::string command;  // opcode："set" / "say" / "if" / "goto" / "label"（其餘 → 未知指令）。
    std::string target;   // 變數名（set）或標籤名（if / goto / label）；say 不使用。
    std::string arg;      // 運算式（set / if 條件）或文字模板（say）；goto / label 不使用。

    Step() = default;
    Step(std::string command_, std::string target_, std::string arg_)
        : command(std::move(command_)), target(std::move(target_)), arg(std::move(arg_)) {}

    // 工廠（canonical opcode）：
    static Step set(std::string name, std::string expr);        // set <name> = <expr>
    static Step say(std::string text);                          // say <text>（${var} 內插）
    static Step if_goto(std::string condition, std::string label);  // if <cond> goto <label>
    static Step goto_label(std::string label);                  // goto <label>
    static Step label(std::string name);                        // label <name>
};

// -----------------------------------------------------------------------------
// 腳本：步驟序列
// -----------------------------------------------------------------------------

// 一段可直譯的腳本：有序步驟序列。純資料容器（標籤解析 / 執行由 Interpreter 負責）。
class Script {
public:
    Script() = default;
    explicit Script(std::vector<Step> steps) : steps_(std::move(steps)) {}

    Script& add(Step s) { steps_.push_back(std::move(s)); return *this; }  // 建構式串接。

    const std::vector<Step>& steps() const noexcept { return steps_; }
    std::size_t size() const noexcept { return steps_.size(); }
    bool empty() const noexcept { return steps_.empty(); }

private:
    std::vector<Step> steps_;
};

// -----------------------------------------------------------------------------
// 輸出 sink（可注入抽象——副作用不綁真實後端）
// -----------------------------------------------------------------------------

// 輸出 / 動作步驟的抽象接收端。`say` 產生的文字經此 emit；由呼叫端決定真正落地
// （印到終端 / 寫紀錄 / 驅動 UI ...）。介面本身無任何平台相依。
class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual void emit(const std::string& text) = 0;
};

// 現成的緩衝式 sink：把每次 emit 依序記錄下來。便於測試與批次消費。
class RecordingSink : public OutputSink {
public:
    void emit(const std::string& text) override { lines_.push_back(text); }

    const std::vector<std::string>& lines() const noexcept { return lines_; }
    std::size_t count() const noexcept { return lines_.size(); }
    bool empty() const noexcept { return lines_.empty(); }
    void clear() noexcept { lines_.clear(); }

private:
    std::vector<std::string> lines_;
};

// -----------------------------------------------------------------------------
// 執行錯誤（不靜默失敗，可定位到步驟）
// -----------------------------------------------------------------------------

// 腳本執行錯誤——未知指令、求值 / 條件失敗、未知標籤、條件型別誤用、重複標籤、疑似
// 無窮迴圈等，一律帶人類可讀訊息與**步驟索引**（0-based，指向肇因步驟）。
struct ScriptError {
    std::size_t step = 0;   // 0-based 肇因步驟索引。
    std::string message;    // 人類可讀原因（含上游求值錯誤細節）。
};

// -----------------------------------------------------------------------------
// 執行狀態：跨步延續的變數 + 程式計數器
// -----------------------------------------------------------------------------

// 腳本的可變執行狀態。持有跨步延續的變數作用域、程式計數器（pc）與統計。
// 可於 `run()` / `step()` 後查詢變數以驗證結果。
class ExecutionContext {
public:
    ExecutionContext() = default;
    // 以 parent 為父作用域（供預先注入變數）；parent 須於本物件存活期間有效（不取得所有權）。
    explicit ExecutionContext(const VariableScope* parent) : vars_(parent) {}

    VariableScope& variables() noexcept { return vars_; }
    const VariableScope& variables() const noexcept { return vars_; }

    std::size_t pc() const noexcept { return pc_; }                       // 下一個要執行的步驟索引。
    bool finished() const noexcept { return finished_; }                  // pc 已越過腳本末端。
    std::size_t steps_executed() const noexcept { return steps_executed_; }  // 已執行步驟數。

private:
    friend class Interpreter;
    VariableScope vars_;
    std::size_t pc_ = 0;
    bool finished_ = false;
    std::size_t steps_executed_ = 0;
};

// -----------------------------------------------------------------------------
// 結果物件（對齊 E7-01 `ParseResult` / E7-05 `EvalResult` 二選一風格）
// -----------------------------------------------------------------------------

// 單步 `step()` 的結果：執行成功（still running）/ 腳本已結束 / 失敗（帶 ScriptError）。
class StepResult {
public:
    static StepResult ran();                     // 執行了一步，腳本仍在跑。
    static StepResult finished();                // 無事可做：pc 已越過末端。
    static StepResult failure(ScriptError e);

    bool ok() const noexcept { return ok_; }              // 僅失敗時為 false。
    explicit operator bool() const noexcept { return ok_; }
    bool is_finished() const noexcept { return finished_; }
    const ScriptError& error() const { return error_; }    // 僅 ok() 為 false 時有效。

private:
    StepResult() = default;
    bool ok_ = true;
    bool finished_ = false;
    ScriptError error_;
};

// 一次跑完 `run()` 的結果：成功（帶已執行步驟數）或失敗（帶 ScriptError）。
class RunResult {
public:
    static RunResult success(std::size_t steps_executed);
    static RunResult failure(ScriptError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    std::size_t steps_executed() const noexcept { return steps_; }  // 僅 ok() 為 true 時有意義。
    const ScriptError& error() const { return error_; }              // 僅 ok() 為 false 時有效。

private:
    RunResult() = default;
    bool ok_ = false;
    std::size_t steps_ = 0;
    ScriptError error_;
};

// -----------------------------------------------------------------------------
// 直譯器
// -----------------------------------------------------------------------------

// 腳本直譯器。綁定一段 `Script` 與一個 `OutputSink`（皆不取得所有權，須於本物件存活
// 期間有效）。可 `step()` 逐步或 `run()` 一次跑完；變數狀態存於內部 `ExecutionContext`。
// 建構時一次掃描並索引所有標籤（重複標籤 → 建構期錯誤，於首次 step/run 明確回報）。
class Interpreter {
public:
    Interpreter(const Script& script, OutputSink& sink)
        : Interpreter(script, sink, nullptr) {}
    // 以 parent 作為預注入變數的父作用域；parent 須於本物件存活期間有效。
    Interpreter(const Script& script, OutputSink& sink, const VariableScope* parent);

    // 執行 pc 所指的單一步驟：求值 / 內插 / 跳轉 / emit 並推進 pc（或跳轉）。pc 越過末端
    // 即標記 finished。已結束時呼叫為安全 no-op（回傳 finished）。任一錯誤 → failure。
    StepResult step();

    // 自當前 pc 一路執行至結束或首個錯誤。
    RunResult run();

    // 已結束（正常跑完）或已失敗（發生過錯誤）→ true。
    bool finished() const noexcept { return ctx_.finished() || failed_; }

    ExecutionContext& context() noexcept { return ctx_; }
    const ExecutionContext& context() const noexcept { return ctx_; }

    // 中止前允許執行的最大步數，防範 `goto` 造成的無窮迴圈；預設 100000。
    void set_step_budget(std::size_t budget) noexcept { budget_ = budget; }
    std::size_t step_budget() const noexcept { return budget_; }

private:
    const Script& script_;
    OutputSink& sink_;
    ExecutionContext ctx_;
    std::size_t budget_ = 100000;
    bool failed_ = false;
    ScriptError error_state_;

    std::unordered_map<std::string, std::size_t> labels_;  // 標籤名 → 步驟索引。
    bool has_build_error_ = false;
    ScriptError build_error_;

    StepResult fail(std::size_t step_idx, std::string msg);
};

// -----------------------------------------------------------------------------
// 值 → 文字（供 say 輸出；決定性、無平台相依）
// -----------------------------------------------------------------------------

// 把一個 `Value` 呈現為輸出用文字：Bool → "true"/"false"、整數 → 十進位、浮點 → 精簡十進位、
// Null → "null"、String → 原樣、List → "[a, b]"、Map → "{k: v}"（遞迴）。決定性輸出。
std::string value_to_text(const Value& v);

}  // namespace ds::script

#endif  // DS_ENGINE_E8_02_INTERPRETER_HPP
