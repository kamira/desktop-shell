// E3-08 網路 / 藍牙開關致動器 — gtest 契約測試。
//
// 覆蓋：八個具名命令註冊到 E6-01 匯流排、Wi-Fi / 藍牙 開 / 關 / 切換 / 查詢經 dispatch
// 分派、null 後端狀態一致、切換冪等性（on/off 冪等、toggle 翻轉）、無效 radio 報錯、
// 無後端各處理器回 Failed、未知命令回 NotFound、unregister、直接呼叫處理器、
// 兩無線電互不干擾、契約版本標記。
#include "radio_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::NullRadioBackend;
using ds::actuators::Radio;
using ds::actuators::RadioActuator;
using ds::actuators::RadioBackend;
using ds::actuators::RadioOp;
using ds::actuators::RadioState;
using ds::actuators::is_valid_radio;
using ds::actuators::kCmdBluetoothOff;
using ds::actuators::kCmdBluetoothOn;
using ds::actuators::kCmdBluetoothStatus;
using ds::actuators::kCmdBluetoothToggle;
using ds::actuators::kCmdWifiOff;
using ds::actuators::kCmdWifiOn;
using ds::actuators::kCmdWifiStatus;
using ds::actuators::kCmdWifiToggle;
using ds::actuators::radio_name;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

namespace {

// 便捷：讀取一個成功結果的 bool 回傳值（操作後啟用狀態）。
bool result_enabled(const ds::command::CommandResult& r) {
    EXPECT_TRUE(r.value.as_bool().has_value());
    return r.value.as_bool().value_or(false);
}

}  // namespace

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_08_Register, RegistersAllEightCommands) {
    CommandBus bus;
    RadioActuator actuator;  // 預設綁 NullRadioBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdWifiOn));
    EXPECT_TRUE(bus.has_command(kCmdWifiOff));
    EXPECT_TRUE(bus.has_command(kCmdWifiToggle));
    EXPECT_TRUE(bus.has_command(kCmdWifiStatus));
    EXPECT_TRUE(bus.has_command(kCmdBluetoothOn));
    EXPECT_TRUE(bus.has_command(kCmdBluetoothOff));
    EXPECT_TRUE(bus.has_command(kCmdBluetoothToggle));
    EXPECT_TRUE(bus.has_command(kCmdBluetoothStatus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(8));
}

TEST(E3_08_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    RadioActuator a1;
    RadioActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的八個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(8));
}

TEST(E3_08_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    RadioActuator actuator{std::shared_ptr<RadioBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_08_Register, UnregisterRemovesAllEight) {
    CommandBus bus;
    RadioActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(8));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// Wi-Fi 開 / 關 / 查詢經 E6-01 分派 + null 後端狀態一致
// ---------------------------------------------------------------------------
TEST(E3_08_Wifi, OnViaBusEnablesBackend) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>();  // 預設皆關
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdWifiOn);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    EXPECT_TRUE(result_enabled(result));
    EXPECT_EQ(result.message, std::string{"wifi on"});
    // null 後端狀態一致。
    EXPECT_TRUE(backend->is_enabled(Radio::Wifi));
    EXPECT_TRUE(actuator.current_state().wifi);
}

TEST(E3_08_Wifi, OffViaBusDisablesBackend) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>(true, true);  // 皆開
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdWifiOff);
    EXPECT_TRUE(result.ok());
    EXPECT_FALSE(result_enabled(result));
    EXPECT_EQ(result.message, std::string{"wifi off"});
    EXPECT_FALSE(backend->is_enabled(Radio::Wifi));
    // 藍牙不受影響。
    EXPECT_TRUE(backend->is_enabled(Radio::Bluetooth));
}

TEST(E3_08_Wifi, StatusReflectsStateWithoutMutating) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>(true, false);
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto got = bus.dispatch(kCmdWifiStatus);
    EXPECT_TRUE(got.ok());
    EXPECT_TRUE(result_enabled(got));
    // 查詢不改狀態。
    EXPECT_TRUE(backend->is_enabled(Radio::Wifi));
    EXPECT_EQ(actuator.current_state(), (RadioState{true, false}));
}

// ---------------------------------------------------------------------------
// 藍牙 開 / 關 / 查詢經 E6-01 分派
// ---------------------------------------------------------------------------
TEST(E3_08_Bluetooth, OnOffViaBus) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>();
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto on = bus.dispatch(kCmdBluetoothOn);
    EXPECT_TRUE(on.ok());
    EXPECT_TRUE(result_enabled(on));
    EXPECT_EQ(on.message, std::string{"bluetooth on"});
    EXPECT_TRUE(backend->is_enabled(Radio::Bluetooth));
    // Wi-Fi 不受影響。
    EXPECT_FALSE(backend->is_enabled(Radio::Wifi));

    auto off = bus.dispatch(kCmdBluetoothOff);
    EXPECT_TRUE(off.ok());
    EXPECT_FALSE(result_enabled(off));
    EXPECT_FALSE(backend->is_enabled(Radio::Bluetooth));
}

TEST(E3_08_Bluetooth, StatusQuery) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>(false, true);
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto got = bus.dispatch(kCmdBluetoothStatus);
    EXPECT_TRUE(got.ok());
    EXPECT_TRUE(result_enabled(got));
    EXPECT_EQ(got.message, std::string{"bluetooth on"});
}

