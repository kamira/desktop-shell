// E8-01 腳本引擎（QML/JS）— 腳本引擎的抽象與嵌入介面（平台中立 / engine 層）
//
// 「腳本引擎」讓概念上以 QML/JS 風格撰寫的腳本能被**載入、執行、並與宿主互動**：
//   - 宿主把具名函式綁上引擎（`bind_function`）；腳本可呼叫之。
//   - 宿主暴露 / 讀寫全域變數 / 物件（`set_global` / `get_global`）。
//   - 腳本呼叫綁定的函式，宿主端於其中經 **E6-01 CommandBus** 分派具名命令
//     （音量 / 電源 / 任意副作用），達成「腳本 → 宿主 → 命令」的嵌入契約。
//
// **相位 1 不綁任何真實 JS 引擎**（無 V8 / QuickJS / QJSEngine / `#ifdef`）：本單元只定義
//   1. **可插拔的 `ScriptEngine` 抽象介面**（evaluate / bind_function / set_global /
//      get_global / call / has_function），與
//   2. 兩個相位 1 後端——
//      * `MiniScriptEngine`：一個**最小內建直譯器**，足以驗證嵌入契約（宿主綁函式 →
//        腳本呼叫 → 經 E6-01 分派命令）；支援極簡的逐行腳本語法（賦值 / 呼叫 / 字面值）。
//      * `NullScriptEngine`：一個 **null/mock 後端**，實作同一介面（保存全域 / 綁定 / 可
//        `call` 宿主函式），但 `evaluate(script)` 明確回報「不執行腳本文字」（Unsupported）
//        ——真實 QML/JS 後端留待相位 2。
//   兩後端可互換（同一份嵌入程式碼對 `ScriptEngine&` 皆可運作），示範「可插拔引擎替換」。
//
// 設計原則（對齊 E8-02 / E6-01）：
//   - **平台中立、純邏輯**：無任何 `#ifdef`、系統呼叫或真實後端；換平台一行不動。
//   - **錯誤明確可定位、不靜默**：語法錯誤（載入 / 求值）帶行號回報；執行期錯誤（未知
//     函式、未定義全域、宿主函式失敗、後端不支援）帶明確原因回報——絕不安靜略過或回預設。
//   - **消費上游、不重造輪子**：腳本值型別沿用 E6-01 的 `CommandValue`（穩定值型別，跨
//     模組邊界不變形）；命令分派沿用 E6-01 `CommandBus`。
#ifndef DS_ENGINE_E8_01_SCRIPT_ENGINE_HPP
#define DS_ENGINE_E8_01_SCRIPT_ENGINE_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "command_bus.hpp"  // E6-01：ds::command::CommandValue / CommandArgs / CommandBus

namespace ds::script {

// 腳本值型別：沿用 E6-01 的穩定值型別（null / bool / 整數 / 浮點 / 字串）。腳本與宿主
// 之間、以及橋接到命令匯流排時，值皆以此承載，跨邊界不變形、不重造。
using ScriptValue = ds::command::CommandValue;

// -----------------------------------------------------------------------------
// 錯誤模型（不靜默失敗）
// -----------------------------------------------------------------------------

// 腳本錯誤種類。None 表無錯；其餘各自對應一類明確可定位的失敗。
enum class ScriptError {
    None,         // 無錯誤。
    Syntax,       // 載入 / 求值時腳本文字語法錯誤（帶行號）。
    Runtime,      // 執行期錯誤：未定義全域、宿主函式回報失敗、引數不合等。
    NotFound,     // 呼叫了未綁定的函式名。
    Unsupported,  // 後端不支援此操作（如 null 後端不執行腳本文字）。
};

// 求值 / 呼叫的結果（二選一：成功帶值，或失敗帶錯誤種類 + 訊息 + 行號）。
// 對齊 E8-02 `RunResult` / E6-01 `CommandResult` 的二選一風格。
class EvalResult {
public:
    // 工廠：成功（帶選用回傳值）。
    static EvalResult make_ok(ScriptValue value = {}) {
        EvalResult r;
        r.ok_ = true;
        r.value_ = std::move(value);
        return r;
    }
    // 工廠：失敗（帶錯誤種類、人類可讀訊息、1-based 行號；行號 0 表不適用）。
    static EvalResult make_error(ScriptError kind, std::string message, std::size_t line = 0) {
        EvalResult r;
        r.ok_ = false;
        r.error_ = kind;
        r.message_ = std::move(message);
        r.line_ = line;
        return r;
    }

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 僅 ok() 為 true 時有意義。
    const ScriptValue& value() const noexcept { return value_; }

