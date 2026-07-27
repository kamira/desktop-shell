// E3-10 桌布設定致動器 — gtest 契約測試
//
// 涵蓋：設定桌布經 E6-01 分派、縮放模式、多顯示器 / 全部、查詢、null 後端狀態一致、
// 無效路徑 / 模式報錯。全平台中立（無 #ifdef / 真實桌布 API）。
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "wallpaper_actuator.hpp"

using ds::actuators::kAllDisplays;
using ds::actuators::kCmdWallpaperGet;
using ds::actuators::kCmdWallpaperSet;
using ds::actuators::kDefaultScaleMode;
using ds::actuators::NullWallpaperBackend;
using ds::actuators::ScaleMode;
using ds::actuators::scale_mode_from_string;
using ds::actuators::scale_mode_to_string;
using ds::actuators::WallpaperActuator;
using ds::actuators::WallpaperBackend;
using ds::actuators::WallpaperSpec;
using ds::actuators::wallpaper_contract_version;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::command::CommandValue;

namespace {

// 建構一個綁定 N 顯示器 null 後端的致動器（回傳致動器 + 後端弱觀察指標）。
std::shared_ptr<NullWallpaperBackend> make_backend(int displays) {
    return std::make_shared<NullWallpaperBackend>(displays);
}

}  // namespace

// ---------------------------------------------------------------------------
// 縮放模式字串 <-> 列舉
// ---------------------------------------------------------------------------
TEST(ScaleMode, RoundTripAllModes) {
    EXPECT_STREQ("fill", scale_mode_to_string(ScaleMode::Fill));
    EXPECT_STREQ("fit", scale_mode_to_string(ScaleMode::Fit));
    EXPECT_STREQ("stretch", scale_mode_to_string(ScaleMode::Stretch));
    EXPECT_STREQ("center", scale_mode_to_string(ScaleMode::Center));

    EXPECT_EQ(ScaleMode::Fill, scale_mode_from_string("fill").value());
    EXPECT_EQ(ScaleMode::Fit, scale_mode_from_string("fit").value());
    EXPECT_EQ(ScaleMode::Stretch, scale_mode_from_string("stretch").value());
    EXPECT_EQ(ScaleMode::Center, scale_mode_from_string("center").value());
}

TEST(ScaleMode, UnknownStringIsNullopt) {
    EXPECT_FALSE(scale_mode_from_string("").has_value());
    EXPECT_FALSE(scale_mode_from_string("cover").has_value());
    EXPECT_FALSE(scale_mode_from_string("Fill").has_value());  // 大小寫敏感
}

TEST(ScaleMode, DefaultIsFill) {
    EXPECT_EQ(ScaleMode::Fill, kDefaultScaleMode);
}

// ---------------------------------------------------------------------------
// 註冊 / 反註冊
// ---------------------------------------------------------------------------
TEST(Register, BothCommandsRegistered) {
    WallpaperActuator act(make_backend(1));
    CommandBus bus;
    EXPECT_TRUE(act.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdWallpaperSet));
    EXPECT_TRUE(bus.has_command(kCmdWallpaperGet));
    EXPECT_EQ(2u, bus.command_count());
}

TEST(Register, DuplicateRegistrationRollsBackAndKeepsExisting) {
    CommandBus bus;
    // 先由 A 佔用 wallpaper.get。
    WallpaperActuator a(make_backend(1));
    ASSERT_TRUE(bus.register_command(kCmdWallpaperGet,
                                     [](const CommandArgs&) { return CommandResult::make_ok(); }));
    // B 想掛兩命令，但 get 已被占用 → 應整體失敗並回滾（不遮蔽既有 get）。
    WallpaperActuator b(make_backend(1));
    EXPECT_FALSE(b.register_on(bus));
    EXPECT_FALSE(bus.has_command(kCmdWallpaperSet));  // set 已回滾
    EXPECT_TRUE(bus.has_command(kCmdWallpaperGet));   // 既有 get 未被覆蓋
    EXPECT_EQ(1u, bus.command_count());
}

TEST(Register, NullBackendCannotRegister) {
    WallpaperActuator act(std::shared_ptr<WallpaperBackend>{});
    CommandBus bus;
    EXPECT_FALSE(act.register_on(bus));
    EXPECT_EQ(0u, bus.command_count());
}

TEST(Register, UnregisterRemovesBothThenZero) {
    WallpaperActuator act(make_backend(1));
    CommandBus bus;
    ASSERT_TRUE(act.register_on(bus));
    EXPECT_EQ(2u, act.unregister_from(bus));
    EXPECT_EQ(0u, bus.command_count());
    EXPECT_EQ(0u, act.unregister_from(bus));  // 再呼叫回 0
}

