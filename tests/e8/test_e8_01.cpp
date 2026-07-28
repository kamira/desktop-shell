// E8-01 腳本引擎（QML/JS）— 契約 / 行為測試（gtest）。
//
// 涵蓋：evaluate 基本腳本（字面值 / 賦值 / 全域讀取）、綁定宿主函式 → 腳本呼叫、
// 經 E6-01 CommandBus 分派命令（透過命令橋接）、set/get global、語法錯誤回報（帶行號）、
// 執行期錯誤回報（未定義全域 / 未綁定函式 / 宿主函式失敗 / 命令失敗）、
// 可插拔引擎替換（同一份宿主程式碼驅動 Mini 與 Null 後端；Null evaluate → Unsupported）、
// 重複綁定不覆蓋、直接 call() 語意。
#include "script_engine.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "command_bus.hpp"  // E6-01 CommandBus / CommandArgs / CommandResult

using ds::script::EvalResult;
using ds::script::HostResult;
using ds::script::MiniScriptEngine;
using ds::script::NullScriptEngine;
using ds::script::ScriptEngine;
using ds::script::ScriptError;
using ds::script::ScriptValue;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;

// -----------------------------------------------------------------------------
// evaluate 基本腳本
// -----------------------------------------------------------------------------

TEST(E8_01_Evaluate, EmptyScriptReturnsNull) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_null());
}

TEST(E8_01_Evaluate, CommentsAndBlankLinesIgnored) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("# a comment\n\n   \n# another\n");
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().is_null());
}

TEST(E8_01_Evaluate, IntegerLiteralExpression) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("42");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().as_int().has_value());
    EXPECT_EQ(*r.value().as_int(), 42);
}

TEST(E8_01_Evaluate, NegativeAndDoubleAndBoolAndStringLiterals) {
    MiniScriptEngine engine;
    EXPECT_EQ(*engine.evaluate("-7").value().as_int(), -7);
    EXPECT_DOUBLE_EQ(*engine.evaluate("3.5").value().as_double(), 3.5);
    EXPECT_EQ(*engine.evaluate("true").value().as_bool(), true);
    EXPECT_EQ(*engine.evaluate("false").value().as_bool(), false);
    EXPECT_TRUE(engine.evaluate("null").value().is_null());
    EXPECT_EQ(*engine.evaluate("\"hello\"").value().as_string(), "hello");
}

TEST(E8_01_Evaluate, StringEscapes) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("\"a\\tb\\nc\\\"d\\\\e\"");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().as_string().has_value());
    EXPECT_EQ(*r.value().as_string(), "a\tb\nc\"d\\e");
}

TEST(E8_01_Evaluate, AssignmentThenGlobalReference) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("set x = 10\nx");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().as_int().has_value());
    EXPECT_EQ(*r.value().as_int(), 10);
    // 全域可由宿主讀回。
    auto g = engine.get_global("x");
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(*g->as_int(), 10);
}

TEST(E8_01_Evaluate, AssignmentWithoutSetKeyword) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("y = \"world\"");
    ASSERT_TRUE(r.ok());
    auto g = engine.get_global("y");
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(*g->as_string(), "world");
}

TEST(E8_01_Evaluate, LastStatementValueIsResult) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("a = 1\nb = 2\nb");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value().as_int(), 2);
}

// -----------------------------------------------------------------------------
// set / get global（宿主 <-> 腳本）
// -----------------------------------------------------------------------------

TEST(E8_01_Global, HostSetGlobalVisibleToScript) {
    MiniScriptEngine engine;
    engine.set_global("volume", ScriptValue(std::int64_t{75}));
    EvalResult r = engine.evaluate("volume");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value().as_int(), 75);
}

TEST(E8_01_Global, GetUnknownGlobalIsNullopt) {
    MiniScriptEngine engine;
    EXPECT_FALSE(engine.get_global("missing").has_value());
}

TEST(E8_01_Global, ScriptAssignmentReadableByHost) {
    MiniScriptEngine engine;
    ASSERT_TRUE(engine.evaluate("greeting = \"hi\"").ok());
    auto g = engine.get_global("greeting");
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(*g->as_string(), "hi");
}

