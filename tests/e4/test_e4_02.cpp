// E4-02 圖片元件 — 單元測試（gtest）
//
// 涵蓋：載入注入來源、各縮放模式(fill/fit/stretch/center/tile)、裁切、透明度、
// 無效來源報錯、尺寸為零處理、render_model 內容、NFR-02（具名目標 / 正規化比例裁切）。
// 全程無真實影像解碼 / 繪製、平台中立、無絕對座標 / 無數字 z-order。
#include "image_element.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>

using ds::elements::CropRect;
using ds::elements::ImageDimensions;
using ds::elements::ImageElement;
using ds::elements::ImageRenderModel;
using ds::elements::ImageSource;
using ds::elements::ImageStatus;
using ds::elements::MemoryImageSource;
using ds::elements::NullImageSource;
using ds::elements::ScaleMode;

namespace {

constexpr float kNanF = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfF = std::numeric_limits<float>::infinity();
constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// 一個尋常的有效來源：具名參照 + 固有尺寸。
MemoryImageSource make_source() {
    return MemoryImageSource("res://icon", ImageDimensions{128, 64});
}

}  // namespace

// --- 載入注入來源 -------------------------------------------------------------

TEST(ImageSourceAbstraction, MemorySourceExposesReferenceAndDimensions) {
    MemoryImageSource src("res://wallpaper", ImageDimensions{1920, 1080});
    EXPECT_TRUE(src.valid());
    EXPECT_EQ(src.reference(), "res://wallpaper");
    EXPECT_EQ(src.dimensions().width, 1920);
    EXPECT_EQ(src.dimensions().height, 1080);
}

TEST(ImageSourceAbstraction, MemorySourceInvalidWhenReferenceEmptyOrDimsNonPositive) {
    EXPECT_FALSE(MemoryImageSource("", ImageDimensions{10, 10}).valid());
    EXPECT_FALSE(MemoryImageSource("res://x", ImageDimensions{0, 10}).valid());
    EXPECT_FALSE(MemoryImageSource("res://x", ImageDimensions{10, 0}).valid());
    EXPECT_FALSE(MemoryImageSource("res://x", ImageDimensions{-5, 10}).valid());
}

TEST(ImageSourceAbstraction, NullSourceIsAlwaysInvalidAndEmpty) {
    NullImageSource src;
    EXPECT_FALSE(src.valid());
    EXPECT_TRUE(src.reference().empty());
    EXPECT_EQ(src.dimensions().width, 0);
    EXPECT_EQ(src.dimensions().height, 0);
}

TEST(ImageElementLoad, LoadValidInjectedSourceCopiesReferenceAndDimensions) {
    ImageElement img;
    EXPECT_FALSE(img.has_source());

    MemoryImageSource src = make_source();
    EXPECT_EQ(img.set_source(src), ImageStatus::Ok);

    EXPECT_TRUE(img.has_source());
    EXPECT_EQ(img.source_reference(), "res://icon");
    EXPECT_EQ(img.source_dimensions().width, 128);
    EXPECT_EQ(img.source_dimensions().height, 64);
}

TEST(ImageElementLoad, LoadCopiesByValueSourceMayOutliveElement) {
    ImageElement img;
    {
        MemoryImageSource transient("res://temp", ImageDimensions{32, 32});
        ASSERT_EQ(img.set_source(transient), ImageStatus::Ok);
    }  // transient 銷毀後元件仍保有複製的參照 / 尺寸（值語意）
    EXPECT_TRUE(img.has_source());
    EXPECT_EQ(img.source_reference(), "res://temp");
    EXPECT_EQ(img.source_dimensions().width, 32);
}

TEST(ImageElementLoad, ClearSourceResetsToEmptyButKeepsOtherState) {
    ImageElement img;
    MemoryImageSource src = make_source();
    ASSERT_EQ(img.set_source(src), ImageStatus::Ok);
    ASSERT_EQ(img.set_scale_mode(ScaleMode::Tile), ImageStatus::Ok);
    ASSERT_EQ(img.set_opacity(0.5f), ImageStatus::Ok);

    img.clear_source();
    EXPECT_FALSE(img.has_source());
    EXPECT_TRUE(img.source_reference().empty());
    EXPECT_EQ(img.source_dimensions().width, 0);
    EXPECT_EQ(img.source_dimensions().height, 0);
    // 其餘狀態不受卸載影響
    EXPECT_EQ(img.scale_mode(), ScaleMode::Tile);
    EXPECT_FLOAT_EQ(img.opacity(), 0.5f);
}

