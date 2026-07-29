// E4-28 透視與景深呈現 — gtest 測試
//
// 涵蓋：預設狀態、深度 → 縮放 / 位移（transform_for_depth 換算公式）、具名消失點（含
// center 無位移、上下左右、角落方向正規化）、以 E4-22 Transform2D 實際變形驗證
// （apply_point 套用結果）、視差強度（0 = 無位移、按比例縮放）、透視強度、無效深度
// （非有限 / 使透視退化，set_depth 與 transform_for_depth 皆不靜默）、透視強度變更會
// 校驗目前深度不致退化、景深模糊（啟用 / 關閉）、render_model() 含 transform。
#include "perspective_element.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "transform2d.hpp"

using ds::elements::PerspectiveElement;
using ds::elements::PerspectiveRenderModel;
using ds::elements::PerspectiveStatus;
using ds::elements::PerspectiveTransformResult;
using ds::elements::VanishingPoint;
using ds::render::Transform2D;
using ds::render::Vec2;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kInvSqrt2 = 0.70710678118654752f;

}  // namespace

// --- 預設狀態 ---
TEST(PerspectiveElement, DefaultStateIsBasePlaneIdentityTransform) {
    PerspectiveElement pe;
    EXPECT_NEAR(pe.depth(), 0.0f, 1e-6f);
    EXPECT_TRUE(pe.vanishing_point() == VanishingPoint::Center);
    EXPECT_NEAR(pe.parallax_strength(), 1.0f, 1e-6f);
    EXPECT_NEAR(pe.perspective_strength(), 1.0f, 1e-6f);
    EXPECT_FALSE(pe.depth_of_field_enabled());

    // 基準面（depth=0）：透視變形為單位矩陣。
    EXPECT_TRUE(pe.transform().approx_equals(Transform2D::identity(), 1e-6f));

    const PerspectiveRenderModel model = pe.render_model();
    EXPECT_NEAR(model.depth, 0.0f, 1e-6f);
    EXPECT_NEAR(model.scale, 1.0f, 1e-6f);
    EXPECT_NEAR(model.depth_blur, 0.0f, 1e-6f);
    EXPECT_TRUE(model.transform.approx_equals(Transform2D::identity(), 1e-6f));
}

// --- 深度 → 縮放：set_depth 成功套用 + transform_for_depth 換算公式 ---
TEST(PerspectiveElement, SetDepthAppliesAndScalesAccordingToFormula) {
    PerspectiveElement pe;
    EXPECT_TRUE(pe.set_depth(1.0f) == PerspectiveStatus::Ok);
    EXPECT_NEAR(pe.depth(), 1.0f, 1e-6f);

    // scale = 1 / (1 + strength(1.0) * depth(1.0)) = 0.5
    const PerspectiveTransformResult result = pe.transform_for_depth(1.0f);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.scale, 0.5f, 1e-5f);
    // 消失點為 Center：無視差位移，縮放為對角線係數。
    EXPECT_NEAR(result.transform.a(), 0.5f, 1e-5f);
    EXPECT_NEAR(result.transform.d(), 0.5f, 1e-5f);
    EXPECT_NEAR(result.transform.e(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.transform.f(), 0.0f, 1e-5f);
}

// --- 深度為負（更近）：縮放係數 > 1（放大）---
TEST(PerspectiveElement, NegativeDepthScalesUp) {
    PerspectiveElement pe;
    // scale = 1 / (1 + 1.0 * -0.5) = 1 / 0.5 = 2.0
    const PerspectiveTransformResult result = pe.transform_for_depth(-0.5f);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.scale, 2.0f, 1e-5f);
}

// --- 消失點位移：Right 方向的視差位移 ---
TEST(PerspectiveElement, VanishingPointRightProducesPositiveXOffset) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_vanishing_point(VanishingPoint::Right) == PerspectiveStatus::Ok);

    // depth=1 → scale=0.5 → shrink=1-0.5=0.5 → offset.x = dir.x(1) * parallax(1) * shrink(0.5) = 0.5
    const PerspectiveTransformResult result = pe.transform_for_depth(1.0f);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.transform.e(), 0.5f, 1e-5f);
    EXPECT_NEAR(result.transform.f(), 0.0f, 1e-5f);
}

// --- 消失點位移：四個角落方向正規化為單位長度 ---
TEST(PerspectiveElement, VanishingPointCornerDirectionIsNormalized) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_vanishing_point(VanishingPoint::TopRight) == PerspectiveStatus::Ok);

    // depth=1 → scale=0.5 → shrink=0.5 → offset = (kInvSqrt2, -kInvSqrt2) * 1 * 0.5
    const PerspectiveTransformResult result = pe.transform_for_depth(1.0f);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.transform.e(), kInvSqrt2 * 0.5f, 1e-5f);
    EXPECT_NEAR(result.transform.f(), -kInvSqrt2 * 0.5f, 1e-5f);
}

