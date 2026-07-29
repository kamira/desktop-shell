// E4-19 旋轉元件 — gtest 測試
//
// 涵蓋：設定角度（含正規化 / 非有限報錯不靜默）、疊加角度(rotate_by)、具名旋轉中心
// (set_pivot：center/top-left 等九宮格錨點)、以 E4-22 Transform2D 計算旋轉矩陣（含繞
// center / 非 center pivot 的具體旋轉驗證、角度為 0 時為單位矩陣、未設來源時退化繞原點）、
// 連續旋轉（set_angular_velocity + advance 跨越多整圈）、以 E4-02 ImageElement 顯示（來源 /
// 縮放模式 / 透明度 / 目標透傳）、render_model() 含 transform + angle + pivot。
#include "rotatable_element.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "image_element.hpp"
#include "transform2d.hpp"

using ds::elements::ImageDimensions;
using ds::elements::ImageStatus;
using ds::elements::MemoryImageSource;
using ds::elements::RotatableElement;
using ds::elements::RotatedRenderModel;
using ds::elements::RotateStatus;
using ds::elements::RotationPivot;
using ds::elements::ScaleMode;
using ds::render::Transform2D;
using ds::render::Vec2;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

}  // namespace

// --- 預設狀態 ---
TEST(RotatableElement, DefaultAngleIsZeroPivotIsCenterVelocityIsZero) {
    RotatableElement rot;
    EXPECT_NEAR(rot.angle(), 0.0f, 1e-6f);
    EXPECT_TRUE(rot.pivot() == RotationPivot::Center);
    EXPECT_NEAR(rot.angular_velocity(), 0.0f, 1e-6f);
    EXPECT_FALSE(rot.has_source());
}

// --- set_angle 成功套用 + 正規化至 [0, 2π) ---
TEST(RotatableElement, SetAngleSucceedsAndNormalizes) {
    RotatableElement rot;
    EXPECT_TRUE(rot.set_angle(kPi / 2.0f) == RotateStatus::Ok);
    EXPECT_NEAR(rot.angle(), kPi / 2.0f, 1e-4f);

    // 超過一整圈：正規化回 [0, 2π)。
    EXPECT_TRUE(rot.set_angle(kTwoPi + 0.1f) == RotateStatus::Ok);
    EXPECT_NEAR(rot.angle(), 0.1f, 1e-4f);

    // 負角度：正規化為等價的正角度。
    EXPECT_TRUE(rot.set_angle(-kPi / 2.0f) == RotateStatus::Ok);
    EXPECT_NEAR(rot.angle(), 3.0f * kPi / 2.0f, 1e-4f);
}

// --- set_angle 非有限值：不靜默、不套用 ---
TEST(RotatableElement, SetAngleRejectsNonFiniteAndKeepsExisting) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_angle(1.0f) == RotateStatus::Ok);

    EXPECT_TRUE(rot.set_angle(kNaN) == RotateStatus::Invalid);
    EXPECT_NEAR(rot.angle(), 1.0f, 1e-6f);  // 既有角度不變

    EXPECT_TRUE(rot.set_angle(kInf) == RotateStatus::Invalid);
    EXPECT_NEAR(rot.angle(), 1.0f, 1e-6f);

    EXPECT_TRUE(rot.set_angle(-kInf) == RotateStatus::Invalid);
    EXPECT_NEAR(rot.angle(), 1.0f, 1e-6f);
}

// --- rotate_by 疊加角度 ---
TEST(RotatableElement, RotateByAccumulates) {
    RotatableElement rot;
    EXPECT_TRUE(rot.rotate_by(kPi / 4.0f) == RotateStatus::Ok);
    EXPECT_TRUE(rot.rotate_by(kPi / 4.0f) == RotateStatus::Ok);
    EXPECT_NEAR(rot.angle(), kPi / 2.0f, 1e-4f);
}