TEST(E8_01_Global, SetGlobalOverwrites) {
    MiniScriptEngine engine;
    engine.set_global("n", ScriptValue(1));
    engine.set_global("n", ScriptValue(2));
    EXPECT_EQ(*engine.get_global("n")->as_int(), 2);
}

// -----------------------------------------------------------------------------
// 綁定宿主函式 → 腳本呼叫
// -----------------------------------------------------------------------------

TEST(E8_01_Bind, ScriptCallsBoundHostFunction) {
    MiniScriptEngine engine;
    std::vector<ScriptValue> seen;
    ASSERT_TRUE(engine.bind_function("record", [&seen](const std::vector<ScriptValue>& a) {
        for (const auto& v : a) seen.push_back(v);
        return HostResult::success(ScriptValue(static_cast<std::int64_t>(a.size())));
    }));
    EXPECT_TRUE(engine.has_function("record"));

    EvalResult r = engine.evaluate("record(1, \"two\", true)");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(*seen[0].as_int(), 1);
    EXPECT_EQ(*seen[1].as_string(), "two");
    EXPECT_EQ(*seen[2].as_bool(), true);
    EXPECT_EQ(*r.value().as_int(), 3);  // 回傳值 = 引數個數。
}

TEST(E8_01_Bind, CallResultAssignedToGlobal) {
    MiniScriptEngine engine;
    ASSERT_TRUE(engine.bind_function("answer", [](const std::vector<ScriptValue>&) {
        return HostResult::success(ScriptValue(std::int64_t{42}));
    }));
    ASSERT_TRUE(engine.evaluate("result = answer()").ok());
    EXPECT_EQ(*engine.get_global("result")->as_int(), 42);
}

TEST(E8_01_Bind, CallArgumentsCanBeGlobals) {
    MiniScriptEngine engine;
    engine.set_global("base", ScriptValue(std::int64_t{100}));
    ScriptValue captured;
    ASSERT_TRUE(engine.bind_function("grab", [&captured](const std::vector<ScriptValue>& a) {
        if (!a.empty()) captured = a[0];
        return HostResult::success();
    }));
    ASSERT_TRUE(engine.evaluate("grab(base)").ok());
    EXPECT_EQ(*captured.as_int(), 100);
}

TEST(E8_01_Bind, RebindDoesNotOverwrite) {
    MiniScriptEngine engine;
    EXPECT_TRUE(engine.bind_function("f", [](const std::vector<ScriptValue>&) {
        return HostResult::success(ScriptValue(1));
    }));
    // 重複綁定同名 → 拒絕（回 false），不靜默覆蓋。
    EXPECT_FALSE(engine.bind_function("f", [](const std::vector<ScriptValue>&) {
        return HostResult::success(ScriptValue(2));
    }));
    EXPECT_EQ(*engine.evaluate("f()").value().as_int(), 1);
}

TEST(E8_01_Bind, EmptyNameOrNullFunctionRejected) {
    MiniScriptEngine engine;
    EXPECT_FALSE(engine.bind_function("", [](const std::vector<ScriptValue>&) {
        return HostResult::success();
    }));
    EXPECT_FALSE(engine.bind_function("g", nullptr));
}

// -----------------------------------------------------------------------------
// 直接 call()（宿主驅動，不經腳本文字）
// -----------------------------------------------------------------------------

TEST(E8_01_Call, DirectCallInvokesHostFunction) {
    MiniScriptEngine engine;
    ASSERT_TRUE(engine.bind_function("add_tag", [](const std::vector<ScriptValue>& a) {
        std::string s;
        for (const auto& v : a) if (auto x = v.as_string()) s += *x;
        return HostResult::success(ScriptValue(s));
    }));
    EvalResult r = engine.call("add_tag", {ScriptValue("a"), ScriptValue("b")});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value().as_string(), "ab");
}

TEST(E8_01_Call, DirectCallUnknownIsNotFound) {
    MiniScriptEngine engine;
    EvalResult r = engine.call("nope", {});
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::NotFound);
}

// -----------------------------------------------------------------------------
// 經 E6-01 分派命令（命令橋接）
// -----------------------------------------------------------------------------

