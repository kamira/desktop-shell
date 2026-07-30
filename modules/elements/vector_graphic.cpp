// E4-20 向量圖形 — 渲染描述模型實作（見 vector_graphic.hpp 規格）。
#include "vector_graphic.hpp"

#include <cmath>  // std::isfinite
#include <utility>

namespace ds::elements {

namespace {

// 三次貝茲近似圓弧的標準「kappa」常數（單一象限貝茲控制點與半徑的比例）。
constexpr double kCircleKappa = 0.5522847498307936;

bool finite_point(const Point& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// 把不透明度 clamp 至 [0,1]（呼叫前已保證為有限值）。
float clamp_opacity(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// VectorPath
// ---------------------------------------------------------------------------

VectorStatus VectorPath::move_to(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return VectorStatus::Invalid;
    }
    PathCommand cmd;
    cmd.kind = PathCommandKind::MoveTo;
    cmd.to = Point{x, y};
    commands_.push_back(cmd);
    started_ = true;
    closed_ = false;  // 新子路徑開放中（即便前一個子路徑已 close）。
    return VectorStatus::Ok;
}

VectorStatus VectorPath::line_to(double x, double y) {
    if (!started_ || closed_) {
        return VectorStatus::Invalid;  // 未 move_to，或上一子路徑已 close 且未再 move_to
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return VectorStatus::Invalid;
    }
    PathCommand cmd;
    cmd.kind = PathCommandKind::LineTo;
    cmd.to = Point{x, y};
    commands_.push_back(cmd);
    return VectorStatus::Ok;
}

VectorStatus VectorPath::curve_to(double c1x, double c1y, double c2x, double c2y, double x,
                                   double y) {
    if (!started_ || closed_) {
        return VectorStatus::Invalid;
    }
    if (!std::isfinite(c1x) || !std::isfinite(c1y) || !std::isfinite(c2x) ||
        !std::isfinite(c2y) || !std::isfinite(x) || !std::isfinite(y)) {
        return VectorStatus::Invalid;
    }
    PathCommand cmd;
    cmd.kind = PathCommandKind::CurveTo;
    cmd.control1 = Point{c1x, c1y};
    cmd.control2 = Point{c2x, c2y};
    cmd.to = Point{x, y};
    commands_.push_back(cmd);
    return VectorStatus::Ok;
}

VectorStatus VectorPath::close() {
    if (!started_ || closed_) {
        return VectorStatus::Invalid;  // 未開放子路徑，或已 close（不重複 close）
    }
    PathCommand cmd;
    cmd.kind = PathCommandKind::Close;
    commands_.push_back(cmd);
    closed_ = true;
    return VectorStatus::Ok;
}

void VectorPath::clear() noexcept {
    commands_.clear();
    started_ = false;
    closed_ = false;
}

// ---------------------------------------------------------------------------
// Shape 工廠函式
// ---------------------------------------------------------------------------

Shape Shape::make_rect(Point origin, double width, double height) {
    Shape s;
    s.kind = ShapeKind::Rect;
    s.origin = origin;
    s.width = width;
    s.height = height;
    return s;
}

Shape Shape::make_circle(Point center, double radius) {
    Shape s;
    s.kind = ShapeKind::Circle;
    s.center = center;
    s.radius = radius;
    return s;
}

Shape Shape::make_polygon(std::vector<Point> vertices) {
    Shape s;
    s.kind = ShapeKind::Polygon;
    s.vertices = std::move(vertices);
    return s;
}

Shape Shape::make_line(Point from, Point to) {
    Shape s;
    s.kind = ShapeKind::Line;
    s.from = from;
    s.to = to;
    return s;
}

// ---------------------------------------------------------------------------
// build_shape_path
// ---------------------------------------------------------------------------

VectorStatus build_shape_path(const Shape& shape, VectorPath& out) {
    switch (shape.kind) {
        case ShapeKind::Rect: {
            if (!finite_point(shape.origin) || !std::isfinite(shape.width) ||
                !std::isfinite(shape.height)) {
                return VectorStatus::Invalid;
            }
            if (!(shape.width > 0.0) || !(shape.height > 0.0)) {
                return VectorStatus::Invalid;  // 退化：零/負寬高
            }
            VectorPath path;
            path.move_to(shape.origin.x, shape.origin.y);
            path.line_to(shape.origin.x + shape.width, shape.origin.y);
            path.line_to(shape.origin.x + shape.width, shape.origin.y + shape.height);
            path.line_to(shape.origin.x, shape.origin.y + shape.height);
            path.close();
            out = std::move(path);
            return VectorStatus::Ok;
        }
        case ShapeKind::Circle: {
            if (!finite_point(shape.center) || !std::isfinite(shape.radius)) {
                return VectorStatus::Invalid;
            }
            if (!(shape.radius > 0.0)) {
                return VectorStatus::Invalid;  // 退化：零/負半徑
            }
            const double cx = shape.center.x;
            const double cy = shape.center.y;
            const double r = shape.radius;
            const double k = r * kCircleKappa;
            VectorPath path;
            path.move_to(cx + r, cy);
            path.curve_to(cx + r, cy + k, cx + k, cy + r, cx, cy + r);
            path.curve_to(cx - k, cy + r, cx - r, cy + k, cx - r, cy);
            path.curve_to(cx - r, cy - k, cx - k, cy - r, cx, cy - r);
            path.curve_to(cx + k, cy - r, cx + r, cy - k, cx + r, cy);
            path.close();
            out = std::move(path);
            return VectorStatus::Ok;
        }
        case ShapeKind::Polygon: {
            if (shape.vertices.size() < 3) {
                return VectorStatus::Invalid;  // 退化：<3 頂點
            }
            for (const Point& v : shape.vertices) {
                if (!finite_point(v)) {
                    return VectorStatus::Invalid;
                }
            }
            VectorPath path;
            path.move_to(shape.vertices[0].x, shape.vertices[0].y);
            for (std::size_t i = 1; i < shape.vertices.size(); ++i) {
                path.line_to(shape.vertices[i].x, shape.vertices[i].y);
            }
            path.close();
            out = std::move(path);
            return VectorStatus::Ok;
        }
        case ShapeKind::Line: {
            if (!finite_point(shape.from) || !finite_point(shape.to)) {
                return VectorStatus::Invalid;
            }
            if (shape.from.x == shape.to.x && shape.from.y == shape.to.y) {
                return VectorStatus::Invalid;  // 退化：零長度線段
            }
            VectorPath path;
            path.move_to(shape.from.x, shape.from.y);
            path.line_to(shape.to.x, shape.to.y);
            out = std::move(path);  // 線段無面積：不 close
            return VectorStatus::Ok;
        }
    }
    return VectorStatus::Invalid;  // 未知 kind（不可達；保留防禦）
}

// ---------------------------------------------------------------------------
// VectorGraphic
// ---------------------------------------------------------------------------

VectorStatus VectorGraphic::set_stroke(const StrokeStyle& stroke) {
    if (!std::isfinite(stroke.width) || !std::isfinite(stroke.paint.opacity)) {
        return VectorStatus::Invalid;
    }
    if (stroke.enabled && !(stroke.width > 0.0)) {
        return VectorStatus::Invalid;  // 啟用描邊卻寬度非正
    }
    stroke_ = stroke;
    stroke_.paint.opacity = clamp_opacity(stroke_.paint.opacity);
    return VectorStatus::Ok;
}

VectorStatus VectorGraphic::set_fill(const FillStyle& fill) {
    if (!std::isfinite(fill.paint.opacity)) {
        return VectorStatus::Invalid;
    }
    fill_ = fill;
    fill_.paint.opacity = clamp_opacity(fill_.paint.opacity);
    return VectorStatus::Ok;
}

VectorRenderModel VectorGraphic::render_model() const {
    VectorRenderModel model;
    model.commands = path_.commands();
    model.stroke = stroke_;
    model.fill = fill_;
    model.empty = path_.empty();
    return model;
}

}  // namespace ds::elements