// --- rotate_by 非有限值：不靜默、不套用 ---
TEST(RotatableElement, RotateByRejectsNonFiniteAndKeepsExisting) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_angle(0.5f) == RotateStatus::Ok);
    EXPECT_TRUE(rot.rotate_by(kNaN) == RotateStatus::Invalid);
    EXPECT_NEAR(rot.angle(), 0.5f, 1e-6f);
}

// --- set_pivot 具名（center / top-left 等九宮格） ---
TEST(RotatableElement, SetPivotNamedCenterAndTopLeftAndOthers) {
    RotatableElement rot;
    EXPECT_TRUE(rot.set_pivot(RotationPivot::TopLeft) == RotateStatus::Ok);
    EXPECT_TRUE(rot.pivot() == RotationPivot::TopLeft);

    EXPECT_TRUE(rot.set_pivot(RotationPivot::Center) == RotateStatus::Ok);
    EXPECT_TRUE(rot.pivot() == RotationPivot::Center);

    EXPECT_TRUE(rot.set_pivot(RotationPivot::BottomRight) == RotateStatus::Ok);
    EXPECT_TRUE(rot.pivot() == RotationPivot::BottomRight);
}

// --- set_angular_velocity 成功套用 ---
TEST(RotatableElement, SetAngularVelocitySucceeds) {
    RotatableElement rot;
    EXPECT_TRUE(rot.set_angular_velocity(kPi) == RotateStatus::Ok);
    EXPECT_NEAR(rot.angular_velocity(), kPi, 1e-4f);
}

// --- set_angular_velocity 非有限值：不靜默、不套用 ---
TEST(RotatableElement, SetAngularVelocityRejectsNonFiniteAndKeepsExisting) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_angular_velocity(1.0f) == RotateStatus::Ok);
    EXPECT_TRUE(rot.set_angular_velocity(kNaN) == RotateStatus::Invalid);
    EXPECT_NEAR(rot.angular_velocity(), 1.0f, 1e-6f);
}

// --- 連續旋轉：advance 依角速度累加，跨越多整圈正規化 ---
TEST(RotatableElement, AdvanceAccumulatesContinuousRotationAcrossFullTurns) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_angular_velocity(kPi) == RotateStatus::Ok);  // 每單位時間轉半圈

    rot.advance(1.0);  // +π
    EXPECT_NEAR(rot.angle(), kPi, 1e-4f);

    rot.advance(1.0);  // +π → 累計 2π，正規化回 0
    EXPECT_NEAR(rot.angle(), 0.0f, 1e-3f);

    rot.advance(0.5);  // +π/2
    EXPECT_NEAR(rot.angle(), kPi / 2.0f, 1e-3f);
}

// --- 角速度為 0：advance 安全 no-op ---
TEST(RotatableElement, AdvanceNoOpWhenAngularVelocityIsZero) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_angle(0.7f) == RotateStatus::Ok);
    rot.advance(100.0);
    EXPECT_NEAR(rot.angle(), 0.7f, 1e-6f);
}

// --- advance 非有限 dt：安全 no-op，不崩潰、不寫入非有限角度 ---
TEST(RotatableElement, AdvanceSafeNoOpOnNonFiniteDt) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_angular_velocity(1.0f) == RotateStatus::Ok);
    ASSERT_TRUE(rot.set_angle(0.3f) == RotateStatus::Ok);
    rot.advance(std::numeric_limits<double>::quiet_NaN());
    EXPECT_NEAR(rot.angle(), 0.3f, 1e-6f);
    EXPECT_TRUE(std::isfinite(rot.angle()));
}

// --- transform：角度為 0 恆為單位矩陣（不論 pivot） ---
TEST(RotatableElement, TransformIsIdentityAtZeroAngle) {
    MemoryImageSource src("res://spin", ImageDimensions{100, 100});
    RotatableElement rot;
    ASSERT_TRUE(rot.set_source(src) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_pivot(RotationPivot::TopLeft) == RotateStatus::Ok);

    const Vec2 p{37.0f, -12.0f};
    const Vec2 out = rot.transform().apply_point(p);
    EXPECT_NEAR(out.x, p.x, 1e-4f);
    EXPECT_NEAR(out.y, p.y, 1e-4f);
}

