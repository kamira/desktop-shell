// E3-11 螢幕擷取致動器 — gtest 契約測試。
//
// 覆蓋：三個具名命令註冊到 E6-01 匯流排、dispatch 觸發後端、全螢幕 / 區域 / 視窗擷取
// 參數傳遞、null 後端回假影像、無效區域回 Failed（不崩潰、不記錄）、結果尺寸回報、
// 存檔路徑 vs 記憶體參照、未知命令回 NotFound、unregister、契約版本標記。
#include "screen_capture_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::CaptureKind;
using ds::actuators::CaptureRegion;
using ds::actuators::CaptureResult;
using ds::actuators::CaptureSpec;
using ds::actuators::NullScreenCaptureBackend;
using ds::actuators::ScreenCaptureActuator;
using ds::actuators::ScreenCaptureBackend;
using ds::actuators::kCmdCaptureFullScreen;
using ds::actuators::kCmdCaptureRegion;
using ds::actuators::kCmdCaptureWindow;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_11_Register, RegistersAllThreeCommands) {
    CommandBus bus;
    ScreenCaptureActuator actuator;  // 預設綁 NullScreenCaptureBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdCaptureFullScreen));
    EXPECT_TRUE(bus.has_command(kCmdCaptureRegion));
    EXPECT_TRUE(bus.has_command(kCmdCaptureWindow));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_11_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    ScreenCaptureActuator a1;
    ScreenCaptureActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的三個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_11_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    ScreenCaptureActuator actuator{std::shared_ptr<ScreenCaptureBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_11_Register, UnregisterRemovesAllThree) {
    CommandBus bus;
    ScreenCaptureActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(3));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 全螢幕擷取經匯流排分派 + null 後端回假影像 + 結果尺寸回報
// ---------------------------------------------------------------------------
TEST(E3_11_FullScreen, CaptureFullReturnsInjectedFakeImageViaBus) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>(2560, 1440, "fake-full");
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdCaptureFullScreen, CommandArgs{});

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    ASSERT_EQ(backend->count(), static_cast<std::size_t>(1));
    // 請求記錄：kind = FullScreen，預設 display 0。
    const CaptureSpec* spec = backend->last_spec();
    ASSERT_TRUE(spec != nullptr);
    EXPECT_TRUE(spec->kind == CaptureKind::FullScreen);
    EXPECT_EQ(spec->display, static_cast<std::int64_t>(0));
    // 結果尺寸回報：回注入的假影像尺寸與參照。
    const CaptureResult* res = backend->last_result();
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->width, static_cast<std::int64_t>(2560));
    EXPECT_EQ(res->height, static_cast<std::int64_t>(1440));
    EXPECT_EQ(res->image_ref, std::string{"fake-full"});
    EXPECT_TRUE(res->path.empty());
    // 匯流排結果 value 帶影像參照。
    ASSERT_TRUE(result.value.as_string().has_value());
    EXPECT_EQ(result.value.as_string().value(), std::string{"fake-full"});
}

TEST(E3_11_FullScreen, CaptureFullHonorsDisplayIndex) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdCaptureFullScreen, CommandArgs{}.set("display", 2));
    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->count(), static_cast<std::size_t>(1));
    EXPECT_EQ(backend->last_spec()->display, static_cast<std::int64_t>(2));
}

TEST(E3_11_FullScreen, NegativeDisplayFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdCaptureFullScreen, CommandArgs{}.set("display", -1));
    EXPECT_EQ(result.status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 區域擷取經匯流排分派 + 結果尺寸 = 請求區域
// ---------------------------------------------------------------------------
TEST(E3_11_Region, CaptureRegionPassesRectAndReportsSize) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("x", 100).set("y", 50).set("width", 800).set("height", 600);
    auto result = bus.dispatch(kCmdCaptureRegion, args);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->count(), static_cast<std::size_t>(1));
    const CaptureSpec* spec = backend->last_spec();
    EXPECT_TRUE(spec->kind == CaptureKind::Region);
    EXPECT_EQ(spec->region, (CaptureRegion{100, 50, 800, 600}));
    // 結果尺寸回報：區域擷取回請求區域寬高。
    const CaptureResult* res = backend->last_result();
    EXPECT_EQ(res->width, static_cast<std::int64_t>(800));
    EXPECT_EQ(res->height, static_cast<std::int64_t>(600));
    EXPECT_FALSE(res->image_ref.empty());
}

