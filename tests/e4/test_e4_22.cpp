// E4-22 2D 變形矩陣 — gtest 契約測試
//
// 涵蓋：單位矩陣、平移 / 旋轉 / 縮放 / 傾斜、點 / 向量變換、矩陣組合順序、
// 反矩陣、奇異矩陣求反明確報錯、往返（變換後反變換還原）。
// float 比較一律用 EXPECT_NEAR / EXPECT_FLOAT_EQ。
#include "transform2d.hpp"

#include <cmath>

#include <gtest/gtest.h>

using ds::render::InverseResult;
using ds::render::Transform2D;
using ds::render::TransformStatus;
using ds::render::Vec2;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kEps = 1e-5f;

// --- 單位矩陣 ---
TEST(Transform2D, DefaultIsIdentity) {
    Transform2D t;  // 預設建構即單位矩陣
    EXPECT_FLOAT_EQ(t.a(), 1.0f);
    EXPECT_FLOAT_EQ(t.b(), 0.0f);
    EXPECT_FLOAT_EQ(t.c(), 0.0f);
    EXPECT_FLOAT_EQ(t.d(), 1.0f);
    EXPECT_FLOAT_EQ(t.e(), 0.0f);
    EXPECT_FLOAT_EQ(t.f(), 0.0f);
    EXPECT_TRUE(t.approx_equals(Transform2D::identity()));
}

TEST(Transform2D, IdentityLeavesPointUnchanged) {
    Transform2D id = Transform2D::identity();
    Vec2 p = id.apply_point(Vec2{3.5f, -2.25f});
    EXPECT_NEAR(p.x, 3.5f, kEps);
    EXPECT_NEAR(p.y, -2.25f, kEps);
}

// --- 平移 ---
TEST(Transform2D, TranslateMovesPoint) {
    Transform2D t = Transform2D::translate(10.0f, -4.0f);
    Vec2 p = t.apply_point(Vec2{1.0f, 2.0f});
    EXPECT_NEAR(p.x, 11.0f, kEps);
    EXPECT_NEAR(p.y, -2.0f, kEps);
}

TEST(Transform2D, TranslateDoesNotAffectVector) {
    // 向量（方向）不受平移影響。
    Transform2D t = Transform2D::translate(10.0f, -4.0f);
    Vec2 v = t.apply_vector(Vec2{1.0f, 2.0f});
    EXPECT_NEAR(v.x, 1.0f, kEps);
    EXPECT_NEAR(v.y, 2.0f, kEps);
}

// --- 縮放 ---
TEST(Transform2D, ScaleScalesPoint) {
    Transform2D t = Transform2D::scale(2.0f, 3.0f);
    Vec2 p = t.apply_point(Vec2{4.0f, 5.0f});
    EXPECT_NEAR(p.x, 8.0f, kEps);
    EXPECT_NEAR(p.y, 15.0f, kEps);
}

TEST(Transform2D, UniformScaleOverload) {
    Transform2D t = Transform2D::scale(2.5f);
    Vec2 p = t.apply_point(Vec2{2.0f, 4.0f});
    EXPECT_NEAR(p.x, 5.0f, kEps);
    EXPECT_NEAR(p.y, 10.0f, kEps);
}

// --- 旋轉 ---
TEST(Transform2D, RotateNinetyDegrees) {
    // 逆時針 90°：(1,0) -> (0,1)
    Transform2D t = Transform2D::rotate(kPi / 2.0f);
    Vec2 p = t.apply_point(Vec2{1.0f, 0.0f});
    EXPECT_NEAR(p.x, 0.0f, kEps);
    EXPECT_NEAR(p.y, 1.0f, kEps);
}

TEST(Transform2D, RotatePreservesLength) {
    Transform2D t = Transform2D::rotate(0.7f);
    Vec2 v = t.apply_vector(Vec2{3.0f, 4.0f});
    float len = std::sqrt(v.x * v.x + v.y * v.y);
    EXPECT_NEAR(len, 5.0f, kEps);  // 旋轉為等距
}

