// E4-25 背景模式 — 渲染描述模型實作（見 background_element.hpp 規格）。
#include "background_element.hpp"

#include <cmath>  // std::isfinite

namespace ds::elements {

namespace {

// 把單一顏色分量 clamp 至 [0,1]（呼叫前已保證為有限值）。
float clamp_unit(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// 顏色是否合法：四分量皆須有限（不接受 NaN / Inf）。合法值後續一律 clamp 至 [0,1]。
bool finite_color(const Color& c) {
    return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
}

Color clamp_color(const Color& c) {
    return Color{clamp_unit(c.r), clamp_unit(c.g), clamp_unit(c.b), clamp_unit(c.a)};
}

// 把不透明度 clamp 至 [0,1]（呼叫前已保證為有限值）。
float clamp_opacity(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// 模式
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_mode(BackgroundMode mode) noexcept {
    if (!is_valid_background_mode(mode)) {
        return BackgroundStatus::Invalid;  // 未知 / 越界列舉值：不套用，模式維持原值
    }
    mode_ = mode;
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 純色
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_color(const Color& color) noexcept {
    if (!finite_color(color)) {
        return BackgroundStatus::Invalid;  // 非有限分量：不靜默改值，不套用
    }
    color_ = clamp_color(color);
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 漸層（可選）
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::add_gradient_stop(const GradientStop& stop) {
    if (!std::isfinite(stop.position) || stop.position < 0.0 || stop.position > 1.0) {
        return BackgroundStatus::Invalid;  // 越界 / 非有限比例：不新增
    }
    if (!finite_color(stop.color)) {
        return BackgroundStatus::Invalid;  // 顏色非有限：不新增
    }
    GradientStop normalized = stop;
    normalized.color = clamp_color(stop.color);
    gradient_stops_.push_back(normalized);
    return BackgroundStatus::Ok;
}

BackgroundStatus BackgroundElement::set_gradient_direction(GradientDirection direction) noexcept {
    if (!is_valid_gradient_direction(direction)) {
        return BackgroundStatus::Invalid;  // 未知 / 越界列舉值：不套用
    }
    gradient_direction_ = direction;
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 圖片（重用 E4-02，不重造圖片邏輯）
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_image(const ImageSource& source) {
    const ImageStatus status = image_.set_source(source);
    // E4-02 的 Ok/Invalid 語意與本單元一致：直接透傳，不重新詮釋。
    return status == ImageStatus::Ok ? BackgroundStatus::Ok : BackgroundStatus::Invalid;
}

// ---------------------------------------------------------------------------
// 模糊
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_blur_radius(double radius) noexcept {
    if (!std::isfinite(radius) || radius < 0.0) {
        return BackgroundStatus::Invalid;  // 非有限 / 負值：不套用
    }
    blur_radius_ = radius;
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 透明度
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_opacity(float opacity) noexcept {
    if (!std::isfinite(opacity)) {
        return BackgroundStatus::Invalid;  // NaN / Inf：不靜默改成預設值
    }
    alpha_.opacity = clamp_opacity(opacity);
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 圓角
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_corner_radius(double radius) noexcept {
    if (!std::isfinite(radius) || radius < 0.0) {
        return BackgroundStatus::Invalid;  // 非有限 / 負值：不套用
    }
    corner_radius_ = radius;
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 邊框
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_border(const BorderStyle& border) noexcept {
    if (!std::isfinite(border.width) || border.width < 0.0) {
        return BackgroundStatus::Invalid;  // 非有限 / 負寬度：不套用
    }
    if (!finite_color(border.color)) {
        return BackgroundStatus::Invalid;  // 顏色非有限：不套用
    }
    border_.width = border.width;
    border_.color = clamp_color(border.color);
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 目標具名 surface
// ---------------------------------------------------------------------------

BackgroundStatus BackgroundElement::set_target(const ds::kernel::SurfaceId& target) {
    if (target.empty()) {
        return BackgroundStatus::Invalid;  // 空目標名不套用（NFR-02：必須具名）
    }
    target_ = target;
    return BackgroundStatus::Ok;
}

// ---------------------------------------------------------------------------
// 渲染描述
// ---------------------------------------------------------------------------

BackgroundRenderModel BackgroundElement::render_model() const {
    BackgroundRenderModel model;
    model.mode = mode_;
    model.color = color_;
    model.gradient_stops = gradient_stops_;
    model.gradient_direction = gradient_direction_;
    model.image = image_.render_model();
    model.blur_radius = blur_radius_;
    model.corner_radius = corner_radius_;
    model.border = border_;
    model.alpha = alpha_;
    model.target = target_;
    return model;
}

}  // namespace ds::elements
