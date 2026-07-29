// E4-19 旋轉元件 — 實作
//
// 純邏輯：以 E4-22 `Transform2D` 計算「繞具名 pivot 的旋轉矩陣」，委由 E4-02
// `ImageElement` 顯示旋轉對象。不含任何平台分支或真實繪圖後端；不新增絕對座標 / 數字
// z-order（NFR-02）。
#include "rotatable_element.hpp"

#include <cmath>  // std::isfinite, std::fmod

namespace ds::elements {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// 把角度正規化至 [0, 2π)（呼叫前已保證為有限值）。
float wrap_angle(float radians) {
    float wrapped = std::fmod(radians, kTwoPi);
    if (wrapped < 0.0f) {
        wrapped += kTwoPi;
    }
    return wrapped;
}

// 具名 pivot 換算為所綁來源固有尺寸的比例位置（NFR-02：具名輸入，內部才換算比例，不對外
// 暴露座標欄位）。
ds::render::Vec2 pivot_fraction(RotationPivot pivot) {
    switch (pivot) {
        case RotationPivot::Center:
            return ds::render::Vec2{0.5f, 0.5f};
        case RotationPivot::TopLeft:
            return ds::render::Vec2{0.0f, 0.0f};
        case RotationPivot::TopCenter:
            return ds::render::Vec2{0.5f, 0.0f};
        case RotationPivot::TopRight:
            return ds::render::Vec2{1.0f, 0.0f};
        case RotationPivot::CenterLeft:
            return ds::render::Vec2{0.0f, 0.5f};
        case RotationPivot::CenterRight:
            return ds::render::Vec2{1.0f, 0.5f};
        case RotationPivot::BottomLeft:
            return ds::render::Vec2{0.0f, 1.0f};
        case RotationPivot::BottomCenter:
            return ds::render::Vec2{0.5f, 1.0f};
        case RotationPivot::BottomRight:
            return ds::render::Vec2{1.0f, 1.0f};
    }
    return ds::render::Vec2{0.5f, 0.5f};  // 不可達：所有具名值皆已覆蓋，防禦性回退為 Center
}

}  // namespace

// ---------------------------------------------------------------------------
// 角度
// ---------------------------------------------------------------------------

RotateStatus RotatableElement::set_angle(float radians) {
    if (!std::isfinite(radians)) {
        return RotateStatus::Invalid;  // NaN / Inf：不靜默改成預設值，維持既有角度
    }
    angle_ = wrap_angle(radians);
    return RotateStatus::Ok;
}

RotateStatus RotatableElement::rotate_by(float delta_radians) {
    if (!std::isfinite(delta_radians)) {
        return RotateStatus::Invalid;  // 非有限疊加量：不套用，維持既有角度
    }
    angle_ = wrap_angle(angle_ + delta_radians);
    return RotateStatus::Ok;
}

// ---------------------------------------------------------------------------
// 連續旋轉：角速度 + advance
// ---------------------------------------------------------------------------

RotateStatus RotatableElement::set_angular_velocity(float radians_per_unit) {
    if (!std::isfinite(radians_per_unit)) {
        return RotateStatus::Invalid;  // 非有限角速度：不套用，維持既有角速度
    }
    angular_velocity_ = radians_per_unit;
    return RotateStatus::Ok;
}

void RotatableElement::advance(double dt) noexcept {
    if (angular_velocity_ == 0.0f) {
        return;  // 角速度為 0：安全 no-op
    }
    const double delta = static_cast<double>(angular_velocity_) * dt;
    if (!std::isfinite(delta)) {
        return;  // dt 非有限（NaN/Inf）：安全 no-op，不把非有限值寫入角度
    }
    angle_ = wrap_angle(angle_ + static_cast<float>(delta));
}

// ---------------------------------------------------------------------------
// 旋轉中心
// ---------------------------------------------------------------------------

RotateStatus RotatableElement::set_pivot(RotationPivot pivot) noexcept {
    pivot_ = pivot;
    return RotateStatus::Ok;  // 具名列舉恆合法
}

// ---------------------------------------------------------------------------
// 旋轉變形矩陣
// ---------------------------------------------------------------------------

ds::render::Transform2D RotatableElement::transform() const {
    const ImageDimensions dims = image_.source_dimensions();
    const ds::render::Vec2 frac = pivot_fraction(pivot_);
    const float px = frac.x * static_cast<float>(dims.width);
    const float py = frac.y * static_cast<float>(dims.height);

    // 繞任意點 (px,py) 旋轉：先移到以該點為原點，再旋轉，再移回。
    return ds::render::Transform2D::translate(px, py) *
           ds::render::Transform2D::rotate(angle_) *
           ds::render::Transform2D::translate(-px, -py);
}

// ---------------------------------------------------------------------------
// 渲染描述
// ---------------------------------------------------------------------------

RotatedRenderModel RotatableElement::render_model() const {
    RotatedRenderModel model;
    model.image = image_.render_model();
    model.transform = transform();
    model.angle_radians = angle_;
    model.pivot = pivot_;
    return model;
}

}  // namespace ds::elements
