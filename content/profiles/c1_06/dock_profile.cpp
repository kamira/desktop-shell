// content/profiles/c1_06/dock_profile.cpp — C1-06 Dock profile 實作
#include "dock_profile.hpp"

#include <utility>

namespace ds::profiles {

const char* to_string(DockVisibility v) noexcept {
    switch (v) {
        case DockVisibility::Visible:
            return "Visible";
        case DockVisibility::Hidden:
            return "Hidden";
    }
    return "unknown";
}

const char* to_string(DockToEdgeResult r) noexcept {
    switch (r) {
        case DockToEdgeResult::Ok:
            return "Ok";
        case DockToEdgeResult::AlreadyDocked:
            return "AlreadyDocked";
        case DockToEdgeResult::InvalidEdge:
            return "InvalidEdge";
        case DockToEdgeResult::InvalidThickness:
            return "InvalidThickness";
        case DockToEdgeResult::RejectedNoCapability:
            return "RejectedNoCapability";
    }
    return "unknown";
}

const char* to_string(RevealResult r) noexcept {
    switch (r) {
        case RevealResult::Revealed:
            return "Revealed";
        case RevealResult::AlreadyVisible:
            return "AlreadyVisible";
        case RevealResult::NotDocked:
            return "NotDocked";
    }
    return "unknown";
}

const char* to_string(AddItemResult r) noexcept {
    switch (r) {
        case AddItemResult::Ok:
            return "Ok";
        case AddItemResult::DuplicateId:
            return "DuplicateId";
    }
    return "unknown";
}

DockProfile::DockProfile(std::string id, ds::kernel::LayerStack& layer_stack,
                          ds::kernel::InputStrategy strategy)
    : id_(std::move(id)), layer_stack_(layer_stack), strategy_(strategy) {}

DockProfile::~DockProfile() {
    // 若仍固定，強制 undock() —— 確保 E1-01 堆疊上不再留有指向本已銷毀物件語意的條目
    // （與 C1-05 dtor 安全慣例一致）。
    if (docked_) {
        undock();
    }
}

DockToEdgeResult DockProfile::dock_to_edge(DockEdge edge, float thickness_ratio,
                                            std::string reveal_action) {
    if (docked_) {
        return DockToEdgeResult::AlreadyDocked;  // 不靜默重新固定；呼叫端須先 undock()。
    }
    // NFR-03：先閘控 E1-01 layer 能力，未過不註冊熱區、不改任何狀態（避免半成品狀態：
    // 熱區已註冊但頂層指派被拒絕——E1-16 熱區登記無移除 API，先閘控可完全避免此情形）。
    if (!layer_stack_.has(ds::kernel::layer_capability())) {
        return DockToEdgeResult::RejectedNoCapability;
    }

    const DockHotZoneStatus status =
        hot_zones_.register_zone(edge, thickness_ratio, std::move(reveal_action));
    if (status == DockHotZoneStatus::InvalidZone) {
        return DockToEdgeResult::InvalidEdge;  // 委派 E1-16：報錯不靜默，未固定。
    }
    if (status == DockHotZoneStatus::InvalidThickness) {
        return DockToEdgeResult::InvalidThickness;
    }

    // status == Ok：委派 E1-01 指派頂層（layer 能力已於上方確認存在）。
    layer_stack_.assign(id_, ds::kernel::SurfaceLayer::Topmost);
    docked_ = true;
    docked_edge_ = edge;
    auto_hide_ = false;
    visibility_ = DockVisibility::Visible;
    return DockToEdgeResult::Ok;
}

bool DockProfile::undock() {
    if (!docked_) {
        return false;  // 未固定，no-op，不靜默。
    }
    layer_stack_.remove(id_);
    docked_ = false;
    docked_edge_.reset();
    auto_hide_ = false;
    visibility_ = DockVisibility::Visible;
    return true;
}

std::optional<DockEdge> DockProfile::docked_edge() const { return docked_edge_; }

bool DockProfile::set_auto_hide(bool enabled) {
    if (!docked_) {
        return false;  // 自動隱藏依附於已固定邊緣，未固定拒絕。
    }
    auto_hide_ = enabled;
    set_visibility_and_notify(enabled ? DockVisibility::Hidden : DockVisibility::Visible);
    return true;
}

RevealResult DockProfile::reveal() {
    if (!docked_) {
        return RevealResult::NotDocked;
    }
    if (visibility_ == DockVisibility::Visible) {
        return RevealResult::AlreadyVisible;  // no-op，不靜默觸發多餘事件。
    }
    set_visibility_and_notify(DockVisibility::Visible);
    return RevealResult::Revealed;
}

bool DockProfile::hide() {
    if (!docked_) {
        return false;  // 未固定，no-op。
    }
    if (visibility_ == DockVisibility::Hidden) {
        return false;  // 已隱藏，no-op，不靜默重複觸發。
    }
    set_visibility_and_notify(DockVisibility::Hidden);
    return true;
}

bool DockProfile::probe_hot_zone(const DockPoint& point, const DockScreenExtent& screen) {
    // 只有「已固定 + 自動隱藏中 + 目前隱藏」才需要熱區叫出；其餘情形一律 no-op，
    // 避免對可見中的 dock 做無意義的重複 reveal（不誤觸發事件）。
    if (!docked_ || !auto_hide_ || visibility_ != DockVisibility::Hidden) {
        return false;
    }
    const std::optional<DockTriggeredZone> hit = hot_zones_.test(point, screen);
    if (!hit.has_value()) {
        return false;  // 未命中任何已註冊熱區。
    }
    if (!docked_edge_.has_value() || hit->edge != *docked_edge_) {
        return false;  // 命中非本 dock 固定之邊緣（防禦性檢查——本 dock 只註冊了自身邊緣）。
    }
    reveal();
    return true;
}

AddItemResult DockProfile::add_item(std::string id, std::string label) {
    for (const auto& existing : items_) {
        if (existing.id == id) {
            return AddItemResult::DuplicateId;  // 不靜默覆寫既有項目。
        }
    }
    items_.push_back(DockItem{std::move(id), std::move(label)});
    for (auto& cb : on_item_added_) {
        cb(items_.back());
    }
    return AddItemResult::Ok;
}

ds::kernel::InputPolicy DockProfile::backend_input_policy() const noexcept {
    return ds::kernel::to_backend_policy(strategy_);
}

ds::kernel::HitResult DockProfile::hit_result() const noexcept {
    return ds::kernel::hit_result(strategy_);
}

void DockProfile::on_reveal(std::function<void()> cb) { on_reveal_.push_back(std::move(cb)); }

void DockProfile::on_hide(std::function<void()> cb) { on_hide_.push_back(std::move(cb)); }

void DockProfile::on_item_added(std::function<void(const DockItem&)> cb) {
    on_item_added_.push_back(std::move(cb));
}

void DockProfile::set_visibility_and_notify(DockVisibility v) {
    if (visibility_ == v) {
        return;
    }
    visibility_ = v;
    if (v == DockVisibility::Visible) {
        for (auto& cb : on_reveal_) {
            cb();
        }
    } else {
        for (auto& cb : on_hide_) {
            cb();
        }
    }
}

}  // namespace ds::profiles
