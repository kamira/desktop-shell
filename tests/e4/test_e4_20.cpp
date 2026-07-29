// E4-20 向量圖形 — 單元測試（gtest）
//
// 涵蓋：路徑指令序列（moveto/lineto/curveto/close）、基本形狀（矩形/圓/多邊形/線段）、
// 描邊/填色屬性、close path、無效路徑報錯、退化形狀報錯、render_model。
// 全程無真實繪製、平台中立、無絕對座標 / 無數字 z-order（NFR-02）。
#include "vector_graphic.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using ds::elements::build_shape_path;
using ds::elements::FillStyle;
using ds::elements::PathCommandKind;
using ds::elements::Point;
using ds::elements::Shape;
using ds::elements::StrokeStyle;
using ds::elements::VectorGraphic;
using ds::elements::VectorPath;
using ds::elements::VectorRenderModel;
using ds::elements::VectorStatus;

namespace {

constexpr double kEps = 1e-9;
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

}  // namespace

// --- VectorPath：路徑指令序列 -------------------------------------------------

TEST(VectorPath, MoveLineCloseProducesExpectedCommandSequence) {
    VectorPath p;
    ASSERT_EQ(p.move_to(0.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.line_to(10.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.line_to(10.0, 10.0), VectorStatus::Ok);
    ASSERT_EQ(p.close(), VectorStatus::Ok);

    ASSERT_EQ(p.commands().size(), 4u);
    EXPECT_EQ(p.commands()[0].kind, PathCommandKind::MoveTo);
    EXPECT_DOUBLE_EQ(p.commands()[0].to.x, 0.0);
    EXPECT_DOUBLE_EQ(p.commands()[0].to.y, 0.0);
    EXPECT_EQ(p.commands()[1].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(p.commands()[1].to.x, 10.0);
    EXPECT_EQ(p.commands()[2].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(p.commands()[2].to.y, 10.0);
    EXPECT_EQ(p.commands()[3].kind, PathCommandKind::Close);
    EXPECT_FALSE(p.empty());
    EXPECT_FALSE(p.is_open_subpath());  // 已 close
}

TEST(VectorPath, CurveToRecordsControlPointsAndEndpoint) {
    VectorPath p;
    ASSERT_EQ(p.move_to(0.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.curve_to(1.0, 2.0, 3.0, 4.0, 5.0, 6.0), VectorStatus::Ok);

    ASSERT_EQ(p.commands().size(), 2u);
    const auto& c = p.commands()[1];
    EXPECT_EQ(c.kind, PathCommandKind::CurveTo);
    EXPECT_DOUBLE_EQ(c.control1.x, 1.0);
    EXPECT_DOUBLE_EQ(c.control1.y, 2.0);
    EXPECT_DOUBLE_EQ(c.control2.x, 3.0);
    EXPECT_DOUBLE_EQ(c.control2.y, 4.0);
    EXPECT_DOUBLE_EQ(c.to.x, 5.0);
    EXPECT_DOUBLE_EQ(c.to.y, 6.0);
}

TEST(VectorPath, MoveToAfterCloseStartsNewOpenSubpath) {
    VectorPath p;
    ASSERT_EQ(p.move_to(0.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.close(), VectorStatus::Ok);
    EXPECT_FALSE(p.is_open_subpath());

    ASSERT_EQ(p.move_to(5.0, 5.0), VectorStatus::Ok);
    EXPECT_TRUE(p.is_open_subpath());
    ASSERT_EQ(p.line_to(6.0, 6.0), VectorStatus::Ok);
    ASSERT_EQ(p.commands().size(), 4u);
}

TEST(VectorPath, ClearResetsToInitialState) {
    VectorPath p;
    ASSERT_EQ(p.move_to(0.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.line_to(1.0, 1.0), VectorStatus::Ok);
    p.clear();
    EXPECT_TRUE(p.empty());
    EXPECT_FALSE(p.is_open_subpath());
    // clear 後如同全新狀態：line_to 之前仍須先 move_to。
    EXPECT_EQ(p.line_to(1.0, 1.0), VectorStatus::Invalid);
}

// --- 無效路徑：不靜默報錯 -----------------------------------------------------

TEST(VectorPath, LineToBeforeMoveToIsInvalidAndNotAppended) {
    VectorPath p;
    EXPECT_EQ(p.line_to(1.0, 1.0), VectorStatus::Invalid);
    EXPECT_TRUE(p.empty());  // 未追加任何指令
}

TEST(VectorPath, CurveToBeforeMoveToIsInvalid) {
    VectorPath p;
    EXPECT_EQ(p.curve_to(0, 0, 1, 1, 2, 2), VectorStatus::Invalid);
    EXPECT_TRUE(p.empty());
}

TEST(VectorPath, CloseBeforeMoveToIsInvalid) {
    VectorPath p;
    EXPECT_EQ(p.close(), VectorStatus::Invalid);
    EXPECT_TRUE(p.empty());
}

TEST(VectorPath, DoubleCloseWithoutNewMoveToIsInvalid) {
    VectorPath p;
    ASSERT_EQ(p.move_to(0.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.line_to(1.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(p.close(), VectorStatus::Ok);
    // 已 close 且未再 move_to：line_to / close 皆 Invalid，且不追加指令。
    EXPECT_EQ(p.line_to(2.0, 0.0), VectorStatus::Invalid);
    EXPECT_EQ(p.close(), VectorStatus::Invalid);
    EXPECT_EQ(p.commands().size(), 3u);  // move+line+close，之後兩次失敗嘗試皆未追加
}

TEST(VectorPath, NonFiniteCoordinatesRejected) {
    VectorPath p;
    EXPECT_EQ(p.move_to(kNan, 0.0), VectorStatus::Invalid);
    EXPECT_EQ(p.move_to(0.0, kInf), VectorStatus::Invalid);
    ASSERT_EQ(p.move_to(0.0, 0.0), VectorStatus::Ok);
    EXPECT_EQ(p.line_to(kNan, 1.0), VectorStatus::Invalid);
    EXPECT_EQ(p.curve_to(kNan, 0, 1, 1, 2, 2), VectorStatus::Invalid);
    EXPECT_EQ(p.commands().size(), 1u);  // 只有最初那次成功的 move_to
}

// --- 基本形狀：Rect / Circle / Polygon / Line ---------------------------------

TEST(BuildShapePath, RectProducesFourCornersAndClose) {
    const Shape rect = Shape::make_rect(Point{1.0, 2.0}, 10.0, 5.0);
    VectorPath path;
    ASSERT_EQ(build_shape_path(rect, path), VectorStatus::Ok);

    ASSERT_EQ(path.commands().size(), 5u);  // move + 3 line + close
    EXPECT_EQ(path.commands()[0].kind, PathCommandKind::MoveTo);
    EXPECT_DOUBLE_EQ(path.commands()[0].to.x, 1.0);
    EXPECT_DOUBLE_EQ(path.commands()[0].to.y, 2.0);
    EXPECT_EQ(path.commands()[1].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(path.commands()[1].to.x, 11.0);
    EXPECT_DOUBLE_EQ(path.commands()[1].to.y, 2.0);
    EXPECT_EQ(path.commands()[2].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(path.commands()[2].to.x, 11.0);
    EXPECT_DOUBLE_EQ(path.commands()[2].to.y, 7.0);
    EXPECT_EQ(path.commands()[3].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(path.commands()[3].to.x, 1.0);
    EXPECT_DOUBLE_EQ(path.commands()[3].to.y, 7.0);
    EXPECT_EQ(path.commands()[4].kind, PathCommandKind::Close);
    EXPECT_FALSE(path.is_open_subpath());
}

TEST(BuildShapePath, DegenerateRectRejected) {
    VectorPath path;
    EXPECT_EQ(build_shape_path(Shape::make_rect(Point{0, 0}, 0.0, 5.0), path),
              VectorStatus::Invalid);
    EXPECT_EQ(build_shape_path(Shape::make_rect(Point{0, 0}, 5.0, -1.0), path),
              VectorStatus::Invalid);
    EXPECT_TRUE(path.empty());  // 未寫入輸出
}

TEST(BuildShapePath, CircleProducesFourCurvesAndClose) {
    const Shape circle = Shape::make_circle(Point{0.0, 0.0}, 3.0);
    VectorPath path;
    ASSERT_EQ(build_shape_path(circle, path), VectorStatus::Ok);

    ASSERT_EQ(path.commands().size(), 6u);  // move + 4 curve + close
    EXPECT_EQ(path.commands()[0].kind, PathCommandKind::MoveTo);
    EXPECT_NEAR(path.commands()[0].to.x, 3.0, kEps);
    EXPECT_NEAR(path.commands()[0].to.y, 0.0, kEps);
    for (int i = 1; i <= 4; ++i) {
        EXPECT_EQ(path.commands()[static_cast<std::size_t>(i)].kind, PathCommandKind::CurveTo);
    }
    EXPECT_EQ(path.commands()[5].kind, PathCommandKind::Close);
    // 最後一個貝茲曲線終點應回到起點附近（近似圓弧的閉合點）。
    EXPECT_NEAR(path.commands()[4].to.x, 3.0, kEps);
    EXPECT_NEAR(path.commands()[4].to.y, 0.0, kEps);
}

TEST(BuildShapePath, DegenerateCircleRejected) {
    VectorPath path;
    EXPECT_EQ(build_shape_path(Shape::make_circle(Point{0, 0}, 0.0), path), VectorStatus::Invalid);
    EXPECT_EQ(build_shape_path(Shape::make_circle(Point{0, 0}, -2.0), path),
              VectorStatus::Invalid);
    EXPECT_TRUE(path.empty());
}

TEST(BuildShapePath, PolygonProducesMoveLinesAndClose) {
    const Shape tri =
        Shape::make_polygon({Point{0.0, 0.0}, Point{4.0, 0.0}, Point{2.0, 3.0}});
    VectorPath path;
    ASSERT_EQ(build_shape_path(tri, path), VectorStatus::Ok);

    ASSERT_EQ(path.commands().size(), 4u);  // move + 2 line + close
    EXPECT_EQ(path.commands()[0].kind, PathCommandKind::MoveTo);
    EXPECT_EQ(path.commands()[1].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(path.commands()[1].to.x, 4.0);
    EXPECT_EQ(path.commands()[2].kind, PathCommandKind::LineTo);
    EXPECT_DOUBLE_EQ(path.commands()[2].to.y, 3.0);
    EXPECT_EQ(path.commands()[3].kind, PathCommandKind::Close);
}

TEST(BuildShapePath, DegeneratePolygonFewerThanThreeVerticesRejected) {
    VectorPath path;
    EXPECT_EQ(build_shape_path(Shape::make_polygon({Point{0, 0}, Point{1, 1}}), path),
              VectorStatus::Invalid);
    EXPECT_EQ(build_shape_path(Shape::make_polygon({}), path), VectorStatus::Invalid);
    EXPECT_TRUE(path.empty());
}

TEST(BuildShapePath, LineProducesOpenTwoCommandPath) {
    const Shape line = Shape::make_line(Point{0.0, 0.0}, Point{5.0, 5.0});
    VectorPath path;
    ASSERT_EQ(build_shape_path(line, path), VectorStatus::Ok);

    ASSERT_EQ(path.commands().size(), 2u);  // move + line；線段無面積不 close
    EXPECT_EQ(path.commands()[0].kind, PathCommandKind::MoveTo);
    EXPECT_EQ(path.commands()[1].kind, PathCommandKind::LineTo);
    EXPECT_TRUE(path.is_open_subpath());
}

TEST(BuildShapePath, DegenerateZeroLengthLineRejected) {
    VectorPath path;
    EXPECT_EQ(build_shape_path(Shape::make_line(Point{1, 1}, Point{1, 1}), path),
              VectorStatus::Invalid);
    EXPECT_TRUE(path.empty());
}

TEST(BuildShapePath, NonFiniteShapeFieldsRejected) {
    VectorPath path;
    EXPECT_EQ(build_shape_path(Shape::make_rect(Point{kNan, 0}, 1.0, 1.0), path),
              VectorStatus::Invalid);
    EXPECT_EQ(build_shape_path(Shape::make_circle(Point{0, 0}, kInf), path),
              VectorStatus::Invalid);
    EXPECT_TRUE(path.empty());
}

// --- 描邊 / 填色屬性 -----------------------------------------------------------

TEST(VectorGraphicStyle, SetStrokeAndFillSucceedWithDefaults) {
    VectorGraphic g;
    StrokeStyle stroke;
    stroke.enabled = true;
    stroke.width = 2.5;
    stroke.paint.opacity = 0.75f;
    ASSERT_EQ(g.set_stroke(stroke), VectorStatus::Ok);
    EXPECT_TRUE(g.stroke().enabled);
    EXPECT_DOUBLE_EQ(g.stroke().width, 2.5);
    EXPECT_FLOAT_EQ(g.stroke().paint.opacity, 0.75f);

    FillStyle fill;
    fill.enabled = true;
    fill.paint.opacity = 0.3f;
    ASSERT_EQ(g.set_fill(fill), VectorStatus::Ok);
    EXPECT_TRUE(g.fill().enabled);
    EXPECT_FLOAT_EQ(g.fill().paint.opacity, 0.3f);
}

TEST(VectorGraphicStyle, StrokeOpacityClampedToUnitRange) {
    VectorGraphic g;
    StrokeStyle stroke;
    stroke.enabled = false;  // 未啟用時寬度規則不適用，但仍驗證有限值
    stroke.width = 1.0;
    stroke.paint.opacity = 5.0f;
    ASSERT_EQ(g.set_stroke(stroke), VectorStatus::Ok);
    EXPECT_FLOAT_EQ(g.stroke().paint.opacity, 1.0f);

    stroke.paint.opacity = -3.0f;
    ASSERT_EQ(g.set_stroke(stroke), VectorStatus::Ok);
    EXPECT_FLOAT_EQ(g.stroke().paint.opacity, 0.0f);
}

TEST(VectorGraphicStyle, EnabledStrokeWithNonPositiveWidthRejected) {
    VectorGraphic g;
    StrokeStyle stroke;
    stroke.enabled = true;
    stroke.width = 0.0;
    EXPECT_EQ(g.set_stroke(stroke), VectorStatus::Invalid);

    stroke.width = -1.0;
    EXPECT_EQ(g.set_stroke(stroke), VectorStatus::Invalid);

    // 未套用：仍是預設值。
    EXPECT_FALSE(g.stroke().enabled);
    EXPECT_DOUBLE_EQ(g.stroke().width, 1.0);
}

TEST(VectorGraphicStyle, NonFiniteStrokeOrFillOpacityRejected) {
    VectorGraphic g;
    StrokeStyle stroke;
    stroke.paint.opacity = kNan;
    EXPECT_EQ(g.set_stroke(stroke), VectorStatus::Invalid);

    StrokeStyle stroke2;
    stroke2.width = kInf;
    EXPECT_EQ(g.set_stroke(stroke2), VectorStatus::Invalid);

    FillStyle fill;
    fill.paint.opacity = kInf;
    EXPECT_EQ(g.set_fill(fill), VectorStatus::Invalid);
}

// --- render_model --------------------------------------------------------------

TEST(VectorGraphicRenderModel, EmptyGraphicReportsEmptyExplicitly) {
    VectorGraphic g;
    const VectorRenderModel m = g.render_model();
    EXPECT_TRUE(m.empty);
    EXPECT_TRUE(m.commands.empty());
}

TEST(VectorGraphicRenderModel, ReflectsPathAndStyles) {
    VectorGraphic g;
    VectorPath path;
    ASSERT_EQ(build_shape_path(Shape::make_rect(Point{0, 0}, 4.0, 2.0), path),
              VectorStatus::Ok);
    g.set_path(path);

    StrokeStyle stroke;
    stroke.enabled = true;
    stroke.width = 1.5;
    stroke.paint.opacity = 0.9f;
    ASSERT_EQ(g.set_stroke(stroke), VectorStatus::Ok);

    FillStyle fill;
    fill.enabled = true;
    fill.paint.opacity = 0.4f;
    ASSERT_EQ(g.set_fill(fill), VectorStatus::Ok);

    const VectorRenderModel m = g.render_model();
    EXPECT_FALSE(m.empty);
    ASSERT_EQ(m.commands.size(), 5u);  // rect：move + 3 line + close
    EXPECT_EQ(m.commands.front().kind, PathCommandKind::MoveTo);
    EXPECT_EQ(m.commands.back().kind, PathCommandKind::Close);
    EXPECT_TRUE(m.stroke.enabled);
    EXPECT_DOUBLE_EQ(m.stroke.width, 1.5);
    EXPECT_FLOAT_EQ(m.stroke.paint.opacity, 0.9f);
    EXPECT_TRUE(m.fill.enabled);
    EXPECT_FLOAT_EQ(m.fill.paint.opacity, 0.4f);
}

TEST(VectorGraphicRenderModel, ManuallyBuiltOpenPathIsNotEmptyButUnclosed) {
    VectorGraphic g;
    VectorPath path;
    ASSERT_EQ(path.move_to(0.0, 0.0), VectorStatus::Ok);
    ASSERT_EQ(path.line_to(1.0, 1.0), VectorStatus::Ok);
    g.set_path(path);

    const VectorRenderModel m = g.render_model();
    EXPECT_FALSE(m.empty);
    ASSERT_EQ(m.commands.size(), 2u);
    EXPECT_NE(m.commands.back().kind, PathCommandKind::Close);
}
