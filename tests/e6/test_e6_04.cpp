// E6-04 延遲與批次執行 — 單元測試（gtest）
//
// 驗證在 E6-01 命令匯流排上擴充的兩種非即時執行語意：
//   延遲（DeferredScheduler）：
//     - 延遲命令在 advance 達門檻才執行（未達門檻不觸發）
//     - 多個延遲的到期順序（跨 tick 先到期先執行；同 tick 依排程 FIFO）
//     - 取消延遲（cancel 後不再執行）
//     - delay < 1 拒絕、advance 負值無效、advance_to、未知命令 NotFound 如實回報
//   批次（Batch / run_batch）：
//     - 依序執行
//     - 中途失敗回報（哪一步失敗）
//     - 原子批次（首次失敗即中止後續）vs Continue（跑完全部）
// 全程注入式邏輯時鐘，不使用真實時間 / sleep / thread；無平台分支。
#include "deferred_batch.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::command::Batch;
using ds::command::BatchMode;
using ds::command::BatchResult;
using ds::command::Command;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::command::CommandValue;
using ds::command::DeferredScheduler;
using ds::command::kInvalidDeferredId;
using ds::command::run_batch;

namespace {

// 建一個把每次呼叫的命令 tag 依序記錄到 log 的處理器工廠。
ds::command::CommandHandler recorder(std::vector<std::string>& log, std::string tag,
                                     bool succeed = true) {
    return [&log, tag, succeed](const CommandArgs&) -> CommandResult {
        log.push_back(tag);
        return succeed ? CommandResult::make_ok()
                       : CommandResult::make_failed("handler '" + tag + "' failed");
    };
}

// -------- 契約版本標記 --------

TEST(Contract, VersionTagPresent) {
    EXPECT_STREQ(ds::command::deferred_batch_contract_version(), "e6_04/1.0.0");
}

// ===========================================================================
// DeferredScheduler — 延遲執行
// ===========================================================================

// 延遲命令在 advance 達門檻才執行（未達門檻不觸發）。
TEST(DeferredScheduler, FiresOnlyWhenThresholdReached) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("beep", recorder(log, "beep")));

    DeferredScheduler sched(bus);
    const auto id = sched.schedule(3, "beep");
    EXPECT_NE(id, kInvalidDeferredId);
    EXPECT_EQ(sched.pending(), 1u);
    EXPECT_EQ(sched.now(), 0);

    // 尚未達門檻：advance 2 tick（now=2 < due=3），不執行。
    auto r1 = sched.advance(2);
    EXPECT_TRUE(r1.empty());
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(sched.now(), 2);
    EXPECT_EQ(sched.pending(), 1u);

    // 達門檻：再 advance 1 tick（now=3 == due），執行。
    auto r2 = sched.advance(1);
    ASSERT_EQ(r2.size(), 1u);
    EXPECT_EQ(r2[0].id, id);
    EXPECT_EQ(r2[0].due, 3);
    EXPECT_EQ(r2[0].command_id, "beep");
    EXPECT_TRUE(r2[0].ok());
    EXPECT_EQ(log, (std::vector<std::string>{"beep"}));
    EXPECT_EQ(sched.pending(), 0u);
    EXPECT_FALSE(sched.has(id));
}

// 單次跨越門檻的 advance 也會執行（now 一舉超過 due）。
TEST(DeferredScheduler, FiresWhenAdvancePastDue) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("x", recorder(log, "x")));

    DeferredScheduler sched(bus);
    sched.schedule(2, "x");
    auto fired = sched.advance(10);  // now=10 >= due=2
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(log, (std::vector<std::string>{"x"}));
}

