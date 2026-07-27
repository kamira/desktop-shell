// E7-05 公式運算引擎 — 契約測試（gtest）
//
// 涵蓋：數字字面量（整數 / 浮點 / 指數）、四則運算 + 優先序 + 括號、一元正負、
// 變數引用（自 VariableScope，含父鏈上溯）、比較與布林邏輯、取模、
// 型別保留（整數運算得整數、含浮點得浮點）、除零明確報錯（帶位置）、
// 語法錯誤定位報錯（非法字元 / 缺運算元 / 未閉合括號 / 空運算式）、
// 未定義變數報錯、公式標記（'=' 與 `${ }`）辨識與去除。
// 平台中立：不含任何平台分支。
#include "formula.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::format::EvalResult;
using ds::format::Evaluator;
using ds::format::evaluate;
using ds::format::formula_body;
using ds::format::is_formula;
using ds::format::Value;
using ds::format::VariableScope;

namespace {

// -----------------------------------------------------------------------------
// 數字字面量與型別保留
// -----------------------------------------------------------------------------

TEST(Formula, IntegerLiteral) {
    EvalResult r = evaluate("42");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_integer());
    EXPECT_EQ(r.value().as_int(), 42);
}

TEST(Formula, FloatLiteral) {
    EvalResult r = evaluate("3.14");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_number());
    EXPECT_FALSE(r.value().is_integer());
    EXPECT_DOUBLE_EQ(r.value().as_number(), 3.14);
}

TEST(Formula, ExponentLiteralIsFloat) {
    EvalResult r = evaluate("1e3");
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().is_integer());
    EXPECT_DOUBLE_EQ(r.value().as_number(), 1000.0);
}

TEST(Formula, IntegerArithmeticStaysInteger) {
    EvalResult r = evaluate("2 + 3 * 4");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_integer());
    EXPECT_EQ(r.value().as_int(), 14);  // 優先序：3*4 先算
}

TEST(Formula, MixedArithmeticBecomesFloat) {
    EvalResult r = evaluate("2 + 3.5");
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().is_integer());
    EXPECT_DOUBLE_EQ(r.value().as_number(), 5.5);
}

// -----------------------------------------------------------------------------
// 四則運算 + 優先序 + 括號
// -----------------------------------------------------------------------------

TEST(Formula, PrecedenceMultiplyBeforeAdd) {
    EXPECT_EQ(evaluate("1 + 2 * 3").value().as_int(), 7);
    EXPECT_EQ(evaluate("2 * 3 + 1").value().as_int(), 7);
}

TEST(Formula, ParenthesesOverridePrecedence) {
    EvalResult r = evaluate("(1 + 2) * 3");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 9);
}

TEST(Formula, NestedParentheses) {
    EvalResult r = evaluate("2 * (3 + (4 - 1))");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 12);
}

TEST(Formula, Subtraction) {
    EXPECT_EQ(evaluate("10 - 4 - 3").value().as_int(), 3);  // 左結合
}

TEST(Formula, IntegerDivisionExact) {
    EvalResult r = evaluate("6 / 2");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_integer());
    EXPECT_EQ(r.value().as_int(), 3);
}

TEST(Formula, IntegerDivisionInexactBecomesFloat) {
    EvalResult r = evaluate("7 / 2");
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().is_integer());
    EXPECT_DOUBLE_EQ(r.value().as_number(), 3.5);
}

TEST(Formula, Modulo) {
    EvalResult r = evaluate("17 % 5");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_integer());
    EXPECT_EQ(r.value().as_int(), 2);
}

// -----------------------------------------------------------------------------
// 一元運算
// -----------------------------------------------------------------------------

TEST(Formula, UnaryMinus) {
    EXPECT_EQ(evaluate("-5").value().as_int(), -5);
    EXPECT_EQ(evaluate("3 + -2").value().as_int(), 1);
    EXPECT_EQ(evaluate("-(2 + 3)").value().as_int(), -5);
}

TEST(Formula, UnaryPlus) {
    EXPECT_EQ(evaluate("+7").value().as_int(), 7);
}

// -----------------------------------------------------------------------------
// 變數引用
// -----------------------------------------------------------------------------

TEST(Formula, VariableReference) {
    VariableScope s;
    s.define("width", Value::integer(100));
    EvalResult r = evaluate("width + 8", s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 108);
}

TEST(Formula, VariableInComplexExpression) {
    VariableScope s;
    s.define("width", Value::integer(100));
    s.define("pad", Value::integer(8));
    EvalResult r = evaluate("2 * (width + pad)", s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 216);
}

TEST(Formula, BareVariableReturnsItsValue) {
    VariableScope s;
    s.define("title", Value::string("hello"));
    EvalResult r = evaluate("title", s);
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().is_string());
    EXPECT_EQ(r.value().as_string(), "hello");
}

TEST(Formula, VariableLookupWalksParentChain) {
    VariableScope parent;
    parent.define("base", Value::integer(50));
    VariableScope child(&parent);
    child.define("extra", Value::integer(5));
    EvalResult r = evaluate("base + extra", child);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 55);
}

TEST(Formula, UndefinedVariableIsErrorWithPosition) {
    VariableScope s;
    EvalResult r = evaluate("1 + missing", s);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().position, 4u);  // 'missing' 起點
    EXPECT_NE(r.error().message.find("missing"), std::string::npos);
}

