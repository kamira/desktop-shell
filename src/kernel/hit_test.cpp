// E1-04 幾何命中測試 — 實作
//
// 純幾何點內判定 + 注入式 alpha 命中 + 具名圖層優先；不含任何平台分支、真實 OS / 繪圖 API。
#include "hit_test.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::isfinite
#include <utility>

namespace ds::kernel {

// --- 工廠 ---
Shape make_rect(float width, float height) {
    Shape s;
    s.kind = ShapeKind::Rect;
    s.width = width;
    s.height = height;
    return s;
}

Shape make_rounded_rect(float width, float height, float corner_radius) {
    Shape s;
    s.kind = ShapeKind::RoundedRect;
    s.width = width;
    s.height = height;
    s.corner_radius = corner_radius;
    return s;
}

Shape make_circle(LocalPoint center, float radius) {
    Shape s;
    s.kind = ShapeKind::Circle;
    s.center = center;
    s.radius = radius;
    return s;
}

Shape make_polygon(std::vector<LocalPoint> vertices) {
    Shape s;
    s.kind = ShapeKind::Polygon;
    s.vertices = std::move(vertices);
    return s;
}

Shape make_path(std::vector<LocalPoint> vertices, FillRule fill) {
    Shape s;
    s.kind = ShapeKind::Path;
    s.vertices = std::move(vertices);
    s.fill = fill;
    return s;
}

namespace {

constexpr float kEdgeEpsilon = 1e-6f;  // 邊界共線判定的容差（本地座標尺度）

bool finite_point(const LocalPoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

bool all_vertices_finite(const std::vector<LocalPoint>& v) {
    for (const auto& p : v) {
        if (!finite_point(p)) {
            return false;
        }
    }
    return true;
}

// 點是否落在線段 [a, b] 上（含端點）——供邊界命中（含邊界視為命中）。
bool on_segment(const LocalPoint& p, const LocalPoint& a, const LocalPoint& b) {
    const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    if (std::fabs(cross) > kEdgeEpsilon) {
        return false;  // 非共線
    }
    // 共線：檢查是否在 a、b 的界框內（含容差）。
    const float min_x = std::min(a.x, b.x) - kEdgeEpsilon;
    const float max_x = std::max(a.x, b.x) + kEdgeEpsilon;
    const float min_y = std::min(a.y, b.y) - kEdgeEpsilon;
    const float max_y = std::max(a.y, b.y) + kEdgeEpsilon;
    return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y;
}

// isLeft > 0：p 在有向邊 a→b 的左側（環繞數用）。
float is_left(const LocalPoint& a, const LocalPoint& b, const LocalPoint& p) {
    return (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
}

// 點在多邊形內（含邊界），依填充規則。頂點序列首尾自動閉合。
bool point_in_polygon(const LocalPoint& p, const std::vector<LocalPoint>& v,
                      FillRule rule) {
    const std::size_t n = v.size();
    // 邊界（落在任一邊上）一律視為命中。
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        if (on_segment(p, v[j], v[i])) {
            return true;
        }
    }
    if (rule == FillRule::EvenOdd) {
        bool inside = false;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            if ((v[i].y > p.y) != (v[j].y > p.y)) {
                const float x_int = (v[j].x - v[i].x) * (p.y - v[i].y) /
                                        (v[j].y - v[i].y) +
                                    v[i].x;
                if (p.x < x_int) {
                    inside = !inside;
                }
            }
        }
        return inside;
    }
    // NonZero 環繞數。
    int wn = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const LocalPoint& a = v[i];
        const LocalPoint& b = v[(i + 1) % n];
        if (a.y <= p.y) {
            if (b.y > p.y && is_left(a, b, p) > 0.0f) {
                ++wn;
            }
        } else {
            if (b.y <= p.y && is_left(a, b, p) < 0.0f) {
                --wn;
            }
        }
    }
    return wn != 0;
}

// 點在圓角矩形內（含邊界）。本地原點 (0,0) 到 (w,h)。
bool point_in_rounded_rect(const LocalPoint& p, float w, float h, float cr) {
    if (p.x < 0.0f || p.y < 0.0f || p.x > w || p.y > h) {
        return false;  // 界框外
    }
    const float r = std::clamp(cr, 0.0f, std::min(w, h) * 0.5f);
    if (r <= 0.0f) {
        return true;  // 退化為一般矩形
    }
    // 判斷是否落在某個角落區域；若是，需在該角落的四分圓內。
    float cx = p.x;
    float cy = p.y;
    bool in_corner = false;
    if (p.x < r && p.y < r) {          // 左上
        cx = r;
        cy = r;
        in_corner = true;
    } else if (p.x > w - r && p.y < r) {  // 右上
        cx = w - r;
        cy = r;
        in_corner = true;
    } else if (p.x < r && p.y > h - r) {  // 左下
        cx = r;
        cy = h - r;
        in_corner = true;
    } else if (p.x > w - r && p.y > h - r) {  // 右下
        cx = w - r;
        cy = h - r;
        in_corner = true;
    }
    if (!in_corner) {
        return true;  // 落在十字形核心（非角落），一律在內
    }
    const float dx = p.x - cx;
    const float dy = p.y - cy;
    return dx * dx + dy * dy <= r * r;  // 角落：四分圓內（含邊界）
}

// 純幾何點內判定（假設 shape 已通過 is_valid、point 已有限）。
bool contains(const LocalPoint& p, const Shape& shape) {
    switch (shape.kind) {
        case ShapeKind::Rect:
            return p.x >= 0.0f && p.y >= 0.0f && p.x <= shape.width &&
                   p.y <= shape.height;
        case ShapeKind::RoundedRect:
            return point_in_rounded_rect(p, shape.width, shape.height,
                                         shape.corner_radius);
        case ShapeKind::Circle: {
            const float dx = p.x - shape.center.x;
            const float dy = p.y - shape.center.y;
            return dx * dx + dy * dy <= shape.radius * shape.radius;
        }
        case ShapeKind::Polygon:
            return point_in_polygon(p, shape.vertices, FillRule::EvenOdd);
        case ShapeKind::Path:
            return point_in_polygon(p, shape.vertices, shape.fill);
    }
    return false;  // 不可達（enum 已窮舉）
}

// 具名圖層 → 優先序序（越大越上層）。NFR-02：內部比較用，對外不暴露數字 z。
int layer_rank(SurfaceLayer layer) {
    switch (layer) {
        case SurfaceLayer::Wallpaper:
            return 0;
        case SurfaceLayer::BelowNormal:
            return 1;
        case SurfaceLayer::Normal:
            return 2;
        case SurfaceLayer::Overlay:
            return 3;
        case SurfaceLayer::Topmost:
            return 4;
    }
    return 2;  // 不可達
}

}  // namespace

