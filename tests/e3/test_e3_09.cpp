// E3-09 系統旗標切換致動器 — gtest 契約測試。
//
// 覆蓋：具名旗標四動詞命令（on/off/toggle/status）註冊到 E6-01 匯流排、開 / 關 / 切換 /
// 查詢經 dispatch 分派、null 後端狀態一致、切換冪等（on/off 冪等、toggle 自逆）、
// 未註冊 / 非法旗標鍵報錯（dispatch 回 NotFound、直接呼叫回 Failed）、旗標鍵驗證、
// 多旗標互不干擾、註冊回滾 / 反註冊、直接呼叫處理器、契約版本標記。
#include "system_flag_actuator.hpp"

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::FlagVerb;
using ds::actuators::NullSystemFlagBackend;
using ds::actuators::SystemFlagActuator;
using ds::actuators::SystemFlagBackend;
using ds::actuators::flag_command_id;
using ds::actuators::is_valid_flag_key;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

namespace {

// 便捷：讀取一個成功結果的 bool 回傳值（操作後旗標狀態）。
bool result_state(const ds::command::CommandResult& r) {
    EXPECT_TRUE(r.value.as_bool().has_value());
    return r.value.as_bool().value_or(false);
}

// 便捷：登記一組具名旗標並掛上匯流排。
SystemFlagActuator make_actuator_with(std::shared_ptr<SystemFlagBackend> backend,
                                      std::initializer_list<const char*> names) {
    SystemFlagActuator actuator{std::move(backend)};
    for (const char* n : names) actuator.add_flag(n);
    return actuator;
}

}  // namespace

// ---------------------------------------------------------------------------
// 旗標鍵驗證 + 命令 id 組裝
// ---------------------------------------------------------------------------
TEST(E3_09_Key, ValidatesFlagKeys) {
    EXPECT_TRUE(is_valid_flag_key("dnd"));
    EXPECT_TRUE(is_valid_flag_key("dark_mode"));
    EXPECT_TRUE(is_valid_flag_key("night_shift"));
    EXPECT_TRUE(is_valid_flag_key("rotation_lock2"));
    EXPECT_FALSE(is_valid_flag_key(""));          // 空
    EXPECT_FALSE(is_valid_flag_key("Dark"));      // 大寫
    EXPECT_FALSE(is_valid_flag_key("dark mode")); // 空白
    EXPECT_FALSE(is_valid_flag_key("a.b"));       // 點號（命令分隔符）
    EXPECT_FALSE(is_valid_flag_key("flag-1"));    // 連字號
}

TEST(E3_09_Key, BuildsVerbCommandIds) {
    EXPECT_EQ(flag_command_id("dnd", FlagVerb::On), std::string{"flag.dnd.on"});
    EXPECT_EQ(flag_command_id("dnd", FlagVerb::Off), std::string{"flag.dnd.off"});
    EXPECT_EQ(flag_command_id("dark_mode", FlagVerb::Toggle),
              std::string{"flag.dark_mode.toggle"});
    EXPECT_EQ(flag_command_id("airplane", FlagVerb::Status),
              std::string{"flag.airplane.status"});
}

// ---------------------------------------------------------------------------
// 登記旗標 + 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_09_Register, RegistersFourCommandsPerFlag) {
    CommandBus bus;
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"dnd", "dark_mode"});
    EXPECT_EQ(actuator.flag_count(), static_cast<std::size_t>(2));
    ASSERT_TRUE(actuator.register_on(bus));
    // 兩個旗標 × 四動詞 = 8 個命令。
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(8));
    EXPECT_TRUE(bus.has_command("flag.dnd.on"));
    EXPECT_TRUE(bus.has_command("flag.dnd.off"));
    EXPECT_TRUE(bus.has_command("flag.dnd.toggle"));
    EXPECT_TRUE(bus.has_command("flag.dnd.status"));
    EXPECT_TRUE(bus.has_command("flag.dark_mode.toggle"));
}

TEST(E3_09_Register, AddFlagRejectsInvalidAndDuplicate) {
    SystemFlagActuator actuator;  // 預設 NullSystemFlagBackend
    EXPECT_TRUE(actuator.add_flag("dnd"));
    EXPECT_FALSE(actuator.add_flag("dnd"));      // 重複
    EXPECT_FALSE(actuator.add_flag("Bad Key"));  // 非法
    EXPECT_FALSE(actuator.add_flag(""));         // 空
    EXPECT_EQ(actuator.flag_count(), static_cast<std::size_t>(1));
}

