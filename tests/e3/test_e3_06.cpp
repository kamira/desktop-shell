// E3-06 亮度設定致動器 — gtest 契約測試。
//
// 覆蓋：三個具名命令註冊到 E6-01 匯流排、設定亮度經 dispatch 分派、範圍夾限（超出
// 0–100 兩端）、相對增減（含負值與夾限）、查詢目前亮度、多顯示器索引（互不干擾、
// 預設索引 0、選填 display 參數）、null 後端狀態一致、無效參數（缺 level / 型別錯 /
// 缺 delta / display 型別錯 / display 為負）回 Failed（不崩潰、不改後端狀態）、
// 未知命令回 NotFound、unregister、直接呼叫處理器、契約版本標記。
#include "brightness_actuator.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using ds::actuators::BrightnessActuator;
using ds::actuators::BrightnessBackend;
using ds::actuators::BrightnessState;
using ds::actuators::NullBrightnessBackend;
using ds::actuators::clamp_brightness;
using ds::actuators::kBrightnessMax;
using ds::actuators::kBrightnessMin;
using ds::actuators::kCmdBrightnessAdjust;
using ds::actuators::kCmdBrightnessGet;
using ds::actuators::kCmdBrightnessSet;
using ds::actuators::kDisplayDefault;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

namespace {

// 便捷：讀取一個成功結果的 int 回傳值（夾限後亮度）。
int result_level(const ds::command::CommandResult& r) {
    EXPECT_TRUE(r.value.as_int().has_value());
    return static_cast<int>(r.value.as_int().value_or(-1));
}

}  // namespace

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_06_Register, RegistersAllThreeCommands) {
    CommandBus bus;
    BrightnessActuator actuator;  // 預設綁 NullBrightnessBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdBrightnessSet));
    EXPECT_TRUE(bus.has_command(kCmdBrightnessGet));
    EXPECT_TRUE(bus.has_command(kCmdBrightnessAdjust));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_06_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    BrightnessActuator a1;
    BrightnessActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的三個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_06_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    BrightnessActuator actuator{std::shared_ptr<BrightnessBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_06_Register, UnregisterRemovesAllThree) {
    CommandBus bus;
    BrightnessActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(3));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 設定亮度經 E6-01 分派 + null 後端狀態一致
// ---------------------------------------------------------------------------
TEST(E3_06_Set, SetBrightnessViaBusUpdatesBackend) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>();
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 73));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    EXPECT_EQ(result_level(result), 73);
    // null 後端狀態一致：查詢預設顯示器應得同值。
    EXPECT_EQ(backend->get(kDisplayDefault), 73);
    EXPECT_EQ(actuator.current_state().level, 73);
    // 訊息帶顯示器索引供診斷。
    EXPECT_EQ(result.message, std::string{"display=0"});
}

TEST(E3_06_Set, GetReflectsPreviouslySetBrightness) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>();
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 42));
    auto got = bus.dispatch(kCmdBrightnessGet);
    EXPECT_TRUE(got.ok());
    EXPECT_EQ(result_level(got), 42);
}

TEST(E3_06_Set, DefaultBackendGetReturnsInjectedDefault) {
    // 未曾設定過的顯示器回可注入的預設亮度（此處 80）。
    auto backend = std::make_shared<NullBrightnessBackend>(80);
    BrightnessActuator actuator{backend};
    auto got = actuator.handle_get(CommandArgs{});
    EXPECT_TRUE(got.ok());
    EXPECT_EQ(result_level(got), 80);
}

// ---------------------------------------------------------------------------
// 範圍夾限：超出 0–100 兩端皆夾回、不失敗
// ---------------------------------------------------------------------------
TEST(E3_06_Clamp, SetAboveMaxClampsToHundred) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>();
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 999));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_level(r), kBrightnessMax);
    EXPECT_EQ(backend->get(kDisplayDefault), kBrightnessMax);
}

TEST(E3_06_Clamp, SetBelowMinClampsToZero) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>();
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", -50));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_level(r), kBrightnessMin);
    EXPECT_EQ(backend->get(kDisplayDefault), kBrightnessMin);
}

