// E8-01 腳本引擎（QML/JS）— 實作（平台中立 / engine 層）
//
// 內含：MiniScriptEngine（相位 1 最小內建直譯器）、NullScriptEngine（null/mock 後端）、
// 以及 E6-01 命令橋接（make_command_binding / bind_command）。無任何 `#ifdef`、系統呼叫、
// 真實 JS 後端（V8 / QuickJS / QJSEngine）——真實後端留待相位 2。
#include "script_engine.hpp"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace ds::script {

namespace {

// 去除前後空白（空白 = 空格 / tab / CR / LF）。
std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// 合法識別字：`[A-Za-z_][A-Za-z0-9_.]*`（允許 `.` 以承載如 `audio.play` 的具名函式 / 全域）。
bool is_identifier(const std::string& s) {
    if (s.empty()) return false;
    char c0 = s[0];
    if (!(std::isalpha(static_cast<unsigned char>(c0)) || c0 == '_')) return false;
    for (std::size_t i = 1; i < s.size(); ++i) {
        char c = s[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.')) return false;
    }
    return true;
}

// 解析字串字面值 `"..."`（支援 \" \\ \n \t）。成功回 true 並填 out；語法錯誤（未終結 /
// 非引號開頭 / 尾隨字元）回 false 並填 err。
bool parse_string_literal(const std::string& s, std::string& out, std::string& err) {
    if (s.size() < 2 || s.front() != '"') {
        err = "expected string literal";
        return false;
    }
    std::string r;
    std::size_t i = 1;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\') {
            if (i + 1 >= s.size()) {
                err = "dangling escape in string literal";
                return false;
            }
            char n = s[++i];
            switch (n) {
                case 'n': r.push_back('\n'); break;
                case 't': r.push_back('\t'); break;
                case '"': r.push_back('"'); break;
                case '\\': r.push_back('\\'); break;
                default:
                    err = std::string("unknown escape sequence: \\") + n;
                    return false;
            }
        } else if (c == '"') {
            // 收尾引號：其後不得有非空白字元。
            std::string tail = trim(s.substr(i + 1));
            if (!tail.empty()) {
                err = "unexpected trailing characters after string literal";
                return false;
            }
            out = std::move(r);
            return true;
        } else {
            r.push_back(c);
        }
    }
    err = "unterminated string literal";
    return false;
}

// 嘗試把 atom 當數值字面值解析。回 true 並填 out（Int 或 Double）；非數值回 false（不填 err）。
bool parse_number_literal(const std::string& s, ScriptValue& out) {
    if (s.empty()) return false;
    // 快速判別：允許前導 +/-、數字、單一小數點；其餘字元一律非數值。
    bool has_dot = false, has_digit = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '-' || c == '+') {
            if (i != 0) return false;
        } else if (c == '.') {
            if (has_dot) return false;
            has_dot = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            has_digit = true;
        } else {
            return false;
        }
    }
    if (!has_digit) return false;

    if (has_dot) {
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() + s.size()) return false;
        out = ScriptValue(d);
        return true;
    }
    // 整數：以 strtoll 解析（承載至 int64）。
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = ScriptValue(static_cast<std::int64_t>(v));
    return true;
}

// 依逗號切分呼叫引數清單，尊重字串引號（引號內逗號不切）。回 true 並填 args；引號未終結
// 回 false 並填 err。空字串（無引數）回 true 且 args 為空。多重引數中若有空 token → 語法錯誤。
bool split_args(const std::string& inner, std::vector<std::string>& args, std::string& err) {
    args.clear();
    std::string cur;
    bool in_str = false;
    bool saw_comma = false;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        char c = inner[i];
        if (in_str) {
            cur.push_back(c);
            if (c == '\\' && i + 1 < inner.size()) {
                cur.push_back(inner[++i]);  // 逐字保留跳脫序列（交由 atom 解析）。
            } else if (c == '"') {
                in_str = false;
            }
        } else if (c == '"') {
            in_str = true;
            cur.push_back(c);
        } else if (c == ',') {
            saw_comma = true;
            args.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (in_str) {
        err = "unterminated string literal in argument list";
        return false;
    }
    std::string last = trim(cur);
    if (saw_comma || !last.empty()) {
        args.push_back(last);
    }
    // 若曾出現逗號，任一 token 為空（如 "a,,b" 或 "a,"）視為語法錯誤。
    for (const auto& a : args) {
        if (a.empty()) {
            err = "empty argument in call";
            return false;
        }
    }
    return true;
}

// 找出 s 中第一個「不在字串引號內」的 '=' 位置；無則回 npos。用於偵測賦值。
std::size_t find_top_level_eq(const std::string& s) {
    bool in_str = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_str = false;
        } else if (c == '"') {
            in_str = true;
        } else if (c == '=') {
            return i;
        }
    }
    return std::string::npos;
}

}  // namespace

// -----------------------------------------------------------------------------
// MiniScriptEngine — atom / expr 求值
// -----------------------------------------------------------------------------