TEST(E3_09_Register, NoFlagsCannotRegister) {
    CommandBus bus;
    SystemFlagActuator actuator;  // 無登記旗標
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_09_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    auto actuator = make_actuator_with(std::shared_ptr<SystemFlagBackend>{}, {"dnd"});
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_09_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    auto a1 = make_actuator_with(std::make_shared<NullSystemFlagBackend>(), {"dnd"});
    auto a2 = make_actuator_with(std::make_shared<NullSystemFlagBackend>(), {"dnd"});
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的四個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(4));
}

TEST(E3_09_Register, PartialConflictRollsBackAllOfThisActuator) {
    CommandBus bus;
    // 先占用 flag.dark_mode.toggle（a1 的其中一個命令）。
    auto a1 = make_actuator_with(std::make_shared<NullSystemFlagBackend>(), {"dark_mode"});
    ASSERT_TRUE(a1.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(4));

    // a2 登記 dnd + dark_mode：dnd 四個可掛，但 dark_mode 會撞 a1 → 整批回滾。
    auto a2 = make_actuator_with(std::make_shared<NullSystemFlagBackend>(),
                                 {"dnd", "dark_mode"});
    EXPECT_FALSE(a2.register_on(bus));
    // 只剩 a1 的四個；a2 的 dnd 命令必須被回滾（不留半掛）。
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(4));
    EXPECT_FALSE(bus.has_command("flag.dnd.on"));
}

TEST(E3_09_Register, UnregisterRemovesAll) {
    CommandBus bus;
    auto actuator = make_actuator_with(std::make_shared<NullSystemFlagBackend>(),
                                       {"dnd", "airplane"});
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(8));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 開 / 關 / 查詢經 E6-01 分派 + null 後端狀態一致
// ---------------------------------------------------------------------------
TEST(E3_09_Dispatch, OnOffViaBusUpdatesBackend) {
    CommandBus bus;
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"dnd"});
    ASSERT_TRUE(actuator.register_on(bus));

    // 初始關。
    EXPECT_FALSE(backend->get("dnd"));

    auto on = bus.dispatch("flag.dnd.on");
    EXPECT_TRUE(on.ok());
    EXPECT_EQ(on.status, CommandStatus::Ok);
    EXPECT_TRUE(result_state(on));
    EXPECT_TRUE(backend->get("dnd"));          // null 後端狀態一致
    EXPECT_TRUE(actuator.current("dnd"));

    auto off = bus.dispatch("flag.dnd.off");
    EXPECT_TRUE(off.ok());
    EXPECT_FALSE(result_state(off));
    EXPECT_FALSE(backend->get("dnd"));
}

TEST(E3_09_Dispatch, StatusIsReadOnly) {
    CommandBus bus;
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"dark_mode"});
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch("flag.dark_mode.on");
    auto s1 = bus.dispatch("flag.dark_mode.status");
    EXPECT_TRUE(s1.ok());
    EXPECT_TRUE(result_state(s1));
    // 再查一次不改變狀態。
    auto s2 = bus.dispatch("flag.dark_mode.status");
    EXPECT_TRUE(result_state(s2));
    EXPECT_TRUE(backend->get("dark_mode"));
}

TEST(E3_09_Dispatch, MessageCarriesFlagState) {
    CommandBus bus;
    auto actuator = make_actuator_with(std::make_shared<NullSystemFlagBackend>(), {"dnd"});
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("flag.dnd.on").message, std::string{"dnd=on"});
    EXPECT_EQ(bus.dispatch("flag.dnd.off").message, std::string{"dnd=off"});
}

// ---------------------------------------------------------------------------
// 切換（toggle）+ 冪等
// ---------------------------------------------------------------------------
TEST(E3_09_Toggle, ToggleFlipsAndIsSelfInverse) {
    CommandBus bus;
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"night_shift"});
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_FALSE(backend->get("night_shift"));
    auto t1 = bus.dispatch("flag.night_shift.toggle");
    EXPECT_TRUE(result_state(t1));                 // off → on
    EXPECT_TRUE(backend->get("night_shift"));
    auto t2 = bus.dispatch("flag.night_shift.toggle");
    EXPECT_FALSE(result_state(t2));                // on → off（自逆）
    EXPECT_FALSE(backend->get("night_shift"));
}

TEST(E3_09_Idempotent, OnOffAreIdempotent) {
    CommandBus bus;
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"airplane"});
    ASSERT_TRUE(actuator.register_on(bus));

    // 連兩次 on：仍為 on（冪等）。
    EXPECT_TRUE(result_state(bus.dispatch("flag.airplane.on")));
    EXPECT_TRUE(result_state(bus.dispatch("flag.airplane.on")));
    EXPECT_TRUE(backend->get("airplane"));

    // 連兩次 off：仍為 off（冪等）。
    EXPECT_FALSE(result_state(bus.dispatch("flag.airplane.off")));
    EXPECT_FALSE(result_state(bus.dispatch("flag.airplane.off")));
    EXPECT_FALSE(backend->get("airplane"));
}

