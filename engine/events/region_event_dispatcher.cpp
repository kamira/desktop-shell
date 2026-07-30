// E5-14 碰撞區域名事件參數 — 實作
//
// 純橋接邏輯：內部持有一個 E5-01 `MouseEventRouter` 完全委派 surface 層級的命中 / 多擊 /
// 訂閱派發；本單元新增的狀態只有「具名 surface → E1-05 `NamedRegionMap`」對照表。事件命中
// 某具名 surface 時，若該 surface 有登記子區域集合，就以事件位置做子區域查詢，把結果併入
// `RegionEvent` 再分派給訂閱者；未登記 / 未命中子區域則正常派發（不附加區域參數）。
// 此檔不含任何平台分支或真實滑鼠後端。
#include "region_event_dispatcher.hpp"

#include <algorithm>
#include <set>

namespace ds::events {

void RegionEventDispatcher::set_surfaces(std::vector<ds::kernel::HitSurface> surfaces) {
    // 先取得具名 id 清單（去重），再把 surfaces 移交給內部 router——resubscribe 只需要
    // id，不需要保留 surfaces 本身（router 已持有一份）。
    resubscribe_router(surfaces);
    router_.set_surfaces(std::move(surfaces));
}

void RegionEventDispatcher::resubscribe_router(const std::vector<ds::kernel::HitSurface>& surfaces) {
    // 清理先前對 router 的全部內部訂閱（整組取代語意，比照 E5-01 `set_surfaces` 本身）。
    for (const auto id : router_subscriptions_) {
        router_.unsubscribe(id);
    }
    router_subscriptions_.clear();

    // 對新清單裡每個具名 surface（去重，避免重複訂閱造成 bridge() 被同一事件呼叫多次）
    // 各訂閱一次 bridge()。空具名一律略過（無效 target，router 的 subscribe 亦會拒絕）。
    std::set<ds::kernel::SurfaceId> seen;
    for (const auto& surface : surfaces) {
        if (surface.id.empty() || seen.count(surface.id) != 0) {
            continue;
        }
        seen.insert(surface.id);
        const SubscriptionId sub =
            router_.subscribe(surface.id, [this](const MouseButtonEvent& event) { bridge(event); });
        if (sub != 0) {
            router_subscriptions_.push_back(sub);
        }
    }
}

void RegionEventDispatcher::set_regions(const ds::kernel::SurfaceId& surface,
                                        ds::kernel::NamedRegionMap regions) {
    if (surface.empty()) {
        return;  // 無效具名：不設定（no-op）
    }
    regions_[surface] = std::move(regions);
}

bool RegionEventDispatcher::has_regions(const ds::kernel::SurfaceId& surface) const {
    return regions_.find(surface) != regions_.end();
}

bool RegionEventDispatcher::remove_regions(const ds::kernel::SurfaceId& surface) {
    return regions_.erase(surface) > 0;
}

SubscriptionId RegionEventDispatcher::subscribe(const ds::kernel::SurfaceId& surface,
                                                RegionEventListener listener) {
    if (surface.empty() || !listener) {
        return 0;  // 無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::make_pair(surface, std::move(listener)));
    return id;
}

bool RegionEventDispatcher::unsubscribe(SubscriptionId id) {
    return listeners_.erase(id) > 0;
}

std::size_t RegionEventDispatcher::listener_count(const ds::kernel::SurfaceId& surface) const {
    std::size_t count = 0;
    for (const auto& kv : listeners_) {
        if (kv.second.first == surface) {
            ++count;
        }
    }
    return count;
}

void RegionEventDispatcher::dispatch(const ds::kernel::SurfaceId& surface,
                                     const RegionEvent& event) {
    if (surface.empty()) {
        return;
    }
    // 分派前取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
    // （與 E5-01 `MouseEventRouter::dispatch` 同慣例）。
    std::vector<RegionEventListener> snapshot;
    for (const auto& kv : listeners_) {
        if (kv.second.first == surface) {
            snapshot.push_back(kv.second.second);
        }
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

void RegionEventDispatcher::bridge(const MouseButtonEvent& event) {
    RegionEvent region_event;
    region_event.mouse = event;
    // 預設（無登記子區域 / 未命中）：region_hit=false、region_name 空、region_params 空——
    // 事件仍正常派發，只是不附加區域參數。

    const auto it = regions_.find(event.target);
    if (it != regions_.end()) {
        const ds::kernel::RegionHit hit = it->second.hit(event.position);
        // hit.status == Invalid（查詢點非有限值）理論不可達：event.position 已由上游 E1-04
        // /（經 E5-01）topmost_hit() 判定過有效才會走到這裡；仍防禦性地視同「無區域資訊」，
        // 不崩潰、不悄悄假造一個命中結果。
        if (hit.status == ds::kernel::HitStatus::Ok && hit.hit) {
            region_event.region_hit = true;
            region_event.region_name = hit.name;
            region_event.region_params = hit.params;
        }
    }

    dispatch(event.target, region_event);
}

RouteStatus RegionEventDispatcher::inject_button(MouseButton button, MouseAction action,
                                                 const ds::kernel::LocalPoint& position) {
    return router_.inject_button(button, action, position);
}

RouteStatus RegionEventDispatcher::inject_down(MouseButton button,
                                               const ds::kernel::LocalPoint& position) {
    return router_.inject_down(button, position);
}

RouteStatus RegionEventDispatcher::inject_up(MouseButton button,
                                             const ds::kernel::LocalPoint& position) {
    return router_.inject_up(button, position);
}

RouteStatus RegionEventDispatcher::inject_click(MouseButton button,
                                                const ds::kernel::LocalPoint& position) {
    return router_.inject_click(button, position);
}

}  // namespace ds::events