// ---------------------------------------------------------------------------
// 設定桌布經 E6-01 分派 + null 後端一致
// ---------------------------------------------------------------------------
TEST(SetDispatch, SetViaBusUpdatesBackend) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    CommandBus bus;
    ASSERT_TRUE(act.register_on(bus));

    CommandArgs args;
    args.set("path", CommandValue{std::string("/img/a.png")});
    const CommandResult r = bus.dispatch(kCmdWallpaperSet, args);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(1, r.value.as_int().value());  // 影響 1 個顯示器

    const auto spec = backend->get(0);
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ("/img/a.png", spec->path);
    EXPECT_EQ(ScaleMode::Fill, spec->mode);  // 預設 fill
}

TEST(SetDispatch, GetViaBusReflectsPriorSet) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    CommandBus bus;
    ASSERT_TRUE(act.register_on(bus));

    CommandArgs set_args;
    set_args.set("path", CommandValue{std::string("/img/b.jpg")});
    set_args.set("mode", CommandValue{std::string("center")});
    ASSERT_TRUE(bus.dispatch(kCmdWallpaperSet, set_args).ok());

    const CommandResult g = bus.dispatch(kCmdWallpaperGet, CommandArgs{});
    ASSERT_TRUE(g.ok());
    EXPECT_EQ("/img/b.jpg", g.value.as_string().value());
    EXPECT_EQ("center", g.message);
}

// ---------------------------------------------------------------------------
// 縮放模式
// ---------------------------------------------------------------------------
TEST(ScaleModeSet, EachModeStoredAndReported) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);

    for (const char* m : {"fill", "fit", "stretch", "center"}) {
        CommandArgs args;
        args.set("path", CommandValue{std::string("/p")});
        args.set("mode", CommandValue{std::string(m)});
        const CommandResult r = act.handle_set(args);
        ASSERT_TRUE(r.ok());
        EXPECT_EQ(std::string("mode=") + m, r.message);
        EXPECT_EQ(scale_mode_from_string(m).value(), backend->get(0)->mode);
    }
}

// ---------------------------------------------------------------------------
// 多顯示器 / 全部
// ---------------------------------------------------------------------------
TEST(MultiDisplay, SetSpecificDisplayOnly) {
    auto backend = make_backend(3);
    WallpaperActuator act(backend);

    CommandArgs args;
    args.set("path", CommandValue{std::string("/only1")});
    args.set("display", CommandValue{1});
    const CommandResult r = act.handle_set(args);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(1, r.value.as_int().value());

    EXPECT_FALSE(backend->get(0).has_value());
    ASSERT_TRUE(backend->get(1).has_value());
    EXPECT_EQ("/only1", backend->get(1)->path);
    EXPECT_FALSE(backend->get(2).has_value());
    EXPECT_EQ(1u, backend->set_count());
}

TEST(MultiDisplay, AbsentDisplaySetsAll) {
    auto backend = make_backend(3);
    WallpaperActuator act(backend);

    CommandArgs args;
    args.set("path", CommandValue{std::string("/all")});
    args.set("mode", CommandValue{std::string("stretch")});
    const CommandResult r = act.handle_set(args);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(3, r.value.as_int().value());  // 三個顯示器全設

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(backend->get(i).has_value());
        EXPECT_EQ("/all", backend->get(i)->path);
        EXPECT_EQ(ScaleMode::Stretch, backend->get(i)->mode);
    }
    EXPECT_EQ(3u, backend->set_count());
}

TEST(MultiDisplay, DisplayOutOfRangeFails) {
    auto backend = make_backend(2);
    WallpaperActuator act(backend);

    CommandArgs args;
    args.set("path", CommandValue{std::string("/x")});
    args.set("display", CommandValue{2});  // 有效索引僅 0,1
    const CommandResult r = act.handle_set(args);
    EXPECT_EQ(CommandStatus::Failed, r.status);
    EXPECT_EQ(0u, backend->set_count());  // 後端狀態未變

    CommandArgs neg;
    neg.set("path", CommandValue{std::string("/x")});
    neg.set("display", CommandValue{-5});
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(neg).status);
}

// ---------------------------------------------------------------------------
// 查詢
// ---------------------------------------------------------------------------
TEST(Query, UnsetDisplayReturnsOkUnset) {
    auto backend = make_backend(2);
    WallpaperActuator act(backend);

    const CommandResult r = act.handle_get(CommandArgs{});
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value.is_null());
    EXPECT_EQ("unset", r.message);
}

TEST(Query, GetSpecificDisplay) {
    auto backend = make_backend(2);
    WallpaperActuator act(backend);

    CommandArgs set_args;
    set_args.set("path", CommandValue{std::string("/d1")});
    set_args.set("mode", CommandValue{std::string("fit")});
    set_args.set("display", CommandValue{1});
    ASSERT_TRUE(act.handle_set(set_args).ok());

    CommandArgs get_args;
    get_args.set("display", CommandValue{1});
    const CommandResult r = act.handle_get(get_args);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ("/d1", r.value.as_string().value());
    EXPECT_EQ("fit", r.message);
}

