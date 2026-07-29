// E8-03 表現控制指令集 — 契約 / 行為測試（gtest）。
//
// 涵蓋：show / hide / switch_surface / transition / wait 五個表現指令、switch_surface
// 經 E4-06 SurfaceSwitcher 實際切換（含未知 surface）、所有指令一律經 E6-01 CommandBus
// 具名分派、與 E8-02 Interpreter/OutputSink 整合（say 步驟編排表現指令）、未知指令報錯
// （不靜默）、無效參數（缺參數 / 空字串 / 非有限或負數秒數 / 非數值文字）、註冊生命週期
// （ready() / 解構後 unregister）。
#include "presentation_commands.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::render::SurfaceSwitcher;
using ds::render::SwitchStatus;
using ds::script::ExecutionContext;
using ds::script::Interpreter;
using ds::script::PresentationController;
using ds::script::PresentationSink;
using ds::script::RunResult;
using ds::script::Script;
using ds::script::Step;
using ds::script::TransitionRecord;
using ds::script::WaitRecord;

namespace commands = ds::script::presentation_commands;

// -----------------------------------------------------------------------------
// 註冊 / 生命週期
// -----------------------------------------------------------------------------

TEST(Registration, ControllerRegistersAllFiveCommandsAndIsReady) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    EXPECT_TRUE(controller.ready());
    EXPECT_TRUE(controller.registration_error().empty());
    EXPECT_TRUE(bus.has_command(commands::kShow));
    EXPECT_TRUE(bus.has_command(commands::kHide));
    EXPECT_TRUE(bus.has_command(commands::kSwitchSurface));
    EXPECT_TRUE(bus.has_command(commands::kTransition));
    EXPECT_TRUE(bus.has_command(commands::kWait));
}

TEST(Registration, SecondControllerOnSameBusFailsReadyWithoutCrashing) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController first(surfaces, bus);
    ASSERT_TRUE(first.ready());

    PresentationController second(surfaces, bus);  // 撞名：show 等 id 已被 first 佔用
    EXPECT_FALSE(second.ready());
    EXPECT_FALSE(second.registration_error().empty());
}

TEST(Registration, DestructorUnregistersOwnCommandsAllowingReuse) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    {
        PresentationController controller(surfaces, bus);
        ASSERT_TRUE(controller.ready());
    }  // controller 解構：應 unregister 五個 id

    EXPECT_FALSE(bus.has_command(commands::kShow));
    PresentationController second(surfaces, bus);
    EXPECT_TRUE(second.ready());  // 可乾淨地重新註冊
}

// -----------------------------------------------------------------------------
// 各表現指令：show / hide（經 E6-01 分派）
// -----------------------------------------------------------------------------

TEST(Show, DispatchedThroughBusMarksTargetVisible) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kShow, CommandArgs{}.set("target", "hero"));
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(controller.is_visible("hero"));
    EXPECT_EQ(controller.visible_count(), 1u);
}

TEST(Hide, DispatchedThroughBusMarksTargetHidden) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    ASSERT_TRUE(bus.dispatch(commands::kShow, CommandArgs{}.set("target", "hero")).ok());
    ASSERT_TRUE(controller.is_visible("hero"));

    CommandResult r = bus.dispatch(commands::kHide, CommandArgs{}.set("target", "hero"));
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(controller.is_visible("hero"));
    EXPECT_EQ(controller.visible_count(), 0u);
}

TEST(Show, NeverShownTargetIsNotVisible) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    EXPECT_FALSE(controller.is_visible("ghost"));
}

// -----------------------------------------------------------------------------
// switch_surface —— 經 E4-06 SurfaceSwitcher 實際切換
// -----------------------------------------------------------------------------

