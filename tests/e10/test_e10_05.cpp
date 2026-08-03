// E10-05 獨立行程 widget 宿主 — gtest 契約測試。
//
// 覆蓋：spawn widget、生命週期（啟動 / 停止 / 存活）、崩潰偵測 + on_crash 回呼、重啟、
// 經 E10-01 通道通訊（宿主事件廣播）、null launcher 行為、無效 spec 明確報錯（含 surface
// 橋接的能力閘控 / 未注入服務 / id 衝突）、契約版本標記。
#include "widget_host.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

using ds::ipc::Message;
using ds::ipc::MessageChannel;
using ds::ipc::NullProcessLauncher;
using ds::ipc::ProcessState;
using ds::ipc::WidgetHost;
using ds::ipc::WidgetHostStatus;
using ds::ipc::WidgetSpec;
using ds::ipc::kWidgetCrashedType;
using ds::ipc::kWidgetStartedType;
using ds::ipc::kWidgetStoppedType;
using ds::ipc::kWidgetSurfaceAttachedType;
using ds::ipc::kWidgetSurfaceDetachedType;

using ds::kernel::AlphaMode;
using ds::kernel::AlphaProfile;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::NullKernelBackend;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::alpha_incapable_matrix;

namespace {

WidgetSpec make_spec(std::string id = "widget.clock", std::string entry = "widget.clock.main") {
    WidgetSpec spec;
    spec.id = std::move(id);
    spec.entry = std::move(entry);
    return spec;
}

}  // namespace

// ---------------------------------------------------------------------------
// NullProcessLauncher — 相位 1 記憶體模擬行程表
// ---------------------------------------------------------------------------

TEST(NullProcessLauncher, SpawnReturnsNonEmptyHandleAndMarksAlive) {
    NullProcessLauncher launcher;
    auto handle = launcher.spawn(make_spec());
    EXPECT_FALSE(handle.empty());
    EXPECT_TRUE(launcher.is_alive(handle));
    EXPECT_EQ(launcher.process_count(), 1u);
}

TEST(NullProcessLauncher, SpawnEmptyIdReturnsEmptyHandle) {
    NullProcessLauncher launcher;
    WidgetSpec spec = make_spec();
    spec.id.clear();
    auto handle = launcher.spawn(spec);
    EXPECT_TRUE(handle.empty());
    EXPECT_EQ(launcher.process_count(), 0u);  // 無效 spec：不留任何記錄
}

TEST(NullProcessLauncher, SpawnDistinctHandlesPerCall) {
    NullProcessLauncher launcher;
    auto a = launcher.spawn(make_spec("widget.a", "widget.a.main"));
    auto b = launcher.spawn(make_spec("widget.a", "widget.a.main"));
    EXPECT_NE(a, b);  // 同一 widget id 重複 spawn 仍得不同 handle
    EXPECT_EQ(launcher.process_count(), 2u);
}

TEST(NullProcessLauncher, TerminateStopsAliveProcessAndReturnsTrue) {
    NullProcessLauncher launcher;
    auto handle = launcher.spawn(make_spec());
    EXPECT_TRUE(launcher.terminate(handle));
    EXPECT_FALSE(launcher.is_alive(handle));
}

TEST(NullProcessLauncher, TerminateUnknownHandleReturnsFalseNoCrash) {
    NullProcessLauncher launcher;
    EXPECT_FALSE(launcher.terminate("proc.unknown#1"));
}

TEST(NullProcessLauncher, TerminateAlreadyTerminatedReturnsFalse) {
    NullProcessLauncher launcher;
    auto handle = launcher.spawn(make_spec());
    ASSERT_TRUE(launcher.terminate(handle));
    EXPECT_FALSE(launcher.terminate(handle));  // 已終止：不重複計為成功
}

TEST(NullProcessLauncher, IsAliveUnknownHandleReturnsFalse) {
    NullProcessLauncher launcher;
    EXPECT_FALSE(launcher.is_alive("proc.unknown#1"));
}

TEST(NullProcessLauncher, SimulateCrashMarksNotAlive) {
    NullProcessLauncher launcher;
    auto handle = launcher.spawn(make_spec());
    EXPECT_TRUE(launcher.simulate_crash(handle));
    EXPECT_FALSE(launcher.is_alive(handle));
}

