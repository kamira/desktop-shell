// E5-06 滾輪事件 — 實作
//
// 純邏輯：以上游 E1-04 `HitTester` 做命中判定 + 訂閱 / 快照分派。
// 此檔不含任何平台分支或真實滑鼠 / 滾輪硬體後端。
#include "wheel_event_input.hpp"

#include <utility>

namespace ds::events {

void WheelEventRouter::set_surfaces(std::vector<ds::kernel::HitSurface> surfaces) {
    surfaces_ = std::move(surfaces);
}

SubscriptionId WheelEventRouter::subscribe(const ds::kernel::SurfaceId& target,
                                           WheelEventListener listener) {
    if (target.empty() || !listener) {
        return 0;  // 無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::make_pair(target, std::move(listener)));
    return id;
}

bool WheelEventRouter::unsubscribe(SubscriptionId id) {
    return listeners_.erase(id) > 0;
}

std::size_t WheelEventRouter::listener_count(const ds::kernel::SurfaceId& target) const {
    std::size_t count = 0;
    for (const auto& kv : listeners_) {
        if (kv.second.first == target) {
            ++count;
        }
    }
    return count;
}

std::pair<WheelRouteStatus, ds::kernel::SurfaceId> WheelEventRouter::resolve_hit(
    const ds::kernel::LocalPoint& position) const {
    const ds::kernel::TopmostHit hit = tester_.topmost_hit(position, surfaces_);
    if (hit.status != ds::kernel::HitStatus::Ok) {
        return {WheelRouteStatus::Invalid, ds::kernel::SurfaceId{}};  // 報錯不靜默
    }
    if (!hit.hit) {
        return {WheelRouteStatus::NoHit, ds::kernel::SurfaceId{}};
    }
    return {WheelRouteStatus::Hit, hit.id};
}

void WheelEventRouter::dispatch(const ds::kernel::SurfaceId& target,
                                const WheelEvent& event) {
    if (target.empty()) {
        return;  // 未命中：不分派給任何人
    }
    // 分派前取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB。
    std::vector<WheelEventListener> snapshot;
    for (const auto& kv : listeners_) {
        if (kv.second.first == target) {
            snapshot.push_back(kv.second.second);
        }
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

WheelRouteStatus WheelEventRouter::inject_wheel(float delta_x, float delta_y,
                                                const ds::kernel::LocalPoint& position) {
    const auto [status, target] = resolve_hit(position);
    if (status != WheelRouteStatus::Hit) {
        return status;  // NoHit / Invalid：不分派給任何人
    }
    const WheelEvent event{delta_x, delta_y, position, target};
    dispatch(target, event);
    return WheelRouteStatus::Hit;
}

}  // namespace ds::events