TEST(SwitchSurface, DispatchDelegatesToSurfaceSwitcherAndUpdatesCurrent) {
    SurfaceSwitcher surfaces;
    ASSERT_EQ(surfaces.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(surfaces.register_surface("page.settings"), SwitchStatus::Ok);
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kSwitchSurface,
                                    CommandArgs{}.set("target", "page.home"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(surfaces.current(), "page.home");
    EXPECT_TRUE(surfaces.has_current());

    r = bus.dispatch(commands::kSwitchSurface, CommandArgs{}.set("target", "page.settings"));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(surfaces.current(), "page.settings");
    EXPECT_EQ(&controller.surfaces(), &surfaces);
}

TEST(SwitchSurface, UnknownSurfaceReportsFailedAndDoesNotChangeCurrent) {
    SurfaceSwitcher surfaces;
    ASSERT_EQ(surfaces.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(surfaces.switch_to("page.home"), SwitchStatus::Ok);
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    CommandResult r = bus.dispatch(commands::kSwitchSurface,
                                    CommandArgs{}.set("target", "page.ghost"));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(surfaces.current(), "page.home");  // 未變更
}

TEST(SwitchSurface, EmptyTargetIsInvalidAndFailed) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    CommandResult r = bus.dispatch(commands::kSwitchSurface, CommandArgs{}.set("target", ""));
    EXPECT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// transition
// -----------------------------------------------------------------------------

TEST(Transition, ValidTransitionIsRecorded) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kTransition, CommandArgs{}
                                                                .set("kind", "fade")
                                                                .set("from", "sceneA")
                                                                .set("to", "sceneB")
                                                                .set("duration", 0.5));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(controller.transitions().size(), 1u);
    const TransitionRecord& rec = controller.transitions()[0];
    EXPECT_EQ(rec.kind, "fade");
    EXPECT_EQ(rec.from, "sceneA");
    EXPECT_EQ(rec.to, "sceneB");
    EXPECT_DOUBLE_EQ(rec.duration_seconds, 0.5);
}

TEST(Transition, AcceptsIntegerDurationToo) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kTransition, CommandArgs{}
                                                                .set("kind", "cut")
                                                                .set("from", "a")
                                                                .set("to", "b")
                                                                .set("duration", 2));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(controller.transitions().size(), 1u);
    EXPECT_DOUBLE_EQ(controller.transitions()[0].duration_seconds, 2.0);
}

// -----------------------------------------------------------------------------
// wait
// -----------------------------------------------------------------------------

TEST(Wait, ValidWaitIsRecorded) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kWait, CommandArgs{}.set("seconds", 1.5));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(controller.waits().size(), 1u);
    EXPECT_DOUBLE_EQ(controller.waits()[0].seconds, 1.5);
}

TEST(Wait, ZeroSecondsIsValid) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kWait, CommandArgs{}.set("seconds", 0.0));
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(controller.waits().size(), 1u);
}

// -----------------------------------------------------------------------------
// 無效參數（不套用、不部分套用，回 Failed）
// -----------------------------------------------------------------------------

TEST(InvalidArgs, ShowMissingTargetIsFailed) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    CommandResult r = bus.dispatch(commands::kShow, CommandArgs{});
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, CommandStatus::Failed);
}

TEST(InvalidArgs, HideEmptyTargetIsFailed) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    CommandResult r = bus.dispatch(commands::kHide, CommandArgs{}.set("target", ""));
    EXPECT_FALSE(r.ok());
}

TEST(InvalidArgs, TransitionMissingFieldsIsFailedAndNotRecorded) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kTransition,
                                    CommandArgs{}.set("kind", "fade").set("from", "a"));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(controller.transitions().empty());  // 不部分套用
}

TEST(InvalidArgs, TransitionNegativeDurationIsFailed) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kTransition, CommandArgs{}
                                                                .set("kind", "fade")
                                                                .set("from", "a")
                                                                .set("to", "b")
                                                                .set("duration", -1.0));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(controller.transitions().empty());
}

TEST(InvalidArgs, WaitNegativeSecondsIsFailed) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    CommandResult r = bus.dispatch(commands::kWait, CommandArgs{}.set("seconds", -0.1));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(controller.waits().empty());
}

TEST(InvalidArgs, WaitStringInsteadOfNumberIsFailed) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    // "seconds" 帶字串而非數值型別 -> numeric_arg 視為缺參數 -> Invalid（不特判，型別即契約）。
    CommandResult r = bus.dispatch(commands::kWait, CommandArgs{}.set("seconds", "soon"));
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(controller.waits().empty());
}

