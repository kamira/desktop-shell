// E3-02 啟動程式 / 開檔 / 網頁搜尋致動器 — gtest 契約測試。
//
// 覆蓋：三個具名命令註冊到 E6-01 匯流排、dispatch 觸發後端、具名參數傳遞（program/args/
// path/query/engine）、null 後端記錄請求、必填參數缺漏回 Failed（不崩潰）、
// 未知命令回 NotFound、unregister、契約版本標記。
#include "launch_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::LaunchActuator;
using ds::actuators::LaunchBackend;
using ds::actuators::LaunchKind;
using ds::actuators::LaunchRequest;
using ds::actuators::NullLaunchBackend;
using ds::actuators::kCmdLaunchProgram;
using ds::actuators::kCmdOpenFile;
using ds::actuators::kCmdWebSearch;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_02_Register, RegistersAllThreeCommands) {
    CommandBus bus;
    LaunchActuator actuator;  // 預設綁 NullLaunchBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdLaunchProgram));
    EXPECT_TRUE(bus.has_command(kCmdOpenFile));
    EXPECT_TRUE(bus.has_command(kCmdWebSearch));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_02_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    LaunchActuator a1;
    LaunchActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的三個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_02_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    LaunchActuator actuator{std::shared_ptr<LaunchBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_02_Register, UnregisterRemovesAllThree) {
    CommandBus bus;
    LaunchActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(3));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// dispatch 觸發後端 + 具名參數傳遞 + null 後端記錄請求
// ---------------------------------------------------------------------------
TEST(E3_02_Dispatch, LaunchProgramRecordsRequestViaBus) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("program", std::string{"notepad"}).set("args", std::string{"file.txt"});
    auto result = bus.dispatch(kCmdLaunchProgram, args);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    const LaunchRequest* rec = backend->last();
    ASSERT_TRUE(rec != nullptr);
    EXPECT_TRUE(rec->kind == LaunchKind::Program);
    EXPECT_EQ(rec->target, std::string{"notepad"});
    ASSERT_EQ(rec->arguments.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(rec->arguments[0], std::string{"file.txt"});
    // null 後端不真的啟動：回傳值帶 target 供驗證。
    ASSERT_TRUE(result.value.as_string().has_value());
    EXPECT_EQ(result.value.as_string().value(), std::string{"notepad"});
}

TEST(E3_02_Dispatch, LaunchProgramWithoutOptionalArgs) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("program", std::string{"calc"});
    auto result = bus.dispatch(kCmdLaunchProgram, args);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    EXPECT_EQ(backend->last()->target, std::string{"calc"});
    EXPECT_TRUE(backend->last()->arguments.empty());
}

TEST(E3_02_Dispatch, OpenFileRecordsRequestViaBus) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("path", std::string{"/home/user/report.pdf"});
    auto result = bus.dispatch(kCmdOpenFile, args);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    const LaunchRequest* rec = backend->last();
    EXPECT_TRUE(rec->kind == LaunchKind::File);
    EXPECT_EQ(rec->target, std::string{"/home/user/report.pdf"});
    EXPECT_TRUE(rec->arguments.empty());
}

TEST(E3_02_Dispatch, WebSearchRecordsQueryAndEngine) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("query", std::string{"c++17 variant"}).set("engine", std::string{"duckduckgo"});
    auto result = bus.dispatch(kCmdWebSearch, args);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    const LaunchRequest* rec = backend->last();
    EXPECT_TRUE(rec->kind == LaunchKind::WebSearch);
    EXPECT_EQ(rec->target, std::string{"c++17 variant"});
    EXPECT_EQ(rec->engine, std::string{"duckduckgo"});
}

TEST(E3_02_Dispatch, WebSearchWithoutEngineLeavesEngineEmpty) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("query", std::string{"weather"});
    auto result = bus.dispatch(kCmdWebSearch, args);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    EXPECT_TRUE(backend->last()->engine.empty());
}

TEST(E3_02_Dispatch, MultipleDispatchesRecordedInOrder) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdLaunchProgram, CommandArgs{}.set("program", std::string{"one"}));
    bus.dispatch(kCmdOpenFile, CommandArgs{}.set("path", std::string{"two"}));
    bus.dispatch(kCmdWebSearch, CommandArgs{}.set("query", std::string{"three"}));

    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(3));
    EXPECT_EQ(backend->records()[0].target, std::string{"one"});
    EXPECT_TRUE(backend->records()[0].kind == LaunchKind::Program);
    EXPECT_EQ(backend->records()[1].target, std::string{"two"});
    EXPECT_TRUE(backend->records()[1].kind == LaunchKind::File);
    EXPECT_EQ(backend->records()[2].target, std::string{"three"});
    EXPECT_TRUE(backend->records()[2].kind == LaunchKind::WebSearch);
}

// ---------------------------------------------------------------------------
// 必填參數缺漏 / 空值 → Failed（不崩潰、不記錄）
// ---------------------------------------------------------------------------
TEST(E3_02_Validation, MissingRequiredArgFailsWithoutRecording) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_EQ(bus.dispatch(kCmdLaunchProgram, CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(bus.dispatch(kCmdOpenFile, CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(bus.dispatch(kCmdWebSearch, CommandArgs{}).status, CommandStatus::Failed);
    // 皆未觸及後端。
    EXPECT_EQ(backend->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_02_Validation, EmptyStringArgFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdLaunchProgram, CommandArgs{}.set("program", std::string{}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_02_Validation, WrongTypeArgFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // program 給整數而非字串：has() 為真但 get_string 回 nullopt → Failed。
    auto r = bus.dispatch(kCmdLaunchProgram, CommandArgs{}.set("program", 42));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->record_count(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_02_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    LaunchActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("launch.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_02_Handler, DirectHandlerCallRecordsRequest) {
    auto backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};
    auto r = actuator.handle_open_file(CommandArgs{}.set("path", std::string{"a.txt"}));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    EXPECT_EQ(backend->last()->target, std::string{"a.txt"});
}

// ---------------------------------------------------------------------------
// NullLaunchBackend 內省輔助
// ---------------------------------------------------------------------------
TEST(E3_02_NullBackend, ClearResetsRecords) {
    NullLaunchBackend backend;
    EXPECT_TRUE(backend.empty());
    backend.perform(LaunchRequest{LaunchKind::File, "x", {}, ""});
    EXPECT_FALSE(backend.empty());
    EXPECT_EQ(backend.record_count(), static_cast<std::size_t>(1));
    backend.clear();
    EXPECT_TRUE(backend.empty());
    EXPECT_TRUE(backend.last() == nullptr);
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_02_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::contract_version()}, std::string{"e3_02/1.0.0"});
}
