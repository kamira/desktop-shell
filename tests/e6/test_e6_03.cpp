// E6-03 條件動作與去抖 — 單元測試（gtest）
//
// 驗證：
//   - 條件動作：條件為真 → 經 E6-01 分派 / 為假 → 不分派；條件求值失敗（語法錯誤、
//     未定義變數、結果非布林）→ 明確回報 ConditionError，不靜默、不分派。
//   - 去抖（Debouncer）：連續觸發只放行一次（合併），advance 後重新武裝可再放行。
//   - 節流（Throttler）：限制放行頻率；advance 推進時鐘後重新放行。
//   - 與上游整合：ConditionalAction 觸發真實 CommandBus 分派、參數傳遞、變數作用域。
//   - 真值橋接 value_truthiness 與契約版本標記。
// 全程純邏輯 + 注入式邏輯 tick，不依賴 wall-clock / thread / 任何平台後端。
#include "conditional_action.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "document.hpp"    // E7-01：Value
#include "variables.hpp"   // E7-02：VariableScope

using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::command::CommandValue;
using ds::command::ConditionalAction;
using ds::command::ConditionalOutcome;
using ds::command::ConditionalStatus;
using ds::command::Debouncer;
using ds::command::Throttler;
using ds::command::Tick;
using ds::format::Value;
using ds::format::VariableScope;

namespace {

// 記錄呼叫次數 / 最後參數的處理器狀態；命令 id = "act"。
struct Recorder {
    int calls = 0;
    std::int64_t last_amount = -1;
};

CommandBus make_bus(Recorder& rec) {
    CommandBus bus;
    bus.register_command("act", [&rec](const CommandArgs& args) {
        rec.calls++;
        if (auto a = args.get_int("amount")) rec.last_amount = *a;
        return CommandResult::make_ok(CommandValue{std::int64_t{7}}, "done");
    });
    return bus;
}

// -------------------- 契約版本 / 真值橋接 --------------------

TEST(Contract, VersionMarker) {
    EXPECT_STREQ(ds::command::conditional_contract_version(), "e6_03/1.0.0");
}

TEST(Truthiness, BoolAndNumber) {
    bool out = false;
    EXPECT_TRUE(ds::command::value_truthiness(Value::boolean(true), out));
    EXPECT_TRUE(out);
    EXPECT_TRUE(ds::command::value_truthiness(Value::boolean(false), out));
    EXPECT_FALSE(out);
    EXPECT_TRUE(ds::command::value_truthiness(Value::integer(5), out));
    EXPECT_TRUE(out);
    EXPECT_TRUE(ds::command::value_truthiness(Value::integer(0), out));
    EXPECT_FALSE(out);
    EXPECT_TRUE(ds::command::value_truthiness(Value::number(0.0), out));
    EXPECT_FALSE(out);
    EXPECT_TRUE(ds::command::value_truthiness(Value::number(2.5), out));
    EXPECT_TRUE(out);
}

TEST(Truthiness, NonBooleanTypesRejected) {
    bool out = true;  // 應保持不動。
    EXPECT_FALSE(ds::command::value_truthiness(Value::null(), out));
    EXPECT_FALSE(ds::command::value_truthiness(Value::string("x"), out));
    EXPECT_TRUE(out);  // 未被觸碰。
}

// -------------------- 條件動作：分派 / 跳過 --------------------

TEST(ConditionalAction, TrueDispatchesViaBus) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    VariableScope scope;
    scope.define("count", Value::integer(3));

    ConditionalAction action;
    action.condition_expr = "count > 0";
    action.command = "act";
    action.args.set("amount", CommandValue{std::int64_t{42}});

    ConditionalOutcome out = action.evaluate_and_dispatch(bus, scope);
    EXPECT_EQ(out.status, ConditionalStatus::Dispatched);
    EXPECT_TRUE(out.dispatched());
    EXPECT_TRUE(out.condition_value);
    EXPECT_EQ(out.result.status, CommandStatus::Ok);
    EXPECT_EQ(rec.calls, 1);
    EXPECT_EQ(rec.last_amount, 42);  // 參數確實傳遞。
    ASSERT_TRUE(out.result.value.as_int().has_value());
    EXPECT_EQ(*out.result.value.as_int(), 7);  // 處理器回傳值透傳。
}

TEST(ConditionalAction, FalseSkipsNoDispatch) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    VariableScope scope;
    scope.define("count", Value::integer(0));

    ConditionalAction action;
    action.condition_expr = "count > 0";
    action.command = "act";

    ConditionalOutcome out = action.evaluate_and_dispatch(bus, scope);
    EXPECT_EQ(out.status, ConditionalStatus::Skipped);
    EXPECT_TRUE(out.skipped());
    EXPECT_FALSE(out.condition_value);
    EXPECT_EQ(rec.calls, 0);  // 未分派。
}