// --- 無效來源 / 尺寸為零報錯（不靜默）----------------------------------------

TEST(ImageElementLoad, InvalidSourceRejectedAndDoesNotMutateExisting) {
    ImageElement img;
    MemoryImageSource good = make_source();
    ASSERT_EQ(img.set_source(good), ImageStatus::Ok);

    NullImageSource bad;
    EXPECT_EQ(img.set_source(bad), ImageStatus::Invalid);
    // 既有有效來源不被無效載入破壞（不部分套用）
    EXPECT_TRUE(img.has_source());
    EXPECT_EQ(img.source_reference(), "res://icon");
}

TEST(ImageElementLoad, ZeroDimensionSourceRejected) {
    ImageElement img;
    MemoryImageSource zero_w("res://z", ImageDimensions{0, 10});
    MemoryImageSource zero_h("res://z", ImageDimensions{10, 0});
    EXPECT_EQ(img.set_source(zero_w), ImageStatus::Invalid);
    EXPECT_EQ(img.set_source(zero_h), ImageStatus::Invalid);
    EXPECT_FALSE(img.has_source());
}

TEST(ImageElementLoad, EmptyReferenceSourceRejected) {
    ImageElement img;
    MemoryImageSource empty_ref("", ImageDimensions{10, 10});
    EXPECT_EQ(img.set_source(empty_ref), ImageStatus::Invalid);
    EXPECT_FALSE(img.has_source());
}

// --- 縮放模式 -----------------------------------------------------------------

TEST(ImageElementScaleMode, DefaultsToFit) {
    ImageElement img;
    EXPECT_EQ(img.scale_mode(), ScaleMode::Fit);
}

TEST(ImageElementScaleMode, AllModesSettableAndReflected) {
    ImageElement img;
    const ScaleMode modes[] = {ScaleMode::Fill, ScaleMode::Fit, ScaleMode::Stretch,
                               ScaleMode::Center, ScaleMode::Tile};
    for (ScaleMode m : modes) {
        EXPECT_EQ(img.set_scale_mode(m), ImageStatus::Ok);
        EXPECT_EQ(img.scale_mode(), m);
        EXPECT_EQ(img.render_model().scale_mode, m);
    }
}

// --- 裁切（正規化 [0,1]，NFR-02 比例）----------------------------------------

TEST(ImageElementCrop, DefaultsToFullImage) {
    ImageElement img;
    const CropRect& c = img.crop();
    EXPECT_DOUBLE_EQ(c.x, 0.0);
    EXPECT_DOUBLE_EQ(c.y, 0.0);
    EXPECT_DOUBLE_EQ(c.width, 1.0);
    EXPECT_DOUBLE_EQ(c.height, 1.0);
}

TEST(ImageElementCrop, ValidNormalizedCropAccepted) {
    ImageElement img;
    CropRect c{0.25, 0.1, 0.5, 0.8};
    EXPECT_EQ(img.set_crop(c), ImageStatus::Ok);
    EXPECT_DOUBLE_EQ(img.crop().x, 0.25);
    EXPECT_DOUBLE_EQ(img.crop().width, 0.5);
    EXPECT_DOUBLE_EQ(img.crop().height, 0.8);
}

TEST(ImageElementCrop, FullEdgeCropAccepted) {
    ImageElement img;
    EXPECT_EQ(img.set_crop(CropRect::full()), ImageStatus::Ok);
    CropRect edge{0.0, 0.0, 1.0, 1.0};
    EXPECT_EQ(img.set_crop(edge), ImageStatus::Ok);
}