TEST(E8_01_CommandBridge, ScriptCallDispatchesCommand) {
    CommandBus bus;
    std::int64_t applied_level = -1;
    ASSERT_TRUE(bus.register_command("volume.set", [&applied_level](const CommandArgs& args) {
        auto lvl = args.get_int("level");
        if (!lvl) return CommandResult::make_failed("missing level");
        applied_level = *lvl;
        return CommandResult::make_ok(ScriptValue(true));
    }));

    MiniScriptEngine engine;
    // 一行式嵌入：把命令 volume.set 暴露為腳本函式 set_volume(level)。
    ASSERT_TRUE(ds::script::bind_command(engine, "set_volume", bus, "volume.set", {"level"}));
    EXPECT_TRUE(engine.has_function("set_volume"));

    EvalResult r = engine.evaluate("set_volume(60)");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(applied_level, 60);
    EXPECT_EQ(*r.value().as_bool(), true);  // 命令回傳值透傳回腳本。
}

TEST(E8_01_CommandBridge, UnknownCommandSurfacesAsRuntimeError) {
    CommandBus bus;  // 無註冊任何命令。
    MiniScriptEngine engine;
    ASSERT_TRUE(ds::script::bind_command(engine, "power_off", bus, "power.off", {}));
    EvalResult r = engine.evaluate("power_off()");
    // 命令未註冊 → CommandBus 回 NotFound → 橋接回報宿主失敗 → 引擎 Runtime 錯誤（不靜默）。
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Runtime);
    EXPECT_NE(r.message().find("unknown command"), std::string::npos);
}

TEST(E8_01_CommandBridge, FailedCommandSurfacesAsRuntimeError) {
    CommandBus bus;
    ASSERT_TRUE(bus.register_command("do.thing", [](const CommandArgs&) {
        return CommandResult::make_failed("actuator offline");
    }));
    MiniScriptEngine engine;
    ASSERT_TRUE(ds::script::bind_command(engine, "do_thing", bus, "do.thing", {}));
    EvalResult r = engine.evaluate("do_thing()");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Runtime);
    EXPECT_NE(r.message().find("actuator offline"), std::string::npos);
}

TEST(E8_01_CommandBridge, PositionalArgsMappedToNamedInOrder) {
    CommandBus bus;
    std::int64_t x = 0, y = 0;
    ASSERT_TRUE(bus.register_command("move", [&x, &y](const CommandArgs& args) {
        x = args.get_int("x").value_or(-1);
        y = args.get_int("y").value_or(-1);
        return CommandResult::make_ok();
    }));
    MiniScriptEngine engine;
    ASSERT_TRUE(ds::script::bind_command(engine, "move", bus, "move", {"x", "y"}));
    ASSERT_TRUE(engine.evaluate("move(3, 8)").ok());
    EXPECT_EQ(x, 3);
    EXPECT_EQ(y, 8);
}

// -----------------------------------------------------------------------------
// 語法錯誤回報（帶行號）
// -----------------------------------------------------------------------------

TEST(E8_01_SyntaxError, UnterminatedStringReportsSyntaxAndLine) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("a = 1\nb = \"oops");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Syntax);
    EXPECT_EQ(r.line(), 2u);
}

TEST(E8_01_SyntaxError, MalformedCallReportsSyntax) {
    MiniScriptEngine engine;
    ASSERT_TRUE(engine.bind_function("f", [](const std::vector<ScriptValue>&) {
        return HostResult::success();
    }));
    EvalResult r = engine.evaluate("f(1");  // 缺右括號。
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Syntax);
    EXPECT_EQ(r.line(), 1u);
}

TEST(E8_01_SyntaxError, InvalidAssignmentTarget) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("1abc = 5");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Syntax);
}

TEST(E8_01_SyntaxError, EmptyArgumentInCall) {
    MiniScriptEngine engine;
    ASSERT_TRUE(engine.bind_function("f", [](const std::vector<ScriptValue>&) {
        return HostResult::success();
    }));
    EvalResult r = engine.evaluate("f(1,,2)");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Syntax);
}

// -----------------------------------------------------------------------------
// 執行期錯誤回報
// -----------------------------------------------------------------------------

