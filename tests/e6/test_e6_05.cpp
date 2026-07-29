// E6-05 跨 surface 控制 — 單元測試（gtest）
//
// 驗證：把 E1-13（多 profile 實例並存）與 E6-01（命令匯流排）接起來的跨 surface 控制：
//   - 廣播（broadcast）到 registry 目前列舉的「全部」存活實例，依建立序、逐目標分派
//   - 單一具名目標（send_to）：成功 / 未知目標明確拒絕（不呼叫匯流排）
//   - 群組命令（send_to_group）：已知 / 未知目標混合，逐目標互不影響
//   - 逐目標結果回報（TargetResult / CrossDispatchReport 聚合：all_ok / ok_count / failed_count）
//   - 確實經 E6-01 CommandBus 分派（處理器被呼叫、參數傳遞、失敗 / 未註冊命令傳遞）
//   - 確實經 E1-13 ProfileInstanceRegistry 列舉 / 驗證具名目標存在性
//   - 未知目標 / 無效（未註冊）命令皆結構化報錯，不靜默
//   - 空目標集合（空 registry / 空群組清單）合法，回空報告
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）。
// NFR-02：本測試不出現任何數字 index / 數字 z-order；目標一律以具名 InstanceId 指涉。
#include "cross_surface_controller.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using ds::command::Command;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandValue;
using ds::command::CrossDispatchReport;
using ds::command::CrossSurfaceController;
using ds::command::TargetDispatchStatus;
using ds::command::TargetResult;
using ds::command::to_string;

using ds::kernel::HitPolicy;
using ds::kernel::InputPolicy;
using ds::kernel::InstanceId;
using ds::kernel::ProfileDefinition;
using ds::kernel::ProfileInstanceRegistry;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;

namespace {

ProfileDefinition make_definition(const std::string& id, SurfaceLayer layer = SurfaceLayer::Normal) {
    ProfileDefinition def;
    def.id = id;
    def.surface.layer = layer;
    def.surface.input = InputPolicy::Accepting;
    def.surface.hit = HitPolicy::Solid;
    def.surface.lifecycle = SurfaceLifecycle::Persistent;
    return def;
}

// 建一個已實例化 n 份的 registry（同一 definition），回傳建立序的具名 id 清單。
std::vector<InstanceId> populate(ProfileInstanceRegistry& registry, int n,
                                  const std::string& definition_id = "widget.clock") {
    std::vector<InstanceId> ids;
    const ProfileDefinition def = make_definition(definition_id);
    for (int i = 0; i < n; ++i) {
        const auto outcome = registry.instantiate(def);
        ids.push_back(outcome.id);
    }
    return ids;
}

// -------- broadcast：全部存活實例 --------

TEST(Broadcast, DispatchesToEveryListedInstanceInOrder) {
    ProfileInstanceRegistry registry;
    const std::vector<InstanceId> ids = populate(registry, 3);

    CommandBus bus;
    std::vector<std::string> seen_targets;
    bus.register_command("mute", [&](const CommandArgs& a) {
        seen_targets.push_back(a.get_string("target").value_or(""));
        return CommandResult::make_ok();
    });

    CrossSurfaceController controller(bus, registry);
    CrossDispatchReport report = controller.broadcast("mute");

    EXPECT_EQ(report.size(), 3u);
    EXPECT_TRUE(report.all_ok());
    EXPECT_EQ(report.ok_count(), 3u);
    EXPECT_EQ(report.failed_count(), 0u);
    ASSERT_EQ(seen_targets.size(), 3u);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(report.per_target[i].target, ids[i]);
        EXPECT_EQ(seen_targets[i], ids[i]);
        EXPECT_TRUE(report.per_target[i].ok());
        EXPECT_EQ(report.per_target[i].status, TargetDispatchStatus::Ok);
    }
}

TEST(Broadcast, EmptyRegistryYieldsEmptyReport) {
    ProfileInstanceRegistry registry;  // 無任何存活實例
    CommandBus bus;
    bus.register_command("mute", [](const CommandArgs&) { return CommandResult::make_ok(); });

    CrossSurfaceController controller(bus, registry);
    CrossDispatchReport report = controller.broadcast("mute");

    EXPECT_TRUE(report.empty());
    EXPECT_EQ(report.size(), 0u);
    // 空報告視為「全部成功」——沒有目標即沒有失敗者。
    EXPECT_TRUE(report.all_ok());
    EXPECT_EQ(report.ok_count(), 0u);
    EXPECT_EQ(report.failed_count(), 0u);
}

TEST(Broadcast, PassesOriginalArgsAlongsideTargetKey) {
    ProfileInstanceRegistry registry;
    populate(registry, 1);

    CommandBus bus;
    std::int64_t seen_level = -1;
    std::string seen_target;
    bus.register_command("set_volume", [&](const CommandArgs& a) {
        seen_level = a.get_int("level").value_or(-1);
        seen_target = a.get_string("target").value_or("");
        return CommandResult::make_ok();
    });

    CommandArgs args;
    args.set("level", 42);

    CrossSurfaceController controller(bus, registry);
    controller.broadcast("set_volume", args);

    EXPECT_EQ(seen_level, 42);
    EXPECT_FALSE(seen_target.empty());
}

// -------- send_to：單一具名目標 --------

