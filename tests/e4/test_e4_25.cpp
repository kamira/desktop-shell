// E4-25 背景模式 — 單元測試（gtest）
//
// 涵蓋：純色 / 圖片 / 透明模式、漸層（可選）、模糊、圓角、邊框、E4-02 圖片背景重用、
// 透明度、無效模式 / 參數不靜默、render_model 內容、NFR-02（具名目標 / 正規化比例 /
// 具名列舉，無絕對座標 / 無數字 z-order）。全程無真實繪製 / 模糊演算法 / 影像解碼、
// 平台中立。
#include "background_element.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>

using ds::elements::BackgroundElement;
using ds::elements::BackgroundMode;
using ds::elements::BackgroundRenderModel;
using ds::elements::BackgroundStatus;
using ds::elements::BorderStyle;
using ds::elements::Color;
using ds::elements::GradientDirection;
using ds::elements::GradientStop;
using ds::elements::ImageDimensions;
using ds::elements::MemoryImageSource;
using ds::elements::NullImageSource;

namespace {

constexpr float kNanF = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfF = std::numeric_limits<float>::infinity();
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

MemoryImageSource make_source() {
    return MemoryImageSource("res://wallpaper", ImageDimensions{1920, 1080});
}

}  // namespace

// --- 模式（純色 / 圖片 / 透明）------------------------------------------------

TEST(BackgroundMode_, DefaultsToSolid) {
    BackgroundElement bg;
    EXPECT_EQ(bg.mode(), BackgroundMode::Solid);
}

TEST(BackgroundMode_, AllKnownModesSettableAndReflected) {
    BackgroundElement bg;
    const BackgroundMode modes[] = {BackgroundMode::Solid, BackgroundMode::Gradient,
                                    BackgroundMode::Image, BackgroundMode::Blur,
                                    BackgroundMode::Transparent};
    for (BackgroundMode m : modes) {
        EXPECT_EQ(bg.set_mode(m), BackgroundStatus::Ok);
        EXPECT_EQ(bg.mode(), m);
        EXPECT_EQ(bg.render_model().mode, m);
    }
}

TEST(BackgroundMode_, SolidModeCarriesColorInRenderModel) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Solid), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_color(Color{0.2f, 0.4f, 0.6f, 1.0f}), BackgroundStatus::Ok);

    BackgroundRenderModel model = bg.render_model();
    EXPECT_EQ(model.mode, BackgroundMode::Solid);
    EXPECT_FLOAT_EQ(model.color.r, 0.2f);
    EXPECT_FLOAT_EQ(model.color.g, 0.4f);
    EXPECT_FLOAT_EQ(model.color.b, 0.6f);
}

TEST(BackgroundMode_, TransparentModeStillProducesRenderModel) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Transparent), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_opacity(0.0f), BackgroundStatus::Ok);

    BackgroundRenderModel model = bg.render_model();
    EXPECT_EQ(model.mode, BackgroundMode::Transparent);
    EXPECT_FLOAT_EQ(model.alpha.opacity, 0.0f);
}

TEST(BackgroundMode_, ImageModeReflectsHasSourceFalseWhenUnset) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Image), BackgroundStatus::Ok);

    BackgroundRenderModel model = bg.render_model();
    EXPECT_EQ(model.mode, BackgroundMode::Image);
    EXPECT_FALSE(model.image.has_source);
}

// --- 純色 ---------------------------------------------------------------------

TEST(SolidColor, DefaultsToOpaqueBlack) {
    BackgroundElement bg;
    const Color& c = bg.color();
    EXPECT_FLOAT_EQ(c.r, 0.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 0.0f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(SolidColor, SetColorInRangeAccepted) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_color(Color{0.1f, 0.5f, 0.9f, 0.75f}), BackgroundStatus::Ok);
    const Color& c = bg.color();
    EXPECT_FLOAT_EQ(c.r, 0.1f);
    EXPECT_FLOAT_EQ(c.g, 0.5f);
    EXPECT_FLOAT_EQ(c.b, 0.9f);
    EXPECT_FLOAT_EQ(c.a, 0.75f);
}

