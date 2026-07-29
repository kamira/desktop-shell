// E5-02 懸停進出事件 — 實作。無任何平台分支，命中判定完全委由上游 E1-04 HitTester。
#include "hover_tracker.hpp"

#include <algorithm>
#include <utility>

namespace ds::events {

SubscriptionId HoverTracker::subscribe(HoverListener listener) {
    if (!listener) {
        return 0;  // 空 listener 拒收
    }
    const SubscriptionId id = next_id_++;
    listeners_[id] = std::move(listener);
    return id;
}

bool HoverTracker::unsubscribe(SubscriptionId id) {
    return listeners_.erase(id) > 0;
}

bool HoverTracker::add_surface(ds::kernel::HitSurface surface) {
    for (auto& existing : surfaces_) {
        if (existing.id == surface.id) {
            existing = std::move(surface);
            return false;  // 覆蓋既有
        }
    }
    surfaces_.push_back(std::move(surface));
    return true;  // 新增
}

bool HoverTracker::remove_surface(const ds::kernel::SurfaceId& id) {
    const auto it = std::find_if(
        surfaces_.begin(), surfaces_.end(),
        [&id](const ds::kernel::HitSurface& s) { return s.id == id; });
    if (it == surfaces_.end()) {
        return false;  // 未知 id
    }
    surfaces_.erase(it);
    if (has_hover_ && hovered_ == id) {
        // 移除的正是目前懸停中的 surface：清除內部狀態。不合成 Leave 事件——
        // 這不是一次滑鼠移動，沒有真實注入座標可供該事件使用。
        has_hover_ = false;
        hovered_ = ds::kernel::SurfaceId{};
    }
    return true;
}

bool HoverTracker::inject_move(const ds::kernel::LocalPoint& point) {
    const ds::kernel::TopmostHit hit = tester_.topmost_hit(point, surfaces_);
    if (hit.status == ds::kernel::HitStatus::Invalid) {
        return false;  // 報錯不靜默：不改狀態、不分派
    }

    if (hit.hit) {
        if (has_hover_ && hovered_ == hit.id) {
            // 同一 surface 內移動：不重發 Enter，改發 Move。
            dispatch(HoverEvent{HoverEventKind::Move, hovered_, point});
        } else {
            if (has_hover_) {
                // 跨 surface 移動：先 Leave 舊。
                dispatch(HoverEvent{HoverEventKind::Leave, hovered_, point});
            }
            hovered_ = hit.id;
            has_hover_ = true;
            dispatch(HoverEvent{HoverEventKind::Enter, hovered_, point});
        }
    } else if (has_hover_) {
        // 移出至無命中處。
        dispatch(HoverEvent{HoverEventKind::Leave, hovered_, point});
        has_hover_ = false;
        hovered_ = ds::kernel::SurfaceId{};
    }
    // 先前本就無懸停、本次亦無命中：no-op，不分派任何事件（無命中處理）。

    return true;
}

bool HoverTracker::current_hover(ds::kernel::SurfaceId& out) const {
    if (!has_hover_) {
        return false;
    }
    out = hovered_;
    return true;
}

void HoverTracker::dispatch(const HoverEvent& event) const {
    // 分派前先取快照：listener 於回呼中訂閱 / 解除訂閱不影響本輪、不破壞疊代。
    std::vector<HoverListener> snapshot;
    snapshot.reserve(listeners_.size());
    for (const auto& [id, listener] : listeners_) {
        (void)id;
        snapshot.push_back(listener);
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

}  // namespace ds::events
