// tests/c4/test_c4_03.cpp — C4-03 取色器 — gtest 單元測試
//
// 涵蓋：pick_at 取色（E2-27 像素取樣注入）、RGB / HEX 轉換、放大鏡覆蓋（E4-30 組裝，含
// 顯隱 / 挖洞 / 換點清舊挖洞 / 能力閘控降級）、複製、無效點（NoReading）、無來源
// （NoSource）、尚未取色即放大鏡（Invalid）。相位 1：只用注入式像素來源，不接真實螢幕。
#include "color_picker.hpp"

#include <memory>

#include "alpha_surface.hpp"     // 上游 E1-03：alpha_capable_matrix()
#include "capability_matrix.hpp"  // 上游 E1-21：CapabilityMatrix
#include "layer_stack.hpp"        // 上游 E1-01：LayerStack
#include "null_backend.hpp"       // 上游 E1-24：NullKernelBackend

#include <gtest/gtest.h>

namespace {

using ds::apps::ColorPick;
using ds::apps::ColorPickerApp;
using ds::apps::PickStatus;
using ds::elements::DimOverlayElement;
using ds::elements::DimStatus;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::sysinfo::NullPixelSampleSource;
using ds::sysinfo::PixelColor;
using ds::sysinfo::PixelSampleSource;
using ds::sysinfo::ScreenAnchor;

// 有 per-pixel alpha 能力的後端（capable 路徑，放大鏡可用）。
NullKernelBackend make_capable_backend() {
    return NullKernelBackend(ds::kernel::alpha_capable_matrix());
}

// 建一個注入了「中心=橘、左上=白」兩個假取樣點的來源（不接真實螢幕）。
std::shared_ptr<NullPixelSampleSource> makeFakeSource() {
    auto src = std::make_shared<NullPixelSampleSource>();
    src->set_pixel(ScreenAnchor::Center, PixelColor::rgb(0xFF, 0x88, 0x00));   // 橘
    src->set_pixel(ScreenAnchor::TopLeft, PixelColor::rgb(0xFF, 0xFF, 0xFF));  // 白
    return src;
}

// 測試固定件：capable 後端 + alpha service + layer stack + dim overlay，供 ColorPickerApp 借用。
struct PickerFixture {
    NullKernelBackend backend = make_capable_backend();
    bool backend_initialized_ = backend.init();  // CHG-20260803-11：成員依宣告順序初始化，故此行在其後成員建構前完成（K-007 對齊）
    AlphaSurfaceService alpha{backend};
    LayerStack layers;
    DimOverlayElement magnifier{alpha, layers, "surface.color_picker"};
};

// ===========================================================================
// pick_at：成功取色（E2-27 像素取樣）+ RGB / HEX 轉換
// ===========================================================================
TEST(ColorPicker, PickAtReturnsInjectedColorWithRgbAndHex) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    EXPECT_FALSE(picker.has_pick());
    EXPECT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    ASSERT_TRUE(picker.has_pick());

    const ColorPick& c = picker.current();
    EXPECT_EQ(c.anchor, ScreenAnchor::Center);
    EXPECT_EQ(c.color, PixelColor::rgb(0xFF, 0x88, 0x00));
    EXPECT_EQ(c.hex, "#FF8800");
    EXPECT_EQ(c.rgb_text, "rgb(255, 136, 0)");
}

TEST(ColorPicker, PickAtDifferentAnchorsYieldDifferentColors) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    ASSERT_EQ(picker.pick_at(ScreenAnchor::TopLeft), PickStatus::Ok);
    EXPECT_EQ(picker.current().hex, "#FFFFFF");
    EXPECT_EQ(picker.current().rgb_text, "rgb(255, 255, 255)");

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    EXPECT_EQ(picker.current().hex, "#FF8800");
}

// ===========================================================================
// 無效點：目前無讀值（NoReading）——不動既有取色狀態
// ===========================================================================
TEST(ColorPicker, PickAtUnknownAnchorReturnsNoReadingAndKeepsPreviousPick) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    const std::string prev_hex = picker.current().hex;

    // BottomRight 未被注入假像素 → 無讀值。
    EXPECT_EQ(picker.pick_at(ScreenAnchor::BottomRight), PickStatus::NoReading);
    // 無效點不靜默清空既有取色狀態。
    EXPECT_TRUE(picker.has_pick());
    EXPECT_EQ(picker.current().hex, prev_hex);
}

TEST(ColorPicker, PickAtOnEmptySourceReturnsNoReadingBeforeAnySuccessfulPick) {
    PickerFixture fx;
    ColorPickerApp picker(std::make_shared<NullPixelSampleSource>(), fx.magnifier);

    EXPECT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::NoReading);
    EXPECT_FALSE(picker.has_pick());
}

// ===========================================================================
// 無來源：source 為 null（NoSource）
// ===========================================================================
TEST(ColorPicker, NoSourceReturnsNoSourceAndNeverHasPick) {
    PickerFixture fx;
    ColorPickerApp picker(nullptr, fx.magnifier);

    EXPECT_FALSE(picker.has_source());
    EXPECT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::NoSource);
    EXPECT_FALSE(picker.has_pick());
}