TEST(E3_06_Clamp, ClampHelperBoundaries) {
    EXPECT_EQ(clamp_brightness(-1), 0);
    EXPECT_EQ(clamp_brightness(0), 0);
    EXPECT_EQ(clamp_brightness(50), 50);
    EXPECT_EQ(clamp_brightness(100), 100);
    EXPECT_EQ(clamp_brightness(101), 100);
}

// ---------------------------------------------------------------------------
// 相對增減（含負值、跨越邊界夾限）
// ---------------------------------------------------------------------------
TEST(E3_06_Adjust, AdjustUpAndDown) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(50);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto up = bus.dispatch(kCmdBrightnessAdjust, CommandArgs{}.set("delta", 20));
    EXPECT_TRUE(up.ok());
    EXPECT_EQ(result_level(up), 70);

    auto down = bus.dispatch(kCmdBrightnessAdjust, CommandArgs{}.set("delta", -25));
    EXPECT_TRUE(down.ok());
    EXPECT_EQ(result_level(down), 45);
    EXPECT_EQ(backend->get(kDisplayDefault), 45);
}

TEST(E3_06_Adjust, AdjustClampsAtBothEnds) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(90);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 上溢夾至 100。
    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessAdjust, CommandArgs{}.set("delta", 50))),
              kBrightnessMax);
    // 從 100 大幅下調夾至 0。
    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessAdjust, CommandArgs{}.set("delta", -1000))),
              kBrightnessMin);
    EXPECT_EQ(backend->get(kDisplayDefault), 0);
}

// ---------------------------------------------------------------------------
// 多顯示器索引：各顯示器互不干擾、預設索引 0、選填 display 參數
// ---------------------------------------------------------------------------
TEST(E3_06_MultiDisplay, SetOnDifferentDisplaysAreIndependent) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(50);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 20).set("display", 0));
    bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 80).set("display", 1));
    bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 55).set("display", 2));

    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessGet, CommandArgs{}.set("display", 0))), 20);
    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessGet, CommandArgs{}.set("display", 1))), 80);
    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessGet, CommandArgs{}.set("display", 2))), 55);
    // 未曾設定過的顯示器回注入預設 50。
    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessGet, CommandArgs{}.set("display", 9))), 50);
}

TEST(E3_06_MultiDisplay, OmittedDisplayDefaultsToZero) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(50);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 不帶 display → 預設 0。以顯式 display=0 查詢應得同值。
    bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", 33));
    EXPECT_EQ(backend->get(0), 33);
    EXPECT_EQ(result_level(bus.dispatch(kCmdBrightnessGet, CommandArgs{}.set("display", 0))), 33);
    // display 1 未動，仍為預設。
    EXPECT_EQ(backend->get(1), 50);
}

TEST(E3_06_MultiDisplay, AdjustIsPerDisplay) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(40);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 只調 display 1；display 0 不受影響。
    auto r = bus.dispatch(kCmdBrightnessAdjust, CommandArgs{}.set("delta", 15).set("display", 1));
    EXPECT_EQ(result_level(r), 55);
    EXPECT_EQ(backend->get(1), 55);
    EXPECT_EQ(backend->get(0), 40);

    // 訊息帶正確顯示器索引。
    EXPECT_EQ(r.message, std::string{"display=1"});
}

TEST(E3_06_MultiDisplay, KnownDisplaysIntrospection) {
    NullBrightnessBackend backend{50};
    EXPECT_TRUE(backend.known_displays().empty());
    backend.set(2, 30);
    backend.set(0, 70);
    // std::map 有序 → {0, 2}。
    EXPECT_EQ(backend.known_displays(), (std::vector<int>{0, 2}));
    EXPECT_EQ(backend.state(0), (BrightnessState{0, 70}));
    EXPECT_EQ(backend.state(2), (BrightnessState{2, 30}));
    // 未設定過的仍回預設。
    EXPECT_EQ(backend.get(5), 50);
}