TEST(ImageElementCrop, OutOfRangeCropRejected) {
    ImageElement img;
    // 負起點
    EXPECT_EQ(img.set_crop(CropRect{-0.1, 0.0, 0.5, 0.5}), ImageStatus::Invalid);
    EXPECT_EQ(img.set_crop(CropRect{0.0, -0.1, 0.5, 0.5}), ImageStatus::Invalid);
    // 溢出右 / 下界
    EXPECT_EQ(img.set_crop(CropRect{0.6, 0.0, 0.5, 0.5}), ImageStatus::Invalid);
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.7, 0.5, 0.5}), ImageStatus::Invalid);
    // 寬 / 高本身 > 1
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.0, 1.5, 0.5}), ImageStatus::Invalid);
}

TEST(ImageElementCrop, DegenerateCropRejected) {
    ImageElement img;
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.0, 0.0, 0.5}), ImageStatus::Invalid);  // 零寬
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.0, 0.5, 0.0}), ImageStatus::Invalid);  // 零高
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.0, -0.5, 0.5}), ImageStatus::Invalid); // 負寬
}

TEST(ImageElementCrop, NonFiniteCropRejected) {
    ImageElement img;
    EXPECT_EQ(img.set_crop(CropRect{kNan, 0.0, 0.5, 0.5}), ImageStatus::Invalid);
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.0, kInf, 0.5}), ImageStatus::Invalid);
}

TEST(ImageElementCrop, InvalidCropDoesNotMutateExisting) {
    ImageElement img;
    ASSERT_EQ(img.set_crop(CropRect{0.1, 0.1, 0.5, 0.5}), ImageStatus::Ok);
    EXPECT_EQ(img.set_crop(CropRect{0.0, 0.0, 2.0, 2.0}), ImageStatus::Invalid);
    // 維持先前合法裁切
    EXPECT_DOUBLE_EQ(img.crop().width, 0.5);
    EXPECT_DOUBLE_EQ(img.crop().x, 0.1);
}

TEST(ImageElementCrop, ClearCropRestoresFull) {
    ImageElement img;
    ASSERT_EQ(img.set_crop(CropRect{0.2, 0.2, 0.3, 0.3}), ImageStatus::Ok);
    img.clear_crop();
    EXPECT_DOUBLE_EQ(img.crop().x, 0.0);
    EXPECT_DOUBLE_EQ(img.crop().width, 1.0);
    EXPECT_DOUBLE_EQ(img.crop().height, 1.0);
}

// --- 透明度 -------------------------------------------------------------------

TEST(ImageElementOpacity, DefaultsToOpaque) {
    ImageElement img;
    EXPECT_FLOAT_EQ(img.opacity(), 1.0f);
}

TEST(ImageElementOpacity, SetOpacityInRange) {
    ImageElement img;
    EXPECT_EQ(img.set_opacity(0.3f), ImageStatus::Ok);
    EXPECT_FLOAT_EQ(img.opacity(), 0.3f);
}

TEST(ImageElementOpacity, OpacityClampedToUnitInterval) {
    ImageElement img;
    EXPECT_EQ(img.set_opacity(1.7f), ImageStatus::Ok);
    EXPECT_FLOAT_EQ(img.opacity(), 1.0f);
    EXPECT_EQ(img.set_opacity(-0.4f), ImageStatus::Ok);
    EXPECT_FLOAT_EQ(img.opacity(), 0.0f);
}

TEST(ImageElementOpacity, NonFiniteOpacityRejectedAndDoesNotMutate) {
    ImageElement img;
    ASSERT_EQ(img.set_opacity(0.6f), ImageStatus::Ok);
    EXPECT_EQ(img.set_opacity(kNanF), ImageStatus::Invalid);
    EXPECT_EQ(img.set_opacity(kInfF), ImageStatus::Invalid);
    EXPECT_FLOAT_EQ(img.opacity(), 0.6f);  // 未被靜默改動
}

// --- 目標具名 surface（NFR-02 具名）-----------------------------------------

TEST(ImageElementTarget, DefaultTargetEmpty) {
    ImageElement img;
    EXPECT_TRUE(img.target().empty());
}

TEST(ImageElementTarget, SetNamedTargetAccepted) {
    ImageElement img;
    EXPECT_EQ(img.set_target("surface.desktop.icon"), ImageStatus::Ok);
    EXPECT_EQ(img.target(), "surface.desktop.icon");
}