// --- transform：旋轉矩陣固定住 pivot 本身（繞任意點旋轉的不變量） ---
TEST(RotatableElement, TransformFixesPivotPointItself) {
    MemoryImageSource src("res://spin", ImageDimensions{100, 100});
    RotatableElement rot;
    ASSERT_TRUE(rot.set_source(src) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_pivot(RotationPivot::Center) == RotateStatus::Ok);
    ASSERT_TRUE(rot.set_angle(kPi / 3.0f) == RotateStatus::Ok);

    // Center pivot 換算自 100x100 來源為 (50,50)。
    const Vec2 pivot_point{50.0f, 50.0f};
    const Vec2 out = rot.transform().apply_point(pivot_point);
    EXPECT_NEAR(out.x, pivot_point.x, 1e-3f);
    EXPECT_NEAR(out.y, pivot_point.y, 1e-3f);
}

// --- transform：繞 Center 旋轉 90 度的具體驗證（用 E4-22 Transform2D 計算） ---
TEST(RotatableElement, TransformRotatesAroundCenterByNinetyDegrees) {
    MemoryImageSource src("res://spin", ImageDimensions{100, 100});
    RotatableElement rot;
    ASSERT_TRUE(rot.set_source(src) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_pivot(RotationPivot::Center) == RotateStatus::Ok);
    ASSERT_TRUE(rot.set_angle(kPi / 2.0f) == RotateStatus::Ok);

    // 中點右緣 (100,50) 繞中心 (50,50) 逆時針轉 90 度 → (50,100)。
    const Vec2 out = rot.transform().apply_point(Vec2{100.0f, 50.0f});
    EXPECT_NEAR(out.x, 50.0f, 1e-3f);
    EXPECT_NEAR(out.y, 100.0f, 1e-3f);
}

// --- transform：繞非 Center 的具名 pivot（TopLeft）旋轉 90 度的具體驗證 ---
TEST(RotatableElement, TransformRotatesAroundTopLeftByNinetyDegrees) {
    MemoryImageSource src("res://spin", ImageDimensions{100, 50});
    RotatableElement rot;
    ASSERT_TRUE(rot.set_source(src) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_pivot(RotationPivot::TopLeft) == RotateStatus::Ok);
    ASSERT_TRUE(rot.set_angle(kPi / 2.0f) == RotateStatus::Ok);

    // 右上角 (100,0) 繞左上角 (0,0) 逆時針轉 90 度 → (0,100)。
    const Vec2 out = rot.transform().apply_point(Vec2{100.0f, 0.0f});
    EXPECT_NEAR(out.x, 0.0f, 1e-3f);
    EXPECT_NEAR(out.y, 100.0f, 1e-3f);
}

// --- transform：未設來源（固有尺寸為零）時明確退化為繞原點旋轉，不報錯 ---
TEST(RotatableElement, TransformWithoutSourceDegradesToOriginRotation) {
    RotatableElement rot;
    ASSERT_TRUE(rot.set_pivot(RotationPivot::Center) == RotateStatus::Ok);  // 無來源下無意義但不報錯
    ASSERT_TRUE(rot.set_angle(kPi / 2.0f) == RotateStatus::Ok);

    // 尺寸為零 → pivot 換算恆為 (0,0)，等同繞原點：(10,0) 轉 90 度 → (0,10)。
    const Vec2 out = rot.transform().apply_point(Vec2{10.0f, 0.0f});
    EXPECT_NEAR(out.x, 0.0f, 1e-3f);
    EXPECT_NEAR(out.y, 10.0f, 1e-3f);
}

