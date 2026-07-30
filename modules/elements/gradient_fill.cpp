// E4-21 漸層填色 — 渲染描述模型實作（見 gradient_fill.hpp 規格）。
#include "gradient_fill.hpp"

#include <algorithm>  // std::upper_bound
#include <cmath>      // std::isfinite, std::fmod

namespace ds::elements {

namespace {

// 分量 / 位置是否為有限值且落在 [0,1] 內。
bool in_unit_range(float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; }

// 逐分量線性內插 a → b，比例 t ∈ [0,1]。
Color lerp_color(const Color& a, const Color& b, float t) {
    return Color{
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

}  // namespace

GradientStatus GradientFill::add_stop(float position, const Color& color) {
    if (!in_unit_range(position)) return GradientStatus::InvalidPosition;
    if (!in_unit_range(color.r) || !in_unit_range(color.g) || !in_unit_range(color.b) ||
        !in_unit_range(color.a)) {
        return GradientStatus::InvalidColor;
    }

    GradientStop stop;
    stop.position = position;
    stop.color = color;

    // 依 position 由小到大插入（stable：同 position 依插入次序排在既有同值之後），
    // 維持 stops_ 恆為排序狀態，供 sample() / render_model() 直接使用。
    const auto it = std::upper_bound(
        stops_.begin(), stops_.end(), stop,
        [](const GradientStop& lhs, const GradientStop& rhs) { return lhs.position < rhs.position; });
    stops_.insert(it, stop);
    return GradientStatus::Ok;
}

GradientStatus GradientFill::set_angle(float degrees) {
    if (!std::isfinite(degrees)) return GradientStatus::InvalidAngle;
    // 正規化至 [0,360)：先對 360 取餘（結果與被除數同號），負值再加 360 折回正範圍。
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f) normalized += 360.0f;
    angle_degrees_ = normalized;
    return GradientStatus::Ok;
}

GradientStatus GradientFill::set_composite(const ds::kernel::AlphaProfile& composite) {
    if (!std::isfinite(composite.opacity)) return GradientStatus::InvalidComposite;
    composite_ = composite;
    // opacity 為比例，夾限至 [0,1]（越界輸入永遠安全，與上游 E1-03 clamp 語意一致）。
    if (composite_.opacity < 0.0f) composite_.opacity = 0.0f;
    else if (composite_.opacity > 1.0f) composite_.opacity = 1.0f;
    return GradientStatus::Ok;
}

Color GradientFill::sample(float t) const {
    if (stops_.empty()) return Color{0.0f, 0.0f, 0.0f, 0.0f};

    float clamped_t = t;
    if (!std::isfinite(clamped_t)) clamped_t = 0.0f;
    else if (clamped_t < 0.0f) clamped_t = 0.0f;
    else if (clamped_t > 1.0f) clamped_t = 1.0f;

    if (stops_.size() == 1) return stops_.front().color;
    if (clamped_t <= stops_.front().position) return stops_.front().color;
    if (clamped_t >= stops_.back().position) return stops_.back().color;

    // 找相鄰兩停靠點：第一個 position > t 者，其前一個即下界。
    const auto upper = std::upper_bound(
        stops_.begin(), stops_.end(), clamped_t,
        [](float value, const GradientStop& stop) { return value < stop.position; });
    const GradientStop& hi = *upper;
    const GradientStop& lo = *(upper - 1);

    const float span = hi.position - lo.position;
    if (span <= 0.0f) return lo.color;  // 同 position 的相鄰停靠點：避免除以零，取下界。
    const float local_t = (clamped_t - lo.position) / span;
    return lerp_color(lo.color, hi.color, local_t);
}

GradientRenderModel GradientFill::render_model() const {
    GradientRenderModel model;
    model.type = type_;
    model.angle_degrees = angle_degrees_;
    model.stops = stops_;  // 已恆為排序狀態。
    model.composite = composite_;
    return model;
}

}  // namespace ds::elements