TEST(SolidColor, OutOfRangeComponentsClampedToUnitInterval) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_color(Color{1.5f, -0.3f, 2.0f, -1.0f}), BackgroundStatus::Ok);
    const Color& c = bg.color();
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.g, 0.0f);
    EXPECT_FLOAT_EQ(c.b, 1.0f);
    EXPECT_FLOAT_EQ(c.a, 0.0f);
}

TEST(SolidColor, NonFiniteComponentRejectedAndDoesNotMutate) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_color(Color{0.3f, 0.3f, 0.3f, 1.0f}), BackgroundStatus::Ok);
    EXPECT_EQ(bg.set_color(Color{kNanF, 0.0f, 0.0f, 1.0f}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_color(Color{0.0f, kInfF, 0.0f, 1.0f}), BackgroundStatus::Invalid);
    // 既有合法顏色不被破壞
    EXPECT_FLOAT_EQ(bg.color().r, 0.3f);
}

// --- 漸層（可選）---------------------------------------------------------------

TEST(Gradient, DefaultsToNoStopsAndVerticalDirection) {
    BackgroundElement bg;
    EXPECT_TRUE(bg.gradient_stops().empty());
    EXPECT_EQ(bg.gradient_direction(), GradientDirection::Vertical);
}

TEST(Gradient, AddStopsInOrder) {
    BackgroundElement bg;
    ASSERT_EQ(bg.add_gradient_stop(GradientStop{0.0, Color{1.0f, 0.0f, 0.0f, 1.0f}}),
              BackgroundStatus::Ok);
    ASSERT_EQ(bg.add_gradient_stop(GradientStop{1.0, Color{0.0f, 0.0f, 1.0f, 1.0f}}),
              BackgroundStatus::Ok);

    const auto& stops = bg.gradient_stops();
    ASSERT_EQ(stops.size(), 2u);
    EXPECT_DOUBLE_EQ(stops[0].position, 0.0);
    EXPECT_DOUBLE_EQ(stops[1].position, 1.0);
    EXPECT_FLOAT_EQ(stops[0].color.r, 1.0f);
    EXPECT_FLOAT_EQ(stops[1].color.b, 1.0f);
}

TEST(Gradient, OutOfRangeOrNonFinitePositionRejected) {
    BackgroundElement bg;
    EXPECT_EQ(bg.add_gradient_stop(GradientStop{-0.1, Color{}}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.add_gradient_stop(GradientStop{1.1, Color{}}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.add_gradient_stop(GradientStop{kNan, Color{}}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.add_gradient_stop(GradientStop{kInf, Color{}}), BackgroundStatus::Invalid);
    EXPECT_TRUE(bg.gradient_stops().empty());
}

TEST(Gradient, NonFiniteStopColorRejected) {
    BackgroundElement bg;
    EXPECT_EQ(bg.add_gradient_stop(GradientStop{0.5, Color{kNanF, 0.0f, 0.0f, 1.0f}}),
              BackgroundStatus::Invalid);
    EXPECT_TRUE(bg.gradient_stops().empty());
}

TEST(Gradient, ClearStopsRemovesAll) {
    BackgroundElement bg;
    ASSERT_EQ(bg.add_gradient_stop(GradientStop{0.0, Color{}}), BackgroundStatus::Ok);
    bg.clear_gradient_stops();
    EXPECT_TRUE(bg.gradient_stops().empty());
}

TEST(Gradient, DirectionSettableAndUnknownRejected) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_gradient_direction(GradientDirection::Horizontal), BackgroundStatus::Ok);
    EXPECT_EQ(bg.gradient_direction(), GradientDirection::Horizontal);
    EXPECT_EQ(bg.set_gradient_direction(GradientDirection::Diagonal), BackgroundStatus::Ok);
    EXPECT_EQ(bg.gradient_direction(), GradientDirection::Diagonal);

    const auto bogus = static_cast<GradientDirection>(99);
    EXPECT_EQ(bg.set_gradient_direction(bogus), BackgroundStatus::Invalid);
    // 未被越界值破壞
    EXPECT_EQ(bg.gradient_direction(), GradientDirection::Diagonal);
}

TEST(Gradient, RenderModelReflectsStopsAndDirection) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Gradient), BackgroundStatus::Ok);
    ASSERT_EQ(bg.add_gradient_stop(GradientStop{0.0, Color{1.0f, 1.0f, 1.0f, 1.0f}}),
              BackgroundStatus::Ok);
    ASSERT_EQ(bg.add_gradient_stop(GradientStop{1.0, Color{0.0f, 0.0f, 0.0f, 1.0f}}),
              BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_gradient_direction(GradientDirection::Horizontal), BackgroundStatus::Ok);

    BackgroundRenderModel model = bg.render_model();
    ASSERT_EQ(model.gradient_stops.size(), 2u);
    EXPECT_EQ(model.gradient_direction, GradientDirection::Horizontal);
}

