// E8-02 對話腳本直譯器 — 契約 / 行為測試（gtest）。
//
// 涵蓋：線性步驟執行、公式求值步驟（set，含整數 / 浮點型別）、條件分支（if goto，取 / 不取）、
// 無條件跳轉（goto）、變數跨步延續、迴圈（goto + 條件 + 變數）、未知指令報錯、
// 注入 sink 收到輸出、say 的 ${var} 內插（含父作用域）、求值 / 未定義變數 / 除零 / 條件型別
// 錯誤定位、未知標籤、重複標籤（建構期錯誤）、無窮迴圈步數預算、逐步 step() 語意、
// value_to_text 呈現、RecordingSink 工具。
#include "interpreter.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "document.hpp"   // E7-01 Value（父作用域預注入字串變數用）
#include "variables.hpp"  // E7-02 VariableScope

using ds::script::ExecutionContext;
using ds::script::Interpreter;
using ds::script::RecordingSink;
using ds::script::RunResult;
using ds::script::Script;
using ds::script::Step;
using ds::script::StepResult;
using ds::script::value_to_text;
using ds::format::Value;
using ds::format::VariableScope;

// -----------------------------------------------------------------------------
// 線性執行 / 注入 sink
// -----------------------------------------------------------------------------

TEST(E8_02_Linear, EmptyScriptRunsToFinished) {
    Script script;
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.steps_executed(), 0u);
    EXPECT_TRUE(interp.finished());
    EXPECT_TRUE(sink.empty());
}