TEST(E3_11_Region, NegativeOriginAllowedPositiveExtentRequired) {
    // 多螢幕虛擬座標：x/y 可為負，只要 width/height 為正即有效。
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("x", -1920).set("y", -100).set("width", 640).set("height", 480);
    auto result = bus.dispatch(kCmdCaptureRegion, args);
    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->count(), static_cast<std::size_t>(1));
    EXPECT_EQ(backend->last_spec()->region, (CaptureRegion{-1920, -100, 640, 480}));
}

TEST(E3_11_Region, InvalidRegionZeroWidthFailsWithoutRecording) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("x", 0).set("y", 0).set("width", 0).set("height", 480);
    auto result = bus.dispatch(kCmdCaptureRegion, args);
    EXPECT_EQ(result.status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));  // 無效區域不觸及後端
}

TEST(E3_11_Region, InvalidRegionNegativeHeightFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("x", 0).set("y", 0).set("width", 100).set("height", -5);
    auto result = bus.dispatch(kCmdCaptureRegion, args);
    EXPECT_EQ(result.status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));
}

TEST(E3_11_Region, MissingCoordinateFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 缺 height。
    CommandArgs args;
    args.set("x", 0).set("y", 0).set("width", 100);
    EXPECT_EQ(bus.dispatch(kCmdCaptureRegion, args).status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));
}

TEST(E3_11_Region, WrongTypeCoordinateFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // width 給字串而非整數：has() 為真但 get_int 回 nullopt → Failed。
    CommandArgs args;
    args.set("x", 0).set("y", 0).set("width", std::string{"wide"}).set("height", 100);
    EXPECT_EQ(bus.dispatch(kCmdCaptureRegion, args).status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 視窗擷取經匯流排分派
// ---------------------------------------------------------------------------
TEST(E3_11_Window, CaptureWindowPassesTargetAndReturnsFake) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>(1024, 768, "fake-window");
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdCaptureWindow,
                               CommandArgs{}.set("window", std::string{"Terminal"}));

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(backend->count(), static_cast<std::size_t>(1));
    const CaptureSpec* spec = backend->last_spec();
    EXPECT_TRUE(spec->kind == CaptureKind::Window);
    EXPECT_EQ(spec->window, std::string{"Terminal"});
    const CaptureResult* res = backend->last_result();
    EXPECT_EQ(res->width, static_cast<std::int64_t>(1024));
    EXPECT_EQ(res->height, static_cast<std::int64_t>(768));
    EXPECT_EQ(res->image_ref, std::string{"fake-window"});
}

TEST(E3_11_Window, MissingWindowFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_EQ(bus.dispatch(kCmdCaptureWindow, CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));
}