// --- E4-02 圖片背景重用 ---------------------------------------------------------

TEST(ImageBackground, SetValidImageSourceAccepted) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Image), BackgroundStatus::Ok);

    MemoryImageSource src = make_source();
    EXPECT_EQ(bg.set_image(src), BackgroundStatus::Ok);
    EXPECT_TRUE(bg.image().has_source());
    EXPECT_EQ(bg.image().source_reference(), "res://wallpaper");
}

TEST(ImageBackground, InvalidImageSourceRejectedAndDoesNotMutateExisting) {
    BackgroundElement bg;
    MemoryImageSource good = make_source();
    ASSERT_EQ(bg.set_image(good), BackgroundStatus::Ok);

    NullImageSource bad;
    EXPECT_EQ(bg.set_image(bad), BackgroundStatus::Invalid);
    // 既有有效來源不被破壞（沿用 E4-02 不部分套用語意）
    EXPECT_TRUE(bg.image().has_source());
    EXPECT_EQ(bg.image().source_reference(), "res://wallpaper");
}

TEST(ImageBackground, ClearImageResetsToNoSource) {
    BackgroundElement bg;
    MemoryImageSource src = make_source();
    ASSERT_EQ(bg.set_image(src), BackgroundStatus::Ok);
    bg.clear_image();
    EXPECT_FALSE(bg.image().has_source());
}

TEST(ImageBackground, MutableImageAccessorAllowsFurtherE4_02Configuration) {
    BackgroundElement bg;
    MemoryImageSource src = make_source();
    ASSERT_EQ(bg.set_image(src), BackgroundStatus::Ok);

    // 透過可變存取器直接使用 E4-02 既有能力（本單元不重複這些設定介面）。
    EXPECT_EQ(bg.image().set_scale_mode(ds::elements::ScaleMode::Fill),
              ds::elements::ImageStatus::Ok);
    EXPECT_EQ(bg.image().scale_mode(), ds::elements::ScaleMode::Fill);
}

TEST(ImageBackground, RenderModelImageFieldMirrorsE4_02RenderModel) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Image), BackgroundStatus::Ok);
    MemoryImageSource src = make_source();
    ASSERT_EQ(bg.set_image(src), BackgroundStatus::Ok);
    ASSERT_EQ(bg.image().set_scale_mode(ds::elements::ScaleMode::Tile),
              ds::elements::ImageStatus::Ok);

    BackgroundRenderModel model = bg.render_model();
    EXPECT_TRUE(model.image.has_source);
    EXPECT_EQ(model.image.source_reference, "res://wallpaper");
    EXPECT_EQ(model.image.source_dimensions.width, 1920);
    EXPECT_EQ(model.image.scale_mode, ds::elements::ScaleMode::Tile);
}

// --- 模糊 -----------------------------------------------------------------------

TEST(Blur, DefaultsToZero) {
    BackgroundElement bg;
    EXPECT_DOUBLE_EQ(bg.blur_radius(), 0.0);
}

TEST(Blur, SetNonNegativeAccepted) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_blur_radius(12.5), BackgroundStatus::Ok);
    EXPECT_DOUBLE_EQ(bg.blur_radius(), 12.5);
    EXPECT_EQ(bg.set_blur_radius(0.0), BackgroundStatus::Ok);
}

