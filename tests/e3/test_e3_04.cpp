// E3-04 電源動作致動器 — gtest 契約測試。
//
// 覆蓋：五個具名電源命令註冊到 E6-01 匯流排、各動作經 dispatch 觸發後端、null 後端記錄
// 請求且未實際動作、無效 / 未知動作回 Failed（不崩潰）、未知命令回 NotFound、結果回報、
// 重複註冊回滾、無後端拒絕註冊、unregister、字串 ↔ 動作互轉、契約版本標記。
#include "power_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::NullPowerBackend;
using ds::actuators::PowerAction;
using ds::actuators::PowerActuator;
using ds::actuators::PowerBackend;
using ds::actuators::PowerRequest;
using ds::actuators::command_id_of;
using ds::actuators::kAllPowerActions;
using ds::actuators::kCmdPowerLock;
using ds::actuators::kCmdPowerLogout;
using ds::actuators::kCmdPowerRestart;
using ds::actuators::kCmdPowerShutdown;
using ds::actuators::kCmdPowerSleep;
using ds::actuators::power_action_from_string;
using ds::actuators::to_string;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_04_Register, RegistersAllFiveCommands) {
    CommandBus bus;
    PowerActuator actuator;  // 預設綁 NullPowerBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdPowerSleep));
    EXPECT_TRUE(bus.has_command(kCmdPowerLock));
    EXPECT_TRUE(bus.has_command(kCmdPowerLogout));
    EXPECT_TRUE(bus.has_command(kCmdPowerRestart));
    EXPECT_TRUE(bus.has_command(kCmdPowerShutdown));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(5));
}

TEST(E3_04_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    PowerActuator a1;
    PowerActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的五個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(5));
}

TEST(E3_04_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    PowerActuator actuator{std::shared_ptr<PowerBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_04_Register, UnregisterRemovesAllFive) {
    CommandBus bus;
    PowerActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(5));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 各電源動作經 E6-01 分派 → 觸發後端、null 後端記錄請求且未實際動作
// ---------------------------------------------------------------------------
TEST(E3_04_Dispatch, SleepDispatchRecordsRequestViaBus) {
    CommandBus bus;
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdPowerSleep);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    const PowerRequest* rec = backend->last();
    ASSERT_TRUE(rec != nullptr);
    EXPECT_TRUE(rec->action == PowerAction::Sleep);
    // null 後端不真的休眠：回傳值帶動作名供驗證。
    ASSERT_TRUE(result.value.as_string().has_value());
    EXPECT_EQ(result.value.as_string().value(), std::string{"sleep"});
}

TEST(E3_04_Dispatch, EachActionDispatchesToBackendWithCorrectAction) {
    CommandBus bus;
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 逐一分派五個電源命令，逐一比對後端所記錄的動作。
    for (const PowerAction action : kAllPowerActions) {
        backend->clear();
        auto result = bus.dispatch(command_id_of(action));
        EXPECT_TRUE(result.ok()) << "dispatch failed for " << to_string(action);
        ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
        EXPECT_TRUE(backend->last()->action == action)
            << "recorded action mismatch for " << to_string(action);
        // 結果回報：帶對應動作字串。
        ASSERT_TRUE(result.value.as_string().has_value());
        EXPECT_EQ(result.value.as_string().value(), std::string{to_string(action)});
    }
}

TEST(E3_04_Dispatch, MultipleDispatchesRecordedInOrder) {
    CommandBus bus;
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdPowerLock);
    bus.dispatch(kCmdPowerLogout);
    bus.dispatch(kCmdPowerShutdown);

    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(3));
    EXPECT_TRUE(backend->records()[0].action == PowerAction::Lock);
    EXPECT_TRUE(backend->records()[1].action == PowerAction::Logout);
    EXPECT_TRUE(backend->records()[2].action == PowerAction::Shutdown);
}

