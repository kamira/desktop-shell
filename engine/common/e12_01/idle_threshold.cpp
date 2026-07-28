// E12-01 idle 記憶體與 CPU 門檻 — 實作（engine 層 / 平台中立純邏輯）
//
// 無 `#ifdef`、無平台分支、無真實資源 API：門檻判斷全為百分比純數，讀值由呼叫端注入。
#include "idle_threshold.hpp"

namespace ds::common {

// ---------------------------------------------------------------------------
// 自由函式
// ---------------------------------------------------------------------------
const char* to_string(ActivityLevel level) noexcept {
    switch (level) {
        case ActivityLevel::Active: return "active";
        case ActivityLevel::Idle:   return "idle";
    }
    return "unknown";  // 不可達；防禦性
}

ds::metrics::SamplingTier lower_tier(ds::metrics::SamplingTier tier) noexcept {
    using ds::metrics::SamplingTier;
    switch (tier) {
        case SamplingTier::High:     return SamplingTier::Normal;
        case SamplingTier::Normal:   return SamplingTier::Low;
        case SamplingTier::Low:      return SamplingTier::Low;       // 夾底：仍週期，不完全停採
        case SamplingTier::OnDemand: return SamplingTier::OnDemand;  // 本就非週期，維持
    }
    return tier;  // 不可達
}

namespace {
// 夾值到 [lo, hi]（本層自足，不引 <algorithm>；純數運算）。
double clamp(double v, double lo, double hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
}  // namespace

// ---------------------------------------------------------------------------
// IdleThresholdPolicy
// ---------------------------------------------------------------------------
IdleThresholdPolicy::IdleThresholdPolicy() = default;

IdleThresholdPolicy IdleThresholdPolicy::defaults() { return IdleThresholdPolicy{}; }

IdleThresholdPolicy& IdleThresholdPolicy::set_thresholds(double cpu_pct, double mem_pct) noexcept {
    // 門檻設定驗證：夾到 [0, 100]，界外值不產生非法門檻。
    cpu_active_ = clamp(cpu_pct, 0.0, 100.0);
    mem_active_ = clamp(mem_pct, 0.0, 100.0);
    return *this;
}

IdleThresholdPolicy& IdleThresholdPolicy::set_hysteresis(double band_pct) noexcept {
    band_ = band_pct < 0.0 ? 0.0 : band_pct;  // 負帶寬無意義 → 0（退化為單門檻）
    return *this;
}

IdleThresholdPolicy& IdleThresholdPolicy::set_tier_mapping(
    ds::metrics::SamplingTier active_tier, ds::metrics::SamplingTier idle_tier) noexcept {
    active_tier_ = active_tier;
    idle_tier_ = idle_tier;
    return *this;
}

double IdleThresholdPolicy::cpu_idle_threshold() const noexcept {
    return clamp(cpu_active_ - band_, 0.0, cpu_active_);  // 下緣，夾到 [0, 上緣]
}

double IdleThresholdPolicy::mem_idle_threshold() const noexcept {
    return clamp(mem_active_ - band_, 0.0, mem_active_);
}

ActivityLevel IdleThresholdPolicy::evaluate(double current_cpu_pct,
                                            double current_mem_pct) noexcept {
    // OR 進：任一資源達活躍門檻即活躍（寧可誤判活躍不誤判閒置，護即時性）。
    const bool hits_active =
        current_cpu_pct >= cpu_active_ || current_mem_pct >= mem_active_;
    // AND 出：兩資源皆低於閒置門檻才閒置。
    const bool below_idle =
        current_cpu_pct < cpu_idle_threshold() && current_mem_pct < mem_idle_threshold();

    if (hits_active) {
        level_ = ActivityLevel::Active;
    } else if (below_idle) {
        level_ = ActivityLevel::Idle;
    }
    // 否則落在死區（介於兩緣）：維持現態 level_（遲滯防抖）。
    return level_;
}

ds::metrics::SamplingTier IdleThresholdPolicy::recommended_tier() const noexcept {
    return level_ == ActivityLevel::Idle ? idle_tier_ : active_tier_;
}

ds::metrics::SamplingTier IdleThresholdPolicy::adjust_tier(
    ds::metrics::SamplingTier base) const noexcept {
    // 閒置時把 tier 降級（與 E2-02 整合的核心）；活躍時原樣回。
    return level_ == ActivityLevel::Idle ? lower_tier(base) : base;
}

}  // namespace ds::common