// ===========================================================================
// 複製：copy() 複製目前 HEX 值到內部剪貼簿
// ===========================================================================
TEST(ColorPicker, CopyStoresHexInClipboard) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    EXPECT_FALSE(picker.has_copied());
    EXPECT_FALSE(picker.copy());  // 尚未取色：no-op

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    EXPECT_TRUE(picker.copy());
    EXPECT_EQ(picker.clipboard(), "#FF8800");
    EXPECT_TRUE(picker.has_copied());

    ASSERT_EQ(picker.pick_at(ScreenAnchor::TopLeft), PickStatus::Ok);
    EXPECT_TRUE(picker.copy());
    EXPECT_EQ(picker.clipboard(), "#FFFFFF");  // 覆寫為最新取到的色
}

// ===========================================================================
// 放大鏡：尚未取色即放大鏡 → Invalid
// ===========================================================================
TEST(ColorPicker, MagnifyBeforeAnyPickIsInvalid) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    EXPECT_EQ(picker.magnify(), DimStatus::Invalid);
    EXPECT_FALSE(picker.magnifier_visible());
    EXPECT_TRUE(picker.active_magnifier_region().empty());
}

// ===========================================================================
// 放大鏡：取色後顯示覆蓋層 + 於取樣點挖洞（E4-30 組裝）
// ===========================================================================
TEST(ColorPicker, MagnifyShowsOverlayAndCutsOutPickedAnchor) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    EXPECT_EQ(picker.magnify(), DimStatus::Ok);

    EXPECT_TRUE(picker.magnifier_visible());
    EXPECT_TRUE(fx.magnifier.visible());
    EXPECT_EQ(picker.active_magnifier_region(), "region.magnifier.center");
    EXPECT_TRUE(fx.magnifier.has_cutout("region.magnifier.center"));
    EXPECT_EQ(fx.magnifier.cutout_count(), 1u);
}

// --- 換點：換取樣點再放大鏡 → 移除舊挖洞、只留新挖洞 ---
TEST(ColorPicker, MagnifyAfterRepickMovesCutoutToNewAnchor) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    ASSERT_EQ(picker.magnify(), DimStatus::Ok);
    ASSERT_TRUE(fx.magnifier.has_cutout("region.magnifier.center"));

    ASSERT_EQ(picker.pick_at(ScreenAnchor::TopLeft), PickStatus::Ok);
    EXPECT_EQ(picker.magnify(), DimStatus::Ok);

    EXPECT_FALSE(fx.magnifier.has_cutout("region.magnifier.center"));  // 舊挖洞已清
    EXPECT_TRUE(fx.magnifier.has_cutout("region.magnifier.top-left"));
    EXPECT_EQ(fx.magnifier.cutout_count(), 1u);  // 不累積
    EXPECT_EQ(picker.active_magnifier_region(), "region.magnifier.top-left");
}

// --- 收起放大鏡：移除挖洞並隱藏覆蓋層 ---
TEST(ColorPicker, DismissMagnifierRemovesCutoutAndHides) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    ASSERT_EQ(picker.magnify(), DimStatus::Ok);

    EXPECT_EQ(picker.dismiss_magnifier(), DimStatus::Ok);
    EXPECT_FALSE(picker.magnifier_visible());
    EXPECT_TRUE(picker.active_magnifier_region().empty());
    EXPECT_FALSE(fx.magnifier.has_cutout("region.magnifier.center"));
    EXPECT_EQ(fx.magnifier.cutout_count(), 0u);
    // 隱藏不銷毀底層 surface（可低成本再顯示）。
    EXPECT_TRUE(fx.magnifier.surface_created());
}

// 未曾放大鏡即 dismiss：恆安全，no-op。
TEST(ColorPicker, DismissMagnifierWithoutPriorMagnifyIsSafe) {
    PickerFixture fx;
    ColorPickerApp picker(makeFakeSource(), fx.magnifier);

    EXPECT_EQ(picker.dismiss_magnifier(), DimStatus::Ok);
    EXPECT_FALSE(picker.magnifier_visible());
}

// ===========================================================================
// NFR-03 降級：per-pixel alpha 不可用 → magnify() Unsupported，取色仍不受影響
// ===========================================================================
TEST(ColorPicker, MagnifyDegradesGracefullyWhenAlphaUnsupported) {
    NullKernelBackend backend;  // 預設保守矩陣：無 per-pixel alpha 能力
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement magnifier(alpha, layers);
    ColorPickerApp picker(makeFakeSource(), magnifier);

    ASSERT_EQ(picker.pick_at(ScreenAnchor::Center), PickStatus::Ok);
    EXPECT_EQ(picker.magnify(), DimStatus::Unsupported);
    EXPECT_FALSE(picker.magnifier_visible());
    EXPECT_TRUE(picker.active_magnifier_region().empty());

    // 取色 / RGB / HEX / 複製不受放大鏡能力影響。
    EXPECT_EQ(picker.current().hex, "#FF8800");
    EXPECT_TRUE(picker.copy());
    EXPECT_EQ(picker.clipboard(), "#FF8800");
}

// ===========================================================================
// to_string(PickStatus)：診斷用穩定字串
// ===========================================================================
TEST(ColorPicker, PickStatusToStringIsStable) {
    EXPECT_EQ(std::string(ds::apps::to_string(PickStatus::Ok)), "Ok");
    EXPECT_EQ(std::string(ds::apps::to_string(PickStatus::NoSource)), "NoSource");
    EXPECT_EQ(std::string(ds::apps::to_string(PickStatus::NoReading)), "NoReading");
}

}  // namespace
