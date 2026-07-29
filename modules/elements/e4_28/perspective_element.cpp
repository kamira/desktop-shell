// E4-28 透視與景深呈現 — 實作
//
// 純邏輯：以 E4-22 `Transform2D` 計算「依相對深度換算的透視變形矩陣」（縮放 + 朝具名消失
// 點方向的視差位移），並附可選的景深模糊量計算。不含任何平台分支或真實繪圖後端；不新增
// 絕對座標 / 數字 z-order（NFR-02）。
#include "perspective_element.hpp"

#include <cmath>  // std::isfinite, std::fabs

namespace ds::elements {

namespace {

// 角落方向正規化用的 1/sqrt(2)（使四個角落方向與上下左右方向同為單位長度，視差位移幅度
// 不因方向不同而失真）。
constexpr float kInvSqrt2 = 0.70710678118654752f;

// 具名消失點換算為單位方向向量（NFR-02：具名輸入，內部才換算方向，不對外暴露座標數字）。
ds::render::Vec2 vanishing_point_direction(VanishingPoint vanishing_point) {
    switch (vanishing_point) {
        case VanishingPoint::Center:
            return ds::render::Vec2{0.0f, 0.0f};
        case VanishingPoint::Top:
            return ds::render::Vec2{0.0f, -1.0f};
        case VanishingPoint::Bottom:
            return ds::render::Vec2{0.0f, 1.0f};
        case VanishingPoint::Left:
            return ds::render::Vec2{-1.0f, 0.0f};
        case VanishingPoint::Right:
            return ds::render::Vec2{1.0f, 0.0f};
        case VanishingPoint::TopLeft:
            return ds::render::Vec2{-kInvSqrt2, -kInvSqrt2};
        case VanishingPoint::TopRight:
            return ds::render::Vec2{kInvSqrt2, -kInvSqrt2};
        case VanishingPoint::BottomLeft:
            return ds::render::Vec2{-kInvSqrt2, kInvSqrt2};
        case VanishingPoint::BottomRight:
            return ds::render::Vec2{kInvSqrt2, kInvSqrt2};
    }
    return ds::render::Vec2{0.0f, 0.0f};  // 不可達：所有具名值皆已覆蓋，防禦性回退為 Center
}

}  // namespace

// ---------------------------------------------------------------------------
// 核心查詢：給定任意假設深度求透視變形（純函式，不改內部狀態）
// ---------------------------------------------------------------------------

PerspectiveTransformResult PerspectiveElement::transform_for_depth(float depth) const {
    if (!std::isfinite(depth)) {
        // 非有限深度：明確回報，不靜默套用任何值。
        return PerspectiveTransformResult{PerspectiveStatus::Invalid,
                                           ds::render::Transform2D::identity(), 0.0f};
    }

    const float denom = 1.0f + perspective_strength_ * depth;
    if (denom <= kMinPerspectiveDenominator) {
        // 深度已達或超越與消失點重合的臨界點（透視退化）：明確回報，不靜默。
        return PerspectiveTransformResult{PerspectiveStatus::Invalid,
                                           ds::render::Transform2D::identity(), 0.0f};
    }

    const float scale = 1.0f / denom;
    const ds::render::Vec2 dir = vanishing_point_direction(vanishing_point_);
    const float shrink = 1.0f - scale;  // >=0 當深度使物件縮小；<0 時視差方向隨之反轉
    const ds::render::Vec2 offset{dir.x * parallax_strength_ * shrink,
                                   dir.y * parallax_strength_ * shrink};

    // 先縮放，再依視差位移（Transform2D::compose：this ∘ rhs 先套用 rhs 再套用 this）。
    const ds::render::Transform2D transform =
        ds::render::Transform2D::translate(offset.x, offset.y) *
        ds::render::Transform2D::scale(scale, scale);

    return PerspectiveTransformResult{PerspectiveStatus::Ok, transform, scale};
}

// ---------------------------------------------------------------------------
// 深度
// ---------------------------------------------------------------------------

PerspectiveStatus PerspectiveElement::set_depth(float depth) {
    const PerspectiveTransformResult probe = transform_for_depth(depth);
    if (!probe.ok()) {
        return PerspectiveStatus::Invalid;  // 不套用，維持既有深度
    }
    depth_ = depth;
    return PerspectiveStatus::Ok;
}

// ---------------------------------------------------------------------------
// 消失點
// ---------------------------------------------------------------------------

PerspectiveStatus PerspectiveElement::set_vanishing_point(VanishingPoint vanishing_point) noexcept {
    vanishing_point_ = vanishing_point;
    return PerspectiveStatus::Ok;  // 具名列舉恆合法
}

// ---------------------------------------------------------------------------
// 視差強度
// ---------------------------------------------------------------------------

PerspectiveStatus PerspectiveElement::set_parallax_strength(float strength) {
    if (!std::isfinite(strength) || strength < 0.0f) {
        return PerspectiveStatus::Invalid;  // 非有限 / 負值：不套用，維持既有值
    }
    parallax_strength_ = strength;
    return PerspectiveStatus::Ok;
}

// ---------------------------------------------------------------------------
// 透視強度
// ---------------------------------------------------------------------------

PerspectiveStatus PerspectiveElement::set_perspective_strength(float strength) {
    if (!std::isfinite(strength) || strength <= 0.0f) {
        return PerspectiveStatus::Invalid;  // 非有限 / 非正值：不套用，維持既有值
    }
    // 確保與目前深度的組合不致退化（維持「目前狀態恆有效」不變量）。
    const float denom = 1.0f + strength * depth_;
    if (denom <= kMinPerspectiveDenominator) {
        return PerspectiveStatus::Invalid;  // 會使目前深度退化：拒絕，維持既有強度
    }
    perspective_strength_ = strength;
    return PerspectiveStatus::Ok;
}

// ---------------------------------------------------------------------------
// 景深模糊
// ---------------------------------------------------------------------------

PerspectiveStatus PerspectiveElement::set_focal_depth(float focal_depth) {
    if (!std::isfinite(focal_depth)) {
        return PerspectiveStatus::Invalid;  // 非有限：不套用，維持既有值
    }
    focal_depth_ = focal_depth;
    return PerspectiveStatus::Ok;
}

PerspectiveStatus PerspectiveElement::set_blur_strength(float strength) {
    if (!std::isfinite(strength) || strength < 0.0f) {
        return PerspectiveStatus::Invalid;  // 非有限 / 負值：不套用，維持既有值
    }
    blur_strength_ = strength;
    return PerspectiveStatus::Ok;
}

// ---------------------------------------------------------------------------
// 目前深度對應的變形矩陣
// ---------------------------------------------------------------------------

ds::render::Transform2D PerspectiveElement::transform() const {
    const PerspectiveTransformResult result = transform_for_depth(depth_);
    // 不變量：set_depth / set_perspective_strength 已互相校驗，目前組合恆有效；
    // 仍以 result.ok() 防禦性判斷，避免任何未預期路徑靜默回傳無效矩陣。
    return result.ok() ? result.transform : ds::render::Transform2D::identity();
}

// ---------------------------------------------------------------------------
// 渲染描述
// ---------------------------------------------------------------------------

PerspectiveRenderModel PerspectiveElement::render_model() const {
    const PerspectiveTransformResult result = transform_for_depth(depth_);

    PerspectiveRenderModel model;
    model.transform = result.ok() ? result.transform : ds::render::Transform2D::identity();
    model.depth = depth_;
    model.scale = result.ok() ? result.scale : 1.0f;
    model.depth_blur =
        depth_of_field_enabled_ ? blur_strength_ * std::fabs(depth_ - focal_depth_) : 0.0f;
    return model;
}

}  // namespace ds::elements