TEST(E3_04_Dispatch, ExtraArgsAreIgnored) {
    // 電源動作不需參數：即使呼叫端塞了多餘參數，仍以動作本身分派。
    CommandBus bus;
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdPowerRestart, CommandArgs{}.set("delay", 30));
    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    EXPECT_TRUE(backend->last()->action == PowerAction::Restart);
}

// ---------------------------------------------------------------------------
// 無效動作 / 未知命令 → 結構化錯誤（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_04_Validation, UnknownActionFailsWithoutRecording) {
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    // 直接以 Unknown 動作呼叫處理器：回 Failed，不觸及後端。
    auto r = actuator.handle_action(PowerAction::Unknown);
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_04_Validation, UnknownActionStringFails) {
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    // 無法辨識的動作字串 → 解析成 Unknown → Failed，不記錄。
    auto r = actuator.handle_action(std::string{"hibernate-forever"});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->record_count(), static_cast<std::size_t>(0));
}

TEST(E3_04_Validation, NoBackendHandlerFails) {
    PowerActuator actuator{std::shared_ptr<PowerBackend>{}};
    auto r = actuator.handle_action(PowerAction::Sleep);
    EXPECT_EQ(r.status, CommandStatus::Failed);
}

TEST(E3_04_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    PowerActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("power.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_04_Handler, DirectHandlerCallRecordsRequest) {
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    auto r = actuator.handle_action(PowerAction::Shutdown);
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    EXPECT_TRUE(backend->last()->action == PowerAction::Shutdown);
}

TEST(E3_04_Handler, StringOverloadResolvesAction) {
    auto backend = std::make_shared<NullPowerBackend>();
    PowerActuator actuator{backend};
    auto r = actuator.handle_action(std::string{"logout"});
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(backend->record_count(), static_cast<std::size_t>(1));
    EXPECT_TRUE(backend->last()->action == PowerAction::Logout);
}

// ---------------------------------------------------------------------------
// NullPowerBackend 內省輔助 — 相位 1「不實際動作」保證
// ---------------------------------------------------------------------------
TEST(E3_04_NullBackend, RecordsButNeverActs) {
    NullPowerBackend backend;
    EXPECT_TRUE(backend.empty());
    auto r = backend.perform(PowerRequest{PowerAction::Shutdown});
    // 只記錄、回 Ok，訊息明示未實際動作。
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(backend.empty());
    EXPECT_EQ(backend.record_count(), static_cast<std::size_t>(1));
    EXPECT_NE(r.message.find("no real power action"), std::string::npos);
    backend.clear();
    EXPECT_TRUE(backend.empty());
    EXPECT_TRUE(backend.last() == nullptr);
}

// ---------------------------------------------------------------------------
// 動作 ↔ 字串 / 命令 id 互轉
// ---------------------------------------------------------------------------
TEST(E3_04_Mapping, StringRoundTrip) {
    for (const PowerAction action : kAllPowerActions) {
        EXPECT_TRUE(power_action_from_string(to_string(action)) == action);
    }
    EXPECT_TRUE(power_action_from_string("nope") == PowerAction::Unknown);
    EXPECT_EQ(std::string{to_string(PowerAction::Unknown)}, std::string{"unknown"});
}

TEST(E3_04_Mapping, CommandIdMapping) {
    EXPECT_EQ(std::string{command_id_of(PowerAction::Sleep)}, std::string{kCmdPowerSleep});
    EXPECT_EQ(std::string{command_id_of(PowerAction::Lock)}, std::string{kCmdPowerLock});
    EXPECT_EQ(std::string{command_id_of(PowerAction::Logout)}, std::string{kCmdPowerLogout});
    EXPECT_EQ(std::string{command_id_of(PowerAction::Restart)}, std::string{kCmdPowerRestart});
    EXPECT_EQ(std::string{command_id_of(PowerAction::Shutdown)}, std::string{kCmdPowerShutdown});
    EXPECT_EQ(std::string{command_id_of(PowerAction::Unknown)}, std::string{});
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_04_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::contract_version()}, std::string{"e3_04/1.0.0"});
}