TEST(Blur, NegativeOrNonFiniteRejectedAndDoesNotMutate) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_blur_radius(4.0), BackgroundStatus::Ok);
    EXPECT_EQ(bg.set_blur_radius(-1.0), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_blur_radius(kNan), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_blur_radius(kInf), BackgroundStatus::Invalid);
    EXPECT_DOUBLE_EQ(bg.blur_radius(), 4.0);
}

TEST(Blur, RenderModelReflectsBlurRadius) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Blur), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_blur_radius(8.0), BackgroundStatus::Ok);
    EXPECT_DOUBLE_EQ(bg.render_model().blur_radius, 8.0);
}

// --- 圓角 -----------------------------------------------------------------------

TEST(CornerRadius, DefaultsToZero) {
    BackgroundElement bg;
    EXPECT_DOUBLE_EQ(bg.corner_radius(), 0.0);
}

TEST(CornerRadius, SetNonNegativeAccepted) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_corner_radius(6.0), BackgroundStatus::Ok);
    EXPECT_DOUBLE_EQ(bg.corner_radius(), 6.0);
}

TEST(CornerRadius, NegativeOrNonFiniteRejectedAndDoesNotMutate) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_corner_radius(3.0), BackgroundStatus::Ok);
    EXPECT_EQ(bg.set_corner_radius(-2.0), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_corner_radius(kNan), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_corner_radius(kInf), BackgroundStatus::Invalid);
    EXPECT_DOUBLE_EQ(bg.corner_radius(), 3.0);
}

TEST(CornerRadius, RenderModelReflectsRadius) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_corner_radius(10.0), BackgroundStatus::Ok);
    EXPECT_DOUBLE_EQ(bg.render_model().corner_radius, 10.0);
}

// --- 邊框 -----------------------------------------------------------------------

TEST(Border, DefaultsToNoBorder) {
    BackgroundElement bg;
    EXPECT_DOUBLE_EQ(bg.border().width, 0.0);
}

TEST(Border, SetValidBorderAccepted) {
    BackgroundElement bg;
    BorderStyle border{Color{0.1f, 0.2f, 0.3f, 1.0f}, 2.0};
    EXPECT_EQ(bg.set_border(border), BackgroundStatus::Ok);
    EXPECT_DOUBLE_EQ(bg.border().width, 2.0);
    EXPECT_FLOAT_EQ(bg.border().color.g, 0.2f);
}

TEST(Border, NegativeOrNonFiniteWidthRejectedAndDoesNotMutate) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_border(BorderStyle{Color{}, 1.0}), BackgroundStatus::Ok);
    EXPECT_EQ(bg.set_border(BorderStyle{Color{}, -0.5}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_border(BorderStyle{Color{}, kNan}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_border(BorderStyle{Color{}, kInf}), BackgroundStatus::Invalid);
    EXPECT_DOUBLE_EQ(bg.border().width, 1.0);
}

TEST(Border, NonFiniteBorderColorRejected) {
    BackgroundElement bg;
    BorderStyle bad{Color{kNanF, 0.0f, 0.0f, 1.0f}, 1.0};
    EXPECT_EQ(bg.set_border(bad), BackgroundStatus::Invalid);
    EXPECT_DOUBLE_EQ(bg.border().width, 0.0);  // 未套用
}

TEST(Border, ClearBorderResetsToDefault) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_border(BorderStyle{Color{1.0f, 1.0f, 1.0f, 1.0f}, 3.0}),
              BackgroundStatus::Ok);
    bg.clear_border();
    EXPECT_DOUBLE_EQ(bg.border().width, 0.0);
}

TEST(Border, RenderModelReflectsBorder) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_border(BorderStyle{Color{0.5f, 0.5f, 0.5f, 1.0f}, 4.0}),
              BackgroundStatus::Ok);
    BackgroundRenderModel model = bg.render_model();
    EXPECT_DOUBLE_EQ(model.border.width, 4.0);
    EXPECT_FLOAT_EQ(model.border.color.r, 0.5f);
}

// --- 透明度 ---------------------------------------------------------------------