EvalResult MiniScriptEngine::eval_atom(const std::string& atom_raw, std::size_t line) {
    std::string atom = trim(atom_raw);
    if (atom.empty()) {
        return EvalResult::make_error(ScriptError::Syntax, "empty expression", line);
    }
    // 字串字面值。
    if (atom.front() == '"') {
        std::string out, err;
        if (!parse_string_literal(atom, out, err)) {
            return EvalResult::make_error(ScriptError::Syntax, err, line);
        }
        return EvalResult::make_ok(ScriptValue(out));
    }
    // 關鍵字字面值。
    if (atom == "true") return EvalResult::make_ok(ScriptValue(true));
    if (atom == "false") return EvalResult::make_ok(ScriptValue(false));
    if (atom == "null") return EvalResult::make_ok(ScriptValue());
    // 數值字面值。
    {
        ScriptValue num;
        if (parse_number_literal(atom, num)) return EvalResult::make_ok(num);
    }
    // 全域參照。
    if (is_identifier(atom)) {
        auto it = globals_.find(atom);
        if (it == globals_.end()) {
            return EvalResult::make_error(ScriptError::Runtime,
                                          "undefined global: " + atom, line);
        }
        return EvalResult::make_ok(it->second);
    }
    return EvalResult::make_error(ScriptError::Syntax,
                                  "invalid expression: " + atom, line);
}

EvalResult MiniScriptEngine::eval_expr(const std::string& expr_raw, std::size_t line) {
    std::string expr = trim(expr_raw);
    if (expr.empty()) {
        return EvalResult::make_error(ScriptError::Syntax, "empty expression", line);
    }
    // 呼叫形式：`IDENT ( ... )`，且尾字元為 ')'。找第一個非引號內的 '('。
    std::size_t paren = std::string::npos;
    {
        bool in_str = false;
        for (std::size_t i = 0; i < expr.size(); ++i) {
            char c = expr[i];
            if (in_str) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') in_str = false;
            } else if (c == '"') {
                in_str = true;
            } else if (c == '(') {
                paren = i;
                break;
            }
        }
    }
    if (paren != std::string::npos) {
        if (expr.back() != ')') {
            return EvalResult::make_error(ScriptError::Syntax,
                                          "malformed call (expected ')')", line);
        }
        std::string fname = trim(expr.substr(0, paren));
        std::string inner = expr.substr(paren + 1, expr.size() - paren - 2);
        if (!is_identifier(fname)) {
            return EvalResult::make_error(ScriptError::Syntax,
                                          "invalid function name: " + fname, line);
        }
        std::vector<std::string> arg_toks;
        std::string err;
        if (!split_args(inner, arg_toks, err)) {
            return EvalResult::make_error(ScriptError::Syntax, err, line);
        }
        std::vector<ScriptValue> args;
        args.reserve(arg_toks.size());
        for (const auto& tok : arg_toks) {
            EvalResult a = eval_atom(tok, line);
            if (!a.ok()) return a;
            args.push_back(a.value());
        }
        // 分派到已綁定的宿主函式。
        auto it = functions_.find(fname);
        if (it == functions_.end()) {
            return EvalResult::make_error(ScriptError::NotFound,
                                          "unbound function: " + fname, line);
        }
        HostResult hr = it->second(args);
        if (!hr.ok) {
            return EvalResult::make_error(ScriptError::Runtime,
                                          "host function '" + fname + "' failed: " + hr.message,
                                          line);
        }
        return EvalResult::make_ok(hr.value);
    }
    // 非呼叫：當作 atom（字面值 / 全域參照）。
    return eval_atom(expr, line);
}

// -----------------------------------------------------------------------------
// MiniScriptEngine — 介面實作
// -----------------------------------------------------------------------------

