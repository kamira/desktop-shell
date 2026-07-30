// E9-03 可互換元件組合 — 實作（平台中立、純邏輯，無任何平台分支）。
#include "composition.hpp"

#include <utility>

namespace ds::package {

namespace {

// 元件的內容清單是否至少提供一項指定 kind 的資源。
bool provides_kind(const Package& component, const std::string& kind) {
    for (const PackageEntry& e : component.entries) {
        if (e.kind == kind) {
            return true;
        }
    }
    return false;
}

}  // namespace

CompatibilityResult check_compatibility(const ComponentSlot& slot, const Package& component) {
    // (a) 元件本身須結構完整 —— 複用 E9-01 的驗證契約，不重造、不放行破損元件。
    PackageResult vr = validate_package(component);
    if (!vr.ok()) {
        return CompatibilityResult::no("元件結構無效：" + vr.error().message);
    }
    // (b) 插槽宣告的每個 required_kind，元件都必須提供。
    for (const std::string& kind : slot.required_kinds) {
        if (!provides_kind(component, kind)) {
            return CompatibilityResult::no("元件未提供插槽 '" + slot.id +
                                           "' 所需的資源類別（kind）：" + kind);
        }
    }
    return CompatibilityResult::ok();
}

std::size_t Composition::index_of(const std::string& slot_id) const noexcept {
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        if (bindings_[i].slot.id == slot_id) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

BindResult Composition::add_slot(const ComponentSlot& slot) {
    if (slot.id.empty()) {
        return BindResult::failure("插槽 id 不得為空");
    }
    if (index_of(slot.id) != static_cast<std::size_t>(-1)) {
        return BindResult::failure("插槽 id 重複：" + slot.id);
    }
    Binding b;
    b.slot = slot;
    b.bound = false;
    bindings_.push_back(std::move(b));
    return BindResult::success();
}

BindResult Composition::bind(const std::string& slot_id, const Package& component) {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return BindResult::failure("插槽不存在：" + slot_id);
    }
    if (bindings_[i].bound) {
        return BindResult::failure("插槽已綁定（如需替換請用 swap）：" + slot_id);
    }
    const CompatibilityResult cr = check_compatibility(bindings_[i].slot, component);
    if (!cr) {
        return BindResult::failure(cr.reason);
    }
    bindings_[i].component = component;
    bindings_[i].bound = true;
    return BindResult::success();
}

BindResult Composition::swap(const std::string& slot_id, const Package& component) {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return BindResult::failure("插槽不存在：" + slot_id);
    }
    if (!bindings_[i].bound) {
        return BindResult::failure("插槽尚未綁定（首次綁定請用 bind）：" + slot_id);
    }
    const CompatibilityResult cr = check_compatibility(bindings_[i].slot, component);
    if (!cr) {
        // 不相容 → 明確報錯且保留原綁定不變（不靜默、不半途破壞狀態）。
        return BindResult::failure(cr.reason);
    }
    bindings_[i].component = component;
    return BindResult::success();
}

BindResult Composition::unbind(const std::string& slot_id) {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return BindResult::failure("插槽不存在：" + slot_id);
    }
    if (!bindings_[i].bound) {
        return BindResult::failure("插槽本已為空：" + slot_id);
    }
    bindings_[i].bound = false;
    bindings_[i].component = Package{};
    return BindResult::success();
}

bool Composition::has_slot(const std::string& slot_id) const noexcept {
    return index_of(slot_id) != static_cast<std::size_t>(-1);
}

bool Composition::is_bound(const std::string& slot_id) const noexcept {
    const std::size_t i = index_of(slot_id);
    return i != static_cast<std::size_t>(-1) && bindings_[i].bound;
}

const Package* Composition::bound_component(const std::string& slot_id) const noexcept {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1) || !bindings_[i].bound) {
        return nullptr;
    }
    return &bindings_[i].component;
}

const ComponentSlot* Composition::slot(const std::string& slot_id) const noexcept {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return nullptr;
    }
    return &bindings_[i].slot;
}

std::vector<std::string> Composition::slot_ids() const {
    std::vector<std::string> ids;
    ids.reserve(bindings_.size());
    for (const Binding& b : bindings_) {
        ids.push_back(b.slot.id);
    }
    return ids;
}

std::vector<std::size_t> Composition::compatible_candidates(
    const std::string& slot_id, const std::vector<Package>& candidates) const {
    std::vector<std::size_t> out;
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return out;  // 插槽不存在 → 無候選。
    }
    for (std::size_t k = 0; k < candidates.size(); ++k) {
        if (check_compatibility(bindings_[i].slot, candidates[k])) {
            out.push_back(k);
        }
    }
    return out;
}

BindResult Composition::validate() const {
    for (const Binding& b : bindings_) {
        if (!b.bound) {
            return BindResult::failure("插槽未綁定：" + b.slot.id);
        }
        const CompatibilityResult cr = check_compatibility(b.slot, b.component);
        if (!cr) {
            return BindResult::failure("插槽 '" + b.slot.id + "' 綁定不相容：" + cr.reason);
        }
    }
    return BindResult::success();
}

}  // namespace ds::package