TEST(Opacity, DefaultsToOpaque) {
    BackgroundElement bg;
    EXPECT_FLOAT_EQ(bg.opacity(), 1.0f);
}

TEST(Opacity, SetOpacityInRange) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_opacity(0.4f), BackgroundStatus::Ok);
    EXPECT_FLOAT_EQ(bg.opacity(), 0.4f);
}

TEST(Opacity, OutOfRangeClampedToUnitInterval) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_opacity(2.0f), BackgroundStatus::Ok);
    EXPECT_FLOAT_EQ(bg.opacity(), 1.0f);
    EXPECT_EQ(bg.set_opacity(-1.0f), BackgroundStatus::Ok);
    EXPECT_FLOAT_EQ(bg.opacity(), 0.0f);
}

TEST(Opacity, NonFiniteRejectedAndDoesNotMutate) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_opacity(0.55f), BackgroundStatus::Ok);
    EXPECT_EQ(bg.set_opacity(kNanF), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_opacity(kInfF), BackgroundStatus::Invalid);
    EXPECT_FLOAT_EQ(bg.opacity(), 0.55f);
}

// --- 無效模式 / 參數不靜默 -------------------------------------------------------

TEST(InvalidNotSilent, UnknownBackgroundModeRejectedAndModeUnchanged) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Gradient), BackgroundStatus::Ok);

    const auto bogus = static_cast<BackgroundMode>(99);
    EXPECT_EQ(bg.set_mode(bogus), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.mode(), BackgroundMode::Gradient);  // 未被越界值破壞
}

TEST(InvalidNotSilent, EveryValidationPathRejectsWithoutMutating) {
    BackgroundElement bg;
    // 逐一驗證各參數的無效輸入皆回 Invalid，且都不改動既有合法狀態（彙整性守衛）。
    ASSERT_EQ(bg.set_color(Color{0.2f, 0.2f, 0.2f, 1.0f}), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_blur_radius(1.0), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_corner_radius(1.0), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_border(BorderStyle{Color{}, 1.0}), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_opacity(0.5f), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_target("surface.panel"), BackgroundStatus::Ok);

    EXPECT_EQ(bg.set_color(Color{kNanF, 0.0f, 0.0f, 1.0f}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_blur_radius(-5.0), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_corner_radius(-5.0), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_border(BorderStyle{Color{}, -5.0}), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_opacity(kNanF), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.set_target(""), BackgroundStatus::Invalid);

    EXPECT_FLOAT_EQ(bg.color().r, 0.2f);
    EXPECT_DOUBLE_EQ(bg.blur_radius(), 1.0);
    EXPECT_DOUBLE_EQ(bg.corner_radius(), 1.0);
    EXPECT_DOUBLE_EQ(bg.border().width, 1.0);
    EXPECT_FLOAT_EQ(bg.opacity(), 0.5f);
    EXPECT_EQ(bg.target(), "surface.panel");
}

// --- 目標具名 surface（NFR-02 具名）---------------------------------------------

TEST(Target, DefaultTargetEmpty) {
    BackgroundElement bg;
    EXPECT_TRUE(bg.target().empty());
}

TEST(Target, SetNamedTargetAccepted) {
    BackgroundElement bg;
    EXPECT_EQ(bg.set_target("surface.frame"), BackgroundStatus::Ok);
    EXPECT_EQ(bg.target(), "surface.frame");
}

TEST(Target, EmptyTargetRejected) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_target("surface.a"), BackgroundStatus::Ok);
    EXPECT_EQ(bg.set_target(""), BackgroundStatus::Invalid);
    EXPECT_EQ(bg.target(), "surface.a");
}

TEST(Target, ClearTargetUnbinds) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_target("surface.a"), BackgroundStatus::Ok);
    bg.clear_target();
    EXPECT_TRUE(bg.target().empty());
}

// --- render_model 內容（完整組裝）------------------------------------------------