TEST(Query, GetDisplayOutOfRangeFails) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);

    CommandArgs args;
    args.set("display", CommandValue{9});
    EXPECT_EQ(CommandStatus::Failed, act.handle_get(args).status);
}

TEST(Query, CurrentSpecIntrospection) {
    auto backend = make_backend(2);
    WallpaperActuator act(backend);
    EXPECT_FALSE(act.current_spec(0).has_value());

    CommandArgs args;
    args.set("path", CommandValue{std::string("/c")});
    ASSERT_TRUE(act.handle_set(args).ok());  // 缺 display → 全部
    ASSERT_TRUE(act.current_spec(0).has_value());
    EXPECT_EQ("/c", act.current_spec(0)->path);
    EXPECT_FALSE(act.current_spec(5).has_value());  // 超範圍回 nullopt
}

// ---------------------------------------------------------------------------
// null 後端狀態一致
// ---------------------------------------------------------------------------
TEST(NullBackend, DisplayCountClampedToAtLeastOne) {
    NullWallpaperBackend b0(0);
    EXPECT_EQ(1, b0.display_count());
    NullWallpaperBackend bneg(-3);
    EXPECT_EQ(1, bneg.display_count());
    NullWallpaperBackend b4(4);
    EXPECT_EQ(4, b4.display_count());
}

TEST(NullBackend, OverwriteSameDisplayKeepsLatest) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);

    CommandArgs a1;
    a1.set("path", CommandValue{std::string("/first")});
    ASSERT_TRUE(act.handle_set(a1).ok());
    CommandArgs a2;
    a2.set("path", CommandValue{std::string("/second")});
    a2.set("mode", CommandValue{std::string("center")});
    ASSERT_TRUE(act.handle_set(a2).ok());

    ASSERT_TRUE(backend->get(0).has_value());
    EXPECT_EQ(WallpaperSpec({std::string("/second"), ScaleMode::Center}), backend->get(0).value());
    EXPECT_EQ(1u, backend->set_count());  // 覆寫，非新增
}

// ---------------------------------------------------------------------------
// 無效路徑 / 模式報錯 + 參數驗證
// ---------------------------------------------------------------------------
TEST(Validation, MissingPathFails) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    const CommandResult r = act.handle_set(CommandArgs{});
    EXPECT_EQ(CommandStatus::Failed, r.status);
    EXPECT_EQ(0u, backend->set_count());
}

TEST(Validation, EmptyPathFails) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    CommandArgs args;
    args.set("path", CommandValue{std::string("")});
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(args).status);
    EXPECT_EQ(0u, backend->set_count());
}

TEST(Validation, WrongTypePathFails) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    CommandArgs args;
    args.set("path", CommandValue{42});  // int，非 string
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(args).status);
    EXPECT_EQ(0u, backend->set_count());
}

TEST(Validation, InvalidModeFails) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    CommandArgs args;
    args.set("path", CommandValue{std::string("/p")});
    args.set("mode", CommandValue{std::string("cover")});  // 未知模式
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(args).status);
    EXPECT_EQ(0u, backend->set_count());  // 後端狀態未變
}

TEST(Validation, WrongTypeModeFails) {
    auto backend = make_backend(1);
    WallpaperActuator act(backend);
    CommandArgs args;
    args.set("path", CommandValue{std::string("/p")});
    args.set("mode", CommandValue{7});  // int，非 string
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(args).status);
}

TEST(Validation, WrongTypeDisplayFails) {
    auto backend = make_backend(2);
    WallpaperActuator act(backend);
    CommandArgs args;
    args.set("path", CommandValue{std::string("/p")});
    args.set("display", CommandValue{std::string("1")});  // string，非 int
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(args).status);
    EXPECT_EQ(0u, backend->set_count());
}

// ---------------------------------------------------------------------------
// 無後端 / 未知命令
// ---------------------------------------------------------------------------
TEST(NoBackend, HandlersFailWhenBackendNull) {
    WallpaperActuator act(std::shared_ptr<WallpaperBackend>{});
    CommandArgs args;
    args.set("path", CommandValue{std::string("/p")});
    EXPECT_EQ(CommandStatus::Failed, act.handle_set(args).status);
    EXPECT_EQ(CommandStatus::Failed, act.handle_get(CommandArgs{}).status);
    EXPECT_FALSE(act.current_spec(0).has_value());
}

TEST(UnknownCommand, BusReturnsNotFound) {
    WallpaperActuator act(make_backend(1));
    CommandBus bus;
    ASSERT_TRUE(act.register_on(bus));
    const CommandResult r = bus.dispatch("wallpaper.nope", CommandArgs{});
    EXPECT_EQ(CommandStatus::NotFound, r.status);
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(Contract, VersionString) {
    EXPECT_STREQ("e3_10/1.0.0", wallpaper_contract_version());
}