// --- 公開 API ---
bool HitTester::is_valid(const Shape& shape) const {
    switch (shape.kind) {
        case ShapeKind::Rect:
            return std::isfinite(shape.width) && std::isfinite(shape.height) &&
                   shape.width >= 0.0f && shape.height >= 0.0f;
        case ShapeKind::RoundedRect:
            return std::isfinite(shape.width) && std::isfinite(shape.height) &&
                   std::isfinite(shape.corner_radius) && shape.width >= 0.0f &&
                   shape.height >= 0.0f && shape.corner_radius >= 0.0f;
        case ShapeKind::Circle:
            return finite_point(shape.center) && std::isfinite(shape.radius) &&
                   shape.radius >= 0.0f;
        case ShapeKind::Polygon:
        case ShapeKind::Path:
            return shape.vertices.size() >= 3 &&
                   all_vertices_finite(shape.vertices);
    }
    return false;
}

HitResult HitTester::hit_test(const LocalPoint& point, const Shape& shape) const {
    if (!is_valid(shape) || !finite_point(point)) {
        return HitResult{HitStatus::Invalid, false};  // 報錯不靜默
    }
    return HitResult{HitStatus::Ok, contains(point, shape)};
}

HitResult HitTester::hit_test_alpha(const LocalPoint& point,
                                    const HitSurface& surface) const {
    // 先做幾何 + 有效性判定（無效直接回 Invalid）。
    const HitResult geo = hit_test(point, surface.shape);
    if (geo.status != HitStatus::Ok) {
        return geo;
    }
    // 命中穿透的 surface 永不命中（落到其後）。
    if (surface.hit == HitPolicy::Transparent) {
        return HitResult{HitStatus::Ok, false};
    }
    if (!geo.inside) {
        return HitResult{HitStatus::Ok, false};  // 幾何未命中
    }
    // 幾何命中；Opaque 模式忽略 alpha 通道。
    if (surface.alpha.mode == AlphaMode::Opaque) {
        return HitResult{HitStatus::Ok, true};
    }
    // PerPixel：以注入 oracle 取該點 alpha（未注入視為 1.0 = 完全存在）。
    float a = 1.0f;
    if (surface.alpha_query) {
        a = surface.alpha_query(point);
    }
    if (!std::isfinite(a)) {
        return HitResult{HitStatus::Invalid, false};  // oracle 回非有限值：報錯不靜默
    }
    a = std::clamp(a, 0.0f, 1.0f);
    const float opacity = std::clamp(surface.alpha.opacity, 0.0f, 1.0f);
    const float threshold = std::clamp(surface.alpha_threshold, 0.0f, 1.0f);
    const float effective = a * opacity;
    // 透明處（有效 alpha 低於門檻）不命中。
    return HitResult{HitStatus::Ok, effective >= threshold};
}

TopmostHit HitTester::topmost_hit(const LocalPoint& point,
                                  const std::vector<HitSurface>& surfaces) const {
    TopmostHit result{HitStatus::Ok, false, SurfaceId{}};
    int best_rank = -1;
    std::size_t best_order = 0;
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        const HitSurface& s = surfaces[i];
        const HitResult r = hit_test_alpha(point, s);
        if (r.status != HitStatus::Ok) {
            return TopmostHit{HitStatus::Invalid, false, SurfaceId{}};  // 報錯不靜默
        }
        if (!r.inside) {
            continue;
        }
        const int rank = layer_rank(s.layer);
        if (!result.hit || rank > best_rank ||
            (rank == best_rank && i > best_order)) {
            // 更高圖層、或同層但宣告較後者，取代為最上層命中。
            result.hit = true;
            result.id = s.id;
            best_rank = rank;
            best_order = i;
        }
    }
    return result;
}

}  // namespace ds::kernel