TEST(Formula, NoScopeMakesAnyVariableUndefined) {
    EvalResult r = evaluate("x");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().position, 0u);
}

TEST(Formula, StringVariableInArithmeticIsTypeError) {
    VariableScope s;
    s.define("name", Value::string("abc"));
    EvalResult r = evaluate("name + 1", s);
    ASSERT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// 比較與布林邏輯
// -----------------------------------------------------------------------------

TEST(Formula, ComparisonProducesBool) {
    EvalResult r = evaluate("3 > 2");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().is_bool());
    EXPECT_TRUE(r.value().as_bool());
}

TEST(Formula, ComparisonOperators) {
    EXPECT_TRUE(evaluate("2 <= 2").value().as_bool());
    EXPECT_FALSE(evaluate("2 < 2").value().as_bool());
    EXPECT_TRUE(evaluate("5 >= 4").value().as_bool());
    EXPECT_TRUE(evaluate("4 == 4").value().as_bool());
    EXPECT_TRUE(evaluate("4 != 5").value().as_bool());
}

TEST(Formula, BooleanLiteralsAndLogic) {
    EXPECT_TRUE(evaluate("true").value().as_bool());
    EXPECT_FALSE(evaluate("false").value().as_bool());
    EXPECT_TRUE(evaluate("true && true").value().as_bool());
    EXPECT_FALSE(evaluate("true && false").value().as_bool());
    EXPECT_TRUE(evaluate("false || true").value().as_bool());
    EXPECT_FALSE(evaluate("!true").value().as_bool());
}

TEST(Formula, LogicalPrecedenceWithComparison) {
    VariableScope s;
    s.define("count", Value::integer(3));
    EvalResult r = evaluate("count > 0 && count < 10", s);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().as_bool());
}

TEST(Formula, ComparisonBindsTighterThanLogical) {
    // 若 && 綁得比 > 緊會型別錯誤；此處應解析為 (1 < 2) && (2 < 3)
    EvalResult r = evaluate("1 < 2 && 2 < 3");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().as_bool());
}

// -----------------------------------------------------------------------------
// 除零報錯（帶位置）
// -----------------------------------------------------------------------------

TEST(Formula, DivisionByZeroIsError) {
    EvalResult r = evaluate("1 / 0");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().position, 2u);  // '/' 的位置
    EXPECT_NE(r.error().message.find("除"), std::string::npos);
}

TEST(Formula, DivisionByZeroViaExpression) {
    EvalResult r = evaluate("10 / (5 - 5)");
    ASSERT_FALSE(r.ok());
}

TEST(Formula, ModuloByZeroIsError) {
    EvalResult r = evaluate("10 % 0");
    ASSERT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// 語法錯誤定位報錯
// -----------------------------------------------------------------------------

TEST(Formula, IllegalCharacterReportsPosition) {
    EvalResult r = evaluate("1 + @");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().position, 4u);  // '@' 的位置
}

TEST(Formula, MissingOperandReportsError) {
    EvalResult r = evaluate("1 +");
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}

TEST(Formula, UnbalancedParenReportsError) {
    EvalResult r = evaluate("(1 + 2");
    ASSERT_FALSE(r.ok());
}

TEST(Formula, UnexpectedRightParen) {
    EvalResult r = evaluate("1 + 2)");
    ASSERT_FALSE(r.ok());
}

TEST(Formula, EmptyExpressionIsError) {
    EvalResult r = evaluate("");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("空"), std::string::npos);
}

TEST(Formula, TrailingTokenIsError) {
    EvalResult r = evaluate("1 2");
    ASSERT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// 公式標記辨識與去除
// -----------------------------------------------------------------------------

TEST(Formula, IsFormulaRecognizesMarkers) {
    EXPECT_TRUE(is_formula("= 2 + 2"));
    EXPECT_TRUE(is_formula("${ a + b }"));
    EXPECT_FALSE(is_formula("hello"));
    EXPECT_FALSE(is_formula(""));
}

TEST(Formula, FormulaBodyStripsEqualsMarker) {
    EXPECT_EQ(formula_body("= 2 * (width + 8)"), "2 * (width + 8)");
}

TEST(Formula, FormulaBodyStripsDollarBraceMarker) {
    EXPECT_EQ(formula_body("${ a + b }"), "a + b");
}

TEST(Formula, EvaluateStripsEqualsMarker) {
    EvalResult r = evaluate("= 2 * (3 + 1)");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 8);
}

TEST(Formula, EvaluateStripsDollarBraceMarker) {
    VariableScope s;
    s.define("a", Value::integer(4));
    s.define("b", Value::integer(6));
    EvalResult r = evaluate("${ a + b }", s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 10);
}

// -----------------------------------------------------------------------------
// Evaluator 實例可重入
// -----------------------------------------------------------------------------

TEST(Formula, EvaluatorInstanceReusable) {
    VariableScope s;
    s.define("k", Value::integer(2));
    Evaluator ev(s);
    EXPECT_EQ(ev.evaluate("k * 3").value().as_int(), 6);
    EXPECT_EQ(ev.evaluate("k + 10").value().as_int(), 12);
}

}  // namespace