// --- 消失點為 Center：任何深度皆無視差位移（只有縮放）---
TEST(PerspectiveElement, VanishingPointCenterNeverOffsets) {
    PerspectiveElement pe;
    for (const float depth : {-0.5f, 0.0f, 1.0f, 3.0f}) {
        const PerspectiveTransformResult result = pe.transform_for_depth(depth);
        ASSERT_TRUE(result.ok());
        EXPECT_NEAR(result.transform.e(), 0.0f, 1e-5f);
        EXPECT_NEAR(result.transform.f(), 0.0f, 1e-5f);
    }
}

// --- 用 E4-22 Transform2D 實際變形：apply_point 驗證縮放 + 位移的組合效果 ---
TEST(PerspectiveElement, TransformAppliesScaleThenOffsetViaE4_22) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_vanishing_point(VanishingPoint::Right) == PerspectiveStatus::Ok);
    ASSERT_TRUE(pe.set_depth(1.0f) == PerspectiveStatus::Ok);  // scale=0.5, offset=(0.5,0)

    const Transform2D transform = pe.transform();
    // 點 (10, 4)：先縮放 0.5 -> (5, 2)，再位移 (0.5, 0) -> (5.5, 2)。
    const Vec2 result = transform.apply_point(Vec2{10.0f, 4.0f});
    EXPECT_NEAR(result.x, 5.5f, 1e-4f);
    EXPECT_NEAR(result.y, 2.0f, 1e-4f);
}

// --- 視差強度為 0：無論消失點為何皆無位移 ---
TEST(PerspectiveElement, ZeroParallaxStrengthDisablesOffset) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_vanishing_point(VanishingPoint::BottomLeft) == PerspectiveStatus::Ok);
    ASSERT_TRUE(pe.set_parallax_strength(0.0f) == PerspectiveStatus::Ok);

    const PerspectiveTransformResult result = pe.transform_for_depth(2.0f);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.transform.e(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.transform.f(), 0.0f, 1e-5f);
}

// --- 視差強度非負有限值檢查 ---
TEST(PerspectiveElement, SetParallaxStrengthRejectsNegativeAndNonFinite) {
    PerspectiveElement pe;
    EXPECT_TRUE(pe.set_parallax_strength(-1.0f) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.parallax_strength(), 1.0f, 1e-6f);  // 既有值不變

    EXPECT_TRUE(pe.set_parallax_strength(kNaN) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_parallax_strength(kInf) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.parallax_strength(), 1.0f, 1e-6f);

    EXPECT_TRUE(pe.set_parallax_strength(2.5f) == PerspectiveStatus::Ok);
    EXPECT_NEAR(pe.parallax_strength(), 2.5f, 1e-6f);
}

// --- 透視強度：影響縮放陡峭程度 ---
TEST(PerspectiveElement, PerspectiveStrengthChangesScaleSteepness) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_perspective_strength(2.0f) == PerspectiveStatus::Ok);

    // scale = 1 / (1 + 2.0 * 1.0) = 1/3
    const PerspectiveTransformResult result = pe.transform_for_depth(1.0f);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.scale, 1.0f / 3.0f, 1e-5f);
}

// --- 透視強度非正 / 非有限值：不套用不靜默 ---
TEST(PerspectiveElement, SetPerspectiveStrengthRejectsNonPositiveAndNonFinite) {
    PerspectiveElement pe;
    EXPECT_TRUE(pe.set_perspective_strength(0.0f) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_perspective_strength(-1.0f) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_perspective_strength(kNaN) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_perspective_strength(kInf) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.perspective_strength(), 1.0f, 1e-6f);  // 既有值不變
}

// --- 透視強度變更會校驗目前深度：會使組合退化則拒絕（不變量維護）---
TEST(PerspectiveElement, SetPerspectiveStrengthRejectsWhenItWouldDegenerateCurrentDepth) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_depth(-0.5f) == PerspectiveStatus::Ok);  // denom = 1 + 1*(-0.5) = 0.5，有效

    // denom = 1 + 3.0 * (-0.5) = -0.5 <= kMinPerspectiveDenominator：會使目前深度退化。
    EXPECT_TRUE(pe.set_perspective_strength(3.0f) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.perspective_strength(), 1.0f, 1e-6f);  // 既有強度不變
    EXPECT_NEAR(pe.depth(), -0.5f, 1e-6f);                // 既有深度也不受影響

    // 目前狀態仍應保持有效（不變量）。
    EXPECT_TRUE(pe.transform_for_depth(pe.depth()).ok());
}

// --- 無效深度：非有限值，set_depth 與 transform_for_depth 皆不靜默拒絕 ---
TEST(PerspectiveElement, InvalidDepthNonFiniteIsRejectedNotSilent) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_depth(2.0f) == PerspectiveStatus::Ok);

    EXPECT_TRUE(pe.set_depth(kNaN) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.depth(), 2.0f, 1e-6f);  // 既有深度不變

    EXPECT_TRUE(pe.set_depth(kInf) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_depth(-kInf) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.depth(), 2.0f, 1e-6f);

    const PerspectiveTransformResult result = pe.transform_for_depth(kNaN);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.status == PerspectiveStatus::Invalid);
    EXPECT_TRUE(result.transform.approx_equals(Transform2D::identity(), 1e-6f));
}

