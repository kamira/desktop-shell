// E1-15 視窗級透明度與懸停淡變 — 實作
//
// 純邏輯：不透明度線性插值 + E5-02 懸停事件驅動淡變起停 + 透過 E1-03 服務套用結果。
// 無平台分支、無真實 OS 時鐘（時間全注入式）。
#include "window_opacity.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::isfinite
#include <utility>    // std::move

namespace ds::kernel {

namespace {

// 正規化不透明度：非有限值視為無效（回 false，呼叫端不套用，NOT 靜默）；
// 有效則 clamp 至 [0,1]（NFR-02：透明度為比例）。
bool normalize_opacity(float in, float& out) {
    if (!std::isfinite(in)) {
        return false;
    }
    out = std::clamp(in, 0.0f, 1.0f);
    return true;
}

// 轉發 E1-03 AlphaStatus → 本單元 WindowOpacityStatus（同語意，不重新定義規則）。
WindowOpacityStatus from_alpha_status(AlphaStatus status) {
    switch (status) {
        case AlphaStatus::Ok:
            return WindowOpacityStatus::Ok;
        case AlphaStatus::Unsupported:
            return WindowOpacityStatus::Unsupported;
        case AlphaStatus::Invalid:
        default:
            return WindowOpacityStatus::Invalid;
    }
}

}  // namespace

WindowOpacity::WindowOpacity(AlphaSurfaceService& alpha_service, SurfaceId surface,
                             double fade_seconds)
    : alpha_service_(alpha_service),
      surface_(std::move(surface)),
      fade_seconds_(fade_seconds > 0.0 ? fade_seconds : 0.0) {}

WindowOpacityStatus WindowOpacity::apply(float opacity) {
    return from_alpha_status(alpha_service_.set_opacity(surface_, opacity));
}

void WindowOpacity::start_fade(float target) {
    fade_from_ = current_opacity_;
    fade_to_ = target;
    fade_elapsed_ = 0.0;
    if (fade_seconds_ <= 0.0) {
        // 瞬時切換：無過場，立即抵達目標。
        current_opacity_ = target;
        fade_from_ = target;
        fade_to_ = target;
    }
}

WindowOpacityStatus WindowOpacity::set_base_opacity(float opacity) {
    float normalized = 1.0f;
    if (!normalize_opacity(opacity, normalized)) {
        return WindowOpacityStatus::Invalid;  // 非有限值：報錯不靜默，不套用
    }
    base_opacity_ = normalized;
    if (!hovering_) {
        // 目前處於「未懸停」情境：這是直接的整體透明度設定（非動畫），立即生效並套用。
        current_opacity_ = normalized;
        fade_from_ = normalized;
        fade_to_ = normalized;
        fade_elapsed_ = fade_seconds_;
        return apply(normalized);
    }
    return WindowOpacityStatus::Ok;  // 懸停中：僅記錄，供之後淡出使用
}

WindowOpacityStatus WindowOpacity::set_hover_opacity(float opacity) {
    float normalized = 1.0f;
    if (!normalize_opacity(opacity, normalized)) {
        return WindowOpacityStatus::Invalid;  // 非有限值：報錯不靜默，不套用
    }
    hover_opacity_ = normalized;
    if (hovering_) {
        // 目前正懸停中：立即生效並套用。
        current_opacity_ = normalized;
        fade_from_ = normalized;
        fade_to_ = normalized;
        fade_elapsed_ = fade_seconds_;
        return apply(normalized);
    }
    return WindowOpacityStatus::Ok;  // 未懸停：僅記錄，供之後淡入使用
}

ds::events::SubscriptionId WindowOpacity::attach(ds::events::HoverTracker& hover) {
    return hover.subscribe(
        [this](const ds::events::HoverEvent& event) { on_hover_event(event); });
}

void WindowOpacity::on_hover_event(const ds::events::HoverEvent& event) {
    if (event.surface != surface_) {
        return;  // 非本 surface 的事件：忽略
    }
    switch (event.kind) {
        case ds::events::HoverEventKind::Enter:
            hovering_ = true;
            start_fade(hover_opacity_);  // 淡入：往懸停目標過場
            break;
        case ds::events::HoverEventKind::Leave:
            hovering_ = false;
            start_fade(base_opacity_);  // 淡出：往基準目標過場
            break;
        case ds::events::HoverEventKind::Move:
        default:
            break;  // 同一 surface 內移動：不影響淡變
    }
}

WindowOpacityStatus WindowOpacity::advance(double dt_seconds) {
    const double dt = dt_seconds > 0.0 ? dt_seconds : 0.0;
    fade_elapsed_ += dt;

    if (fade_seconds_ <= 0.0) {
        current_opacity_ = fade_to_;
    } else {
        const double progress = std::clamp(fade_elapsed_ / fade_seconds_, 0.0, 1.0);
        current_opacity_ = static_cast<float>(
            static_cast<double>(fade_from_) +
            (static_cast<double>(fade_to_) - static_cast<double>(fade_from_)) * progress);
    }

    return apply(current_opacity_);
}

}  // namespace ds::kernel
