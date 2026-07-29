// E6-05 跨 surface 控制 — 實作
#include "cross_surface_controller.hpp"

namespace ds::command {

const char* cross_surface_contract_version() noexcept {
    // 跨 surface 控制契約版本。API 面若不相容變更，遞增主版本。
    return "e6_05/1.0.0";
}

const char* to_string(TargetDispatchStatus s) noexcept {
    switch (s) {
        case TargetDispatchStatus::Ok:
            return "ok";
        case TargetDispatchStatus::Failed:
            return "failed";
        case TargetDispatchStatus::CommandNotFound:
            return "command_not_found";
        case TargetDispatchStatus::UnknownTarget:
            return "unknown_target";
    }
    return "unknown";
}

namespace {

// 對單一具名目標分派：先驗證存活於 registry（NFR：未知目標明確報錯、不呼叫匯流排），
// 再把目標 id 併入參數（鍵 "target"）交給 CommandBus::dispatch。
TargetResult dispatch_to_one(CommandBus& bus, const ds::kernel::ProfileInstanceRegistry& registry,
                              const ds::kernel::InstanceId& target, const CommandId& command,
                              const CommandArgs& args) {
    TargetResult tr;
    tr.target = target;

    if (!registry.contains(target)) {
        tr.status = TargetDispatchStatus::UnknownTarget;
        tr.result = CommandResult::make_failed("unknown target: " + target);
        return tr;
    }

    CommandArgs routed = args;
    routed.set("target", target);
    CommandResult r = bus.dispatch(command, routed);
    tr.result = r;
    switch (r.status) {
        case CommandStatus::Ok:
            tr.status = TargetDispatchStatus::Ok;
            break;
        case CommandStatus::Failed:
            tr.status = TargetDispatchStatus::Failed;
            break;
        case CommandStatus::NotFound:
            tr.status = TargetDispatchStatus::CommandNotFound;
            break;
    }
    return tr;
}

}  // namespace

CrossDispatchReport CrossSurfaceController::broadcast(const CommandId& command,
                                                        const CommandArgs& args) const {
    CrossDispatchReport report;
    const std::vector<ds::kernel::InstanceId> targets = registry_.list();
    report.per_target.reserve(targets.size());
    for (const auto& target : targets) {
        report.per_target.push_back(dispatch_to_one(bus_, registry_, target, command, args));
    }
    return report;
}

TargetResult CrossSurfaceController::send_to(const ds::kernel::InstanceId& target,
                                              const CommandId& command,
                                              const CommandArgs& args) const {
    return dispatch_to_one(bus_, registry_, target, command, args);
}

CrossDispatchReport CrossSurfaceController::send_to_group(
    const std::vector<ds::kernel::InstanceId>& targets, const CommandId& command,
    const CommandArgs& args) const {
    CrossDispatchReport report;
    report.per_target.reserve(targets.size());
    for (const auto& target : targets) {
        report.per_target.push_back(dispatch_to_one(bus_, registry_, target, command, args));
    }
    return report;
}

}  // namespace ds::command