// --- 無效深度：使透視退化（深度達到 / 超越與消失點重合的臨界點）---
TEST(PerspectiveElement, InvalidDepthDegenerateIsRejectedNotSilent) {
    PerspectiveElement pe;
    // denom = 1 + 1.0 * -2.0 = -1.0 <= kMinPerspectiveDenominator：明確退化。
    const PerspectiveTransformResult result = pe.transform_for_depth(-2.0f);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.status == PerspectiveStatus::Invalid);
    EXPECT_NEAR(result.scale, 0.0f, 1e-6f);
    EXPECT_TRUE(result.transform.approx_equals(Transform2D::identity(), 1e-6f));

    // set_depth 對同樣退化深度：不套用，維持既有（預設 0）深度。
    EXPECT_TRUE(pe.set_depth(-2.0f) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.depth(), 0.0f, 1e-6f);
}

// --- 消失點：具名列舉恆合法（恆回 Ok）---
TEST(PerspectiveElement, SetVanishingPointAlwaysOk) {
    PerspectiveElement pe;
    const VanishingPoint points[] = {
        VanishingPoint::Center,      VanishingPoint::Top,         VanishingPoint::Bottom,
        VanishingPoint::Left,        VanishingPoint::Right,       VanishingPoint::TopLeft,
        VanishingPoint::TopRight,    VanishingPoint::BottomLeft,  VanishingPoint::BottomRight,
    };
    for (const VanishingPoint vp : points) {
        EXPECT_TRUE(pe.set_vanishing_point(vp) == PerspectiveStatus::Ok);
        EXPECT_TRUE(pe.vanishing_point() == vp);
    }
}

// --- 景深模糊：關閉時恆為 0（即使對焦深度與目前深度不同）---
TEST(PerspectiveElement, DepthOfFieldDisabledByDefaultProducesZeroBlur) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_depth(3.0f) == PerspectiveStatus::Ok);
    ASSERT_TRUE(pe.set_focal_depth(0.0f) == PerspectiveStatus::Ok);

    EXPECT_NEAR(pe.render_model().depth_blur, 0.0f, 1e-6f);
}

// --- 景深模糊：啟用後依 |depth - focal_depth| * blur_strength 計算 ---
TEST(PerspectiveElement, DepthOfFieldEnabledComputesBlurFromFocalDistance) {
    PerspectiveElement pe;
    pe.set_depth_of_field_enabled(true);
    ASSERT_TRUE(pe.set_focal_depth(2.0f) == PerspectiveStatus::Ok);
    ASSERT_TRUE(pe.set_blur_strength(0.5f) == PerspectiveStatus::Ok);
    ASSERT_TRUE(pe.set_depth(5.0f) == PerspectiveStatus::Ok);

    // depth_blur = 0.5 * |5 - 2| = 1.5
    EXPECT_NEAR(pe.render_model().depth_blur, 1.5f, 1e-5f);
    EXPECT_TRUE(pe.depth_of_field_enabled());
}

// --- 景深模糊：對焦深度 / 模糊強度非有限或非法值不靜默拒絕 ---
TEST(PerspectiveElement, SetFocalDepthAndBlurStrengthRejectInvalid) {
    PerspectiveElement pe;
    EXPECT_TRUE(pe.set_focal_depth(kNaN) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_focal_depth(kInf) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.focal_depth(), 0.0f, 1e-6f);

    EXPECT_TRUE(pe.set_blur_strength(-1.0f) == PerspectiveStatus::Invalid);
    EXPECT_TRUE(pe.set_blur_strength(kNaN) == PerspectiveStatus::Invalid);
    EXPECT_NEAR(pe.blur_strength(), 1.0f, 1e-6f);

    EXPECT_TRUE(pe.set_focal_depth(-1.5f) == PerspectiveStatus::Ok);
    EXPECT_NEAR(pe.focal_depth(), -1.5f, 1e-6f);
    EXPECT_TRUE(pe.set_blur_strength(2.0f) == PerspectiveStatus::Ok);
    EXPECT_NEAR(pe.blur_strength(), 2.0f, 1e-6f);
}

// --- render_model() 含 transform，與 transform_for_depth(目前深度) 一致 ---
TEST(PerspectiveElement, RenderModelContainsMatchingTransformDepthAndScale) {
    PerspectiveElement pe;
    ASSERT_TRUE(pe.set_vanishing_point(VanishingPoint::Left) == PerspectiveStatus::Ok);
    ASSERT_TRUE(pe.set_depth(1.5f) == PerspectiveStatus::Ok);

    const PerspectiveRenderModel model = pe.render_model();
    const PerspectiveTransformResult expected = pe.transform_for_depth(1.5f);
    ASSERT_TRUE(expected.ok());

    EXPECT_NEAR(model.depth, 1.5f, 1e-6f);
    EXPECT_NEAR(model.scale, expected.scale, 1e-6f);
    EXPECT_TRUE(model.transform.approx_equals(expected.transform, 1e-5f));
    EXPECT_TRUE(model.transform.approx_equals(pe.transform(), 1e-6f));
}
