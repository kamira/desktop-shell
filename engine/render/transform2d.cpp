// E4-22 2D 變形矩陣 — 實作（平台中立純數學，見 transform2d.hpp）
#include "transform2d.hpp"

#include <cmath>

namespace ds::render {

Transform2D Transform2D::identity() {
    return Transform2D(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
}

Transform2D Transform2D::translate(float tx, float ty) {
    // | 1 0 tx ; 0 1 ty ; 0 0 1 |
    return Transform2D(1.0f, 0.0f, 0.0f, 1.0f, tx, ty);
}

Transform2D Transform2D::rotate(float radians) {
    const float cs = std::cos(radians);
    const float sn = std::sin(radians);
    // 逆時針旋轉：(x,y) -> (cos*x - sin*y, sin*x + cos*y)
    // 對應係數 a=cos, b=sin, c=-sin, d=cos。
    return Transform2D(cs, sn, -sn, cs, 0.0f, 0.0f);
}

Transform2D Transform2D::scale(float sx, float sy) {
    // | sx 0 0 ; 0 sy 0 ; 0 0 1 |
    return Transform2D(sx, 0.0f, 0.0f, sy, 0.0f, 0.0f);
}

Transform2D Transform2D::scale(float s) {
    return scale(s, s);
}

Transform2D Transform2D::shear(float shx, float shy) {
    // x' = x + shx*y ; y' = shy*x + y
    // 對應係數 a=1, b=shy, c=shx, d=1。
    return Transform2D(1.0f, shy, shx, 1.0f, 0.0f, 0.0f);
}

Transform2D Transform2D::compose(const Transform2D& rhs) const {
    // this ∘ rhs：先套用 rhs，再套用 this。以 3x3 矩陣乘法 (this) * (rhs) 計算，
    // 末列固定 [0 0 1]。
    const Transform2D& A = *this;
    const Transform2D& B = rhs;
    return Transform2D(
        A.a_ * B.a_ + A.c_ * B.b_,          // a
        A.b_ * B.a_ + A.d_ * B.b_,          // b
        A.a_ * B.c_ + A.c_ * B.d_,          // c
        A.b_ * B.c_ + A.d_ * B.d_,          // d
        A.a_ * B.e_ + A.c_ * B.f_ + A.e_,   // e
        A.b_ * B.e_ + A.d_ * B.f_ + A.f_);  // f
}

Vec2 Transform2D::apply_point(const Vec2& p) const {
    return Vec2{a_ * p.x + c_ * p.y + e_,
               b_ * p.x + d_ * p.y + f_};
}

Vec2 Transform2D::apply_vector(const Vec2& v) const {
    // 方向不受平移影響：只套用線性部分。
    return Vec2{a_ * v.x + c_ * v.y,
               b_ * v.x + d_ * v.y};
}

bool Transform2D::is_invertible(float epsilon) const {
    return std::fabs(determinant()) > epsilon;
}

InverseResult Transform2D::inverse(float epsilon) const {
    const float det = determinant();
    if (std::fabs(det) <= epsilon) {
        // 奇異：明確回報，不靜默給出無意義數值。matrix 保持單位矩陣佔位。
        return InverseResult{TransformStatus::Singular, Transform2D::identity()};
    }
    const float inv_det = 1.0f / det;
    // 線性部分反矩陣：1/det * [ d -c ; -b a ]
    const float ia = d_ * inv_det;
    const float ib = -b_ * inv_det;
    const float ic = -c_ * inv_det;
    const float id = a_ * inv_det;
    // 反平移：-Minv * (e, f)
    const float ie = (c_ * f_ - d_ * e_) * inv_det;
    const float if_ = (b_ * e_ - a_ * f_) * inv_det;
    return InverseResult{TransformStatus::Ok, Transform2D(ia, ib, ic, id, ie, if_)};
}

bool Transform2D::approx_equals(const Transform2D& other, float epsilon) const {
    return std::fabs(a_ - other.a_) <= epsilon &&
           std::fabs(b_ - other.b_) <= epsilon &&
           std::fabs(c_ - other.c_) <= epsilon &&
           std::fabs(d_ - other.d_) <= epsilon &&
           std::fabs(e_ - other.e_) <= epsilon &&
           std::fabs(f_ - other.f_) <= epsilon;
}

}  // namespace ds::render