TEST(ImageElementTarget, EmptyTargetRejected) {
    ImageElement img;
    ASSERT_EQ(img.set_target("surface.a"), ImageStatus::Ok);
    EXPECT_EQ(img.set_target(""), ImageStatus::Invalid);
    EXPECT_EQ(img.target(), "surface.a");  // 不被空名破壞
}

TEST(ImageElementTarget, ClearTargetUnbinds) {
    ImageElement img;
    ASSERT_EQ(img.set_target("surface.a"), ImageStatus::Ok);
    img.clear_target();
    EXPECT_TRUE(img.target().empty());
}

// --- render_model 內容 --------------------------------------------------------

TEST(ImageRenderModelContent, EmptyWhenNoSource) {
    ImageElement img;
    ImageRenderModel m = img.render_model();
    EXPECT_FALSE(m.has_source);
    EXPECT_TRUE(m.source_reference.empty());
    EXPECT_EQ(m.source_dimensions.width, 0);
    EXPECT_EQ(m.source_dimensions.height, 0);
    // 未設來源時，其餘欄位仍為預設（明確，不假裝有資料）
    EXPECT_EQ(m.scale_mode, ScaleMode::Fit);
    EXPECT_FLOAT_EQ(m.alpha.opacity, 1.0f);
    EXPECT_TRUE(m.target.empty());
}

TEST(ImageRenderModelContent, ReflectsFullConfiguredState) {
    ImageElement img;
    MemoryImageSource src("res://photo", ImageDimensions{800, 600});
    ASSERT_EQ(img.set_source(src), ImageStatus::Ok);
    ASSERT_EQ(img.set_scale_mode(ScaleMode::Fill), ImageStatus::Ok);
    ASSERT_EQ(img.set_crop(CropRect{0.1, 0.2, 0.6, 0.7}), ImageStatus::Ok);
    ASSERT_EQ(img.set_opacity(0.8f), ImageStatus::Ok);
    ASSERT_EQ(img.set_target("surface.frame"), ImageStatus::Ok);

    ImageRenderModel m = img.render_model();
    EXPECT_TRUE(m.has_source);
    EXPECT_EQ(m.source_reference, "res://photo");
    EXPECT_EQ(m.source_dimensions.width, 800);
    EXPECT_EQ(m.source_dimensions.height, 600);
    EXPECT_EQ(m.scale_mode, ScaleMode::Fill);
    EXPECT_DOUBLE_EQ(m.crop.x, 0.1);
    EXPECT_DOUBLE_EQ(m.crop.width, 0.6);
    EXPECT_DOUBLE_EQ(m.crop.height, 0.7);
    EXPECT_FLOAT_EQ(m.alpha.opacity, 0.8f);
    EXPECT_EQ(m.target, "surface.frame");
}

// --- NFR-02：具名目標 / 正規化比例，無絕對座標 / 無 z-order ------------------

TEST(Nfr02Compliance, CropAlwaysNormalizedFraction) {
    ImageElement img;
    MemoryImageSource src = make_source();
    ASSERT_EQ(img.set_source(src), ImageStatus::Ok);
    ASSERT_EQ(img.set_crop(CropRect{0.3, 0.3, 0.4, 0.4}), ImageStatus::Ok);

    const CropRect& c = img.render_model().crop;
    // 裁切各分量恆落在 [0,1]（比例，非螢幕像素座標）
    EXPECT_GE(c.x, 0.0);
    EXPECT_GE(c.y, 0.0);
    EXPECT_LE(c.x + c.width, 1.0);
    EXPECT_LE(c.y + c.height, 1.0);
}

TEST(Nfr02Compliance, TargetIsNamedSurfaceIdString) {
    ImageElement img;
    ASSERT_EQ(img.set_target("surface.named"), ImageStatus::Ok);
    // 目標是具名字串（SurfaceId），非數字 handle / index
    const ds::kernel::SurfaceId& t = img.render_model().target;
    EXPECT_EQ(t, std::string("surface.named"));
}

TEST(Nfr02Compliance, OpacityIsRatioNotCoordinate) {
    ImageElement img;
    ASSERT_EQ(img.set_opacity(0.42f), ImageStatus::Ok);
    // 透明度為 [0,1] 比例
    float o = img.render_model().alpha.opacity;
    EXPECT_GE(o, 0.0f);
    EXPECT_LE(o, 1.0f);
}