// ---------------------------------------------------------------------------
// 切換冪等性：on/off 冪等、toggle 翻轉
// ---------------------------------------------------------------------------
TEST(E3_08_Idempotency, OnIsIdempotent) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>();
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_TRUE(result_enabled(bus.dispatch(kCmdWifiOn)));
    // 再開一次仍為開（冪等）。
    EXPECT_TRUE(result_enabled(bus.dispatch(kCmdWifiOn)));
    EXPECT_TRUE(backend->is_enabled(Radio::Wifi));
}

TEST(E3_08_Idempotency, OffIsIdempotent) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>(true, false);
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_FALSE(result_enabled(bus.dispatch(kCmdWifiOff)));
    // 再關一次仍為關（冪等）。
    EXPECT_FALSE(result_enabled(bus.dispatch(kCmdWifiOff)));
    EXPECT_FALSE(backend->is_enabled(Radio::Wifi));
}

TEST(E3_08_Idempotency, ToggleFlipsEachTime) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>();  // Wi-Fi 起始關
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 關 → 開 → 關 → 開。
    EXPECT_TRUE(result_enabled(bus.dispatch(kCmdWifiToggle)));
    EXPECT_TRUE(backend->is_enabled(Radio::Wifi));
    EXPECT_FALSE(result_enabled(bus.dispatch(kCmdWifiToggle)));
    EXPECT_FALSE(backend->is_enabled(Radio::Wifi));
    EXPECT_TRUE(result_enabled(bus.dispatch(kCmdWifiToggle)));
    EXPECT_FALSE(result_enabled(bus.dispatch(kCmdWifiToggle)));
}

TEST(E3_08_Idempotency, BluetoothToggleIndependentOfWifi) {
    CommandBus bus;
    auto backend = std::make_shared<NullRadioBackend>();
    RadioActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdBluetoothToggle);  // 藍牙 → 開
    EXPECT_TRUE(backend->is_enabled(Radio::Bluetooth));
    EXPECT_FALSE(backend->is_enabled(Radio::Wifi));  // Wi-Fi 不受影響
    EXPECT_EQ(actuator.current_state(), (RadioState{false, true}));
}

// ---------------------------------------------------------------------------
// 無效 radio → Failed（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_08_InvalidRadio, HandleRejectsOutOfRangeRadio) {
    auto backend = std::make_shared<NullRadioBackend>();
    RadioActuator actuator{backend};
    // 以越界列舉值構造無效 radio。
    const Radio bogus = static_cast<Radio>(99);
    EXPECT_FALSE(is_valid_radio(bogus));
    EXPECT_EQ(radio_name(bogus), nullptr);

    auto r = actuator.handle(bogus, RadioOp::On, CommandArgs{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    // 後端狀態不變（兩無線電仍關）。
    EXPECT_EQ(actuator.current_state(), (RadioState{false, false}));
}

TEST(E3_08_InvalidRadio, ValidRadiosAcceptedByHelpers) {
    EXPECT_TRUE(is_valid_radio(Radio::Wifi));
    EXPECT_TRUE(is_valid_radio(Radio::Bluetooth));
    EXPECT_EQ(std::string{radio_name(Radio::Wifi)}, std::string{"wifi"});
    EXPECT_EQ(std::string{radio_name(Radio::Bluetooth)}, std::string{"bluetooth"});
}

// ---------------------------------------------------------------------------
// 無後端 → 各處理器回 Failed（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_08_NoBackend, HandlersFailWithoutBackend) {
    RadioActuator actuator{std::shared_ptr<RadioBackend>{}};
    EXPECT_EQ(actuator.handle(Radio::Wifi, RadioOp::On, CommandArgs{}).status,
              CommandStatus::Failed);
    EXPECT_EQ(actuator.handle(Radio::Bluetooth, RadioOp::Status, CommandArgs{}).status,
              CommandStatus::Failed);
    EXPECT_EQ(actuator.current_state(), (RadioState{}));
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_08_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    RadioActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("net.wifi.nonexistent").status, CommandStatus::NotFound);
    EXPECT_EQ(bus.dispatch("net.cellular.on").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// null 後端狀態一致：一連串操作後 set/get/toggle 全一致
// ---------------------------------------------------------------------------
TEST(E3_08_NullBackend, SequenceRemainsConsistent) {
    NullRadioBackend backend;
    EXPECT_FALSE(backend.is_enabled(Radio::Wifi));
    EXPECT_FALSE(backend.is_enabled(Radio::Bluetooth));

    backend.set_enabled(Radio::Wifi, true);
    EXPECT_TRUE(backend.is_enabled(Radio::Wifi));
    EXPECT_EQ(backend.state(), (RadioState{true, false}));

    backend.set_enabled(Radio::Bluetooth, true);
    EXPECT_EQ(backend.state(), (RadioState{true, true}));

    backend.set_enabled(Radio::Wifi, false);
    EXPECT_EQ(backend.state(), (RadioState{false, true}));
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_08_Handler, DirectHandlerCallTogglesRadio) {
    auto backend = std::make_shared<NullRadioBackend>();
    RadioActuator actuator{backend};
    auto r = actuator.handle(Radio::Bluetooth, RadioOp::On, CommandArgs{});
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(result_enabled(r));
    EXPECT_TRUE(backend->is_enabled(Radio::Bluetooth));
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_08_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::radio_toggle_contract_version()},
              std::string{"e3_08/1.0.0"});
}
