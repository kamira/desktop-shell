// E1-02 輸入策略四態 — 實作
//
// 純記憶體語意；不含任何平台分支或真實 OS API，任何平台可編譯執行。
#include "input_strategy.hpp"

namespace ds::kernel {

// --- 純函式：四態 → 行為 / 對映 ---

InputHitResult hit_result(InputStrategy s) noexcept {
    switch (s) {
        case InputStrategy::ClickThrough:
            return InputHitResult::Transparent;  // 唯一穿透態
        case InputStrategy::Interactive:
        case InputStrategy::Capture:
        case InputStrategy::Inert:
            return InputHitResult::Solid;
    }
    return InputHitResult::Solid;  // 防禦：不可達
}

RouteDecision route_decision(InputStrategy s) noexcept {
    switch (s) {
        case InputStrategy::Interactive:
            return RouteDecision::Deliver;
        case InputStrategy::Capture:
            return RouteDecision::CaptureAll;
        case InputStrategy::ClickThrough:
            return RouteDecision::PassBelow;
        case InputStrategy::Inert:
            return RouteDecision::Swallow;
    }
    return RouteDecision::Deliver;  // 防禦：不可達
}

InputPolicy to_backend_policy(InputStrategy s) noexcept {
    switch (s) {
        case InputStrategy::Interactive:
            return InputPolicy::Accepting;
        case InputStrategy::Capture:
            return InputPolicy::Modal;
        case InputStrategy::ClickThrough:
            return InputPolicy::PassThrough;
        case InputStrategy::Inert:
            // 命中實心 → 對齊 Accepting；「吞掉不處理」由 Controller 路由層補足。
            return InputPolicy::Accepting;
    }
    return InputPolicy::Accepting;  // 防禦：不可達
}

const char* to_string(InputStrategy s) noexcept {
    switch (s) {
        case InputStrategy::Interactive:
            return "Interactive";
        case InputStrategy::Capture:
            return "Capture";
        case InputStrategy::ClickThrough:
            return "ClickThrough";
        case InputStrategy::Inert:
            return "Inert";
    }
    return "unknown";
}

const CapabilityId& capture_capability_id() {
    static const CapabilityId kId = "input.capture";
    return kId;
}

// ---------------------------------------------------------------------------
// InputStrategyController
// ---------------------------------------------------------------------------

InputStrategyController::InputStrategyController(KernelBackend& backend) noexcept
    : backend_(backend) {}

std::pair<SurfaceId, InputStrategy>* InputStrategyController::find(
    const SurfaceId& id) {
    for (auto& e : stack_) {
        if (e.first == id) {
            return &e;
        }
    }
    return nullptr;
}

const std::pair<SurfaceId, InputStrategy>* InputStrategyController::find(
    const SurfaceId& id) const {
    for (const auto& e : stack_) {
        if (e.first == id) {
            return &e;
        }
    }
    return nullptr;
}

bool InputStrategyController::set_strategy(const SurfaceId& id,
                                           InputStrategy strategy) {
    // surface 須存在於後端（保守）。
    if (!backend_.has_surface(id)) {
        return false;
    }
    // NFR-03：Capture（全域獨占）需能力閘控通過，否則不改任何狀態。
    if (strategy == InputStrategy::Capture &&
        !backend_.has(capture_capability_id())) {
        return false;
    }
    // 下推至 E1-24 K3（對齊三態）；後端拒絕（理應不會，已驗 has_surface）則不改記錄。
    if (!backend_.set_input_policy(id, to_backend_policy(strategy))) {
        return false;
    }
    // 記錄：首次登記入堆疊（back=最上層），再次設定就地更新保留原位。
    if (auto* e = find(id)) {
        e->second = strategy;
    } else {
        stack_.emplace_back(id, strategy);
    }
    return true;
}

InputStrategy InputStrategyController::strategy(const SurfaceId& id) const {
    const auto* e = find(id);
    return e != nullptr ? e->second : kDefaultStrategy;
}

bool InputStrategyController::has_strategy(const SurfaceId& id) const {
    return find(id) != nullptr;
}

bool InputStrategyController::forget(const SurfaceId& id) {
    for (auto it = stack_.begin(); it != stack_.end(); ++it) {
        if (it->first == id) {
            stack_.erase(it);
            return true;
        }
    }
    return false;
}

bool InputStrategyController::capture_active(SurfaceId* who) const {
    for (const auto& e : stack_) {
        if (e.second == InputStrategy::Capture) {
            if (who != nullptr) {
                *who = e.first;
            }
            return true;
        }
    }
    return false;
}

InputStrategyController::Routed InputStrategyController::resolve_one(
    const InputEvent& ev) const {
    Routed r;
    r.event = ev;
    r.delivered_to.clear();
    r.decision = RouteDecision::PassBelow;

    // 找 target 於堆疊之位置；不在堆疊（含空 target）→ 無人接收、視為落出。
    std::size_t idx = stack_.size();
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        if (stack_[i].first == ev.target) {
            idx = i;
            break;
        }
    }
    if (idx == stack_.size()) {
        return r;  // target 不在堆疊：PassBelow、空遞送。
    }

    // 自 target 向下（index 遞減，往堆疊底）逐層解析。
    for (std::size_t j = idx + 1; j-- > 0;) {
        switch (stack_[j].second) {
            case InputStrategy::Interactive:
                r.delivered_to = stack_[j].first;
                r.decision = RouteDecision::Deliver;
                return r;
            case InputStrategy::Inert:
                r.delivered_to.clear();
                r.decision = RouteDecision::Swallow;
                return r;
            case InputStrategy::ClickThrough:
                continue;  // 穿透，續往下一層。
            case InputStrategy::Capture:
                // 理論上已由 route() 的全域 capture 前處理攔截；防禦性遞送捕捉者。
                r.delivered_to = stack_[j].first;
                r.decision = RouteDecision::CaptureAll;
                return r;
        }
    }
    // 落出堆疊底：無人接收。
    r.delivered_to.clear();
    r.decision = RouteDecision::PassBelow;
    return r;
}

std::vector<InputStrategyController::Routed> InputStrategyController::route(
    const std::vector<InputEvent>& events) const {
    std::vector<Routed> out;
    out.reserve(events.size());

    // 全域獨占前處理：任一 surface 處於 Capture → 所有事件強制導向捕捉者。
    SurfaceId captor;
    const bool captured = capture_active(&captor);

    for (const auto& ev : events) {
        if (captured) {
            Routed r;
            r.event = ev;
            r.delivered_to = captor;
            r.decision = RouteDecision::CaptureAll;
            out.push_back(std::move(r));
        } else {
            out.push_back(resolve_one(ev));
        }
    }
    return out;
}

std::vector<InputStrategyController::Routed>
InputStrategyController::poll_and_route() {
    return route(backend_.poll_input());
}

}  // namespace ds::kernel