// ---------------------------------------------------------------------------
// 多旗標互不干擾
// ---------------------------------------------------------------------------
TEST(E3_09_MultiFlag, FlagsAreIndependent) {
    CommandBus bus;
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"dnd", "dark_mode", "airplane"});
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch("flag.dnd.on");
    bus.dispatch("flag.airplane.on");
    // dark_mode 未動 → 仍為關。
    EXPECT_TRUE(backend->get("dnd"));
    EXPECT_FALSE(backend->get("dark_mode"));
    EXPECT_TRUE(backend->get("airplane"));

    bus.dispatch("flag.dnd.off");
    EXPECT_FALSE(backend->get("dnd"));
    EXPECT_TRUE(backend->get("airplane"));  // airplane 不受影響
}

// ---------------------------------------------------------------------------
// 未註冊 / 非法旗標鍵報錯
// ---------------------------------------------------------------------------
TEST(E3_09_Unknown, UnregisteredFlagCommandReturnsNotFound) {
    CommandBus bus;
    auto actuator = make_actuator_with(std::make_shared<NullSystemFlagBackend>(), {"dnd"});
    ASSERT_TRUE(actuator.register_on(bus));
    // 未登記旗標的命令從未掛上匯流排 → NotFound（不崩潰）。
    EXPECT_EQ(bus.dispatch("flag.wifi.on").status, CommandStatus::NotFound);
    EXPECT_EQ(bus.dispatch("flag.dnd.nonexistent").status, CommandStatus::NotFound);
}

TEST(E3_09_Unknown, DirectHandlerOnUnregisteredFlagFails) {
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"dnd"});
    // 直接呼叫處理器給未登記旗標鍵 → Failed（has_flag 保護，不崩潰、不改後端）。
    EXPECT_EQ(actuator.handle_on("wifi").status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_toggle("wifi").status, CommandStatus::Failed);
    EXPECT_FALSE(backend->has("wifi"));  // 後端未被寫入
}

TEST(E3_09_Unknown, NoBackendHandlersFail) {
    auto actuator = make_actuator_with(std::shared_ptr<SystemFlagBackend>{}, {"dnd"});
    EXPECT_EQ(actuator.handle_on("dnd").status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_status("dnd").status, CommandStatus::Failed);
    EXPECT_FALSE(actuator.current("dnd"));
}

// ---------------------------------------------------------------------------
// 直接呼叫處理器（語意等價，不經匯流排）
// ---------------------------------------------------------------------------
TEST(E3_09_Handler, DirectCallsMatchDispatch) {
    auto backend = std::make_shared<NullSystemFlagBackend>();
    auto actuator = make_actuator_with(backend, {"dnd"});

    EXPECT_TRUE(result_state(actuator.handle_on("dnd")));
    EXPECT_TRUE(backend->get("dnd"));
    EXPECT_FALSE(result_state(actuator.handle_toggle("dnd")));  // on → off
    EXPECT_FALSE(backend->get("dnd"));
    EXPECT_FALSE(result_state(actuator.handle_status("dnd")));  // 唯讀回目前（off）
}

// ---------------------------------------------------------------------------
// null 後端狀態一致：注入初值 + 直接操作
// ---------------------------------------------------------------------------
TEST(E3_09_NullBackend, InjectedInitialStateAndConsistency) {
    NullSystemFlagBackend backend{std::map<std::string, bool>{{"dnd", true}}};
    EXPECT_TRUE(backend.get("dnd"));
    EXPECT_TRUE(backend.has("dnd"));
    EXPECT_FALSE(backend.get("unset"));  // 未設定視為關
    EXPECT_FALSE(backend.has("unset"));

    backend.set("dark_mode", true);
    EXPECT_TRUE(backend.get("dark_mode"));
    EXPECT_EQ(backend.size(), static_cast<std::size_t>(2));
}

TEST(E3_09_Introspect, FlagNamesAreSorted) {
    auto actuator = make_actuator_with(std::make_shared<NullSystemFlagBackend>(),
                                       {"night_shift", "airplane", "dnd"});
    const auto names = actuator.flag_names();
    ASSERT_EQ(names.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(names[0], std::string{"airplane"});
    EXPECT_EQ(names[1], std::string{"dnd"});
    EXPECT_EQ(names[2], std::string{"night_shift"});
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_09_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::system_flag_contract_version()},
              std::string{"e3_09/1.0.0"});
}