// 多個延遲的到期順序：跨 tick 先到期先執行。
TEST(DeferredScheduler, MultipleFireInDueOrderAcrossTicks) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("a", recorder(log, "a")));
    ASSERT_TRUE(bus.register_command("b", recorder(log, "b")));
    ASSERT_TRUE(bus.register_command("c", recorder(log, "c")));

    DeferredScheduler sched(bus);
    // 故意以「非到期順序」排入，驗證執行順序由 due 決定而非排程順序。
    sched.schedule(5, "c");
    sched.schedule(1, "a");
    sched.schedule(3, "b");

    auto fired = sched.advance(5);
    ASSERT_EQ(fired.size(), 3u);
    EXPECT_EQ(fired[0].command_id, "a");  // due=1
    EXPECT_EQ(fired[1].command_id, "b");  // due=3
    EXPECT_EQ(fired[2].command_id, "c");  // due=5
    EXPECT_EQ(log, (std::vector<std::string>{"a", "b", "c"}));
}

// 同一 tick 到期者依排程先後（FIFO）決定性執行。
TEST(DeferredScheduler, SameTickFiresInScheduleOrderFIFO) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("first", recorder(log, "first")));
    ASSERT_TRUE(bus.register_command("second", recorder(log, "second")));
    ASSERT_TRUE(bus.register_command("third", recorder(log, "third")));

    DeferredScheduler sched(bus);
    sched.schedule(2, "first");
    sched.schedule(2, "second");
    sched.schedule(2, "third");

    auto fired = sched.advance(2);
    ASSERT_EQ(fired.size(), 3u);
    EXPECT_EQ(log, (std::vector<std::string>{"first", "second", "third"}));
}

// 分階段 advance：只有已到期者觸發，其餘保留待後續 advance。
TEST(DeferredScheduler, StagedAdvanceFiresIncrementally) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("near", recorder(log, "near")));
    ASSERT_TRUE(bus.register_command("far", recorder(log, "far")));

    DeferredScheduler sched(bus);
    sched.schedule(2, "near");
    sched.schedule(6, "far");

    auto r1 = sched.advance(2);  // now=2：near 到期
    ASSERT_EQ(r1.size(), 1u);
    EXPECT_EQ(r1[0].command_id, "near");
    EXPECT_EQ(sched.pending(), 1u);

    auto r2 = sched.advance(2);  // now=4：far 未到期
    EXPECT_TRUE(r2.empty());
    EXPECT_EQ(sched.pending(), 1u);

    auto r3 = sched.advance(2);  // now=6：far 到期
    ASSERT_EQ(r3.size(), 1u);
    EXPECT_EQ(r3[0].command_id, "far");
    EXPECT_EQ(log, (std::vector<std::string>{"near", "far"}));
    EXPECT_EQ(sched.pending(), 0u);
}

// 取消延遲：cancel 後該命令不再執行。
TEST(DeferredScheduler, CancelPreventsExecution) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("keep", recorder(log, "keep")));
    ASSERT_TRUE(bus.register_command("drop", recorder(log, "drop")));

    DeferredScheduler sched(bus);
    const auto keep_id = sched.schedule(2, "keep");
    const auto drop_id = sched.schedule(2, "drop");
    EXPECT_EQ(sched.pending(), 2u);

    EXPECT_TRUE(sched.cancel(drop_id));
    EXPECT_FALSE(sched.has(drop_id));
    EXPECT_TRUE(sched.has(keep_id));
    EXPECT_EQ(sched.pending(), 1u);

    // 重複取消 / 未知 id / 無效 id：回 false，不崩潰。
    EXPECT_FALSE(sched.cancel(drop_id));
    EXPECT_FALSE(sched.cancel(99999));
    EXPECT_FALSE(sched.cancel(kInvalidDeferredId));

    auto fired = sched.advance(2);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].command_id, "keep");
    EXPECT_EQ(log, (std::vector<std::string>{"keep"}));  // drop 從未執行
}

// delay < 1 拒絕排程（不排入、回無效 id、不靜默）。
TEST(DeferredScheduler, RejectsNonPositiveDelay) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("now", recorder(log, "now")));

    DeferredScheduler sched(bus);
    EXPECT_EQ(sched.schedule(0, "now"), kInvalidDeferredId);
    EXPECT_EQ(sched.schedule(-5, "now"), kInvalidDeferredId);
    EXPECT_EQ(sched.pending(), 0u);

    auto fired = sched.advance(100);
    EXPECT_TRUE(fired.empty());
    EXPECT_TRUE(log.empty());
}

