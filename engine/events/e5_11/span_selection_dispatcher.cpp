// E5-11 可點區段選取事件 — 實作
//
// 純橋接邏輯：內部持有一個 E5-01 `MouseEventRouter` 完全委派 surface 層級的命中 / 多擊 /
// 訂閱派發；本單元新增的狀態只有「具名 surface → E4-12 `ClickableTextElement` 指標（不擁有）」
// 對照表。`MouseAction::Click` 事件命中某具名 surface 時，若該 surface 已綁定可點文字區段
// 來源，就以事件本地座標對該元件做 `hit_span()` 查詢；命中則組成 `SpanSelectionEvent` 分派給
// 訂閱者，未命中（或未綁定 / 動作非 Click）則不分派。此檔不含任何平台分支或真實滑鼠後端。
#include "span_selection_dispatcher.hpp"

#include <optional>
#include <set>
#include <string>

namespace ds::events {

void SpanSelectionDispatcher::set_surfaces(std::vector<ds::kernel::HitSurface> surfaces) {
    // 先取得具名 id 清單（去重），再把 surfaces 移交給內部 router——resubscribe 只需要 id，
    // 不需要保留 surfaces 本身（router 已持有一份）。
    resubscribe_router(surfaces);
    router_.set_surfaces(std::move(surfaces));
}

void SpanSelectionDispatcher::resubscribe_router(
    const std::vector<ds::kernel::HitSurface>& surfaces) {
    // 清理先前對 router 的全部內部訂閱（整組取代語意，比照 E5-01 `set_surfaces` 本身）。
    for (const auto id : router_subscriptions_) {
        router_.unsubscribe(id);
    }
    router_subscriptions_.clear();

    // 對新清單裡每個具名 surface（去重，避免重複訂閱造成 bridge() 被同一事件呼叫多次）各訂閱
    // 一次 bridge()。空具名一律略過（無效 target，router 的 subscribe 亦會拒絕）。
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

void SpanSelectionDispatcher::bind_span_source(const ds::kernel::SurfaceId& surface,
                                               const ds::elements::ClickableTextElement& element) {
    if (surface.empty()) {
        return;  // 無效具名：不設定（no-op）
    }
    elements_[surface] = &element;
}

bool SpanSelectionDispatcher::has_span_source(const ds::kernel::SurfaceId& surface) const {
    return elements_.find(surface) != elements_.end();
}

bool SpanSelectionDispatcher::unbind_span_source(const ds::kernel::SurfaceId& surface) {
    return elements_.erase(surface) > 0;
}

SubscriptionId SpanSelectionDispatcher::subscribe(const ds::kernel::SurfaceId& surface,
                                                  SpanSelectionListener listener) {
    if (surface.empty() || !listener) {
        return 0;  // 無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::make_pair(surface, std::move(listener)));
    return id;
}

bool SpanSelectionDispatcher::unsubscribe(SubscriptionId id) { return listeners_.erase(id) > 0; }

std::size_t SpanSelectionDispatcher::listener_count(const ds::kernel::SurfaceId& surface) const {
    std::size_t count = 0;
    for (const auto& kv : listeners_) {
        if (kv.second.first == surface) {
            ++count;
        }
    }
    return count;
}

void SpanSelectionDispatcher::dispatch(const ds::kernel::SurfaceId& surface,
                                       const SpanSelectionEvent& event) {
    if (surface.empty()) {
        return;
    }
    // 分派前取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
    // （與 E5-01 `MouseEventRouter::dispatch` 同慣例）。
    std::vector<SpanSelectionListener> snapshot;
    for (const auto& kv : listeners_) {
        if (kv.second.first == surface) {
            snapshot.push_back(kv.second.second);
        }
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

void SpanSelectionDispatcher::bridge(const MouseButtonEvent& event) {
    // 只有「點擊」（Click）代表使用者完成一次選取意圖；Down/Up 屬按鍵狀態變化，不判定區段。
    if (event.action != MouseAction::Click) {
        return;
    }

    const auto it = elements_.find(event.target);
    if (it == elements_.end()) {
        return;  // 該 surface 未綁定可點文字區段來源：不判定，不分派
    }

    const std::optional<std::string> span_id = it->second->hit_span(event.position);
    if (!span_id.has_value()) {
        return;  // 無區段命中的點擊不發選取事件（規格明定，不悄悄發空 id 事件）
    }

    SpanSelectionEvent selection;
    selection.mouse = event;
    selection.span_id = *span_id;
    dispatch(event.target, selection);
}

RouteStatus SpanSelectionDispatcher::inject_button(MouseButton button, MouseAction action,
                                                    const ds::kernel::LocalPoint& position) {
    return router_.inject_button(button, action, position);
}

RouteStatus SpanSelectionDispatcher::inject_down(MouseButton button,
                                                 const ds::kernel::LocalPoint& position) {
    return router_.inject_down(button, position);
}

RouteStatus SpanSelectionDispatcher::inject_up(MouseButton button,
                                               const ds::kernel::LocalPoint& position) {
    return router_.inject_up(button, position);
}

RouteStatus SpanSelectionDispatcher::inject_click(MouseButton button,
                                                  const ds::kernel::LocalPoint& position) {
    return router_.inject_click(button, position);
}

}  // namespace ds::events
