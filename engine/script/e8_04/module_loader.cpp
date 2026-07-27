// E8-04 行程內模組載入 — 實作（平台中立、純邏輯，無任何平台分支）。
#include "module_loader.hpp"

#include <exception>

namespace ds::ext {

const char* contract_version() noexcept { return "e8_04/1.0.0"; }

const char* to_string(CapabilityKind kind) noexcept {
    switch (kind) {
        case CapabilityKind::Sensor:
            return "sensor";
        case CapabilityKind::Component:
            return "component";
        case CapabilityKind::Action:
            return "action";
    }
    return "unknown";
}

const char* to_string(LoadStatus status) noexcept {
    switch (status) {
        case LoadStatus::Ok:
            return "ok";
        case LoadStatus::InvalidManifest:
            return "invalid_manifest";
        case LoadStatus::IncompatibleFormat:
            return "incompatible_format";
        case LoadStatus::AlreadyLoaded:
            return "already_loaded";
        case LoadStatus::MissingRequirement:
            return "missing_requirement";
        case LoadStatus::MissingPermission:
            return "missing_permission";
        case LoadStatus::RegistrationFailed:
            return "registration_failed";
    }
    return "unknown";
}

namespace {

// 逗號連接（給訊息用）。
std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += ", ";
        out += items[i];
    }
    return out;
}

}  // namespace

LoadResult ModuleLoader::load(const Module& module) {
    const ds::package::Manifest& mf = module.manifest;

    // 1. 驗證 manifest：name 必須非空。
    if (mf.name.empty()) {
        return {LoadStatus::InvalidManifest, "manifest 缺少 name（模組無法識別）", {}};
    }
    // format_version 相容性（沿用 E9-02 判定：major 相同且 minor 不高於支援上限）。
    if (!ds::package::is_format_compatible(mf.format_version)) {
        return {LoadStatus::IncompatibleFormat,
                "模組 '" + mf.name + "' 的 format_version 不相容", {}};
    }

    // 2. 重複載入偵測：同名已載入 → 拒絕（不覆蓋）。
    if (loaded_.find(mf.name) != loaded_.end()) {
        return {LoadStatus::AlreadyLoaded, "模組 '" + mf.name + "' 已載入", {}};
    }

    // 3. requires 閘控：每一項 required_capabilities 都須由 host 提供。
    //    任一不滿足即明確拒絕（附全部缺項）——不靜默載入。
    {
        std::vector<std::string> missing;
        for (const std::string& cap : mf.required_capabilities) {
            if (!host_.has_capability(cap)) missing.push_back(cap);
        }
        if (!missing.empty()) {
            return {LoadStatus::MissingRequirement,
                    "模組 '" + mf.name + "' 所需能力未被 host 滿足: " + join(missing),
                    std::move(missing)};
        }
    }

    // 4. permissions 閘控：每一項 permissions 都須由 host 授予。
    {
        std::vector<std::string> missing;
        for (const std::string& perm : mf.permissions) {
            if (!host_.grants_permission(perm)) missing.push_back(perm);
        }
        if (!missing.empty()) {
            return {LoadStatus::MissingPermission,
                    "模組 '" + mf.name + "' 所需權限未被 host 授予: " + join(missing),
                    std::move(missing)};
        }
    }

    // 5. 呼叫模組註冊入口。缺入口 / 回 false / 拋例外 → 失敗（不提交）。
    if (!module.on_register) {
        return {LoadStatus::RegistrationFailed,
                "模組 '" + mf.name + "' 缺少註冊入口 on_register", {}};
    }
    ModuleContext ctx;
    bool registered = false;
    try {
        registered = module.on_register(ctx);
    } catch (const std::exception& e) {
        return {LoadStatus::RegistrationFailed,
                "模組 '" + mf.name + "' 註冊入口拋例外: " + e.what(), {}};
    } catch (...) {
        return {LoadStatus::RegistrationFailed,
                "模組 '" + mf.name + "' 註冊入口拋未知例外", {}};
    }
    if (!registered) {
        return {LoadStatus::RegistrationFailed,
                "模組 '" + mf.name + "' 註冊入口回報失敗", {}};
    }

    // 6. 提交前先驗跨模組衝突（整批檢查，不部分提交）。
    for (const Capability& cap : ctx.provided()) {
        auto it = capabilities_.find(cap);
        if (it != capabilities_.end()) {
            return {LoadStatus::RegistrationFailed,
                    "模組 '" + mf.name + "' 提供的能力 " + to_string(cap.kind) + ":" + cap.id +
                        " 與已載入模組 '" + it->second + "' 衝突",
                    {}};
        }
    }
    // 無衝突 → 整批提交：併入共享能力表 + 追蹤已載入模組（含 on_unload 鉤子）。
    for (const Capability& cap : ctx.provided()) {
        capabilities_.emplace(cap, mf.name);
    }
    loaded_.emplace(mf.name, LoadedModule{ctx.provided(), module.on_unload});

    return {LoadStatus::Ok, {}, {}};
}

bool ModuleLoader::unload(const std::string& name) {
    auto it = loaded_.find(name);
    if (it == loaded_.end()) return false;  // 未載入

    // 呼叫模組卸載鉤子（若有）。鉤子拋例外不得阻斷卸載本身 —— 吞掉但仍完成移除。
    if (it->second.on_unload) {
        try {
            it->second.on_unload();
        } catch (...) {
            // 卸載鉤子的例外不外傳；資源移除照常進行。
        }
    }

    // 自共享能力表精準移除此模組登記的全部能力。
    for (const Capability& cap : it->second.capabilities) {
        auto cit = capabilities_.find(cap);
        // 僅移除確實由本模組登記者（防禦性；正常情況必相符）。
        if (cit != capabilities_.end() && cit->second == name) {
            capabilities_.erase(cit);
        }
    }
    loaded_.erase(it);
    return true;
}

}  // namespace ds::ext