// advance 負值視為無效：不倒轉時間、不執行。
TEST(DeferredScheduler, NegativeAdvanceIsNoOp) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("t", recorder(log, "t")));

    DeferredScheduler sched(bus);
    sched.schedule(1, "t");
    auto fired = sched.advance(-3);
    EXPECT_TRUE(fired.empty());
    EXPECT_EQ(sched.now(), 0);       // 未倒轉
    EXPECT_EQ(sched.pending(), 1u);  // 仍待執行
}

// advance_to：推進到絕對 tick；target <= now 為 no-op。
TEST(DeferredScheduler, AdvanceToAbsoluteTick) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("g", recorder(log, "g")));

    DeferredScheduler sched(bus);
    sched.schedule(4, "g");

    EXPECT_TRUE(sched.advance_to(3).empty());  // now=3 < due=4
    EXPECT_EQ(sched.now(), 3);
    auto fired = sched.advance_to(4);  // now=4 == due
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].command_id, "g");
    EXPECT_TRUE(sched.advance_to(2).empty());  // 過去：no-op
    EXPECT_EQ(sched.now(), 4);
}

// 延遲分派未知命令：如實回報 NotFound（不崩潰、不靜默）。
TEST(DeferredScheduler, UnknownCommandReportsNotFound) {
    CommandBus bus;  // 未註冊任何命令
    DeferredScheduler sched(bus);
    sched.schedule(1, "ghost");

    auto fired = sched.advance(1);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].command_id, "ghost");
    EXPECT_FALSE(fired[0].ok());
    EXPECT_EQ(fired[0].result.status, CommandStatus::NotFound);
}

// 延遲命令帶參數：參數如實傳入處理器。
TEST(DeferredScheduler, DeferredArgsPassedThrough) {
    CommandBus bus;
    std::int64_t seen = -1;
    ASSERT_TRUE(bus.register_command("set", [&seen](const CommandArgs& a) -> CommandResult {
        if (auto v = a.get_int("level")) seen = *v;
        return CommandResult::make_ok();
    }));

    DeferredScheduler sched(bus);
    sched.schedule(1, "set", CommandArgs{}.set("level", CommandValue(std::int64_t{7})));
    auto fired = sched.advance(1);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_TRUE(fired[0].ok());
    EXPECT_EQ(seen, 7);
}

// ===========================================================================
// Batch / run_batch — 批次執行
// ===========================================================================

// 批次依序執行（順序保證）。
TEST(Batch, RunsInOrder) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("one", recorder(log, "one")));
    ASSERT_TRUE(bus.register_command("two", recorder(log, "two")));
    ASSERT_TRUE(bus.register_command("three", recorder(log, "three")));

    Batch batch;
    batch.add("one").add("two").add("three");
    EXPECT_EQ(batch.size(), 3u);

    BatchResult res = run_batch(bus, batch);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.executed, 3u);
    EXPECT_EQ(res.total, 3u);
    EXPECT_FALSE(res.failed_index.has_value());
    EXPECT_FALSE(res.aborted());
    EXPECT_EQ(log, (std::vector<std::string>{"one", "two", "three"}));
    ASSERT_EQ(res.steps.size(), 3u);
    EXPECT_EQ(res.steps[0].command_id, "one");
    EXPECT_EQ(res.steps[2].index, 2u);
}

