// E5-03 生命週期事件 — 實作
//
// 純邏輯：固定狀態機約束合法轉換，轉換合法時分派事件。無任何平台分支。
#include "lifecycle_source.hpp"

#include <utility>
#include <vector>

namespace ds::events {

bool is_legal_transition(LifecyclePhase from, LifecyclePhase to) noexcept {
    if (from == to) {
        return false;  // 自我轉換：無實質轉換
    }
    switch (from) {
        case LifecyclePhase::Created:
            return to == LifecyclePhase::Shown ||
                   to == LifecyclePhase::Destroyed;
        case LifecyclePhase::Shown:
            return to == LifecyclePhase::Activated ||
                   to == LifecyclePhase::Deactivated ||
                   to == LifecyclePhase::Hidden ||
                   to == LifecyclePhase::Destroyed;
        case LifecyclePhase::Hidden:
            return to == LifecyclePhase::Shown ||
                   to == LifecyclePhase::Destroyed;
        case LifecyclePhase::Activated:
            return to == LifecyclePhase::Deactivated ||
                   to == LifecyclePhase::Hidden ||
                   to == LifecyclePhase::Destroyed;
        case LifecyclePhase::Deactivated:
            return to == LifecyclePhase::Activated ||
                   to == LifecyclePhase::Shown ||
                   to == LifecyclePhase::Hidden ||
                   to == LifecyclePhase::Destroyed;
        case LifecyclePhase::Destroyed:
            return false;  // 終端相位，無合法轉出
    }
    return false;
}

SubscriptionId LifecycleSource::subscribe(LifecycleListener listener) {
    if (!listener) {
        return 0;  // 空 listener：拒收
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::move(listener));
    return id;
}

bool LifecycleSource::unsubscribe(SubscriptionId id) {
    return listeners_.erase(id) > 0;
}

bool LifecycleSource::create(SurfaceId surface) {
    const auto result = phases_.emplace(surface, LifecyclePhase::Created);
    return result.second;  // 已存在則 emplace 失敗，回 false
}

bool LifecycleSource::transition(SurfaceId surface, LifecyclePhase to) {
    const auto it = phases_.find(surface);
    if (it == phases_.end()) {
        return false;  // 未登記的 surface
    }
    const LifecyclePhase from = it->second;
    if (!is_legal_transition(from, to)) {
        return false;  // 非法轉換：不改狀態、不發事件
    }
    it->second = to;

    // 分派前取快照：listener 於回呼中訂閱 / 解除訂閱不影響本輪、避免疊代中改容器的 UB。
    std::vector<LifecycleListener> snapshot;
    snapshot.reserve(listeners_.size());
    for (const auto& kv : listeners_) {
        snapshot.push_back(kv.second);
    }

    const LifecycleEvent event{surface, from, to};
    for (const auto& listener : snapshot) {
        listener(event);
    }
    return true;
}

bool LifecycleSource::phase_of(SurfaceId surface, LifecyclePhase& out) const {
    const auto it = phases_.find(surface);
    if (it == phases_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool LifecycleSource::has_surface(SurfaceId surface) const {
    return phases_.find(surface) != phases_.end();
}

}  // namespace ds::events
