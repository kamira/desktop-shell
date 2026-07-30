// E4-06 Surface 編號定址與切換 — 實作（見 surface_switcher.hpp 規格）。
#include "surface_switcher.hpp"

#include <utility>

namespace ds::render {

// --- 內部尋找（具名鍵線性掃描；同子系統/同建置慣例的 surface 數量小，見 E1-03）---
std::vector<ds::kernel::SurfaceId>::iterator SurfaceSwitcher::find(
    const ds::kernel::SurfaceId& id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (*it == id) {
            return it;
        }
    }
    return entries_.end();
}

std::vector<ds::kernel::SurfaceId>::const_iterator SurfaceSwitcher::find(
    const ds::kernel::SurfaceId& id) const {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (*it == id) {
            return it;
        }
    }
    return entries_.end();
}

// --- 註冊 / 移除 ---
SwitchStatus SurfaceSwitcher::register_surface(const ds::kernel::SurfaceId& id) {
    if (id.empty() || find(id) != entries_.end()) {
        return SwitchStatus::Invalid;  // 空 id 或重複註冊：不覆蓋、不部分套用
    }
    entries_.push_back(id);
    return SwitchStatus::Ok;
}

SwitchStatus SurfaceSwitcher::unregister_surface(const ds::kernel::SurfaceId& id) {
    auto it = find(id);
    if (it == entries_.end()) {
        return SwitchStatus::NotFound;  // 未知 id：不崩潰
    }
    entries_.erase(it);
    if (has_current_ && current_ == id) {
        // 移除的正是目前 surface：回到「尚無目前 surface」；不觸發 on_switch（保留給 switch_to）。
        current_.clear();
        has_current_ = false;
    }
    return SwitchStatus::Ok;
}

// --- 查詢 / 列舉 ---
bool SurfaceSwitcher::has(const ds::kernel::SurfaceId& id) const {
    return find(id) != entries_.end();
}

std::vector<ds::kernel::SurfaceId> SurfaceSwitcher::list() const {
    return entries_;  // 複本：依註冊順序的具名清單，不外露任何內部數字索引語意
}

// --- 切換 ---
SwitchStatus SurfaceSwitcher::switch_to(const ds::kernel::SurfaceId& id) {
    if (find(id) == entries_.end()) {
        return SwitchStatus::NotFound;  // 未註冊：不變更目前狀態、不觸發通知
    }
    const ds::kernel::SurfaceId from = has_current_ ? current_ : ds::kernel::SurfaceId{};
    current_ = id;
    has_current_ = true;
    for (const auto& listener : listeners_) {
        if (listener) {
            listener(from, current_);
        }
    }
    return SwitchStatus::Ok;
}

// --- 切換通知 ---
void SurfaceSwitcher::on_switch(SwitchListener listener) {
    if (listener) {
        listeners_.push_back(std::move(listener));
    }
}

}  // namespace ds::render
