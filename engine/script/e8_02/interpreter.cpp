// E8-02 對話腳本直譯器 — 實作。見 interpreter.hpp 檔首說明。
#include "interpreter.hpp"

#include <sstream>
#include <utility>

#include "variables.hpp"  // E7-02：ds::format::resolve（say 的 ${var} 內插）

namespace ds::script {

// -----------------------------------------------------------------------------
// Step 工廠
// -----------------------------------------------------------------------------

Step Step::set(std::string name, std::string expr) {
    return Step("set", std::move(name), std::move(expr));
}
Step Step::say(std::string text) {
    return Step("say", std::string{}, std::move(text));
}
Step Step::if_goto(std::string condition, std::string label) {
    return Step("if", std::move(label), std::move(condition));
}
Step Step::goto_label(std::string label) {
    return Step("goto", std::move(label), std::string{});
}
Step Step::label(std::string name) {
    return Step("label", std::move(name), std::string{});
}

// -----------------------------------------------------------------------------
// 結果物件工廠
// -----------------------------------------------------------------------------

StepResult StepResult::ran() {
    StepResult r;
    r.ok_ = true;
    r.finished_ = false;
    return r;
}
StepResult StepResult::finished() {
    StepResult r;
    r.ok_ = true;
    r.finished_ = true;
    return r;
}
StepResult StepResult::failure(ScriptError e) {
    StepResult r;
    r.ok_ = false;
    r.finished_ = false;
    r.error_ = std::move(e);
    return r;
}

RunResult RunResult::success(std::size_t steps_executed) {
    RunResult r;
    r.ok_ = true;
    r.steps_ = steps_executed;
    return r;
}
RunResult RunResult::failure(ScriptError e) {
    RunResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// -----------------------------------------------------------------------------
// value_to_text
// -----------------------------------------------------------------------------

std::string value_to_text(const Value& v) {
    switch (v.type()) {
        case ds::format::ValueType::Null:
            return "null";
        case ds::format::ValueType::Bool:
            return v.as_bool() ? "true" : "false";
        case ds::format::ValueType::Number: {
            if (v.is_integer()) {
                return std::to_string(v.as_int());
            }
            std::ostringstream os;
            os << v.as_number();  // 精簡十進位（預設格式，決定性）。
            return os.str();
        }
        case ds::format::ValueType::String:
            return v.as_string();
        case ds::format::ValueType::List: {
            std::string out = "[";
            const auto& items = v.as_list();
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += ", ";
                out += value_to_text(items[i]);
            }
            out += "]";
            return out;
        }
        case ds::format::ValueType::Map: {
            std::string out = "{";
            const auto& members = v.as_map();
            for (std::size_t i = 0; i < members.size(); ++i) {
                if (i) out += ", ";
                out += members[i].first;
                out += ": ";
                out += value_to_text(members[i].second);
            }
            out += "}";
            return out;
        }
    }
    return std::string{};  // 不可達（列舉已窮盡）。
}

// -----------------------------------------------------------------------------
// 內部輔助
// -----------------------------------------------------------------------------

namespace {

// 求值錯誤 → 帶欄位置的可讀細節（沿用 E7-05 EvalError 的 0-based 欄位）。
std::string eval_detail(const ds::format::EvalError& e) {
    std::ostringstream os;
    os << e.message << " (at column " << e.position << ")";
    return os.str();
}

// 真值判斷（與 E7-05 一致）：Bool 原樣、Number 非零為真；其餘型別不可轉 → 回傳 false。
// 成功時把結果寫入 out 並回傳 true；型別不可轉 → 回傳 false（out 不動）。
bool truth_of(const Value& v, bool& out) {
    if (v.is_bool()) {
        out = v.as_bool();
        return true;
    }
    if (v.is_number()) {
        out = (v.as_number() != 0.0);
        return true;
    }
    return false;
}

}  // namespace

// -----------------------------------------------------------------------------
// Interpreter
// -----------------------------------------------------------------------------

Interpreter::Interpreter(const Script& script, OutputSink& sink, const VariableScope* parent)
    : script_(script), sink_(sink), ctx_(parent) {
    // 一次掃描索引所有標籤；重複標籤 → 建構期錯誤（於首次 step/run 明確回報，不靜默）。
    const auto& steps = script_.steps();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].command != "label") continue;
        const std::string& name = steps[i].target;
        if (name.empty()) {
            if (!has_build_error_) {
                has_build_error_ = true;
                build_error_ = ScriptError{i, "label step has empty name"};
            }
            continue;
        }
        auto inserted = labels_.emplace(name, i);
        if (!inserted.second && !has_build_error_) {
            has_build_error_ = true;
            build_error_ = ScriptError{i, "duplicate label '" + name + "'"};
        }
    }
}