TEST(E3_11_Window, EmptyWindowFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdCaptureWindow, CommandArgs{}.set("window", std::string{}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->count(), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 存檔路徑 vs 記憶體參照（輸出影像參照或存檔路徑）
// ---------------------------------------------------------------------------
TEST(E3_11_Output, SavePathProducesPathResult) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    CommandArgs args;
    args.set("x", 0).set("y", 0).set("width", 320).set("height", 240)
        .set("path", std::string{"/tmp/shot.png"});
    auto result = bus.dispatch(kCmdCaptureRegion, args);

    EXPECT_TRUE(result.ok());
    const CaptureResult* res = backend->last_result();
    ASSERT_TRUE(res != nullptr);
    EXPECT_EQ(res->path, std::string{"/tmp/shot.png"});
    EXPECT_TRUE(res->image_ref.empty());  // 存檔時不回記憶體參照
    // 匯流排結果 value 帶存檔路徑。
    ASSERT_TRUE(result.value.as_string().has_value());
    EXPECT_EQ(result.value.as_string().value(), std::string{"/tmp/shot.png"});
}

TEST(E3_11_Output, NoPathProducesImageRefResult) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>(800, 600, "ref-A");
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdCaptureFullScreen, CommandArgs{});
    EXPECT_TRUE(result.ok());
    const CaptureResult* res = backend->last_result();
    EXPECT_EQ(res->image_ref, std::string{"ref-A"});
    EXPECT_TRUE(res->path.empty());
}

// ---------------------------------------------------------------------------
// 多次分派依序記錄
// ---------------------------------------------------------------------------
TEST(E3_11_Dispatch, MultipleCapturesRecordedInOrder) {
    CommandBus bus;
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdCaptureFullScreen, CommandArgs{});
    bus.dispatch(kCmdCaptureRegion,
                 CommandArgs{}.set("x", 1).set("y", 2).set("width", 3).set("height", 4));
    bus.dispatch(kCmdCaptureWindow, CommandArgs{}.set("window", std::string{"W"}));

    ASSERT_EQ(backend->count(), static_cast<std::size_t>(3));
    EXPECT_TRUE(backend->specs()[0].kind == CaptureKind::FullScreen);
    EXPECT_TRUE(backend->specs()[1].kind == CaptureKind::Region);
    EXPECT_EQ(backend->specs()[1].region, (CaptureRegion{1, 2, 3, 4}));
    EXPECT_TRUE(backend->specs()[2].kind == CaptureKind::Window);
    EXPECT_EQ(backend->specs()[2].window, std::string{"W"});
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_11_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    ScreenCaptureActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("screen.capture.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_11_Handler, DirectHandlerCallRecordsRequest) {
    auto backend = std::make_shared<NullScreenCaptureBackend>();
    ScreenCaptureActuator actuator{backend};
    auto r = actuator.handle_capture_window(CommandArgs{}.set("window", std::string{"X"}));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(backend->count(), static_cast<std::size_t>(1));
    EXPECT_EQ(backend->last_spec()->window, std::string{"X"});
}

// ---------------------------------------------------------------------------
// NullScreenCaptureBackend 內省輔助
// ---------------------------------------------------------------------------
TEST(E3_11_NullBackend, ClearResetsRecords) {
    NullScreenCaptureBackend backend;
    EXPECT_TRUE(backend.empty());
    CaptureSpec s;
    s.kind = CaptureKind::FullScreen;
    backend.capture(s);
    EXPECT_FALSE(backend.empty());
    EXPECT_EQ(backend.count(), static_cast<std::size_t>(1));
    backend.clear();
    EXPECT_TRUE(backend.empty());
    EXPECT_TRUE(backend.last_spec() == nullptr);
    EXPECT_TRUE(backend.last_result() == nullptr);
}

TEST(E3_11_NullBackend, SetFakeImageOverridesDimensions) {
    NullScreenCaptureBackend backend;
    backend.set_fake_image(640, 360, "custom");
    CaptureSpec s;
    s.kind = CaptureKind::Window;
    s.window = "any";
    CaptureResult r = backend.capture(s);
    EXPECT_EQ(r.width, static_cast<std::int64_t>(640));
    EXPECT_EQ(r.height, static_cast<std::int64_t>(360));
    EXPECT_EQ(r.image_ref, std::string{"custom"});
}

// ---------------------------------------------------------------------------
// 契約版本標記（前綴命名，避免與 E3-02 contract_version 衝突）
// ---------------------------------------------------------------------------
TEST(E3_11_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::screen_capture_contract_version()},
              std::string{"e3_11/1.0.0"});
}