TEST(E8_02_Linear, LinearStepsExecuteInOrder) {
    Script script;
    script.add(Step::say("first")).add(Step::say("second")).add(Step::say("third"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.steps_executed(), 3u);
    ASSERT_EQ(sink.count(), 3u);
    EXPECT_EQ(sink.lines()[0], "first");
    EXPECT_EQ(sink.lines()[1], "second");
    EXPECT_EQ(sink.lines()[2], "third");
}

TEST(E8_02_Linear, SayEmitsToInjectedSink) {
    Script script;
    script.add(Step::say("hello world"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_EQ(sink.lines()[0], "hello world");
}

// -----------------------------------------------------------------------------
// 公式求值步驟（set）
// -----------------------------------------------------------------------------

TEST(E8_02_Set, EvaluatesFormulaToInteger) {
    Script script;
    script.add(Step::set("x", "6 / 2"));  // 整除 → 整數
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    const Value* x = interp.context().variables().find("x");
    ASSERT_NE(x, nullptr);
    ASSERT_TRUE(x->is_integer());
    EXPECT_EQ(x->as_int(), 3);
}

TEST(E8_02_Set, EvaluatesFormulaToFloat) {
    Script script;
    script.add(Step::set("x", "7 / 2"));  // 非整除 → 浮點
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    const Value* x = interp.context().variables().find("x");
    ASSERT_NE(x, nullptr);
    ASSERT_TRUE(x->is_number());
    EXPECT_FALSE(x->is_integer());
    EXPECT_DOUBLE_EQ(x->as_number(), 3.5);
}

TEST(E8_02_Set, BooleanFromComparison) {
    Script script;
    script.add(Step::set("ok", "3 > 1"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    const Value* v = interp.context().variables().find("ok");
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(v->is_bool());
    EXPECT_TRUE(v->as_bool());
}

// -----------------------------------------------------------------------------
// 變數跨步延續
// -----------------------------------------------------------------------------

TEST(E8_02_Continuity, VariablesPersistAcrossSteps) {
    Script script;
    script.add(Step::set("x", "1"))
        .add(Step::set("x", "x + 2"))   // 讀前一步定義的 x
        .add(Step::set("y", "x * 10"));  // 讀累積後的 x
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    const Value* x = interp.context().variables().find("x");
    const Value* y = interp.context().variables().find("y");
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    EXPECT_EQ(x->as_int(), 3);
    EXPECT_EQ(y->as_int(), 30);
}

TEST(E8_02_Continuity, ReadsPreSeededParentScope) {
    VariableScope parent;
    parent.define("base", Value::integer(100));
    Script script;
    script.add(Step::set("w", "base + 8"));
    RecordingSink sink;
    Interpreter interp(script, sink, &parent);
    ASSERT_TRUE(interp.run().ok());
    const Value* w = interp.context().variables().find("w");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->as_int(), 108);
}

// -----------------------------------------------------------------------------
// 條件分支 / 跳轉
// -----------------------------------------------------------------------------

TEST(E8_02_Branch, ConditionalBranchTaken) {
    Script script;
    script.add(Step::set("x", "5"))
        .add(Step::if_goto("x > 0", "positive"))
        .add(Step::say("non-positive"))
        .add(Step::goto_label("done"))
        .add(Step::label("positive"))
        .add(Step::say("positive"))
        .add(Step::label("done"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_EQ(sink.lines()[0], "positive");
}

TEST(E8_02_Branch, ConditionalBranchNotTaken) {
    Script script;
    script.add(Step::set("x", "0"))
        .add(Step::if_goto("x > 0", "positive"))
        .add(Step::say("non-positive"))
        .add(Step::goto_label("done"))
        .add(Step::label("positive"))
        .add(Step::say("positive"))
        .add(Step::label("done"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_EQ(sink.lines()[0], "non-positive");
}

TEST(E8_02_Branch, UnconditionalGotoSkips) {
    Script script;
    script.add(Step::goto_label("end"))
        .add(Step::say("skipped"))
        .add(Step::label("end"))
        .add(Step::say("reached"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_EQ(sink.lines()[0], "reached");
}

TEST(E8_02_Branch, LoopWithGotoAndCondition) {
    // set i=0; loop: say ${i}; i=i+1; if i < 3 goto loop → emits 0,1,2
    Script script;
    script.add(Step::set("i", "0"))
        .add(Step::label("loop"))
        .add(Step::say("${i}"))
        .add(Step::set("i", "i + 1"))
        .add(Step::if_goto("i < 3", "loop"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 3u);
    EXPECT_EQ(sink.lines()[0], "0");
    EXPECT_EQ(sink.lines()[1], "1");
    EXPECT_EQ(sink.lines()[2], "2");
    const Value* i = interp.context().variables().find("i");
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->as_int(), 3);
}

// -----------------------------------------------------------------------------
// say 的 ${var} 內插（消費 E7-02 resolve）
// -----------------------------------------------------------------------------

TEST(E8_02_Say, InterpolatesNumericVariable) {
    Script script;
    script.add(Step::set("count", "40 + 2")).add(Step::say("Count is ${count}"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_EQ(sink.lines()[0], "Count is 42");
}

TEST(E8_02_Say, InterpolatesParentScopeString) {
    VariableScope parent;
    parent.define("name", Value::string("World"));
    Script script;
    script.add(Step::say("Hello ${name}!"));
    RecordingSink sink;
    Interpreter interp(script, sink, &parent);
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_EQ(sink.lines()[0], "Hello World!");
}

// -----------------------------------------------------------------------------
// 錯誤：不靜默、可定位到步驟
// -----------------------------------------------------------------------------

TEST(E8_02_Error, UnknownCommandReported) {
    Script script;
    script.add(Step::say("ok")).add(Step("frobnicate", "", ""));
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().step, 1u);
    EXPECT_NE(r.error().message.find("unknown command"), std::string::npos);
    EXPECT_NE(r.error().message.find("frobnicate"), std::string::npos);
}

TEST(E8_02_Error, UndefinedVariableInSet) {
    Script script;
    script.add(Step::set("x", "y + 1"));  // y 未定義
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().step, 0u);
    EXPECT_NE(r.error().message.find("set 'x'"), std::string::npos);
}

TEST(E8_02_Error, DivideByZeroInSet) {
    Script script;
    script.add(Step::set("x", "1 / 0"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().step, 0u);
}

TEST(E8_02_Error, ConditionTypeErrorWhenNotBoolOrNumber) {
    VariableScope parent;
    parent.define("label_text", Value::string("hi"));  // 字串變數
    Script script;
    script.add(Step::if_goto("label_text", "somewhere")).add(Step::label("somewhere"));
    RecordingSink sink;
    Interpreter interp(script, sink, &parent);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().step, 0u);
    EXPECT_NE(r.error().message.find("not boolean or numeric"), std::string::npos);
}

TEST(E8_02_Error, GotoUnknownLabel) {
    Script script;
    script.add(Step::goto_label("nowhere"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().step, 0u);
    EXPECT_NE(r.error().message.find("unknown label"), std::string::npos);
}

TEST(E8_02_Error, SayUndefinedVariableInterpolation) {
    Script script;
    script.add(Step::say("value is ${missing}"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().step, 0u);
    EXPECT_NE(r.error().message.find("say:"), std::string::npos);
    EXPECT_TRUE(sink.empty());  // 失敗時不得已 emit 半成品
}

TEST(E8_02_Error, DuplicateLabelIsBuildError) {
    Script script;
    script.add(Step::label("dup")).add(Step::say("x")).add(Step::label("dup"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("duplicate label"), std::string::npos);
}

TEST(E8_02_Error, InfiniteLoopHitsStepBudget) {
    Script script;
    script.add(Step::label("spin")).add(Step::goto_label("spin"));
    RecordingSink sink;
    Interpreter interp(script, sink);
    interp.set_step_budget(50);
    RunResult r = interp.run();
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("budget"), std::string::npos);
    EXPECT_TRUE(interp.finished());
}

TEST(E8_02_Error, StepAfterFailureStaysFailed) {
    Script script;
    script.add(Step("bogus", "", ""));
    RecordingSink sink;
    Interpreter interp(script, sink);
    StepResult a = interp.step();
    ASSERT_FALSE(a.ok());
    StepResult b = interp.step();  // 再呼叫仍回同一失敗，不繼續
    EXPECT_FALSE(b.ok());
    EXPECT_EQ(b.error().message, a.error().message);
}

// -----------------------------------------------------------------------------
// 逐步 step() 語意
// -----------------------------------------------------------------------------

TEST(E8_02_Step, IncrementalStepping) {
    Script script;
    script.add(Step::set("a", "1")).add(Step::set("b", "a + 1"));
    RecordingSink sink;
    Interpreter interp(script, sink);

    EXPECT_EQ(interp.context().pc(), 0u);
    StepResult s0 = interp.step();
    EXPECT_TRUE(s0.ok());
    EXPECT_FALSE(s0.is_finished());
    EXPECT_EQ(interp.context().pc(), 1u);
    EXPECT_FALSE(interp.finished());

    StepResult s1 = interp.step();
    EXPECT_TRUE(s1.ok());
    EXPECT_TRUE(interp.finished());

    StepResult s2 = interp.step();  // 已結束 → 安全 no-op
    EXPECT_TRUE(s2.ok());
    EXPECT_TRUE(s2.is_finished());

    EXPECT_EQ(interp.context().steps_executed(), 2u);
    const Value* b = interp.context().variables().find("b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->as_int(), 2);
}

// -----------------------------------------------------------------------------
// value_to_text 呈現
// -----------------------------------------------------------------------------

TEST(E8_02_Text, ScalarRendering) {
    EXPECT_EQ(value_to_text(Value::null()), "null");
    EXPECT_EQ(value_to_text(Value::boolean(true)), "true");
    EXPECT_EQ(value_to_text(Value::boolean(false)), "false");
    EXPECT_EQ(value_to_text(Value::integer(42)), "42");
    EXPECT_EQ(value_to_text(Value::number(3.5)), "3.5");
    EXPECT_EQ(value_to_text(Value::string("hi")), "hi");
}

TEST(E8_02_Text, ContainerRendering) {
    Value list = Value::list({Value::integer(1), Value::integer(2)});
    EXPECT_EQ(value_to_text(list), "[1, 2]");
    Value map = Value::map({{"k", Value::integer(9)}});
    EXPECT_EQ(value_to_text(map), "{k: 9}");
}

// -----------------------------------------------------------------------------
// RecordingSink 工具
// -----------------------------------------------------------------------------

TEST(E8_02_Sink, ClearAndCount) {
    RecordingSink sink;
    sink.emit("a");
    sink.emit("b");
    EXPECT_EQ(sink.count(), 2u);
    sink.clear();
    EXPECT_TRUE(sink.empty());
    EXPECT_EQ(sink.count(), 0u);
}
