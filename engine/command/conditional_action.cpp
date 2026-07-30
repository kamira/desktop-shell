// E6-03 條件動作與去抖 — 實作
//
// 去抖 / 節流（Debouncer / Throttler）與真值橋接為 header-only inline（純整數 / Value 判斷、
// 無平台分支）。本 TU 提供：
//   - `conditional_contract_version()`：契約版本標記（使 STATIC 程式庫非空、集中版本字串）。
//   - `ConditionalAction::evaluate_and_dispatch`：把 E7-05 條件求值橋接到 E6-01 分派的編排邏輯。
#include "conditional_action.hpp"

namespace ds::command {

const char* conditional_contract_version() noexcept {
    // 條件動作 / 去抖節流契約版本。API 面若不相容變更，遞增主版本。
    return "e6_03/1.0.0";
}

bool value_truthiness(const ds::format::Value& v, bool& out) noexcept {
    using ds::format::ValueType;
    switch (v.type()) {
        case ValueType::Bool:
            out = v.as_bool();
            return true;
        case ValueType::Number:
            out = (v.as_number() != 0.0);
            return true;
        default:
            // Null / String / List / Map：非布林條件，不臆測真假。
            return false;
    }
}

namespace {

// 以（可選）作用域求條件並橋接到布林；失敗回 ConditionError（不靜默）。
// 成功時透過 out_value 帶回布林真值，回傳的 outcome 為 Skipped 佔位（呼叫端據 out_value 決策）。
ConditionalOutcome evaluate_condition(const std::string& expr,
                                      const ds::format::Evaluator& evaluator,
                                      bool& out_value) {
    ds::format::EvalResult r = evaluator.evaluate(expr);
    if (!r.ok()) {
        // 語法錯誤 / 未定義變數 / 除零 / 型別誤用 —— 帶位置與訊息，明確回報。
        const ds::format::EvalError& e = r.error();
        return ConditionalOutcome::make_condition_error(e.message, e.position);
    }
    bool truth = false;
    if (!value_truthiness(r.value(), truth)) {
        // 條件求值成功但結果非布林 / 非數值 —— 不臆測，明確回報型別錯誤。
        return ConditionalOutcome::make_condition_error(
            "condition did not evaluate to a boolean or numeric value", 0);
    }
    out_value = truth;
    return ConditionalOutcome::make_skipped();
}

// 條件為真時分派、否則跳過的共用尾段。
ConditionalOutcome dispatch_if_true(const ConditionalAction& action,
                                    const CommandBus& bus,
                                    bool condition_true) {
    if (!condition_true) return ConditionalOutcome::make_skipped();
    return ConditionalOutcome::make_dispatched(bus.dispatch(action.command, action.args));
}

}  // namespace

ConditionalOutcome ConditionalAction::evaluate_and_dispatch(
    const CommandBus& bus, const ds::format::VariableScope& scope) const {
    ds::format::Evaluator evaluator(scope);
    bool condition_true = false;
    ConditionalOutcome pre = evaluate_condition(condition_expr, evaluator, condition_true);
    if (pre.is_error()) return pre;  // 條件求值失敗 —— 直接回報，不分派。
    return dispatch_if_true(*this, bus, condition_true);
}

ConditionalOutcome ConditionalAction::evaluate_and_dispatch(const CommandBus& bus) const {
    ds::format::Evaluator evaluator;  // 無作用域：裸變數引用 → 未定義錯誤。
    bool condition_true = false;
    ConditionalOutcome pre = evaluate_condition(condition_expr, evaluator, condition_true);
    if (pre.is_error()) return pre;
    return dispatch_if_true(*this, bus, condition_true);
}

}  // namespace ds::command
