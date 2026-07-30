// E8-05 外部模組開發介面（SDK）— 實作（平台中立、純邏輯，無任何平台分支）。
#include "module_sdk.hpp"

namespace ds::ext::sdk {

const char* contract_version() noexcept { return "e8_05/1.0.0"; }

const char* to_string(ModuleState state) noexcept {
    switch (state) {
        case ModuleState::Created:
            return "created";
        case ModuleState::Initialized:
            return "initialized";
        case ModuleState::Started:
            return "started";
        case ModuleState::Stopped:
            return "stopped";
        case ModuleState::TornDown:
            return "torn_down";
    }
    return "unknown";
}

ds::ext::Module make_module(IModule& module, const HostServiceRegistry& services) {
    ds::ext::Module m;
    m.manifest = module.info().to_manifest();

    // on_register：E8-04 載入器在通過 manifest / requires / permissions 閘控後呼叫。
    // 這裡建構 SDK ModuleContext（橋接 E8-04 註冊面 + 宿主服務），驅動 init → start。
    // 任一失敗回 false → 載入器回 RegistrationFailed 並整批回滾（不留痕）。
    IModule* mod = &module;
    const HostServiceRegistry* svc = &services;
    m.on_register = [mod, svc](ds::ext::ModuleContext& registration) -> bool {
        ModuleContext ctx(registration, *svc);
        if (!mod->init(ctx)) return false;
        if (!mod->start()) return false;
        return true;
    };

    // on_unload：載入器卸載此模組時呼叫。可逆停止後做最終清理。
    m.on_unload = [mod]() {
        mod->stop();
        mod->teardown();
    };

    return m;
}

}  // namespace ds::ext::sdk
