// E5-01 基本滑鼠按鍵事件 — 實作
//
// 純邏輯：以上游 E1-04 `HitTester` 做命中判定 + 多擊游標追蹤 + 訂閱 / 快照分派。
// 此檔不含任何平台分支或真實滑鼠後端。
#include "mouse_button_input.hpp"

#include <utility>

namespace ds::events {

void MouseEventRouter::set_surfaces(std::vector<ds::kernel::HitSurface> surfaces) {
    surfaces_ = std::move(surfaces);
}

SubscriptionId MouseEventRouter::subscribe(const ds::kernel::SurfaceId& target,
                                           MouseButtonListener listener) {
    if (target.empty() || !listener) {
        return 0;  // 無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::make_pair(target, std::move(listener)));
    return id;
}

bool MouseEventRouter::unsubscribe(SubscriptionId id) {
    return listeners_.erase(id) > 0;
}

std::size_t MouseEventRouter::listener_count(const ds::kernel::SurfaceId& target) const {
    std::size_t count = 0;
    for (const auto& kv : listeners_) {
        if (kv.second.first == target) {
            ++count;
        }
    }
    return count;
}

std::pair<RouteStatus, ds::kernel::SurfaceId> MouseEventRouter::resolve_hit(
    const ds::kernel::LocalPoint& position) const {
    const ds::kernel::TopmostHit hit = tester_.topmost_hit(position, surfaces_);
    if (hit.status != ds::kernel::HitStatus::Ok) {
        return {RouteStatus::Invalid, ds::kernel::SurfaceId{}};  // 報錯不靜默
    }
    if (!hit.hit) {
        return {RouteStatus::NoHit, ds::kernel::SurfaceId{}};
    }
    return {RouteStatus::Hit, hit.id};
}

void MouseEventRouter::dispatch(const ds::kernel::SurfaceId& target,
                                const MouseButtonEvent& event) {
    if (target.empty()) {
        return;  // 未命中：不分派給任何人
    }
    // 分派前取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB。
    std::vector<MouseButtonListener> snapshot;
    for (const auto& kv : listeners_) {
        if (kv.second.first == target) {
            snapshot.push_back(kv.second.second);
        }
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

RouteStatus MouseEventRouter::inject_button(MouseButton button, MouseAction action,
                                            const ds::kernel::LocalPoint& position) {
    const auto [status, target] = resolve_hit(position);

    if (action == MouseAction::Click) {
        // 直接注入的 Click：獨立於 Down/Up 多擊游標，click_count 固定為 1。
        if (status != RouteStatus::Hit) {
            return status;
        }
        MouseButtonEvent event{button, MouseAction::Click, position, 1, target};
        dispatch(target, event);
        return RouteStatus::Hit;
    }

    if (action == MouseAction::Down) {
        if (status != RouteStatus::Hit) {
            cursor_ = ClickCursor{};  // 未命中 / 無效：中斷任何進行中的多擊序列
            return status;
        }
        const bool continues = cursor_.active && cursor_.button == button &&
                                cursor_.target == target;
        if (continues) {
            ++cursor_.count;
        } else {
            cursor_ = ClickCursor{true, button, target, 1};
        }
        MouseButtonEvent event{button, MouseAction::Down, position, cursor_.count, target};
        dispatch(target, event);
        return RouteStatus::Hit;
    }

    // action == MouseAction::Up
    if (status != RouteStatus::Hit) {
        cursor_ = ClickCursor{};  // 未命中 / 無效：中斷任何進行中的多擊序列
        return status;
    }
    const bool matches_cursor =
        cursor_.active && cursor_.button == button && cursor_.target == target;
    const std::size_t click_count = matches_cursor ? cursor_.count : 1;

    MouseButtonEvent up_event{button, MouseAction::Up, position, click_count, target};
    dispatch(target, up_event);

    if (matches_cursor) {
        // Up 與游標相符：合成並分派 Click；游標保持啟用，讓後續同鍵同 surface 的 Down
        // 可繼續遞增（構成雙擊 / 三擊）。
        MouseButtonEvent click_event{button, MouseAction::Click, position, click_count, target};
        dispatch(target, click_event);
    } else {
        // 孤立的 Up（未有相符 Down 游標）：不合成 Click，且重置游標中斷序列。
        cursor_ = ClickCursor{};
    }
    return RouteStatus::Hit;
}

RouteStatus MouseEventRouter::inject_down(MouseButton button,
                                          const ds::kernel::LocalPoint& position) {
    return inject_button(button, MouseAction::Down, position);
}

RouteStatus MouseEventRouter::inject_up(MouseButton button,
                                        const ds::kernel::LocalPoint& position) {
    return inject_button(button, MouseAction::Up, position);
}

RouteStatus MouseEventRouter::inject_click(MouseButton button,
                                           const ds::kernel::LocalPoint& position) {
    inject_button(button, MouseAction::Down, position);
    return inject_button(button, MouseAction::Up, position);
}

}  // namespace ds::events
