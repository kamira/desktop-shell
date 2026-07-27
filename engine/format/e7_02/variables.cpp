// E7-02 變數系統 — 實作。見 variables.hpp 檔首語意。平台中立、無任何 `#ifdef`。
#include "variables.hpp"

#include <sstream>
#include <stdexcept>

namespace ds::format {

// -----------------------------------------------------------------------------
// VariableScope
// -----------------------------------------------------------------------------

void VariableScope::define(std::string name, Value value) {
    for (auto& e : vars_) {
        if (e.first == name) {  // 同名就地覆寫，保留插入位置。
            e.second = std::move(value);
            return;
        }
    }
    vars_.emplace_back(std::move(name), std::move(value));
}

const Value* VariableScope::find(const std::string& name) const {
    for (const auto& e : vars_) {
        if (e.first == name) {
            return &e.second;
        }
    }
    return parent_ ? parent_->find(name) : nullptr;  // 上溯父鏈。
}

bool VariableScope::has_local(const std::string& name) const {
    for (const auto& e : vars_) {
        if (e.first == name) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> VariableScope::names() const {
    std::vector<std::string> out;
    out.reserve(vars_.size());
    for (const auto& e : vars_) {
        out.push_back(e.first);
    }
    return out;
}

// -----------------------------------------------------------------------------
// ResolveResult / DocumentResolveResult
// -----------------------------------------------------------------------------

ResolveResult ResolveResult::success(Value v) {
    ResolveResult r;
    r.ok_ = true;
    r.value_ = std::move(v);
    return r;
}

ResolveResult ResolveResult::failure(ResolveError e) {
    ResolveResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

DocumentResolveResult DocumentResolveResult::success(Document d) {
    DocumentResolveResult r;
    r.ok_ = true;
    r.document_ = std::move(d);
    return r;
}

DocumentResolveResult DocumentResolveResult::failure(ResolveError e) {
    DocumentResolveResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// -----------------------------------------------------------------------------
// 內部：受控例外承載 ResolveError（讓遞迴下降碼保持乾淨；於邊界捕捉轉 failure）
// -----------------------------------------------------------------------------

namespace {

struct ResolveException {
    ResolveError err;
};

[[noreturn]] void fail(std::string message, std::string variable = {}) {
    throw ResolveException{ResolveError{std::move(message), std::move(variable)}};
}

// 把純量 Value 呈現為字串（供內嵌引用替換）。容器型別無法字串化 → 明確失敗。
std::string render_scalar(const Value& v) {
    switch (v.type()) {
        case ValueType::Null:
            return "null";
        case ValueType::Bool:
            return v.as_bool() ? "true" : "false";
        case ValueType::Number: {
            if (v.is_integer()) {
                return std::to_string(v.as_int());
            }
            std::ostringstream os;
            os << v.as_number();  // 預設精度給出簡潔浮點呈現（如 3.14）。
            return os.str();
        }
        case ValueType::String:
            return v.as_string();
        case ValueType::List:
        case ValueType::Map:
            fail("cannot interpolate container-typed variable into a string");
    }
    fail("unknown value type in interpolation");  // 不可達；滿足編譯器窮盡性。
}

// 字串引用解析的一個片段：字面文字，或一個變數引用（name）。
struct Part {
    bool is_ref = false;
    std::string text;  // 字面內容，或引用的變數名。
};

// 把字串切成 literal / ref 片段。偵測未終止 `${` 與空變數名並失敗。
std::vector<Part> split_parts(const std::string& s) {
    std::vector<Part> parts;
    std::string literal;
    const std::size_t n = s.size();
    std::size_t i = 0;
    auto flush_literal = [&]() {
        if (!literal.empty()) {
            parts.push_back(Part{false, literal});
            literal.clear();
        }
    };
    while (i < n) {
        const char c = s[i];
        if (c == '$') {
            if (i + 1 < n && s[i + 1] == '$') {  // $$ → 字面 $
                literal.push_back('$');
                i += 2;
                continue;
            }
            if (i + 1 < n && s[i + 1] == '{') {  // ${ … } 引用起始
                const std::size_t close = s.find('}', i + 2);
                if (close == std::string::npos) {
                    fail("unterminated variable reference (missing '}')");
                }
                const std::string name = s.substr(i + 2, close - (i + 2));
                if (name.empty()) {
                    fail("empty variable name in reference '${}'");
                }
                flush_literal();
                parts.push_back(Part{true, name});
                i = close + 1;
                continue;
            }
            literal.push_back('$');  // 孤立 $。
            i += 1;
            continue;
        }
        literal.push_back(c);
        i += 1;
    }
    flush_literal();
    return parts;
}

// 前置宣告：字串 / Value 的遞迴求值互相引用。
Value resolve_value_impl(const Value& value, const VariableScope& scope,
                         std::vector<std::string>& active);

// 求值單一變數引用：查表（含父鏈）、偵測循環、遞迴求值其值。
Value resolve_reference(const std::string& name, const VariableScope& scope,
                        std::vector<std::string>& active) {
    const Value* v = scope.find(name);
    if (v == nullptr) {
        fail("undefined variable '" + name + "'", name);  // 未定義 → 明確回報。
    }
    for (const auto& a : active) {
        if (a == name) {
            fail("circular variable reference involving '" + name + "'", name);
        }
    }
    active.push_back(name);
    Value resolved = resolve_value_impl(*v, scope, active);  // 巢狀引用：遞迴求值。
    active.pop_back();
    return resolved;
}

// 求值一個字串純量：依片段替換。整串恰為單一引用 → 保留變數值原生型別；
// 否則串接為字串（內嵌引用以 render_scalar 字串化）。
Value resolve_string(const std::string& s, const VariableScope& scope,
                     std::vector<std::string>& active) {
    const std::vector<Part> parts = split_parts(s);
    if (parts.size() == 1 && parts[0].is_ref) {
        return resolve_reference(parts[0].text, scope, active);  // 型別保留。
    }
    std::string out;
    for (const auto& p : parts) {
        if (p.is_ref) {
            const Value rv = resolve_reference(p.text, scope, active);
            out += render_scalar(rv);
        } else {
            out += p.text;
        }
    }
    return Value::string(out);
}

Value resolve_value_impl(const Value& value, const VariableScope& scope,
                         std::vector<std::string>& active) {
    switch (value.type()) {
        case ValueType::String:
            return resolve_string(value.as_string(), scope, active);
        case ValueType::List: {
            std::vector<Value> items;
            items.reserve(value.size());
            for (const auto& item : value.as_list()) {
                items.push_back(resolve_value_impl(item, scope, active));
            }
            return Value::list(std::move(items));
        }
        case ValueType::Map: {
            std::vector<Value::Member> members;
            members.reserve(value.size());
            for (const auto& m : value.as_map()) {
                members.emplace_back(m.first,
                                     resolve_value_impl(m.second, scope, active));
            }
            return Value::map(std::move(members));
        }
        case ValueType::Null:
        case ValueType::Bool:
        case ValueType::Number:
            return value;  // 純量非字串：原樣複製。
    }
    return value;  // 不可達；窮盡性。
}

}  // namespace

// -----------------------------------------------------------------------------
// 公開入口
// -----------------------------------------------------------------------------

ResolveResult resolve(const Value& value, const VariableScope& scope) {
    std::vector<std::string> active;
    try {
        return ResolveResult::success(resolve_value_impl(value, scope, active));
    } catch (const ResolveException& ex) {
        return ResolveResult::failure(ex.err);
    }
}

DocumentResolveResult resolve(const Document& doc, const VariableScope& scope) {
    std::vector<std::string> active;
    try {
        Document out;
        out.format_version = doc.format_version;  // 版本欄位原樣保留。
        out.root = resolve_value_impl(doc.root, scope, active);
        return DocumentResolveResult::success(std::move(out));
    } catch (const ResolveException& ex) {
        return DocumentResolveResult::failure(ex.err);
    }
}

}  // namespace ds::format