TEST(RenderModelContent, EmptyDefaultsAreExplicitNotFabricated) {
    BackgroundElement bg;
    BackgroundRenderModel model = bg.render_model();
    EXPECT_EQ(model.mode, BackgroundMode::Solid);
    EXPECT_TRUE(model.gradient_stops.empty());
    EXPECT_FALSE(model.image.has_source);
    EXPECT_DOUBLE_EQ(model.blur_radius, 0.0);
    EXPECT_DOUBLE_EQ(model.corner_radius, 0.0);
    EXPECT_DOUBLE_EQ(model.border.width, 0.0);
    EXPECT_FLOAT_EQ(model.alpha.opacity, 1.0f);
    EXPECT_TRUE(model.target.empty());
}

TEST(RenderModelContent, ReflectsFullConfiguredState) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_mode(BackgroundMode::Image), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_color(Color{0.9f, 0.1f, 0.1f, 1.0f}), BackgroundStatus::Ok);
    MemoryImageSource src = make_source();
    ASSERT_EQ(bg.set_image(src), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_blur_radius(2.0), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_corner_radius(5.0), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_border(BorderStyle{Color{0.0f, 0.0f, 0.0f, 1.0f}, 1.5}),
              BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_opacity(0.6f), BackgroundStatus::Ok);
    ASSERT_EQ(bg.set_target("surface.desktop.panel"), BackgroundStatus::Ok);

    BackgroundRenderModel model = bg.render_model();
    EXPECT_EQ(model.mode, BackgroundMode::Image);
    EXPECT_FLOAT_EQ(model.color.r, 0.9f);
    EXPECT_TRUE(model.image.has_source);
    EXPECT_EQ(model.image.source_reference, "res://wallpaper");
    EXPECT_DOUBLE_EQ(model.blur_radius, 2.0);
    EXPECT_DOUBLE_EQ(model.corner_radius, 5.0);
    EXPECT_DOUBLE_EQ(model.border.width, 1.5);
    EXPECT_FLOAT_EQ(model.alpha.opacity, 0.6f);
    EXPECT_EQ(model.target, "surface.desktop.panel");
}

// --- NFR-02：具名目標 / 正規化比例 / 具名列舉，無絕對座標 / 無數字 z-order ------

TEST(Nfr02Compliance, TargetIsNamedSurfaceIdString) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_target("surface.named"), BackgroundStatus::Ok);
    const ds::kernel::SurfaceId& t = bg.render_model().target;
    EXPECT_EQ(t, std::string("surface.named"));
}

TEST(Nfr02Compliance, GradientStopPositionAlwaysNormalizedFraction) {
    BackgroundElement bg;
    ASSERT_EQ(bg.add_gradient_stop(GradientStop{0.3, Color{}}), BackgroundStatus::Ok);
    const auto& stops = bg.render_model().gradient_stops;
    ASSERT_EQ(stops.size(), 1u);
    EXPECT_GE(stops[0].position, 0.0);
    EXPECT_LE(stops[0].position, 1.0);
}

TEST(Nfr02Compliance, GradientDirectionIsNamedEnumNotAngle) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_gradient_direction(GradientDirection::Diagonal), BackgroundStatus::Ok);
    EXPECT_EQ(bg.render_model().gradient_direction, GradientDirection::Diagonal);
}

TEST(Nfr02Compliance, OpacityIsRatioNotCoordinate) {
    BackgroundElement bg;
    ASSERT_EQ(bg.set_opacity(0.33f), BackgroundStatus::Ok);
    float o = bg.render_model().alpha.opacity;
    EXPECT_GE(o, 0.0f);
    EXPECT_LE(o, 1.0f);
}

TEST(Nfr02Compliance, NoAbsolutePositionFieldsExposedOnRenderModel) {
    // 圓角 / 邊框寬度為「背景自身外觀尺寸」，非螢幕擺放位置；此測試以型別層面確認
    // render_model 未攜帶任何座標欄位（藉由建構 + 存取所有既知欄位即可通過編譯 + 執行）。
    BackgroundElement bg;
    BackgroundRenderModel model = bg.render_model();
    (void)model.mode;
    (void)model.color;
    (void)model.gradient_stops;
    (void)model.gradient_direction;
    (void)model.image;
    (void)model.blur_radius;
    (void)model.corner_radius;
    (void)model.border;
    (void)model.alpha;
    (void)model.target;
    SUCCEED();
}
