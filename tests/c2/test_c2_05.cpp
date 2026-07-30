// tests/c2/test_c2_05.cpp — C2-05 圖片 widget（gtest）
//
// 涵蓋：configure（圖片清單 / fit 套用、選填 fit 省略沿用預設、重新設定取代並重置索引）、
// set_image 索引切換（越界拒絕）、next 依序前進與抵尾循環繞回（單張 / 空清單安全 no-op）、
// E4-02 整合（render_model 反映目前所選圖片 + 目標 surface 綁定 C1-01 基底 id、fit 模式套用）、
// 空來源降級（未設定時安全查詢 / 切換）、以及各類無效設定（非 map / images 缺失或非 List 或
// 空清單 / 圖片項結構或具名值不合法 / fit 非字串或未知具名值，一律不部分套用）。
#include "image_widget.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::elements::ImageRenderModel;
using ds::elements::ScaleMode;
using ds::format::Value;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::profiles::SkinProfile;
using ds::widgets::ImageWidget;
using ds::widgets::ImageWidgetStatus;

namespace {

Value make_image(const std::string& ref, double width, double height) {
    return Value::map({
        {"ref", Value::string(ref)},
        {"width", Value::number(width, /*integral=*/true)},
        {"height", Value::number(height, /*integral=*/true)},
    });
}

// 三張圖片定義：img0/img1/img2，皆 32x32；fit=center。
Value three_image_definition() {
    return Value::map({
        {"images", Value::list({
                       make_image("res://img0", 32, 32),
                       make_image("res://img1", 32, 32),
                       make_image("res://img2", 32, 32),
                   })},
        {"fit", Value::string("center")},
    });
}

// 供每個測試各自建構獨立的 skin + widget（skin 不需 load_skin：widget 只取用其具名 id）。
struct Fixture {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.image", backend, layers};
    ImageWidget widget{skin};
};

}  // namespace

// -----------------------------------------------------------------------------
// 空來源降級 —— 未 configure 時的安全狀態
// -----------------------------------------------------------------------------

TEST(ImageWidget, EmptyImagesDegradeSafely) {
    Fixture f;

    EXPECT_EQ(f.widget.image_count(), 0u);
    EXPECT_EQ(f.widget.current_index(), 0u);
    EXPECT_EQ(f.widget.fit_mode(), ScaleMode::Fit);  // E4-02 元件預設

    // 安全 no-op / 明確拒絕：不崩潰、不靜默改狀態。
    f.widget.next();
    EXPECT_EQ(f.widget.current_index(), 0u);
    EXPECT_EQ(f.widget.set_image(0), ImageWidgetStatus::Invalid);

    ImageRenderModel model = f.widget.render_model();
    EXPECT_FALSE(model.has_source);  // 明確降級，不靜默假裝有資料
    // 目標 surface 仍綁定所掛載 C1-01 基底的具名 id（於建構時即綁定，不倚賴 configure）。
    EXPECT_EQ(model.target, f.skin.id());
}

// -----------------------------------------------------------------------------
// configure —— 圖片清單 / fit 套用
// -----------------------------------------------------------------------------

TEST(ImageWidget, ConfigureAppliesImagesAndFit) {
    Fixture f;

    EXPECT_EQ(f.widget.configure(three_image_definition()), ImageWidgetStatus::Ok);
    EXPECT_EQ(f.widget.image_count(), 3u);
    EXPECT_EQ(f.widget.current_index(), 0u);
    EXPECT_EQ(f.widget.fit_mode(), ScaleMode::Center);

    ImageRenderModel model = f.widget.render_model();
    EXPECT_TRUE(model.has_source);
    EXPECT_EQ(model.source_reference, "res://img0");
    EXPECT_EQ(model.source_dimensions.width, 32);
    EXPECT_EQ(model.source_dimensions.height, 32);
    EXPECT_EQ(model.scale_mode, ScaleMode::Center);
    EXPECT_EQ(model.target, f.skin.id());  // E4-02 整合：目標 surface 綁定 C1-01 基底 id
}

TEST(ImageWidget, ConfigureOmittedFitKeepsDefault) {
    Fixture f;

    Value def = Value::map({
        {"images", Value::list({make_image("res://only", 8, 8)})},
    });
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Ok);
    EXPECT_EQ(f.widget.fit_mode(), ScaleMode::Fit);  // E4-02 元件預設，未給則沿用
}

TEST(ImageWidget, ReconfigureReplacesImagesAndResetsToIndexZero) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_image_definition()), ImageWidgetStatus::Ok);
    ASSERT_EQ(f.widget.set_image(2), ImageWidgetStatus::Ok);
    ASSERT_EQ(f.widget.current_index(), 2u);

    Value replacement = Value::map({
        {"images", Value::list({make_image("res://new0", 4, 4), make_image("res://new1", 4, 4)})},
    });
    EXPECT_EQ(f.widget.configure(replacement), ImageWidgetStatus::Ok);
    EXPECT_EQ(f.widget.image_count(), 2u);
    EXPECT_EQ(f.widget.current_index(), 0u);  // 重新設定即從第 0 張開始
    EXPECT_EQ(f.widget.render_model().source_reference, "res://new0");
}