TEST(NullProcessLauncher, SimulateCrashUnknownOrDeadReturnsFalse) {
    NullProcessLauncher launcher;
    EXPECT_FALSE(launcher.simulate_crash("proc.unknown#1"));
    auto handle = launcher.spawn(make_spec());
    ASSERT_TRUE(launcher.terminate(handle));
    EXPECT_FALSE(launcher.simulate_crash(handle));  // 已死：無法「再崩潰」一次
}

// ---------------------------------------------------------------------------
// WidgetHost — 生命週期（啟動 / 停止 / 存活）
// ---------------------------------------------------------------------------

TEST(WidgetHostLifecycle, StartValidSpecSucceedsAndIsAlive) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);

    EXPECT_EQ(host.state(), ProcessState::NotStarted);
    EXPECT_FALSE(host.is_alive());

    auto status = host.start(make_spec("widget.clock", "widget.clock.main"));
    EXPECT_EQ(status, WidgetHostStatus::Ok);
    EXPECT_EQ(host.state(), ProcessState::Running);
    EXPECT_TRUE(host.is_alive());
    EXPECT_EQ(host.widget_id(), "widget.clock");
    EXPECT_FALSE(host.process_handle().empty());
    EXPECT_TRUE(host.has_widget());
}

TEST(WidgetHostLifecycle, StartInvalidSpecEmptyIdReturnsInvalidNoProcessSpawned) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);

    WidgetSpec spec = make_spec();
    spec.id.clear();
    auto status = host.start(spec);
    EXPECT_EQ(status, WidgetHostStatus::Invalid);
    EXPECT_EQ(host.state(), ProcessState::NotStarted);
    EXPECT_EQ(launcher.process_count(), 0u);
}

TEST(WidgetHostLifecycle, StartInvalidSpecEmptyEntryReturnsInvalid) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);

    WidgetSpec spec = make_spec();
    spec.entry.clear();
    auto status = host.start(spec);
    EXPECT_EQ(status, WidgetHostStatus::Invalid);
    EXPECT_EQ(launcher.process_count(), 0u);
}

TEST(WidgetHostLifecycle, StartWhenAlreadyRunningReturnsAlreadyRunning) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);

    auto second = host.start(make_spec("widget.other", "widget.other.main"));
    EXPECT_EQ(second, WidgetHostStatus::AlreadyRunning);
    EXPECT_EQ(host.widget_id(), "widget.clock");  // 未被第二次 start 覆蓋
    EXPECT_EQ(launcher.process_count(), 1u);       // 未多啟動一個行程
}

TEST(WidgetHostLifecycle, StopRunningSucceedsAndNotAlive) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);
    auto handle = host.process_handle();

    auto status = host.stop();
    EXPECT_EQ(status, WidgetHostStatus::Ok);
    EXPECT_EQ(host.state(), ProcessState::Stopped);
    EXPECT_FALSE(host.is_alive());
    EXPECT_FALSE(launcher.is_alive(handle));
}

TEST(WidgetHostLifecycle, StopNotRunningReturnsNotRunningNoCrash) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    EXPECT_EQ(host.stop(), WidgetHostStatus::NotRunning);
}

TEST(WidgetHostLifecycle, RestartWithoutPriorStartReturnsInvalid) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    EXPECT_EQ(host.restart(), WidgetHostStatus::Invalid);
}

TEST(WidgetHostLifecycle, RestartAfterStartSpawnsNewProcessHandle) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);
    auto old_handle = host.process_handle();

    auto status = host.restart();
    EXPECT_EQ(status, WidgetHostStatus::Ok);
    EXPECT_EQ(host.state(), ProcessState::Running);
    EXPECT_TRUE(host.is_alive());
    EXPECT_NE(host.process_handle(), old_handle);  // 新行程，非沿用舊 handle
    EXPECT_FALSE(launcher.is_alive(old_handle));   // 舊行程已終止
    EXPECT_EQ(host.widget_id(), "widget.clock");   // 沿用同一 spec
}

// ---------------------------------------------------------------------------
// WidgetHost — 崩潰偵測 + on_crash 回呼
// ---------------------------------------------------------------------------