EvalResult MiniScriptEngine::evaluate(const std::string& script) {
    ScriptValue last{};  // 最後一條敘述的值（空腳本 → null）。
    std::size_t line_no = 0;
    std::size_t pos = 0;
    const std::size_t n = script.size();

    while (pos <= n) {
        // 取一行（以 '\n' 分隔；最後一段無換行也處理）。
        std::size_t nl = script.find('\n', pos);
        std::string raw = (nl == std::string::npos) ? script.substr(pos)
                                                     : script.substr(pos, nl - pos);
        ++line_no;

        std::string line = trim(raw);
        // 忽略空行與註解。
        if (!line.empty() && line[0] != '#') {
            std::size_t eq = find_top_level_eq(line);
            if (eq != std::string::npos) {
                // 賦值：lhs = rhs。lhs 可選前綴 `set `。
                std::string lhs = trim(line.substr(0, eq));
                std::string rhs = line.substr(eq + 1);
                if (lhs.size() >= 4 && lhs.compare(0, 4, "set ") == 0) {
                    lhs = trim(lhs.substr(4));
                }
                if (!is_identifier(lhs)) {
                    return EvalResult::make_error(ScriptError::Syntax,
                                                  "invalid assignment target: " + lhs, line_no);
                }
                EvalResult v = eval_expr(rhs, line_no);
                if (!v.ok()) return v;
                globals_[lhs] = v.value();
                last = v.value();
            } else {
                // 裸運算式（通常是呼叫，供副作用）。
                EvalResult v = eval_expr(line, line_no);
                if (!v.ok()) return v;
                last = v.value();
            }
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return EvalResult::make_ok(last);
}

bool MiniScriptEngine::bind_function(const std::string& name, HostFunction fn) {
    if (name.empty() || !fn) return false;
    if (functions_.find(name) != functions_.end()) return false;  // 不靜默覆蓋。
    functions_.emplace(name, std::move(fn));
    return true;
}

void MiniScriptEngine::set_global(const std::string& name, ScriptValue value) {
    globals_[name] = std::move(value);
}

std::optional<ScriptValue> MiniScriptEngine::get_global(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) return std::nullopt;
    return it->second;
}

EvalResult MiniScriptEngine::call(const std::string& name, const std::vector<ScriptValue>& args) {
    auto it = functions_.find(name);
    if (it == functions_.end()) {
        return EvalResult::make_error(ScriptError::NotFound, "unbound function: " + name);
    }
    HostResult hr = it->second(args);
    if (!hr.ok) {
        return EvalResult::make_error(ScriptError::Runtime,
                                      "host function '" + name + "' failed: " + hr.message);
    }
    return EvalResult::make_ok(hr.value);
}

bool MiniScriptEngine::has_function(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

// -----------------------------------------------------------------------------
// NullScriptEngine — 介面實作（不執行腳本文字）
// -----------------------------------------------------------------------------

EvalResult NullScriptEngine::evaluate(const std::string& /*script*/) {
    // 明確回報：null 後端不解析 / 不執行腳本文字（相位 2 才有真實後端）。不靜默、不假成功。
    return EvalResult::make_error(ScriptError::Unsupported,
                                  "null engine does not execute script text "
                                  "(real QML/JS backend deferred to phase 2)");
}

bool NullScriptEngine::bind_function(const std::string& name, HostFunction fn) {
    if (name.empty() || !fn) return false;
    if (functions_.find(name) != functions_.end()) return false;
    functions_.emplace(name, std::move(fn));
    return true;
}

void NullScriptEngine::set_global(const std::string& name, ScriptValue value) {
    globals_[name] = std::move(value);
}

std::optional<ScriptValue> NullScriptEngine::get_global(const std::string& name) const {
    auto it = globals_.find(name);
    if (it == globals_.end()) return std::nullopt;
    return it->second;
}

EvalResult NullScriptEngine::call(const std::string& name, const std::vector<ScriptValue>& args) {
    // 宿主函式的呼叫與引擎後端無關，故 null 後端仍可分派——嵌入契約（綁定 + 呼叫）成立。
    auto it = functions_.find(name);
    if (it == functions_.end()) {
        return EvalResult::make_error(ScriptError::NotFound, "unbound function: " + name);
    }
    HostResult hr = it->second(args);
    if (!hr.ok) {
        return EvalResult::make_error(ScriptError::Runtime,
                                      "host function '" + name + "' failed: " + hr.message);
    }
    return EvalResult::make_ok(hr.value);
}

bool NullScriptEngine::has_function(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

// -----------------------------------------------------------------------------
// E6-01 命令橋接
// -----------------------------------------------------------------------------

HostFunction make_command_binding(ds::command::CommandBus& bus,
                                  ds::command::CommandId command_id,
                                  std::vector<std::string> arg_names) {
    // 以值捕獲 command_id / arg_names，以參照捕獲 bus（呼叫端保證存活期）。
    return [&bus, command_id = std::move(command_id), arg_names = std::move(arg_names)](
               const std::vector<ScriptValue>& args) -> HostResult {
        ds::command::CommandArgs cargs;
        // 位置引數依序對映到具名參數（多餘的引數忽略；缺的參數留白）。
        const std::size_t n = arg_names.size() < args.size() ? arg_names.size() : args.size();
        for (std::size_t i = 0; i < n; ++i) {
            cargs.set(arg_names[i], args[i]);
        }
        ds::command::CommandResult r = bus.dispatch(command_id, cargs);
        if (r.ok()) {
            return HostResult::success(r.value);
        }
        // NotFound / Failed → 明確回報為宿主失敗（引擎轉成腳本執行期錯誤，不靜默）。
        return HostResult::failure(r.message);
    };
}

bool bind_command(ScriptEngine& engine,
                  const std::string& function_name,
                  ds::command::CommandBus& bus,
                  ds::command::CommandId command_id,
                  std::vector<std::string> arg_names) {
    return engine.bind_function(function_name,
                                make_command_binding(bus, std::move(command_id),
                                                     std::move(arg_names)));
}

}  // namespace ds::script