// -----------------------------------------------------------------------------
// set_image —— 索引切換
// -----------------------------------------------------------------------------

TEST(ImageWidget, SetImageSelectsByIndex) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_image_definition()), ImageWidgetStatus::Ok);

    EXPECT_EQ(f.widget.set_image(1), ImageWidgetStatus::Ok);
    EXPECT_EQ(f.widget.current_index(), 1u);
    EXPECT_EQ(f.widget.render_model().source_reference, "res://img1");
    // fit 模式不受 set_image 影響。
    EXPECT_EQ(f.widget.fit_mode(), ScaleMode::Center);
}

TEST(ImageWidget, SetImageRejectsOutOfRangeIndex) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_image_definition()), ImageWidgetStatus::Ok);

    EXPECT_EQ(f.widget.set_image(3), ImageWidgetStatus::Invalid);
    EXPECT_EQ(f.widget.current_index(), 0u);  // 不改變目前所選
    EXPECT_EQ(f.widget.render_model().source_reference, "res://img0");
}

// -----------------------------------------------------------------------------
// next —— 依序前進 / 抵尾循環繞回 / 單張或空清單安全 no-op
// -----------------------------------------------------------------------------

TEST(ImageWidget, NextAdvancesThroughImagesAndWrapsAround) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_image_definition()), ImageWidgetStatus::Ok);

    f.widget.next();
    EXPECT_EQ(f.widget.current_index(), 1u);
    EXPECT_EQ(f.widget.render_model().source_reference, "res://img1");

    f.widget.next();
    EXPECT_EQ(f.widget.current_index(), 2u);
    EXPECT_EQ(f.widget.render_model().source_reference, "res://img2");

    f.widget.next();  // 抵尾循環繞回第 0 張
    EXPECT_EQ(f.widget.current_index(), 0u);
    EXPECT_EQ(f.widget.render_model().source_reference, "res://img0");
}

TEST(ImageWidget, NextIsSafeNoOpWithSingleImage) {
    Fixture f;
    Value def = Value::map({{"images", Value::list({make_image("res://solo", 8, 8)})}});
    ASSERT_EQ(f.widget.configure(def), ImageWidgetStatus::Ok);

    f.widget.next();
    EXPECT_EQ(f.widget.current_index(), 0u);
    EXPECT_EQ(f.widget.render_model().source_reference, "res://solo");
}

TEST(ImageWidget, NextIsSafeNoOpWithEmptyImages) {
    Fixture f;
    f.widget.next();
    EXPECT_EQ(f.widget.current_index(), 0u);
    EXPECT_FALSE(f.widget.render_model().has_source);
}

// -----------------------------------------------------------------------------
// 無效設定 —— 一律不部分套用
// -----------------------------------------------------------------------------

TEST(ImageWidget, ConfigureRejectsNonMapDefinition) {
    Fixture f;
    EXPECT_EQ(f.widget.configure(Value::string("nope")), ImageWidgetStatus::Invalid);
    EXPECT_EQ(f.widget.image_count(), 0u);
}

TEST(ImageWidget, ConfigureRejectsMissingImages) {
    Fixture f;
    Value def = Value::map({{"fit", Value::string("fill")}});
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsImagesNotList) {
    Fixture f;
    Value def = Value::map({{"images", Value::string("res://img0")}});
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsEmptyImagesList) {
    Fixture f;
    Value def = Value::map({{"images", Value::list({})}});
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsInvalidImageItemStructure) {
    Fixture f;
    // 圖片項缺 width。
    Value def = Value::map({
        {"images", Value::list({Value::map({{"ref", Value::string("res://x")}})})},
    });
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsImageWithEmptyRef) {
    Fixture f;
    Value def = Value::map({{"images", Value::list({make_image("", 8, 8)})}});
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsImageWithNonPositiveDimensions) {
    Fixture f;
    Value def = Value::map({{"images", Value::list({make_image("res://x", 0, 8)})}});
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsFitNotString) {
    Fixture f;
    Value def = Value::map({
        {"images", Value::list({make_image("res://x", 8, 8)})},
        {"fit", Value::number(1.0)},  // 非字串
    });
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, ConfigureRejectsUnknownFitName) {
    Fixture f;
    Value def = Value::map({
        {"images", Value::list({make_image("res://x", 8, 8)})},
        {"fit", Value::string("zoom")},  // 未知具名值
    });
    EXPECT_EQ(f.widget.configure(def), ImageWidgetStatus::Invalid);
}

TEST(ImageWidget, InvalidConfigureDoesNotPartiallyApplyOverExistingConfig) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_image_definition()), ImageWidgetStatus::Ok);
    ASSERT_EQ(f.widget.image_count(), 3u);

    Value bad = Value::map({
        {"images", Value::list({make_image("res://bad", -1, 8)})},
        {"fit", Value::string("fill")},
    });
    EXPECT_EQ(f.widget.configure(bad), ImageWidgetStatus::Invalid);

    // 既有設定完全不受影響。
    EXPECT_EQ(f.widget.image_count(), 3u);
    EXPECT_EQ(f.widget.fit_mode(), ScaleMode::Center);
    EXPECT_EQ(f.widget.render_model().source_reference, "res://img0");
}