// --- 傾斜 ---
TEST(Transform2D, ShearX) {
    // shx = x 隨 y 傾斜：x' = x + shx*y
    Transform2D t = Transform2D::shear(2.0f, 0.0f);
    Vec2 p = t.apply_point(Vec2{1.0f, 3.0f});
    EXPECT_NEAR(p.x, 1.0f + 2.0f * 3.0f, kEps);  // 7
    EXPECT_NEAR(p.y, 3.0f, kEps);
}

TEST(Transform2D, ShearY) {
    // shy = y 隨 x 傾斜：y' = shy*x + y
    Transform2D t = Transform2D::shear(0.0f, 2.0f);
    Vec2 p = t.apply_point(Vec2{3.0f, 1.0f});
    EXPECT_NEAR(p.x, 3.0f, kEps);
    EXPECT_NEAR(p.y, 2.0f * 3.0f + 1.0f, kEps);  // 7
}

// --- 組合順序 ---
TEST(Transform2D, ComposeAppliesRightOperandFirst) {
    // T = translate ∘ scale：先縮放再平移。
    Transform2D scale = Transform2D::scale(2.0f, 2.0f);
    Transform2D trans = Transform2D::translate(10.0f, 0.0f);
    Transform2D combined = trans.compose(scale);
    Vec2 p = combined.apply_point(Vec2{1.0f, 0.0f});
    // 先縮放 (1,0)->(2,0)，再平移 +10 -> (12,0)
    EXPECT_NEAR(p.x, 12.0f, kEps);
    EXPECT_NEAR(p.y, 0.0f, kEps);
}

TEST(Transform2D, ComposeOrderMatters) {
    Transform2D scale = Transform2D::scale(2.0f, 2.0f);
    Transform2D trans = Transform2D::translate(10.0f, 0.0f);
    // 相反順序：先平移再縮放。
    Transform2D other = scale.compose(trans);
    Vec2 p = other.apply_point(Vec2{1.0f, 0.0f});
    // 先平移 (1,0)->(11,0)，再縮放 *2 -> (22,0)
    EXPECT_NEAR(p.x, 22.0f, kEps);
    EXPECT_NEAR(p.y, 0.0f, kEps);
}

TEST(Transform2D, OperatorStarEqualsCompose) {
    Transform2D a = Transform2D::rotate(0.5f);
    Transform2D b = Transform2D::scale(1.5f, 2.0f);
    Transform2D viaOp = a * b;
    Transform2D viaFn = a.compose(b);
    EXPECT_TRUE(viaOp.approx_equals(viaFn));
}

TEST(Transform2D, ComposeWithIdentityIsNoop) {
    Transform2D a = Transform2D::rotate(0.3f).compose(Transform2D::translate(2.0f, 5.0f));
    Transform2D id = Transform2D::identity();
    EXPECT_TRUE(a.compose(id).approx_equals(a));
    EXPECT_TRUE(id.compose(a).approx_equals(a));
}

TEST(Transform2D, ComposeMatchesSequentialApply) {
    Transform2D a = Transform2D::rotate(0.4f);
    Transform2D b = Transform2D::scale(2.0f, 0.5f);
    Transform2D c = Transform2D::translate(3.0f, -1.0f);
    Transform2D combined = a.compose(b).compose(c);  // 先 c 後 b 後 a
    Vec2 p{2.0f, 3.0f};
    Vec2 viaCombined = combined.apply_point(p);
    Vec2 viaSteps = a.apply_point(b.apply_point(c.apply_point(p)));
    EXPECT_NEAR(viaCombined.x, viaSteps.x, kEps);
    EXPECT_NEAR(viaCombined.y, viaSteps.y, kEps);
}