TEST(WidgetHostCrash, PollCrashWhenNotRunningReturnsFalse) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    EXPECT_FALSE(host.poll_crash());  // 尚未啟動
}

TEST(WidgetHostCrash, PollCrashWhenStillAliveReturnsFalse) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);
    EXPECT_FALSE(host.poll_crash());
    EXPECT_EQ(host.state(), ProcessState::Running);
}

TEST(WidgetHostCrash, PollCrashDetectsSimulatedCrashAndFiresCallback) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec("widget.clock", "widget.clock.main")), WidgetHostStatus::Ok);

    std::vector<std::string> crashed_ids;
    host.on_crash([&](const std::string& id) { crashed_ids.push_back(id); });

    ASSERT_TRUE(launcher.simulate_crash(host.process_handle()));
    // is_alive() 即時查詢 launcher：崩潰後、poll_crash() 之前即已反映為不存活。
    EXPECT_FALSE(host.is_alive());

    bool detected = host.poll_crash();
    EXPECT_TRUE(detected);
    EXPECT_EQ(host.state(), ProcessState::Crashed);
    ASSERT_EQ(crashed_ids.size(), 1u);
    EXPECT_EQ(crashed_ids[0], "widget.clock");

    // 崩潰後再次 poll（非 Running 狀態）不重複觸發。
    EXPECT_FALSE(host.poll_crash());
    EXPECT_EQ(crashed_ids.size(), 1u);
}

TEST(WidgetHostCrash, RestartAfterCrashSucceeds) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);
    ASSERT_TRUE(launcher.simulate_crash(host.process_handle()));
    ASSERT_TRUE(host.poll_crash());
    ASSERT_EQ(host.state(), ProcessState::Crashed);

    auto status = host.restart();
    EXPECT_EQ(status, WidgetHostStatus::Ok);
    EXPECT_EQ(host.state(), ProcessState::Running);
    EXPECT_TRUE(host.is_alive());
}

// ---------------------------------------------------------------------------
// WidgetHost — 經 E10-01 通道通訊（宿主事件廣播）
// ---------------------------------------------------------------------------

TEST(WidgetHostChannelCommunication, StartPublishesStartedMessageWithPayload) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);

    std::vector<Message> seen;
    channel.subscribe(kWidgetStartedType, [&](const Message& m) { seen.push_back(m); });

    ASSERT_EQ(host.start(make_spec("widget.clock", "widget.clock.main")), WidgetHostStatus::Ok);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].payload.get_string("widget_id"), "widget.clock");
    EXPECT_EQ(seen[0].payload.get_string("process_handle"), host.process_handle());
}

TEST(WidgetHostChannelCommunication, StopPublishesStoppedMessage) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);

    std::vector<Message> seen;
    channel.subscribe(kWidgetStoppedType, [&](const Message& m) { seen.push_back(m); });
    ASSERT_EQ(host.stop(), WidgetHostStatus::Ok);
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].payload.get_string("widget_id"), "widget.clock");
}

TEST(WidgetHostChannelCommunication, CrashPublishesCrashedMessage) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);
    ASSERT_EQ(host.start(make_spec()), WidgetHostStatus::Ok);

    std::vector<Message> seen;
    channel.subscribe(kWidgetCrashedType, [&](const Message& m) { seen.push_back(m); });
    ASSERT_TRUE(launcher.simulate_crash(host.process_handle()));
    ASSERT_TRUE(host.poll_crash());
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].payload.get_string("widget_id"), "widget.clock");
}

TEST(WidgetHostChannelCommunication, HostAndCallerShareSameChannelForDirectSendReceive) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);

    // 呼叫端可直接經 host.channel()（即 E10-01 MessageChannel）與 widget 側收送，
    // 與宿主事件廣播（publish/subscribe）互不干擾（見 E10-01 契約：send/receive 為獨立佇列）。
    host.channel().send(Message{"widget.command", ds::command::CommandArgs{}.set("op", "refresh")});
    EXPECT_TRUE(channel.has_pending());

    auto got = channel.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->type, "widget.command");
    EXPECT_EQ(got->payload.get_string("op"), "refresh");
}

// ---------------------------------------------------------------------------
// WidgetHost — widget surface 經 E1-03 協定橋接
// ---------------------------------------------------------------------------

