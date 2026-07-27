// E3-01 外部命令執行致動器 — gtest 契約測試。
//
// 覆蓋：command.run 註冊到 E6-01 匯流排、dispatch 觸發執行器、參數（program/args/cwd/env）
// 傳遞正確、注入成功結果、注入失敗（非零 exit）處理、null 執行器 no-op、必填 / 無效請求
// 報錯（不崩潰、不觸及執行器）、未知命令回 NotFound、unregister、契約版本標記。
//
// 平台中立：本測試不含 sys.platform / #ifdef _WIN32 等平台分支，不真的 spawn 行程。
#include "command_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::CommandActuator;
using ds::actuators::CommandRunner;
using ds::actuators::CommandSpec;
using ds::actuators::EnvVar;
using ds::actuators::NullCommandRunner;
using ds::actuators::RunResult;
using ds::actuators::kCmdCommandRun;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_01_Register, RegistersCommandRun) {
    CommandBus bus;
    CommandActuator actuator;  // 預設綁 NullCommandRunner
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdCommandRun));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(1));
}

TEST(E3_01_Register, DuplicateRegistrationFails) {
    CommandBus bus;
    CommandActuator a1;
    CommandActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器掛同名命令：E6-01 不覆蓋 → 回 false，已註冊的仍是 a1 的。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(1));
}

TEST(E3_01_Register, NullRunnerActuatorCannotRegister) {
    CommandBus bus;
    CommandActuator actuator{std::shared_ptr<CommandRunner>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_01_Register, UnregisterRemovesCommand) {
    CommandBus bus;
    CommandActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(actuator.unregister_from(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister：已無命令，回 false。
    EXPECT_FALSE(actuator.unregister_from(bus));
}

// ---------------------------------------------------------------------------
// dispatch 觸發執行器 + 參數傳遞正確 + null 執行器記錄請求
// ---------------------------------------------------------------------------
TEST(E3_01_Dispatch, RunRecordsSpecViaBus) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("program", std::string{"grep"})
        .set("args", std::string{"-r pattern"})
        .set("cwd", std::string{"/home/user/proj"})
        .set("env", std::string{"LANG=C"});
    auto result = bus.dispatch(kCmdCommandRun, args);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    ASSERT_EQ(runner->record_count(), static_cast<std::size_t>(1));
    const CommandSpec* rec = runner->last();
    ASSERT_TRUE(rec != nullptr);
    EXPECT_EQ(rec->program, std::string{"grep"});
    ASSERT_EQ(rec->arguments.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(rec->arguments[0], std::string{"-r"});
    EXPECT_EQ(rec->arguments[1], std::string{"pattern"});
    EXPECT_EQ(rec->working_directory, std::string{"/home/user/proj"});
    ASSERT_EQ(rec->environment.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(rec->environment[0].name, std::string{"LANG"});
    EXPECT_EQ(rec->environment[0].value, std::string{"C"});
}

TEST(E3_01_Dispatch, RunProgramOnlyLeavesOptionalsEmpty) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{"ls"}));
    EXPECT_TRUE(result.ok());
    ASSERT_EQ(runner->record_count(), static_cast<std::size_t>(1));
    const CommandSpec* rec = runner->last();
    EXPECT_EQ(rec->program, std::string{"ls"});
    EXPECT_TRUE(rec->arguments.empty());
    EXPECT_TRUE(rec->working_directory.empty());
    EXPECT_TRUE(rec->environment.empty());
}

TEST(E3_01_Dispatch, MultipleRunsRecordedInOrder) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{"one"}));
    bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{"two"}));

    ASSERT_EQ(runner->record_count(), static_cast<std::size_t>(2));
    EXPECT_EQ(runner->records()[0].program, std::string{"one"});
    EXPECT_EQ(runner->records()[1].program, std::string{"two"});
}

// ---------------------------------------------------------------------------
// 注入成功結果：exit_code 0 → Ok，帶 exit_code 與 stdout
// ---------------------------------------------------------------------------
TEST(E3_01_Result, InjectedSuccessMapsToOk) {
    CommandBus bus;
    RunResult injected;
    injected.exit_code = 0;
    injected.stdout_text = "hello world\n";
    auto runner = std::make_shared<NullCommandRunner>(injected);
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{"echo"}));
    EXPECT_EQ(r.status, CommandStatus::Ok);
    ASSERT_TRUE(r.value.as_int().has_value());
    EXPECT_EQ(r.value.as_int().value(), static_cast<std::int64_t>(0));
    EXPECT_EQ(r.message, std::string{"hello world\n"});
}

// ---------------------------------------------------------------------------
// 注入失敗（非零 exit）：→ Failed，帶 exit_code 與 stderr
// ---------------------------------------------------------------------------
TEST(E3_01_Result, InjectedNonZeroExitMapsToFailed) {
    CommandBus bus;
    RunResult injected;
    injected.exit_code = 2;
    injected.stderr_text = "no such file";
    auto runner = std::make_shared<NullCommandRunner>(injected);
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{"cat"}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    ASSERT_TRUE(r.value.as_int().has_value());
    EXPECT_EQ(r.value.as_int().value(), static_cast<std::int64_t>(2));
    EXPECT_EQ(r.message, std::string{"no such file"});
    // 失敗仍是一次真正的分派 + 執行：執行器已被觸及並記錄請求。
    EXPECT_EQ(runner->record_count(), static_cast<std::size_t>(1));
}