// --- 反矩陣 ---
TEST(Transform2D, InverseOfTranslate) {
    Transform2D t = Transform2D::translate(5.0f, -3.0f);
    InverseResult r = t.inverse();
    ASSERT_EQ(r.status, TransformStatus::Ok);
    ASSERT_TRUE(r.ok());
    Vec2 p = r.matrix.apply_point(Vec2{5.0f, -3.0f});
    EXPECT_NEAR(p.x, 0.0f, kEps);
    EXPECT_NEAR(p.y, 0.0f, kEps);
}

TEST(Transform2D, InverseTimesOriginalIsIdentity) {
    Transform2D t =
        Transform2D::rotate(0.9f).compose(Transform2D::scale(2.0f, 3.0f)).compose(
            Transform2D::translate(4.0f, -2.0f));
    InverseResult r = t.inverse();
    ASSERT_TRUE(r.ok());
    Transform2D back = r.matrix.compose(t);
    EXPECT_TRUE(back.approx_equals(Transform2D::identity(), 1e-4f));
}

TEST(Transform2D, IsInvertibleTrueForNonSingular) {
    EXPECT_TRUE(Transform2D::scale(2.0f, 3.0f).is_invertible());
    EXPECT_TRUE(Transform2D::rotate(1.2f).is_invertible());
}

// --- 奇異矩陣求反明確報錯 ---
TEST(Transform2D, SingularInverseReportsExplicitly) {
    // scale(0, 1) 行列式為 0 -> 奇異。
    Transform2D t = Transform2D::scale(0.0f, 1.0f);
    EXPECT_FALSE(t.is_invertible());
    InverseResult r = t.inverse();
    EXPECT_EQ(r.status, TransformStatus::Singular);
    EXPECT_FALSE(r.ok());  // 明確回報奇異，不靜默
}

TEST(Transform2D, SingularCollapsedMatrix) {
    // 兩列相依（c,d 為 a,b 的倍數）-> det = 0。
    Transform2D t(1.0f, 2.0f, 2.0f, 4.0f, 0.0f, 0.0f);  // det = 1*4 - 2*2 = 0
    EXPECT_NEAR(t.determinant(), 0.0f, 1e-6f);
    InverseResult r = t.inverse();
    EXPECT_EQ(r.status, TransformStatus::Singular);
}

// --- 往返（變換後反變換還原）---
TEST(Transform2D, RoundTripRecoversPoint) {
    Transform2D t =
        Transform2D::translate(7.0f, 2.0f).compose(Transform2D::rotate(0.6f)).compose(
            Transform2D::scale(1.5f, 2.5f));
    InverseResult r = t.inverse();
    ASSERT_TRUE(r.ok());
    Vec2 original{3.0f, -4.0f};
    Vec2 transformed = t.apply_point(original);
    Vec2 recovered = r.matrix.apply_point(transformed);
    EXPECT_NEAR(recovered.x, original.x, 1e-4f);
    EXPECT_NEAR(recovered.y, original.y, 1e-4f);
}

TEST(Transform2D, RoundTripVector) {
    Transform2D t = Transform2D::rotate(1.1f).compose(Transform2D::shear(0.5f, 0.0f));
    InverseResult r = t.inverse();
    ASSERT_TRUE(r.ok());
    Vec2 v{2.0f, 5.0f};
    Vec2 back = r.matrix.apply_vector(t.apply_vector(v));
    EXPECT_NEAR(back.x, v.x, 1e-4f);
    EXPECT_NEAR(back.y, v.y, 1e-4f);
}

// --- determinant ---
TEST(Transform2D, DeterminantOfScale) {
    EXPECT_NEAR(Transform2D::scale(3.0f, 4.0f).determinant(), 12.0f, kEps);
}

TEST(Transform2D, DeterminantOfRotationIsOne) {
    EXPECT_NEAR(Transform2D::rotate(2.3f).determinant(), 1.0f, kEps);
}

}  // namespace