    // 僅 ok() 為 false 時有意義。
    ScriptError error() const noexcept { return error_; }
    const std::string& message() const noexcept { return message_; }
    std::size_t line() const noexcept { return line_; }  // 1-based；0 = 不適用。

private:
    EvalResult() = default;
    bool ok_ = false;
    ScriptValue value_{};
    ScriptError error_ = ScriptError::None;
    std::string message_{};
    std::size_t line_ = 0;
};

// -----------------------------------------------------------------------------
// 宿主函式（嵌入契約：腳本 → 宿主回呼）
// -----------------------------------------------------------------------------

// 宿主函式回傳值：成功帶值，或失敗帶訊息。讓宿主端（如命令分派）能把失敗**明確**回報，
// 由引擎轉成腳本執行期錯誤（不靜默吞掉）。
struct HostResult {
    bool ok = true;
    ScriptValue value{};
    std::string message{};

    static HostResult success(ScriptValue v = {}) { return HostResult{true, std::move(v), {}}; }
    static HostResult failure(std::string msg) { return HostResult{false, {}, std::move(msg)}; }
};

// 綁定到引擎的宿主函式：接收位置引數（ScriptValue 序列）、執行副作用、回傳 HostResult。
// 腳本呼叫該具名函式時觸發；宿主可於其中經 E6-01 CommandBus 分派命令。
using HostFunction = std::function<HostResult(const std::vector<ScriptValue>&)>;

// -----------------------------------------------------------------------------
// 可插拔的腳本引擎抽象介面
// -----------------------------------------------------------------------------

// 腳本引擎的嵌入契約。任何後端（相位 1 的 mini / null；相位 2 的真實 QML/JS）皆實作此
// 介面，宿主程式碼只相依此抽象、不相依任何具體後端——達成「可插拔引擎替換」。
class ScriptEngine {
public:
    virtual ~ScriptEngine() = default;

    // 後端識別名（診斷 / 測試用），如 "mini" / "null"。
    virtual const char* backend_name() const noexcept = 0;

    // 載入並執行一段腳本文字；回傳最後一條敘述的值（或 null）。語法 / 執行期錯誤明確
    // 回報（帶行號 / 訊息），絕不靜默。
    virtual EvalResult evaluate(const std::string& script) = 0;

    // 綁定一個具名宿主函式，供腳本呼叫。name 非空且尚未綁定時成功回 true；重複綁定
    // 不靜默覆蓋（回 false）。
    virtual bool bind_function(const std::string& name, HostFunction fn) = 0;

    // 暴露 / 覆寫一個具名全域變數（宿主 → 腳本）。
    virtual void set_global(const std::string& name, ScriptValue value) = 0;

    // 讀取一個具名全域變數（腳本 / 宿主寫入後）；不存在回 std::nullopt。
    virtual std::optional<ScriptValue> get_global(const std::string& name) const = 0;

    // 直接以具名 + 位置引數呼叫一個已綁定的宿主函式（宿主驅動，不經腳本文字解析）。
    // 未綁定 → NotFound；宿主回報失敗 → Runtime。
    virtual EvalResult call(const std::string& name, const std::vector<ScriptValue>& args) = 0;

    // 是否已綁定指定函式名。
    virtual bool has_function(const std::string& name) const = 0;
};

// -----------------------------------------------------------------------------
// 相位 1 後端 A：最小內建直譯器
// -----------------------------------------------------------------------------

// `MiniScriptEngine` — 相位 1 的最小內建直譯器，足以驗證嵌入契約。支援極簡的**逐行**腳本：
//
//   # 註解 / 空行忽略
//   set <name> = <expr>     設定全域 <name> 為 <expr> 的值。
//   <name> = <expr>         同上（`set` 可省）。
//   <name> = <call>         呼叫函式並把回傳值指派給全域 <name>。
//   <call>                  呼叫函式，捨棄回傳值（供副作用，如分派命令）。
//   <expr>                  裸運算式（字面值 / 全域參照），其值成為該段腳本的結果。
//
// <expr> 文法（相位 1 極簡、不遞迴）：
//   - 字面值：整數 `-?[0-9]+`、浮點（含 `.`）、字串 `"..."`（支援 \" \\ \n \t）、
//     `true` / `false` / `null`。
//   - 全域參照：識別字 `[A-Za-z_][A-Za-z0-9_.]*`（解析為 set_global 過的值；未定義 → Runtime）。
//   - 呼叫：`IDENT ( arg1, arg2, ... )`，每個 arg 為上述字面值或全域參照（不巢狀呼叫）。
//
// 未知函式 / 未定義全域 / 宿主函式失敗 / 語法錯誤一律帶行號 / 訊息明確回報（不靜默）。
// 真實 JS 語意（物件 / 控制流 / 運算子）留待相位 2 的真實後端。
class MiniScriptEngine : public ScriptEngine {
public:
    MiniScriptEngine() = default;