TEST(E8_01_RuntimeError, UndefinedGlobalReference) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("missing_var");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Runtime);
    EXPECT_NE(r.message().find("undefined global"), std::string::npos);
}

TEST(E8_01_RuntimeError, CallToUnboundFunction) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("nope()");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::NotFound);
    EXPECT_NE(r.message().find("unbound function"), std::string::npos);
}

TEST(E8_01_RuntimeError, HostFunctionFailurePropagates) {
    MiniScriptEngine engine;
    ASSERT_TRUE(engine.bind_function("boom", [](const std::vector<ScriptValue>&) {
        return HostResult::failure("kaboom");
    }));
    EvalResult r = engine.evaluate("boom()");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Runtime);
    EXPECT_NE(r.message().find("kaboom"), std::string::npos);
}

TEST(E8_01_RuntimeError, ErrorStopsExecutionAtOffendingLine) {
    MiniScriptEngine engine;
    EvalResult r = engine.evaluate("ok = 1\nundefined_ref\nok = 2");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.line(), 2u);
    // 第 3 行未執行：ok 應仍為 1。
    EXPECT_EQ(*engine.get_global("ok")->as_int(), 1);
}

// -----------------------------------------------------------------------------
// 可插拔引擎替換（同一份宿主程式碼驅動不同後端）
// -----------------------------------------------------------------------------

namespace {
// 對抽象介面撰寫的宿主嵌入程式碼：綁定 + 設全域 + 直接 call（不依賴 evaluate）。
// 對任何 ScriptEngine 後端皆可運作。回傳 call 的結果值（成功時）。
EvalResult drive_engine(ScriptEngine& engine) {
    engine.set_global("gain", ScriptValue(std::int64_t{3}));
    engine.bind_function("echo", [](const std::vector<ScriptValue>& a) {
        return HostResult::success(a.empty() ? ScriptValue() : a[0]);
    });
    return engine.call("echo", {ScriptValue("via-abstract")});
}
}  // namespace

TEST(E8_01_Pluggable, SameHostCodeDrivesMiniAndNull) {
    MiniScriptEngine mini;
    NullScriptEngine null_engine;

    EvalResult rm = drive_engine(mini);
    EvalResult rn = drive_engine(null_engine);

    ASSERT_TRUE(rm.ok());
    ASSERT_TRUE(rn.ok());
    EXPECT_EQ(*rm.value().as_string(), "via-abstract");
    EXPECT_EQ(*rn.value().as_string(), "via-abstract");
    // 全域與綁定在兩後端皆生效（嵌入契約與後端無關）。
    EXPECT_EQ(*mini.get_global("gain")->as_int(), 3);
    EXPECT_EQ(*null_engine.get_global("gain")->as_int(), 3);
    EXPECT_TRUE(mini.has_function("echo"));
    EXPECT_TRUE(null_engine.has_function("echo"));
}

TEST(E8_01_Pluggable, BackendNamesDistinct) {
    MiniScriptEngine mini;
    NullScriptEngine null_engine;
    EXPECT_STREQ(mini.backend_name(), "mini");
    EXPECT_STREQ(null_engine.backend_name(), "null");
}

TEST(E8_01_Pluggable, NullEngineEvaluateIsUnsupportedNotSilent) {
    NullScriptEngine null_engine;
    EvalResult r = null_engine.evaluate("anything");
    // Null 後端不執行腳本文字：明確回 Unsupported（不假成功、不靜默）。
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ScriptError::Unsupported);
}

TEST(E8_01_Pluggable, NullEngineCommandBridgeStillDispatchesViaCall) {
    // 命令橋接（綁定 + 直接 call）在 null 後端上仍能經 E6-01 分派命令——後端無關。
    CommandBus bus;
    bool fired = false;
    ASSERT_TRUE(bus.register_command("ping", [&fired](const CommandArgs&) {
        fired = true;
        return CommandResult::make_ok(ScriptValue(true));
    }));
    NullScriptEngine null_engine;
    ASSERT_TRUE(ds::script::bind_command(null_engine, "ping", bus, "ping", {}));
    EvalResult r = null_engine.call("ping", {});
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(fired);
    EXPECT_EQ(*r.value().as_bool(), true);
}
