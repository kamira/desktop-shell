// E4-05 過場動畫與緩動 — Tween 實作
#include "tween.hpp"

namespace ds::render {

Tween::Tween(double from, double to, double duration, EasingType easing) noexcept
    : from_(from), to_(to), duration_(duration), easing_(easing) {}

double Tween::progress() const noexcept {
    if (duration_ <= 0.0) return 1.0;
    return Easing::clamp01(elapsed_ / duration_);
}

double Tween::at(double time_seconds) const noexcept {
    // duration<=0：瞬間完成，任何非負時間皆已抵達終點。
    const double p = (duration_ <= 0.0)
                         ? 1.0
                         : Easing::clamp01(time_seconds / duration_);
    const double eased = Easing::apply(easing_, p);
    return from_ + (to_ - from_) * eased;
}

double Tween::advance(double dt_seconds) noexcept {
    if (dt_seconds > 0.0) {
        elapsed_ += dt_seconds;  // 經過時間單調不減；負 dt 視為 0（不回捲）。
    }
    return value();
}

bool Tween::done() const noexcept {
    return elapsed_ >= duration_;
}

}  // namespace ds::render
