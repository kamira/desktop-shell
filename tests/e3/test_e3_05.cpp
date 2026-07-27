// E3-05 音量設定致動器 — gtest 契約測試。
//
// 覆蓋：五個具名命令註冊到 E6-01 匯流排、設定音量經 dispatch 分派、範圍夾限（超出
// 0–100 兩端）、靜音 / 取消靜音、相對增減（含負值與夾限）、查詢目前音量、null 後端
// 狀態一致、必填參數缺漏 / 型別錯回 Failed（不崩潰）、未知命令回 NotFound、
// unregister、直接呼叫處理器、契約版本標記。
#include "volume_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::NullVolumeBackend;
using ds::actuators::VolumeActuator;
using ds::actuators::VolumeBackend;
using ds::actuators::VolumeState;
using ds::actuators::clamp_volume;
using ds::actuators::kCmdVolumeAdjust;
using ds::actuators::kCmdVolumeGet;
using ds::actuators::kCmdVolumeMute;
using ds::actuators::kCmdVolumeSet;
using ds::actuators::kCmdVolumeUnmute;
using ds::actuators::kVolumeMax;
using ds::actuators::kVolumeMin;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

namespace {

// 便捷：讀取一個成功結果的 int 回傳值（夾限後音量）。
int result_level(const ds::command::CommandResult& r) {
    EXPECT_TRUE(r.value.as_int().has_value());
    return static_cast<int>(r.value.as_int().value_or(-1));
}

}  // namespace

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_05_Register, RegistersAllFiveCommands) {
    CommandBus bus;
    VolumeActuator actuator;  // 預設綁 NullVolumeBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdVolumeSet));
    EXPECT_TRUE(bus.has_command(kCmdVolumeGet));
    EXPECT_TRUE(bus.has_command(kCmdVolumeMute));
    EXPECT_TRUE(bus.has_command(kCmdVolumeUnmute));
    EXPECT_TRUE(bus.has_command(kCmdVolumeAdjust));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(5));
}

TEST(E3_05_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    VolumeActuator a1;
    VolumeActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的五個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(5));
}

TEST(E3_05_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    VolumeActuator actuator{std::shared_ptr<VolumeBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_05_Register, UnregisterRemovesAllFive) {
    CommandBus bus;
    VolumeActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(5));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 設定音量經 E6-01 分派 + null 後端狀態一致
// ---------------------------------------------------------------------------
TEST(E3_05_Set, SetVolumeViaBusUpdatesBackend) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>();
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdVolumeSet, CommandArgs{}.set("level", 73));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    EXPECT_EQ(result_level(result), 73);
    // null 後端狀態一致：查詢應得同值。
    EXPECT_EQ(backend->get(), 73);
    EXPECT_EQ(actuator.current_state().level, 73);
}

TEST(E3_05_Set, GetReflectsPreviouslySetVolume) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>();
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdVolumeSet, CommandArgs{}.set("level", 42));
    auto got = bus.dispatch(kCmdVolumeGet);
    EXPECT_TRUE(got.ok());
    EXPECT_EQ(result_level(got), 42);
}

// ---------------------------------------------------------------------------
// 範圍夾限：超出 0–100 兩端皆夾回、不失敗
// ---------------------------------------------------------------------------
TEST(E3_05_Clamp, SetAboveMaxClampsToHundred) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>();
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdVolumeSet, CommandArgs{}.set("level", 999));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_level(r), kVolumeMax);
    EXPECT_EQ(backend->get(), kVolumeMax);
}

TEST(E3_05_Clamp, SetBelowMinClampsToZero) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>();
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdVolumeSet, CommandArgs{}.set("level", -50));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_level(r), kVolumeMin);
    EXPECT_EQ(backend->get(), kVolumeMin);
}

TEST(E3_05_Clamp, ClampHelperBoundaries) {
    EXPECT_EQ(clamp_volume(-1), 0);
    EXPECT_EQ(clamp_volume(0), 0);
    EXPECT_EQ(clamp_volume(50), 50);
    EXPECT_EQ(clamp_volume(100), 100);
    EXPECT_EQ(clamp_volume(101), 100);
}

