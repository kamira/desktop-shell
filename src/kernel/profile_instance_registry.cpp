// E1-13 多 profile 實例並存 — 實作
//
// 純記憶體維護，無平台分支、無真實 OS API。能力閘控（NFR-03）全透過組合的 E1-01
// `LayerStack` 進行 —— 本檔不另外持有第二份能力矩陣，避免兩份矩陣互相漂移。
#include "profile_instance_registry.hpp"

#include <utility>

namespace ds::kernel {

const char* to_string(InstantiateStatus s) noexcept {
    switch (s) {
        case InstantiateStatus::Ok:                     return "instantiate.ok";
        case InstantiateStatus::RejectedEmptyDefinition: return "instantiate.rejected_empty_definition";
        case InstantiateStatus::RejectedNoCapability:    return "instantiate.rejected_no_capability";
        case InstantiateStatus::RejectedInstanceLimit:   return "instantiate.rejected_instance_limit";
    }
    return "unknown";  // 不可達（列舉已窮盡）；保守回退。
}

ProfileInstanceRegistry::ProfileInstanceRegistry(CapabilityMatrix caps, std::size_t max_instances)
    : layer_stack_(std::move(caps)), max_instances_(max_instances) {}

ProfileInstance* ProfileInstanceRegistry::find(const InstanceId& id) {
    for (ProfileInstance& inst : instances_) {
        if (inst.id == id) {
            return &inst;
        }
    }
    return nullptr;
}

const ProfileInstance* ProfileInstanceRegistry::find(const InstanceId& id) const {
    for (const ProfileInstance& inst : instances_) {
        if (inst.id == id) {
            return &inst;
        }
    }
    return nullptr;
}

InstanceId ProfileInstanceRegistry::make_instance_id(const ProfileDefinitionId& definition_id) {
    // 具名字串 "<definition_id>#<n>"：可回溯所屬定義、仍屬字串型別（NFR-02）。
    // 保守迴圈確保新 id 目前未被使用（正常情況下第一次即成功；序號單調遞增不重用）。
    InstanceId id;
    do {
        id = definition_id + "#" + std::to_string(next_seq_++);
    } while (find(id) != nullptr);
    return id;
}

InstantiateOutcome ProfileInstanceRegistry::instantiate(const ProfileDefinition& definition) {
    if (definition.id.empty()) {
        return InstantiateOutcome{InstantiateStatus::RejectedEmptyDefinition, InstanceId{}};
    }
    if (instances_.size() >= max_instances_) {
        return InstantiateOutcome{InstantiateStatus::RejectedInstanceLimit, InstanceId{}};
    }

    const InstanceId new_id = make_instance_id(definition.id);

    // 經 E1-01 指派具名圖層；new_id 保證全新，故 assign 只會回 Ok 或（無能力時）
    // RejectedNoCapability，永不 Moved / RejectedEmptyId。
    const LayerAssign assign_result = layer_stack_.assign(new_id, definition.surface.layer);
    if (assign_result == LayerAssign::RejectedNoCapability) {
        return InstantiateOutcome{InstantiateStatus::RejectedNoCapability, InstanceId{}};
    }

    // 複製一份獨立的 SurfaceProfile（隔離保證：往後修改 definition 不影響已建立的實例）。
    instances_.push_back(ProfileInstance{new_id, definition.id, definition.surface, /*visible=*/true});
    return InstantiateOutcome{InstantiateStatus::Ok, new_id};
}

const ProfileInstance* ProfileInstanceRegistry::get(const InstanceId& id) const {
    return find(id);
}

bool ProfileInstanceRegistry::contains(const InstanceId& id) const {
    return find(id) != nullptr;
}

std::vector<InstanceId> ProfileInstanceRegistry::list() const {
    std::vector<InstanceId> out;
    out.reserve(instances_.size());
    for (const ProfileInstance& inst : instances_) {
        out.push_back(inst.id);  // 建立序（穩定）。
    }
    return out;
}

std::size_t ProfileInstanceRegistry::count_of_definition(const ProfileDefinitionId& definition_id) const {
    std::size_t n = 0;
    for (const ProfileInstance& inst : instances_) {
        if (inst.definition_id == definition_id) {
            ++n;
        }
    }
    return n;
}

const SurfaceLayer* ProfileInstanceRegistry::layer_of(const InstanceId& id) const {
    return layer_stack_.layer_of(id);
}

bool ProfileInstanceRegistry::destroy(const InstanceId& id) {
    ProfileInstance* inst = find(id);
    if (inst == nullptr) {
        return false;  // 未知 id / 重複銷毀：明確不靜默。
    }
    // NFR-03：先經 LayerStack.remove 之 has() 閘控；不可用則拒絕、不改任何狀態
    // （含不移除本地記錄，兩者保持一致）。
    if (!layer_stack_.remove(id)) {
        return false;
    }
    for (auto it = instances_.begin(); it != instances_.end(); ++it) {
        if (it->id == id) {
            instances_.erase(it);
            break;
        }
    }
    return true;
}

bool ProfileInstanceRegistry::show(const InstanceId& id) {
    ProfileInstance* inst = find(id);
    if (inst == nullptr) {
        return false;
    }
    inst->visible = true;
    return true;
}

bool ProfileInstanceRegistry::hide(const InstanceId& id) {
    ProfileInstance* inst = find(id);
    if (inst == nullptr) {
        return false;
    }
    inst->visible = false;
    return true;
}

bool ProfileInstanceRegistry::is_visible(const InstanceId& id) const {
    const ProfileInstance* inst = find(id);
    return inst != nullptr && inst->visible;  // 未知 / 已銷毀：保守回 false。
}

}  // namespace ds::kernel