// -----------------------------------------------------------------------------
// 未知指令（經 CommandBus 既有契約回 NotFound，不靜默）
// -----------------------------------------------------------------------------

TEST(UnknownCommand, DirectDispatchOfUnregisteredIdReportsNotFound) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    CommandResult r = bus.dispatch("ds::script.teleport", CommandArgs{});
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, CommandStatus::NotFound);
}

// -----------------------------------------------------------------------------
// 與 E8-02 整合：PresentationSink 作為 Interpreter 的 OutputSink
// -----------------------------------------------------------------------------

TEST(E8_02_Integration, SayStepsDriveShowAndSwitchSurfaceThroughSink) {
    SurfaceSwitcher surfaces;
    ASSERT_EQ(surfaces.register_surface("main"), SwitchStatus::Ok);
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    Script script;
    script.add(Step::say("show hero"))
        .add(Step::say("switch_surface main"))
        .add(Step::say("wait 0.25"));

    PresentationSink sink(bus);
    Interpreter interp(script, sink);
    RunResult r = interp.run();

    ASSERT_TRUE(r.ok());
    ASSERT_EQ(sink.count(), 3u);
    EXPECT_TRUE(sink.all_ok());
    EXPECT_EQ(sink.failure_count(), 0u);

    EXPECT_TRUE(controller.is_visible("hero"));
    EXPECT_EQ(surfaces.current(), "main");
    ASSERT_EQ(controller.waits().size(), 1u);
    EXPECT_DOUBLE_EQ(controller.waits()[0].seconds, 0.25);
}

TEST(E8_02_Integration, TransitionLineParsedFromScriptText) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    Script script;
    script.add(Step::say("transition fade sceneA sceneB 0.5"));

    PresentationSink sink(bus);
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());

    EXPECT_TRUE(sink.all_ok());
    ASSERT_EQ(controller.transitions().size(), 1u);
    EXPECT_EQ(controller.transitions()[0].kind, "fade");
    EXPECT_EQ(controller.transitions()[0].from, "sceneA");
    EXPECT_EQ(controller.transitions()[0].to, "sceneB");
    EXPECT_DOUBLE_EQ(controller.transitions()[0].duration_seconds, 0.5);
}

TEST(E8_02_Integration, UnknownInstructionLineIsNotSilentlyDropped) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    Script script;
    script.add(Step::say("teleport nowhere"));

    PresentationSink sink(bus);
    Interpreter interp(script, sink);
    // 直譯器本身仍成功跑完（say 只是 emit 文字給 sink；表現層的失敗不會讓 E8-02 本身失敗）——
    // 但 sink 忠實記錄了那一行分派失敗，供呼叫端檢查，並非靜默吞掉。
    ASSERT_TRUE(interp.run().ok());
    ASSERT_EQ(sink.count(), 1u);
    EXPECT_FALSE(sink.all_ok());
    EXPECT_EQ(sink.failure_count(), 1u);
    EXPECT_EQ(sink.results()[0].status, CommandStatus::NotFound);
}

TEST(E8_02_Integration, InvalidParamLineViaScriptTextIsFailedNotSilent) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);

    Script script;
    script.add(Step::say("show"));  // 缺 target

    PresentationSink sink(bus);
    Interpreter interp(script, sink);
    ASSERT_TRUE(interp.run().ok());

    ASSERT_EQ(sink.count(), 1u);
    EXPECT_FALSE(sink.all_ok());
    EXPECT_EQ(sink.results()[0].status, CommandStatus::Failed);
    EXPECT_FALSE(controller.is_visible(""));
    EXPECT_EQ(controller.visible_count(), 0u);
}

TEST(PresentationSinkUtility, ClearResetsResults) {
    SurfaceSwitcher surfaces;
    CommandBus bus;
    PresentationController controller(surfaces, bus);
    (void)controller;

    PresentationSink sink(bus);
    sink.emit("show hero");
    ASSERT_EQ(sink.count(), 1u);
    sink.clear();
    EXPECT_EQ(sink.count(), 0u);
    EXPECT_TRUE(sink.all_ok());  // 空歷史視為 true
}