// ---------------------------------------------------------------------------
// 靜音 / 取消靜音（不改變音量位階）
// ---------------------------------------------------------------------------
TEST(E3_05_Mute, MuteThenUnmutePreservesLevel) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>();
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdVolumeSet, CommandArgs{}.set("level", 60));

    auto muted = bus.dispatch(kCmdVolumeMute);
    EXPECT_TRUE(muted.ok());
    EXPECT_TRUE(backend->is_muted());
    EXPECT_EQ(muted.message, std::string{"muted"});
    // 靜音不改變音量位階。
    EXPECT_EQ(backend->get(), 60);
    EXPECT_EQ(result_level(muted), 60);

    auto unmuted = bus.dispatch(kCmdVolumeUnmute);
    EXPECT_TRUE(unmuted.ok());
    EXPECT_FALSE(backend->is_muted());
    EXPECT_EQ(unmuted.message, std::string{"unmuted"});
    EXPECT_EQ(backend->get(), 60);
}

TEST(E3_05_Mute, StatePairConsistency) {
    auto backend = std::make_shared<NullVolumeBackend>(30, /*muted=*/true);
    VolumeActuator actuator{backend};
    const VolumeState s = actuator.current_state();
    EXPECT_EQ(s.level, 30);
    EXPECT_TRUE(s.muted);
    EXPECT_EQ(s, (VolumeState{30, true}));
    EXPECT_NE(s, (VolumeState{30, false}));
}

// ---------------------------------------------------------------------------
// 相對增減（含負值、跨越邊界夾限）
// ---------------------------------------------------------------------------
TEST(E3_05_Adjust, AdjustUpAndDown) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>(50);
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto up = bus.dispatch(kCmdVolumeAdjust, CommandArgs{}.set("delta", 20));
    EXPECT_TRUE(up.ok());
    EXPECT_EQ(result_level(up), 70);

    auto down = bus.dispatch(kCmdVolumeAdjust, CommandArgs{}.set("delta", -25));
    EXPECT_TRUE(down.ok());
    EXPECT_EQ(result_level(down), 45);
    EXPECT_EQ(backend->get(), 45);
}

TEST(E3_05_Adjust, AdjustClampsAtBothEnds) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>(90);
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 上溢夾至 100。
    EXPECT_EQ(result_level(bus.dispatch(kCmdVolumeAdjust, CommandArgs{}.set("delta", 50))),
              kVolumeMax);
    // 從 100 大幅下調夾至 0。
    EXPECT_EQ(result_level(bus.dispatch(kCmdVolumeAdjust, CommandArgs{}.set("delta", -1000))),
              kVolumeMin);
    EXPECT_EQ(backend->get(), 0);
}

// ---------------------------------------------------------------------------
// 必填參數缺漏 / 型別錯 → Failed（不崩潰、不改後端狀態）
// ---------------------------------------------------------------------------
TEST(E3_05_Validation, SetMissingLevelFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>(55);
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdVolumeSet, CommandArgs{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    // 後端狀態不變。
    EXPECT_EQ(backend->get(), 55);
}

TEST(E3_05_Validation, SetWrongTypeFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>(55);
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // level 給字串而非整數：has() 為真但 get_int 回 nullopt → Failed。
    auto r = bus.dispatch(kCmdVolumeSet, CommandArgs{}.set("level", std::string{"loud"}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->get(), 55);
}

TEST(E3_05_Validation, AdjustMissingDeltaFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullVolumeBackend>(55);
    VolumeActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdVolumeAdjust, CommandArgs{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->get(), 55);
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_05_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    VolumeActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("volume.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// null 後端狀態一致：一連串操作後 set/get/mute 全一致
// ---------------------------------------------------------------------------
TEST(E3_05_NullBackend, SequenceRemainsConsistent) {
    NullVolumeBackend backend{50};
    EXPECT_EQ(backend.get(), 50);
    EXPECT_FALSE(backend.is_muted());

    backend.set(200);  // 建構外直接呼叫也夾限
    EXPECT_EQ(backend.get(), kVolumeMax);
    backend.set(-5);
    EXPECT_EQ(backend.get(), kVolumeMin);

    backend.set_muted(true);
    EXPECT_TRUE(backend.is_muted());
    EXPECT_EQ(backend.state(), (VolumeState{0, true}));
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_05_Handler, DirectHandlerCallSetsVolume) {
    auto backend = std::make_shared<NullVolumeBackend>();
    VolumeActuator actuator{backend};
    auto r = actuator.handle_set(CommandArgs{}.set("level", 33));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_level(r), 33);
    EXPECT_EQ(backend->get(), 33);
}

TEST(E3_05_Handler, NoBackendHandlersFail) {
    VolumeActuator actuator{std::shared_ptr<VolumeBackend>{}};
    EXPECT_EQ(actuator.handle_get(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_set(CommandArgs{}.set("level", 10)).status,
              CommandStatus::Failed);
    EXPECT_EQ(actuator.current_state(), (VolumeState{}));
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_05_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::volume_contract_version()},
              std::string{"e3_05/1.0.0"});
}