    const char* backend_name() const noexcept override { return "mini"; }

    EvalResult evaluate(const std::string& script) override;
    bool bind_function(const std::string& name, HostFunction fn) override;
    void set_global(const std::string& name, ScriptValue value) override;
    std::optional<ScriptValue> get_global(const std::string& name) const override;
    EvalResult call(const std::string& name, const std::vector<ScriptValue>& args) override;
    bool has_function(const std::string& name) const override;

    // 內省 / 診斷。
    std::size_t function_count() const noexcept { return functions_.size(); }
    std::size_t global_count() const noexcept { return globals_.size(); }

private:
    // 求值單一 <expr> 文字（字面值 / 全域參照 / 呼叫）；帶行號供錯誤定位。
    EvalResult eval_expr(const std::string& expr, std::size_t line);
    // 求值單一 atom（字面值 / 全域參照）。
    EvalResult eval_atom(const std::string& atom, std::size_t line);

    std::map<std::string, HostFunction> functions_;
    std::map<std::string, ScriptValue> globals_;
};

// -----------------------------------------------------------------------------
// 相位 1 後端 B：null / mock 引擎
// -----------------------------------------------------------------------------

// `NullScriptEngine` — 相位 1 的 null/mock 後端。實作完整介面：保存全域、綁定宿主函式、
// 並可直接 `call` 之（宿主端行為與引擎無關，故仍可運作）——足以讓「綁定 + 呼叫 + 全域」
// 這條嵌入契約在無真實腳本後端時仍能被同一份宿主程式碼驅動。
//
// 差異只在 `evaluate(script)`：null 後端**不解析 / 不執行腳本文字**，明確回報 `Unsupported`
// （不靜默、不假裝成功）——真實 QML/JS 腳本執行留待相位 2 的後端。示範「可插拔引擎替換」：
// 同一份嵌入程式碼可換用 MiniScriptEngine 或 NullScriptEngine。
class NullScriptEngine : public ScriptEngine {
public:
    NullScriptEngine() = default;

    const char* backend_name() const noexcept override { return "null"; }

    // null 後端不執行腳本文字：明確回 Unsupported（不靜默）。
    EvalResult evaluate(const std::string& script) override;
    bool bind_function(const std::string& name, HostFunction fn) override;
    void set_global(const std::string& name, ScriptValue value) override;
    std::optional<ScriptValue> get_global(const std::string& name) const override;
    EvalResult call(const std::string& name, const std::vector<ScriptValue>& args) override;
    bool has_function(const std::string& name) const override;

    std::size_t function_count() const noexcept { return functions_.size(); }
    std::size_t global_count() const noexcept { return globals_.size(); }

private:
    std::map<std::string, HostFunction> functions_;
    std::map<std::string, ScriptValue> globals_;
};

// -----------------------------------------------------------------------------
// E6-01 命令橋接（嵌入契約：腳本 → 宿主 → 命令分派）
// -----------------------------------------------------------------------------

// 產生一個把「腳本函式呼叫」橋接到「E6-01 命令分派」的宿主函式：位置引數依序對映到
// `arg_names` 組成 `CommandArgs`，在 `bus` 上分派 `command_id`，並把 `CommandResult` 轉為
// `HostResult`（Ok → success(值)；NotFound / Failed → failure(訊息)，明確回報不靜默）。
// bus 須於回傳函式存活期間有效（不取得所有權）。
HostFunction make_command_binding(ds::command::CommandBus& bus,
                                  ds::command::CommandId command_id,
                                  std::vector<std::string> arg_names);

// 便捷：把一個命令直接以 `function_name` 綁進 `engine`（等同
// `engine.bind_function(function_name, make_command_binding(bus, command_id, arg_names))`）。
// 回傳綁定是否成功（重複函式名 → false）。這是把 E6-01 命令暴露給腳本的一行式嵌入 API。
bool bind_command(ScriptEngine& engine,
                  const std::string& function_name,
                  ds::command::CommandBus& bus,
                  ds::command::CommandId command_id,
                  std::vector<std::string> arg_names);

}  // namespace ds::script

#endif  // DS_ENGINE_E8_01_SCRIPT_ENGINE_HPP