TEST(ConditionalAction, FormulaMarkerAndBooleanLiteralNoScope) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    ConditionalAction action;
    action.condition_expr = "= 1 < 2";  // 前導 '=' 公式標記；純字面量，無需作用域。
    action.command = "act";

    ConditionalOutcome out = action.evaluate_and_dispatch(bus);  // 無作用域多載。
    EXPECT_EQ(out.status, ConditionalStatus::Dispatched);
    EXPECT_EQ(rec.calls, 1);
}

TEST(ConditionalAction, LogicalOperatorsWithScope) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    VariableScope scope;
    scope.define("ready", Value::boolean(true));
    scope.define("busy", Value::boolean(false));

    ConditionalAction action;
    action.condition_expr = "ready && !busy";
    action.command = "act";

    ConditionalOutcome out = action.evaluate_and_dispatch(bus, scope);
    EXPECT_EQ(out.status, ConditionalStatus::Dispatched);
    EXPECT_EQ(rec.calls, 1);
}

// -------------------- 條件動作：求值失敗明確回報，不靜默 --------------------

TEST(ConditionalAction, SyntaxErrorReportedNotDispatched) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    ConditionalAction action;
    action.condition_expr = "1 +";  // 語法錯誤。
    action.command = "act";

    ConditionalOutcome out = action.evaluate_and_dispatch(bus);
    EXPECT_EQ(out.status, ConditionalStatus::ConditionError);
    EXPECT_TRUE(out.is_error());
    EXPECT_FALSE(out.message.empty());  // 帶人類可讀原因。
    EXPECT_EQ(rec.calls, 0);            // 絕不因求值失敗而分派。
}

TEST(ConditionalAction, UndefinedVariableReportedNotDispatched) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    VariableScope scope;  // 未定義 nope。

    ConditionalAction action;
    action.condition_expr = "nope > 0";
    action.command = "act";

    ConditionalOutcome out = action.evaluate_and_dispatch(bus, scope);
    EXPECT_EQ(out.status, ConditionalStatus::ConditionError);
    EXPECT_FALSE(out.message.empty());
    EXPECT_EQ(rec.calls, 0);
}

TEST(ConditionalAction, NonBooleanResultReportedNotDispatched) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    VariableScope scope;
    scope.define("name", Value::string("hello"));

    ConditionalAction action;
    action.condition_expr = "name";  // 裸字串變數 → 結果非布林 / 非數值。
    action.command = "act";

    ConditionalOutcome out = action.evaluate_and_dispatch(bus, scope);
    EXPECT_EQ(out.status, ConditionalStatus::ConditionError);
    EXPECT_FALSE(out.message.empty());
    EXPECT_EQ(rec.calls, 0);
}

TEST(ConditionalAction, TrueButUnknownCommandReturnsNotFound) {
    Recorder rec;
    CommandBus bus = make_bus(rec);  // 只註冊 "act"。

    ConditionalAction action;
    action.condition_expr = "= true";
    action.command = "missing";  // 未註冊。

    ConditionalOutcome out = action.evaluate_and_dispatch(bus);
    // 條件為真 → 有嘗試分派；未知命令由 E6-01 回 NotFound（不崩潰）。
    EXPECT_EQ(out.status, ConditionalStatus::Dispatched);
    EXPECT_EQ(out.result.status, CommandStatus::NotFound);
    EXPECT_EQ(rec.calls, 0);
}

// -------------------- 去抖（Debouncer） --------------------

TEST(Debouncer, ImmediateTriggerDoesNotPass) {
    Debouncer d(10);
    EXPECT_FALSE(d.trigger(0));  // 去抖延後放行。
    EXPECT_TRUE(d.pending());
}

TEST(Debouncer, BurstCoalescesToSinglePass) {
    Debouncer d(10);
    // 連續觸發（皆在視窗內），deadline 不斷後推。
    EXPECT_FALSE(d.trigger(0));
    EXPECT_FALSE(d.trigger(3));
    EXPECT_FALSE(d.trigger(6));
    // 尚未靜默滿 window（最後 trigger 於 6，deadline = 16）。
    EXPECT_FALSE(d.advance(15));
    EXPECT_TRUE(d.pending());
    // 靜默滿 window → 放行一次。
    EXPECT_TRUE(d.advance(16));
    EXPECT_FALSE(d.pending());
    // 不再重複放行。
    EXPECT_FALSE(d.advance(30));
    EXPECT_FALSE(d.advance(100));
}