TEST(E3_01_Result, NonZeroExitWithoutStderrHasSyntheticMessage) {
    CommandBus bus;
    RunResult injected;
    injected.exit_code = 127;  // 無 stderr
    auto runner = std::make_shared<NullCommandRunner>(injected);
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{"missing"}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_NE(r.message.find("127"), std::string::npos);
}

// ---------------------------------------------------------------------------
// null 執行器 no-op：未注入結果 → 預設 exit 0 成功，且不觸碰 OS（只記錄）
// ---------------------------------------------------------------------------
TEST(E3_01_NullRunner, DefaultIsNoOpSuccess) {
    NullCommandRunner runner;
    CommandSpec spec;
    spec.program = "anything";
    RunResult r = runner.run(spec);
    EXPECT_TRUE(r.success());
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stdout_text.empty());
    EXPECT_TRUE(r.stderr_text.empty());
    ASSERT_EQ(runner.record_count(), static_cast<std::size_t>(1));
    EXPECT_EQ(runner.last()->program, std::string{"anything"});
}

TEST(E3_01_NullRunner, SetAndClearResult) {
    NullCommandRunner runner;
    RunResult injected;
    injected.exit_code = 5;
    runner.set_result(injected);
    EXPECT_EQ(runner.run(CommandSpec{}).exit_code, 5);
    runner.clear_result();
    EXPECT_EQ(runner.run(CommandSpec{}).exit_code, 0);  // 回到 no-op 成功
}

TEST(E3_01_NullRunner, ClearResetsRecords) {
    NullCommandRunner runner;
    EXPECT_TRUE(runner.empty());
    CommandSpec spec;
    spec.program = "x";
    runner.run(spec);
    EXPECT_FALSE(runner.empty());
    EXPECT_EQ(runner.record_count(), static_cast<std::size_t>(1));
    runner.clear();
    EXPECT_TRUE(runner.empty());
    EXPECT_TRUE(runner.last() == nullptr);
}

// ---------------------------------------------------------------------------
// 無效請求：必填 program 缺漏 / 空值 / 型別不符 → Failed（不崩潰、不觸及執行器）
// ---------------------------------------------------------------------------
TEST(E3_01_Validation, MissingProgramFailsWithoutRecording) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_EQ(bus.dispatch(kCmdCommandRun, CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(runner->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_01_Validation, EmptyProgramFails) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", std::string{}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(runner->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_01_Validation, WrongTypeProgramFails) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    // program 給整數而非字串：has() 為真但 get_string 回 nullopt → Failed。
    auto r = bus.dispatch(kCmdCommandRun, CommandArgs{}.set("program", 42));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(runner->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_01_Validation, WrongTypeOptionalArgFails) {
    CommandBus bus;
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    ASSERT_TRUE(actuator.register_on(bus));

    // args 給整數：program 有效但 args 型別不符 → Failed，且不觸及執行器。
    auto r = bus.dispatch(
        kCmdCommandRun, CommandArgs{}.set("program", std::string{"ls"}).set("args", 7));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(runner->record_count(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_01_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    CommandActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("command.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// 處理器 / run_spec 可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_01_Handler, DirectHandlerCallRecordsSpec) {
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    auto r = actuator.handle_command_run(CommandArgs{}.set("program", std::string{"pwd"}));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(runner->record_count(), static_cast<std::size_t>(1));
    EXPECT_EQ(runner->last()->program, std::string{"pwd"});
}

TEST(E3_01_Handler, RunSpecExecutesPrebuiltSpec) {
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    CommandSpec spec;
    spec.program = "make";
    spec.arguments = {"-j4", "all"};
    spec.working_directory = "/build";
    spec.environment = {EnvVar{"CC", "clang"}};
    auto r = actuator.run_spec(spec);
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(runner->record_count(), static_cast<std::size_t>(1));
    EXPECT_EQ(*runner->last(), spec);
}

TEST(E3_01_Handler, RunSpecRejectsEmptyProgram) {
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    auto r = actuator.run_spec(CommandSpec{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(runner->record_count(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// env 解析：無 '=' 的字串不加入環境（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_01_Env, MalformedEnvIsIgnored) {
    auto runner = std::make_shared<NullCommandRunner>();
    CommandActuator actuator{runner};
    auto r = actuator.handle_command_run(
        CommandArgs{}.set("program", std::string{"env"}).set("env", std::string{"noequals"}));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(runner->record_count(), static_cast<std::size_t>(1));
    EXPECT_TRUE(runner->last()->environment.empty());
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_01_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::command_actuator_contract_version()},
              std::string{"e3_01/1.0.0"});
}