StepResult Interpreter::fail(std::size_t step_idx, std::string msg) {
    failed_ = true;
    error_state_ = ScriptError{step_idx, std::move(msg)};
    return StepResult::failure(error_state_);
}

StepResult Interpreter::step() {
    if (has_build_error_) {
        return fail(build_error_.step, build_error_.message);
    }
    if (failed_) {
        return StepResult::failure(error_state_);
    }
    if (ctx_.finished_ || ctx_.pc_ >= script_.size()) {
        ctx_.finished_ = true;
        return StepResult::finished();
    }
    if (ctx_.steps_executed_ >= budget_) {
        return fail(ctx_.pc_, "step budget exceeded (possible infinite loop)");
    }

    const std::size_t here = ctx_.pc_;
    const Step& s = script_.steps()[here];

    if (s.command == "label") {
        ctx_.pc_ = here + 1;
    } else if (s.command == "goto") {
        if (s.target.empty()) {
            return fail(here, "goto requires a label name");
        }
        auto it = labels_.find(s.target);
        if (it == labels_.end()) {
            return fail(here, "goto to unknown label '" + s.target + "'");
        }
        ctx_.pc_ = it->second;
    } else if (s.command == "set") {
        if (s.target.empty()) {
            return fail(here, "set requires a variable name");
        }
        ds::format::EvalResult r = ds::format::evaluate(s.arg, ctx_.vars_);
        if (!r.ok()) {
            return fail(here, "set '" + s.target + "': " + eval_detail(r.error()));
        }
        ctx_.vars_.define(s.target, r.value());
        ctx_.pc_ = here + 1;
    } else if (s.command == "say") {
        // 以 E7-02 對文字模板做 ${var} 內插（消費上游、不重造）。
        ds::format::ResolveResult rr = ds::format::resolve(Value::string(s.arg), ctx_.vars_);
        if (!rr.ok()) {
            std::string msg = "say: " + rr.error().message;
            if (!rr.error().variable.empty()) {
                msg += " (variable '" + rr.error().variable + "')";
            }
            return fail(here, std::move(msg));
        }
        sink_.emit(value_to_text(rr.value()));
        ctx_.pc_ = here + 1;
    } else if (s.command == "if") {
        ds::format::EvalResult r = ds::format::evaluate(s.arg, ctx_.vars_);
        if (!r.ok()) {
            return fail(here, "if condition: " + eval_detail(r.error()));
        }
        bool cond = false;
        if (!truth_of(r.value(), cond)) {
            return fail(here, "if condition is not boolean or numeric");
        }
        if (cond) {
            if (s.target.empty()) {
                return fail(here, "if requires a label name");
            }
            auto it = labels_.find(s.target);
            if (it == labels_.end()) {
                return fail(here, "if goto unknown label '" + s.target + "'");
            }
            ctx_.pc_ = it->second;
        } else {
            ctx_.pc_ = here + 1;
        }
    } else {
        return fail(here, "unknown command '" + s.command + "'");
    }

    ctx_.steps_executed_ += 1;
    if (ctx_.pc_ >= script_.size()) {
        ctx_.finished_ = true;
    }
    return StepResult::ran();
}

RunResult Interpreter::run() {
    while (!finished()) {
        StepResult r = step();
        if (!r.ok()) {
            return RunResult::failure(r.error());
        }
        if (r.is_finished()) {
            break;
        }
    }
    if (failed_) {
        return RunResult::failure(error_state_);
    }
    return RunResult::success(ctx_.steps_executed_);
}

}  // namespace ds::script