// --- 以 E4-02 顯示：來源 / 縮放模式 / 透明度 / 目標透傳 ---
TEST(RotatableElement, DisplaySettingsPassThroughToImageElement) {
    MemoryImageSource src("res://icon", ImageDimensions{64, 64});
    RotatableElement rot;
    ASSERT_TRUE(rot.set_source(src) == ImageStatus::Ok);
    EXPECT_TRUE(rot.has_source());
    EXPECT_EQ(rot.source_reference(), "res://icon");
    EXPECT_EQ(rot.source_dimensions().width, 64);
    EXPECT_EQ(rot.source_dimensions().height, 64);

    EXPECT_TRUE(rot.set_scale_mode(ScaleMode::Fill) == ImageStatus::Ok);
    EXPECT_TRUE(rot.scale_mode() == ScaleMode::Fill);

    EXPECT_TRUE(rot.set_opacity(0.5f) == ImageStatus::Ok);
    EXPECT_NEAR(rot.opacity(), 0.5f, 1e-4f);

    EXPECT_TRUE(rot.set_target("layer.icon") == ImageStatus::Ok);
    EXPECT_EQ(rot.target(), "layer.icon");

    // E4-02 對非有限透明度不靜默：透傳的錯誤照樣回報。
    EXPECT_TRUE(rot.set_opacity(kNaN) == ImageStatus::Invalid);
    EXPECT_NEAR(rot.opacity(), 0.5f, 1e-4f);  // 既有值不變
}

// --- render_model：含底層 image + transform + angle + pivot ---
TEST(RotatableElement, RenderModelIncludesImageTransformAngleAndPivot) {
    MemoryImageSource src("res://gear", ImageDimensions{100, 100});
    RotatableElement rot;
    ASSERT_TRUE(rot.set_source(src) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_scale_mode(ScaleMode::Fit) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_opacity(0.8f) == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_target("layer.gear") == ImageStatus::Ok);
    ASSERT_TRUE(rot.set_pivot(RotationPivot::Center) == RotateStatus::Ok);
    ASSERT_TRUE(rot.set_angle(kPi / 2.0f) == RotateStatus::Ok);

    const RotatedRenderModel model = rot.render_model();

    // 底層 E4-02 圖片渲染描述照實帶出。
    EXPECT_TRUE(model.image.has_source);
    EXPECT_EQ(model.image.source_reference, "res://gear");
    EXPECT_EQ(model.image.source_dimensions.width, 100);
    EXPECT_EQ(model.image.source_dimensions.height, 100);
    EXPECT_TRUE(model.image.scale_mode == ScaleMode::Fit);
    EXPECT_NEAR(model.image.alpha.opacity, 0.8f, 1e-4f);
    EXPECT_EQ(model.image.target, "layer.gear");

    // 旋轉描述：角度 / pivot 一致，且 transform 與獨立呼叫 transform() 相符。
    EXPECT_NEAR(model.angle_radians, kPi / 2.0f, 1e-4f);
    EXPECT_TRUE(model.pivot == RotationPivot::Center);
    EXPECT_TRUE(model.transform.approx_equals(rot.transform(), 1e-5f));

    // 具體驗證 render_model 的 transform 確實是繞 Center 的 90 度旋轉。
    const Vec2 out = model.transform.apply_point(Vec2{100.0f, 50.0f});
    EXPECT_NEAR(out.x, 50.0f, 1e-3f);
    EXPECT_NEAR(out.y, 100.0f, 1e-3f);
}

// --- 未設來源時 render_model 明確回報空狀態（不假裝有資料） ---
TEST(RotatableElement, RenderModelWithoutSourceReportsNoSource) {
    RotatableElement rot;
    const RotatedRenderModel model = rot.render_model();
    EXPECT_FALSE(model.image.has_source);
    EXPECT_EQ(model.image.source_reference, "");
    EXPECT_TRUE(model.pivot == RotationPivot::Center);
    EXPECT_NEAR(model.angle_radians, 0.0f, 1e-6f);
}
