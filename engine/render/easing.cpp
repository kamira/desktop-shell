// E4-05 過場動畫與緩動 — 緩動曲線實作
//
// 全部為封閉形式的純函式。<cmath> 只用於正弦曲線的 std::cos；其餘為多項式。
#include "easing.hpp"

#include <cmath>

namespace ds::render {

namespace {
// π（不依賴 M_PI 這種非標準巨集，保持可攜）。
constexpr double kPi = 3.14159265358979323846;
}  // namespace

double Easing::clamp01(double t) noexcept {
    if (t < 0.0) return 0.0;
    if (t > 1.0) return 1.0;
    return t;
}

double Easing::linear(double t) noexcept {
    return clamp01(t);
}

double Easing::in_quad(double t) noexcept {
    t = clamp01(t);
    return t * t;
}

double Easing::out_quad(double t) noexcept {
    t = clamp01(t);
    return 1.0 - (1.0 - t) * (1.0 - t);
}

double Easing::in_out_quad(double t) noexcept {
    t = clamp01(t);
    if (t < 0.5) return 2.0 * t * t;
    const double u = -2.0 * t + 2.0;
    return 1.0 - (u * u) / 2.0;
}

double Easing::in_cubic(double t) noexcept {
    t = clamp01(t);
    return t * t * t;
}

double Easing::out_cubic(double t) noexcept {
    t = clamp01(t);
    const double u = 1.0 - t;
    return 1.0 - u * u * u;
}

double Easing::in_out_cubic(double t) noexcept {
    t = clamp01(t);
    if (t < 0.5) return 4.0 * t * t * t;
    const double u = -2.0 * t + 2.0;
    return 1.0 - (u * u * u) / 2.0;
}

double Easing::in_sine(double t) noexcept {
    t = clamp01(t);
    return 1.0 - std::cos((t * kPi) / 2.0);
}

double Easing::out_sine(double t) noexcept {
    t = clamp01(t);
    return std::sin((t * kPi) / 2.0);
}

double Easing::in_out_sine(double t) noexcept {
    t = clamp01(t);
    return -(std::cos(kPi * t) - 1.0) / 2.0;
}

double Easing::apply(EasingType type, double t) noexcept {
    switch (type) {
        case EasingType::Linear:     return linear(t);
        case EasingType::InQuad:     return in_quad(t);
        case EasingType::OutQuad:    return out_quad(t);
        case EasingType::InOutQuad:  return in_out_quad(t);
        case EasingType::InCubic:    return in_cubic(t);
        case EasingType::OutCubic:   return out_cubic(t);
        case EasingType::InOutCubic: return in_out_cubic(t);
        case EasingType::InSine:     return in_sine(t);
        case EasingType::OutSine:    return out_sine(t);
        case EasingType::InOutSine:  return in_out_sine(t);
    }
    // 未知列舉（防禦性）：回退等速，不丟例外。
    return linear(t);
}

}  // namespace ds::render