// ---------------------------------------------------------------------------
// 無效參數 → Failed（不崩潰、不改後端狀態）
// ---------------------------------------------------------------------------
TEST(E3_06_Validation, SetMissingLevelFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(55);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdBrightnessSet, CommandArgs{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->get(kDisplayDefault), 55);  // 後端狀態不變（回預設）
}

TEST(E3_06_Validation, SetWrongTypeLevelFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(55);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // level 給字串而非整數：has() 為真但 get_int 回 nullopt → Failed。
    auto r = bus.dispatch(kCmdBrightnessSet, CommandArgs{}.set("level", std::string{"bright"}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->get(kDisplayDefault), 55);
}

TEST(E3_06_Validation, AdjustMissingDeltaFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(55);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdBrightnessAdjust, CommandArgs{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->get(kDisplayDefault), 55);
}

TEST(E3_06_Validation, WrongTypeDisplayFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(55);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // display 給字串 → Failed，且不改後端。
    auto r = bus.dispatch(kCmdBrightnessSet,
                          CommandArgs{}.set("level", 10).set("display", std::string{"main"}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->get(0), 55);
}

TEST(E3_06_Validation, NegativeDisplayFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullBrightnessBackend>(55);
    BrightnessActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // display 為負 → Failed。set / get / adjust 皆然。
    EXPECT_EQ(bus.dispatch(kCmdBrightnessSet,
                           CommandArgs{}.set("level", 10).set("display", -1)).status,
              CommandStatus::Failed);
    EXPECT_EQ(bus.dispatch(kCmdBrightnessGet, CommandArgs{}.set("display", -3)).status,
              CommandStatus::Failed);
    EXPECT_EQ(bus.dispatch(kCmdBrightnessAdjust,
                           CommandArgs{}.set("delta", 5).set("display", -1)).status,
              CommandStatus::Failed);
    // 後端未受影響。
    EXPECT_EQ(backend->get(0), 55);
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_06_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    BrightnessActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("brightness.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// null 後端狀態一致：一連串多顯示器操作後 set/get 全一致
// ---------------------------------------------------------------------------
TEST(E3_06_NullBackend, SequenceRemainsConsistent) {
    NullBrightnessBackend backend{50};
    EXPECT_EQ(backend.get(0), 50);
    EXPECT_EQ(backend.default_level(), 50);

    backend.set(0, 200);  // 建構外直接呼叫也夾限
    EXPECT_EQ(backend.get(0), kBrightnessMax);
    backend.set(0, -5);
    EXPECT_EQ(backend.get(0), kBrightnessMin);

    backend.set(1, 60);
    EXPECT_EQ(backend.state(1), (BrightnessState{1, 60}));
    EXPECT_EQ(backend.state(0), (BrightnessState{0, 0}));
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_06_Handler, DirectHandlerCallSetsBrightness) {
    auto backend = std::make_shared<NullBrightnessBackend>();
    BrightnessActuator actuator{backend};
    auto r = actuator.handle_set(CommandArgs{}.set("level", 33).set("display", 1));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_level(r), 33);
    EXPECT_EQ(backend->get(1), 33);
    EXPECT_EQ(actuator.current_state(1), (BrightnessState{1, 33}));
}

TEST(E3_06_Handler, NoBackendHandlersFail) {
    BrightnessActuator actuator{std::shared_ptr<BrightnessBackend>{}};
    EXPECT_EQ(actuator.handle_get(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_set(CommandArgs{}.set("level", 10)).status,
              CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_adjust(CommandArgs{}.set("delta", 5)).status,
              CommandStatus::Failed);
    EXPECT_EQ(actuator.current_state(), (BrightnessState{}));
}

TEST(E3_06_Handler, CurrentStateNegativeDisplayReturnsDefault) {
    auto backend = std::make_shared<NullBrightnessBackend>(70);
    BrightnessActuator actuator{backend};
    // 負索引不觸碰後端，回預設 BrightnessState{}。
    EXPECT_EQ(actuator.current_state(-1), (BrightnessState{}));
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_06_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::brightness_contract_version()},
              std::string{"e3_06/1.0.0"});
}