TEST(Debouncer, RearmsAfterReleaseAndPassesAgain) {
    Debouncer d(5);
    EXPECT_FALSE(d.trigger(0));
    EXPECT_TRUE(d.advance(5));   // 第一次放行。
    // 重新武裝：新的一輪觸發。
    EXPECT_FALSE(d.trigger(10));
    EXPECT_FALSE(d.advance(14)); // 尚未滿 window（deadline = 15）。
    EXPECT_TRUE(d.advance(15));  // 第二次放行。
}

TEST(Debouncer, MonotonicClockIgnoresBackwardTick) {
    Debouncer d(10);
    EXPECT_FALSE(d.trigger(20));      // deadline = 30。
    EXPECT_FALSE(d.advance(5));       // 倒退 tick 被夾住，不放行。
    EXPECT_EQ(d.now(), 20);
    EXPECT_TRUE(d.advance(30));       // 前進滿 window → 放行。
}

TEST(Debouncer, WindowClampedToAtLeastOne) {
    Debouncer d(0);  // <1 夾為 1。
    EXPECT_EQ(d.window(), 1);
    EXPECT_FALSE(d.trigger(0));   // deadline = 1。
    EXPECT_FALSE(d.advance(0));   // now 仍為 0 < 1。
    EXPECT_TRUE(d.advance(1));
}

// -------------------- 節流（Throttler） --------------------

TEST(Throttler, FirstPassesThenRateLimited) {
    Throttler t(10);
    EXPECT_TRUE(t.trigger(0));    // 首次即放行。
    EXPECT_FALSE(t.trigger(3));   // 距上次放行 < interval → 擋下。
    EXPECT_FALSE(t.trigger(9));
    EXPECT_TRUE(t.trigger(10));   // 滿 interval → 放行。
    EXPECT_FALSE(t.trigger(12));
    EXPECT_FALSE(t.trigger(19));
    EXPECT_TRUE(t.trigger(20));   // 再滿 interval → 放行。
}

TEST(Throttler, AdvanceThenTriggerPassesAgain) {
    Throttler t(10);
    EXPECT_TRUE(t.trigger(0));
    EXPECT_FALSE(t.trigger(5));   // 被擋。
    EXPECT_TRUE(t.advance(10));   // 純推進時鐘（確有前進）。
    EXPECT_FALSE(t.advance(10));  // 未再前進。
    EXPECT_TRUE(t.trigger(10));   // advance 後已滿 interval → 重新放行。
}

TEST(Throttler, WouldPassIsPureQuery) {
    Throttler t(10);
    EXPECT_TRUE(t.would_pass(0));
    EXPECT_TRUE(t.trigger(0));
    EXPECT_FALSE(t.would_pass(5));   // 查詢不改狀態。
    EXPECT_FALSE(t.would_pass(5));
    EXPECT_TRUE(t.would_pass(10));
    // would_pass 未改變內部狀態 → 於 5 觸發仍被擋。
    EXPECT_FALSE(t.trigger(5));
}

TEST(Throttler, MonotonicClockIgnoresBackwardTick) {
    Throttler t(10);
    EXPECT_TRUE(t.trigger(100));
    EXPECT_FALSE(t.trigger(50));   // 倒退，被夾住 → 距上次仍為 0 → 擋。
    EXPECT_EQ(t.now(), 100);
    EXPECT_TRUE(t.trigger(110));
}

TEST(Throttler, IntervalClampedToAtLeastOne) {
    Throttler t(0);  // <1 夾為 1。
    EXPECT_EQ(t.interval(), 1);
    EXPECT_TRUE(t.trigger(0));
    EXPECT_FALSE(t.trigger(0));  // 同 tick，距上次 0 < 1。
    EXPECT_TRUE(t.trigger(1));
}

// -------------------- 整合：條件動作 + 去抖，同一匯流排 --------------------

TEST(Integration, ConditionalGatedThenDebounced) {
    Recorder rec;
    CommandBus bus = make_bus(rec);

    VariableScope scope;
    scope.define("enabled", Value::boolean(true));

    ConditionalAction action;
    action.condition_expr = "enabled";
    action.command = "act";

    // 去抖僅放行一次，放行時才做條件分派。
    Debouncer d(5);
    int dispatched = 0;
    for (Tick tk : {Tick{0}, Tick{1}, Tick{2}}) {
        if (d.trigger(tk)) {
            if (action.evaluate_and_dispatch(bus, scope).dispatched()) dispatched++;
        }
    }
    // 連續觸發皆未即時放行。
    EXPECT_EQ(rec.calls, 0);
    // 靜默滿 window → 放行一次 → 條件成立 → 分派一次。
    if (d.advance(7)) {
        if (action.evaluate_and_dispatch(bus, scope).dispatched()) dispatched++;
    }
    EXPECT_EQ(dispatched, 1);
    EXPECT_EQ(rec.calls, 1);
}

}  // namespace