TEST(SendTo, KnownTargetDispatchesAndReturnsOk) {
    ProfileInstanceRegistry registry;
    const std::vector<InstanceId> ids = populate(registry, 1);

    CommandBus bus;
    int calls = 0;
    bus.register_command("ping", [&](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok(CommandValue(std::string("pong")));
    });

    CrossSurfaceController controller(bus, registry);
    TargetResult r = controller.send_to(ids[0], "ping");

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.status, TargetDispatchStatus::Ok);
    EXPECT_EQ(r.target, ids[0]);
    ASSERT_TRUE(r.result.value.as_string().has_value());
    EXPECT_EQ(*r.result.value.as_string(), "pong");
}

TEST(SendTo, UnknownTargetRejectedWithoutCallingBus) {
    ProfileInstanceRegistry registry;
    populate(registry, 1);

    CommandBus bus;
    int calls = 0;
    bus.register_command("ping", [&](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok();
    });

    CrossSurfaceController controller(bus, registry);
    TargetResult r = controller.send_to("no.such.instance", "ping");

    EXPECT_EQ(calls, 0);  // 未知目標：不呼叫匯流排
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, TargetDispatchStatus::UnknownTarget);
    EXPECT_FALSE(r.result.ok());
    EXPECT_EQ(r.target, "no.such.instance");
}

TEST(SendTo, UnregisteredCommandReturnsCommandNotFoundDistinctFromUnknownTarget) {
    ProfileInstanceRegistry registry;
    const std::vector<InstanceId> ids = populate(registry, 1);

    CommandBus bus;  // "does_not_exist" 從未註冊

    CrossSurfaceController controller(bus, registry);
    TargetResult r = controller.send_to(ids[0], "does_not_exist");

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, TargetDispatchStatus::CommandNotFound);
    EXPECT_NE(r.status, TargetDispatchStatus::UnknownTarget);  // 目標存在，只是命令未註冊
    EXPECT_FALSE(r.result.ok());
}

TEST(SendTo, HandlerFailureReportsFailedStatus) {
    ProfileInstanceRegistry registry;
    const std::vector<InstanceId> ids = populate(registry, 1);

    CommandBus bus;
    bus.register_command("risky", [](const CommandArgs&) {
        return CommandResult::make_failed("boom");
    });

    CrossSurfaceController controller(bus, registry);
    TargetResult r = controller.send_to(ids[0], "risky");

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, TargetDispatchStatus::Failed);
    EXPECT_EQ(r.result.message, "boom");
}

// -------- send_to_group：群組命令 --------

TEST(SendToGroup, MixedKnownAndUnknownTargetsAreIndependent) {
    ProfileInstanceRegistry registry;
    const std::vector<InstanceId> ids = populate(registry, 2);

    CommandBus bus;
    int calls = 0;
    bus.register_command("hide", [&](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok();
    });

    const std::vector<InstanceId> group = {ids[0], "ghost.instance", ids[1]};

    CrossSurfaceController controller(bus, registry);
    CrossDispatchReport report = controller.send_to_group(group, "hide");

    ASSERT_EQ(report.size(), 3u);
    EXPECT_EQ(calls, 2);  // 只有兩個已知目標觸發匯流排
    EXPECT_FALSE(report.all_ok());
    EXPECT_EQ(report.ok_count(), 2u);
    EXPECT_EQ(report.failed_count(), 1u);

    EXPECT_EQ(report.per_target[0].target, ids[0]);
    EXPECT_TRUE(report.per_target[0].ok());

    EXPECT_EQ(report.per_target[1].target, "ghost.instance");
    EXPECT_EQ(report.per_target[1].status, TargetDispatchStatus::UnknownTarget);
    EXPECT_FALSE(report.per_target[1].ok());

    EXPECT_EQ(report.per_target[2].target, ids[1]);
    EXPECT_TRUE(report.per_target[2].ok());
}

TEST(SendToGroup, EmptyTargetListYieldsEmptyReport) {
    ProfileInstanceRegistry registry;
    populate(registry, 2);

    CommandBus bus;
    bus.register_command("hide", [](const CommandArgs&) { return CommandResult::make_ok(); });

    CrossSurfaceController controller(bus, registry);
    CrossDispatchReport report = controller.send_to_group({}, "hide");

    EXPECT_TRUE(report.empty());
    EXPECT_TRUE(report.all_ok());
}

TEST(SendToGroup, AllUnknownTargetsStillReportsPerTargetWithoutCallingBus) {
    ProfileInstanceRegistry registry;  // 沒有任何存活實例

    CommandBus bus;
    int calls = 0;
    bus.register_command("hide", [&](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok();
    });

    CrossSurfaceController controller(bus, registry);
    CrossDispatchReport report =
        controller.send_to_group({"a.ghost", "b.ghost"}, "hide");

    EXPECT_EQ(calls, 0);
    ASSERT_EQ(report.size(), 2u);
    EXPECT_FALSE(report.all_ok());
    EXPECT_EQ(report.failed_count(), 2u);
    for (const auto& tr : report.per_target) {
        EXPECT_EQ(tr.status, TargetDispatchStatus::UnknownTarget);
    }
}

// -------- 診斷字串 / 契約版本 --------

TEST(ToString, CoversAllStatuses) {
    EXPECT_STREQ(to_string(TargetDispatchStatus::Ok), "ok");
    EXPECT_STREQ(to_string(TargetDispatchStatus::Failed), "failed");
    EXPECT_STREQ(to_string(TargetDispatchStatus::CommandNotFound), "command_not_found");
    EXPECT_STREQ(to_string(TargetDispatchStatus::UnknownTarget), "unknown_target");
}

TEST(ContractVersion, IsStableAndNamespaced) {
    const std::string v = ds::command::cross_surface_contract_version();
    EXPECT_EQ(v, "e6_05/1.0.0");
}

}  // namespace