// 原子批次中途失敗：於第一個失敗步驟中止並回報是哪一步。
TEST(Batch, AtomicAbortsAtFirstFailureAndReportsStep) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("ok1", recorder(log, "ok1", true)));
    ASSERT_TRUE(bus.register_command("boom", recorder(log, "boom", false)));
    ASSERT_TRUE(bus.register_command("ok2", recorder(log, "ok2", true)));

    std::vector<Command> cmds{{"ok1", {}}, {"boom", {}}, {"ok2", {}}};
    BatchResult res = run_batch(bus, cmds, BatchMode::Atomic);

    EXPECT_FALSE(res.ok);
    ASSERT_TRUE(res.failed_index.has_value());
    EXPECT_EQ(*res.failed_index, 1u);  // 第二步（index 1）失敗
    EXPECT_EQ(res.executed, 2u);       // 只跑到失敗步為止
    EXPECT_EQ(res.total, 3u);
    EXPECT_TRUE(res.aborted());
    // ok2 未執行（原子中止）。
    EXPECT_EQ(log, (std::vector<std::string>{"ok1", "boom"}));
    ASSERT_EQ(res.steps.size(), 2u);
    EXPECT_EQ(res.steps[1].result.status, CommandStatus::Failed);
}

// Continue 模式：不論成敗跑完全部，逐步回報；failed_index 為首個失敗步。
TEST(Batch, ContinueRunsAllAndReportsFirstFailure) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("ok1", recorder(log, "ok1", true)));
    ASSERT_TRUE(bus.register_command("boom", recorder(log, "boom", false)));
    ASSERT_TRUE(bus.register_command("ok2", recorder(log, "ok2", true)));

    std::vector<Command> cmds{{"ok1", {}}, {"boom", {}}, {"ok2", {}}};
    BatchResult res = run_batch(bus, cmds, BatchMode::Continue);

    EXPECT_FALSE(res.ok);
    ASSERT_TRUE(res.failed_index.has_value());
    EXPECT_EQ(*res.failed_index, 1u);
    EXPECT_EQ(res.executed, 3u);  // 全部執行
    EXPECT_EQ(res.total, 3u);
    EXPECT_FALSE(res.aborted());
    EXPECT_EQ(log, (std::vector<std::string>{"ok1", "boom", "ok2"}));
}

// 批次遇未知命令：NotFound 亦視為失敗，如實回報（不靜默）。
TEST(Batch, UnknownCommandCountsAsFailure) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("real", recorder(log, "real", true)));

    std::vector<Command> cmds{{"real", {}}, {"ghost", {}}, {"real", {}}};
    BatchResult atomic = run_batch(bus, cmds, BatchMode::Atomic);
    EXPECT_FALSE(atomic.ok);
    ASSERT_TRUE(atomic.failed_index.has_value());
    EXPECT_EQ(*atomic.failed_index, 1u);
    EXPECT_EQ(atomic.steps[1].result.status, CommandStatus::NotFound);
    EXPECT_EQ(atomic.executed, 2u);
}

// 空批次：無步驟即無失敗（ok=true, executed=0）。
TEST(Batch, EmptyBatchSucceedsVacuously) {
    CommandBus bus;
    BatchResult res = run_batch(bus, std::vector<Command>{}, BatchMode::Atomic);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.executed, 0u);
    EXPECT_EQ(res.total, 0u);
    EXPECT_FALSE(res.failed_index.has_value());
    EXPECT_FALSE(res.aborted());
}

// 全成功批次：ok=true、無失敗步。
TEST(Batch, AllSucceed) {
    CommandBus bus;
    std::vector<std::string> log;
    ASSERT_TRUE(bus.register_command("a", recorder(log, "a", true)));
    ASSERT_TRUE(bus.register_command("b", recorder(log, "b", true)));

    Batch batch;
    batch.add("a", CommandArgs{}).add(Command{"b", {}});
    BatchResult res = run_batch(bus, batch, BatchMode::Atomic);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.executed, 2u);
    EXPECT_FALSE(res.aborted());
}

// 批次步驟參數傳遞。
TEST(Batch, StepArgsPassedThrough) {
    CommandBus bus;
    std::string got;
    ASSERT_TRUE(bus.register_command("name", [&got](const CommandArgs& a) -> CommandResult {
        if (auto v = a.get_string("who")) got = *v;
        return CommandResult::make_ok();
    }));

    Batch batch;
    batch.add("name", CommandArgs{}.set("who", CommandValue("neo")));
    BatchResult res = run_batch(bus, batch);
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(got, "neo");
}

}  // namespace