TEST(WidgetHostSurfaceBridge, StartWithSurfaceIdCreatesAlphaSurfaceAndPublishesAttached) {
    NullKernelBackend backend(alpha_capable_matrix());
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService surface_service(backend);
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel, &surface_service);

    std::vector<Message> seen;
    channel.subscribe(kWidgetSurfaceAttachedType, [&](const Message& m) { seen.push_back(m); });

    WidgetSpec spec = make_spec();
    spec.surface_id = "surface.widget.clock";
    spec.surface_alpha = AlphaProfile{AlphaMode::PerPixel, 0.5f};

    auto status = host.start(spec);
    EXPECT_EQ(status, WidgetHostStatus::Ok);
    EXPECT_TRUE(surface_service.has_alpha_surface("surface.widget.clock"));
    const auto* profile = surface_service.alpha_profile("surface.widget.clock");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(profile->opacity, 0.5f);

    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].payload.get_string("widget_id"), "widget.clock");
    EXPECT_EQ(seen[0].payload.get_string("surface_id"), "surface.widget.clock");
    EXPECT_EQ(seen[0].payload.get_string("mode"), "per_pixel");
    EXPECT_DOUBLE_EQ(*seen[0].payload.get_double("opacity"), 0.5);
}

TEST(WidgetHostSurfaceBridge, StopDestroysSurfaceAndPublishesDetached) {
    NullKernelBackend backend(alpha_capable_matrix());
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService surface_service(backend);
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel, &surface_service);

    WidgetSpec spec = make_spec();
    spec.surface_id = "surface.widget.clock";
    ASSERT_EQ(host.start(spec), WidgetHostStatus::Ok);
    ASSERT_TRUE(surface_service.has_alpha_surface("surface.widget.clock"));

    std::vector<Message> seen;
    channel.subscribe(kWidgetSurfaceDetachedType, [&](const Message& m) { seen.push_back(m); });

    ASSERT_EQ(host.stop(), WidgetHostStatus::Ok);
    EXPECT_FALSE(surface_service.has_alpha_surface("surface.widget.clock"));
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].payload.get_string("surface_id"), "surface.widget.clock");
}

TEST(WidgetHostSurfaceBridge, StartSurfaceUnsupportedReturnsStatusAndSpawnsNoProcess) {
    NullKernelBackend backend(alpha_incapable_matrix());  // 保守預設：per-pixel alpha 不可用
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService surface_service(backend);
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel, &surface_service);

    WidgetSpec spec = make_spec();
    spec.surface_id = "surface.widget.clock";
    auto status = host.start(spec);
    EXPECT_EQ(status, WidgetHostStatus::SurfaceUnsupported);
    EXPECT_EQ(host.state(), ProcessState::NotStarted);
    EXPECT_EQ(launcher.process_count(), 0u);  // 明確拒絕，不留孤兒行程
}

TEST(WidgetHostSurfaceBridge, StartSurfaceRequestedButNoServiceInjectedReturnsInvalid) {
    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel);  // 未注入 surface_service

    WidgetSpec spec = make_spec();
    spec.surface_id = "surface.widget.clock";
    auto status = host.start(spec);
    EXPECT_EQ(status, WidgetHostStatus::Invalid);
    EXPECT_EQ(launcher.process_count(), 0u);
}

TEST(WidgetHostSurfaceBridge, StartSurfaceIdConflictReturnsInvalidNoProcessSpawned) {
    NullKernelBackend backend(alpha_capable_matrix());
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService surface_service(backend);
    ASSERT_EQ(surface_service.create_alpha_surface("surface.taken", {}, {}),
              ds::kernel::AlphaStatus::Ok);

    NullProcessLauncher launcher;
    MessageChannel channel;
    WidgetHost host(launcher, channel, &surface_service);

    WidgetSpec spec = make_spec();
    spec.surface_id = "surface.taken";
    auto status = host.start(spec);
    EXPECT_EQ(status, WidgetHostStatus::Invalid);
    EXPECT_EQ(launcher.process_count(), 0u);
}

// ---------------------------------------------------------------------------
// 契約版本
// ---------------------------------------------------------------------------

TEST(ContractVersion, ReturnsExpectedString) {
    EXPECT_STREQ(ds::ipc::widget_host_contract_version(), "e10_05/1.0.0");
}
