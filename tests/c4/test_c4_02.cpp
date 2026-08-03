// tests/c4/test_c4_02.cpp — C4-02 截圖與標註 — gtest 單元測試
//
// 涵蓋：capture 擷取區域（E3-11 注入式後端）、無效區域（不動既有狀態）、無後端、
// 標註（箭頭 / 框 / 文字，E4-20 組裝，含退化形狀 / 空文字）、多標註累加與清空、新擷取
// 重置標註與選取框、選取框覆蓋（E4-30 組裝，含顯隱 / 能力閘控降級）、匯出結果 bundle、
// to_string 穩定字串。相位 1：只用注入式擷取後端，不接真實螢幕。
#include "screenshot_annotator.hpp"

#include <memory>
#include <string>

#include "alpha_surface.hpp"      // 上游 E1-03：alpha_capable_matrix()
#include "capability_matrix.hpp"  // 上游 E1-21：CapabilityMatrix
#include "layer_stack.hpp"        // 上游 E1-01：LayerStack
#include "null_backend.hpp"       // 上游 E1-24：NullKernelBackend

#include <gtest/gtest.h>

namespace {

using ds::actuators::CaptureRegion;
using ds::actuators::NullScreenCaptureBackend;
using ds::actuators::ScreenCaptureBackend;
using ds::apps::AnnotateStatus;
using ds::apps::Annotation;
using ds::apps::AnnotationKind;
using ds::apps::CaptureStatus;
using ds::apps::ExportResult;
using ds::apps::kSelectionRegionName;
using ds::apps::ScreenshotAnnotatorApp;
using ds::elements::DimOverlayElement;
using ds::elements::DimStatus;
using ds::elements::Point;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;

// 有 per-pixel alpha 能力的後端（capable 路徑，選取框覆蓋可用）。
NullKernelBackend make_capable_backend() {
    return NullKernelBackend(ds::kernel::alpha_capable_matrix());
}

// 測試固定件：capable 後端 + alpha service + layer stack + dim overlay，供
// ScreenshotAnnotatorApp 借用作選取框覆蓋。
struct AnnotatorFixture {
    NullKernelBackend backend = make_capable_backend();
    bool backend_initialized_ = backend.init();  // CHG-20260803-11：成員依宣告順序初始化，故此行在其後成員建構前完成（K-007 對齊）
    AlphaSurfaceService alpha{backend};
    LayerStack layers;
    DimOverlayElement selection{alpha, layers, "surface.screenshot_selection"};
};

std::shared_ptr<ScreenCaptureBackend> makeCaptureBackend() {
    return std::make_shared<NullScreenCaptureBackend>();
}

// ===========================================================================
// capture：成功擷取指定區域（E3-11 注入式後端）
// ===========================================================================
TEST(ScreenshotAnnotator, CaptureRegionSucceedsAndReportsRequestedDimensions) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    EXPECT_FALSE(app.has_capture());
    EXPECT_EQ(app.capture(CaptureRegion{0, 0, 200, 100}), CaptureStatus::Ok);
    ASSERT_TRUE(app.has_capture());

    const auto& c = app.current_capture();
    EXPECT_EQ(c.width, 200);
    EXPECT_EQ(c.height, 100);
    EXPECT_FALSE(c.image_ref.empty());
    EXPECT_TRUE(c.path.empty());
}

TEST(ScreenshotAnnotator, CaptureWithSavePathReturnsPathInsteadOfImageRef) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 50, 50}, "/tmp/shot.png"), CaptureStatus::Ok);
    EXPECT_EQ(app.current_capture().path, "/tmp/shot.png");
    EXPECT_TRUE(app.current_capture().image_ref.empty());
}

// ===========================================================================
// 無效區域：不動既有擷取狀態
// ===========================================================================
TEST(ScreenshotAnnotator, CaptureInvalidRegionDoesNotOverwritePreviousCapture) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);
    const auto prev_w = app.current_capture().width;

    EXPECT_EQ(app.capture(CaptureRegion{0, 0, 0, 100}), CaptureStatus::InvalidRegion);
    EXPECT_EQ(app.capture(CaptureRegion{0, 0, 100, -5}), CaptureStatus::InvalidRegion);

    EXPECT_TRUE(app.has_capture());
    EXPECT_EQ(app.current_capture().width, prev_w);
}

TEST(ScreenshotAnnotator, CaptureInvalidRegionBeforeAnyCaptureLeavesHasCaptureFalse) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    EXPECT_EQ(app.capture(CaptureRegion{0, 0, -1, -1}), CaptureStatus::InvalidRegion);
    EXPECT_FALSE(app.has_capture());
}

// ===========================================================================
// 無後端：backend 為 null
// ===========================================================================
TEST(ScreenshotAnnotator, CaptureWithoutBackendReturnsNoBackend) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(nullptr, fx.selection);

    EXPECT_FALSE(app.has_backend());
    EXPECT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::NoBackend);
    EXPECT_FALSE(app.has_capture());
}

// ===========================================================================
// 標註：尚未擷取即標註 → NoCapture
// ===========================================================================
TEST(ScreenshotAnnotator, AnnotateBeforeAnyCaptureReturnsNoCapture) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    EXPECT_EQ(app.annotate_arrow(Point{0, 0}, Point{10, 10}), AnnotateStatus::NoCapture);
    EXPECT_EQ(app.annotate_box(Point{0, 0}, 10, 10), AnnotateStatus::NoCapture);
    EXPECT_EQ(app.annotate_text(Point{0, 0}, "hi"), AnnotateStatus::NoCapture);
    EXPECT_EQ(app.annotation_count(), 0u);
}

// ===========================================================================
// 標箭頭（E4-20 直線標註）
// ===========================================================================
TEST(ScreenshotAnnotator, AnnotateArrowAddsLineAnnotation) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.annotate_arrow(Point{0, 0}, Point{10, 10}), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotation_count(), 1u);

    const Annotation& a = app.annotations()[0];
    EXPECT_EQ(a.kind, AnnotationKind::Arrow);
    EXPECT_FALSE(a.render.empty);
    EXPECT_EQ(a.render.commands.size(), 2u);  // MoveTo + LineTo（線段不 close）
    EXPECT_TRUE(a.render.stroke.enabled);
}

TEST(ScreenshotAnnotator, AnnotateArrowZeroLengthIsInvalid) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.annotate_arrow(Point{5, 5}, Point{5, 5}), AnnotateStatus::Invalid);
    EXPECT_EQ(app.annotation_count(), 0u);
}

// ===========================================================================
// 標框（E4-20 矩形標註）
// ===========================================================================
TEST(ScreenshotAnnotator, AnnotateBoxAddsRectAnnotation) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.annotate_box(Point{5, 5}, 40, 20), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotation_count(), 1u);

    const Annotation& a = app.annotations()[0];
    EXPECT_EQ(a.kind, AnnotationKind::Box);
    EXPECT_FALSE(a.render.empty);
    EXPECT_EQ(a.render.commands.size(), 5u);  // MoveTo + 3×LineTo + Close
    EXPECT_EQ(a.anchor.x, 5);
    EXPECT_EQ(a.anchor.y, 5);
}

TEST(ScreenshotAnnotator, AnnotateBoxDegenerateIsInvalid) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.annotate_box(Point{0, 0}, 0, 10), AnnotateStatus::Invalid);
    EXPECT_EQ(app.annotate_box(Point{0, 0}, 10, -1), AnnotateStatus::Invalid);
    EXPECT_EQ(app.annotation_count(), 0u);
}

// ===========================================================================
// 標文字（不經向量路徑）
// ===========================================================================
TEST(ScreenshotAnnotator, AnnotateTextAddsTextAnnotation) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.annotate_text(Point{1, 2}, "hello"), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotation_count(), 1u);

    const Annotation& a = app.annotations()[0];
    EXPECT_EQ(a.kind, AnnotationKind::Text);
    EXPECT_TRUE(a.render.empty);  // 文字不建向量路徑
    EXPECT_EQ(a.text, "hello");
    EXPECT_EQ(a.anchor.x, 1);
    EXPECT_EQ(a.anchor.y, 2);
}

TEST(ScreenshotAnnotator, AnnotateTextEmptyIsInvalid) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.annotate_text(Point{0, 0}, ""), AnnotateStatus::Invalid);
    EXPECT_EQ(app.annotation_count(), 0u);
}

// ===========================================================================
// 多標註：累加、順序、清空
// ===========================================================================
TEST(ScreenshotAnnotator, MultipleAnnotationsAccumulateInOrder) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    ASSERT_EQ(app.annotate_arrow(Point{0, 0}, Point{1, 1}), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotate_box(Point{0, 0}, 10, 10), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotate_text(Point{0, 0}, "note"), AnnotateStatus::Ok);

    ASSERT_EQ(app.annotation_count(), 3u);
    EXPECT_EQ(app.annotations()[0].kind, AnnotationKind::Arrow);
    EXPECT_EQ(app.annotations()[1].kind, AnnotationKind::Box);
    EXPECT_EQ(app.annotations()[2].kind, AnnotationKind::Text);
}

TEST(ScreenshotAnnotator, ClearAnnotationsRemovesAll) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);
    ASSERT_EQ(app.annotate_box(Point{0, 0}, 10, 10), AnnotateStatus::Ok);

    app.clear_annotations();
    EXPECT_EQ(app.annotation_count(), 0u);
}

TEST(ScreenshotAnnotator, NewCaptureResetsAnnotations) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);
    ASSERT_EQ(app.annotate_box(Point{0, 0}, 10, 10), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotate_text(Point{0, 0}, "note"), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotation_count(), 2u);

    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 50, 50}), CaptureStatus::Ok);
    EXPECT_EQ(app.annotation_count(), 0u);
}

// ===========================================================================
// 選取框覆蓋（E4-30 組裝）
// ===========================================================================
TEST(ScreenshotAnnotator, ShowSelectionBeforeCaptureIsInvalid) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    EXPECT_EQ(app.show_selection(), DimStatus::Invalid);
    EXPECT_FALSE(app.selection_visible());
}

TEST(ScreenshotAnnotator, ShowSelectionAfterCaptureAddsCutoutAndVisible) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.show_selection(), DimStatus::Ok);
    EXPECT_TRUE(app.selection_visible());
    EXPECT_TRUE(fx.selection.has_cutout(kSelectionRegionName));
    EXPECT_EQ(fx.selection.cutout_count(), 1u);
}

TEST(ScreenshotAnnotator, HideSelectionRemovesCutoutAndHides) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);
    ASSERT_EQ(app.show_selection(), DimStatus::Ok);

    EXPECT_EQ(app.hide_selection(), DimStatus::Ok);
    EXPECT_FALSE(app.selection_visible());
    EXPECT_FALSE(fx.selection.has_cutout(kSelectionRegionName));
    EXPECT_EQ(fx.selection.cutout_count(), 0u);
}

TEST(ScreenshotAnnotator, HideSelectionWithoutPriorShowIsSafe) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);

    EXPECT_EQ(app.hide_selection(), DimStatus::Ok);
    EXPECT_FALSE(app.selection_visible());
}

TEST(ScreenshotAnnotator, NewCaptureResetsSelectionCutout) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);
    ASSERT_EQ(app.show_selection(), DimStatus::Ok);
    ASSERT_TRUE(fx.selection.has_cutout(kSelectionRegionName));

    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 50, 50}), CaptureStatus::Ok);
    EXPECT_FALSE(fx.selection.has_cutout(kSelectionRegionName));
    EXPECT_FALSE(app.selection_visible());
}

TEST(ScreenshotAnnotator, ShowSelectionDegradesGracefullyWhenAlphaUnsupported) {
    NullKernelBackend backend;  // 預設保守矩陣：無 per-pixel alpha 能力
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    AlphaSurfaceService alpha(backend);
    LayerStack layers;
    DimOverlayElement selection(alpha, layers);
    ScreenshotAnnotatorApp app(makeCaptureBackend(), selection);

    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}), CaptureStatus::Ok);
    EXPECT_EQ(app.show_selection(), DimStatus::Unsupported);
    EXPECT_FALSE(app.selection_visible());

    // 標註 / 匯出不受選取框覆蓋能力影響。
    EXPECT_EQ(app.annotate_box(Point{0, 0}, 10, 10), AnnotateStatus::Ok);
    EXPECT_TRUE(app.export_result().ok);
}

// ===========================================================================
// 匯出（export）
// ===========================================================================
TEST(ScreenshotAnnotator, ExportResultBeforeCaptureIsNotOk) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);

    const ExportResult r = app.export_result();
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.width, 0);
    EXPECT_EQ(r.height, 0);
    EXPECT_EQ(r.annotation_count, 0u);
}

TEST(ScreenshotAnnotator, ExportResultAfterCaptureAndAnnotationsPopulatesFields) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 200, 150}), CaptureStatus::Ok);
    ASSERT_EQ(app.annotate_arrow(Point{0, 0}, Point{5, 5}), AnnotateStatus::Ok);
    ASSERT_EQ(app.annotate_text(Point{0, 0}, "note"), AnnotateStatus::Ok);

    const ExportResult r = app.export_result();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.width, 200);
    EXPECT_EQ(r.height, 150);
    EXPECT_FALSE(r.image_ref.empty());
    EXPECT_TRUE(r.path.empty());
    EXPECT_EQ(r.annotation_count, 2u);
}

TEST(ScreenshotAnnotator, ExportResultReflectsSavePath) {
    AnnotatorFixture fx;
    ScreenshotAnnotatorApp app(makeCaptureBackend(), fx.selection);
    ASSERT_EQ(app.capture(CaptureRegion{0, 0, 100, 100}, "/tmp/out.png"), CaptureStatus::Ok);

    const ExportResult r = app.export_result();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.path, "/tmp/out.png");
    EXPECT_TRUE(r.image_ref.empty());
}

// ===========================================================================
// to_string()：診斷用穩定字串
// ===========================================================================
TEST(ScreenshotAnnotator, ToStringIsStableForAllEnums) {
    EXPECT_EQ(std::string(ds::apps::to_string(CaptureStatus::Ok)), "Ok");
    EXPECT_EQ(std::string(ds::apps::to_string(CaptureStatus::NoBackend)), "NoBackend");
    EXPECT_EQ(std::string(ds::apps::to_string(CaptureStatus::InvalidRegion)), "InvalidRegion");

    EXPECT_EQ(std::string(ds::apps::to_string(AnnotateStatus::Ok)), "Ok");
    EXPECT_EQ(std::string(ds::apps::to_string(AnnotateStatus::NoCapture)), "NoCapture");
    EXPECT_EQ(std::string(ds::apps::to_string(AnnotateStatus::Invalid)), "Invalid");

    EXPECT_EQ(std::string(ds::apps::to_string(AnnotationKind::Arrow)), "Arrow");
    EXPECT_EQ(std::string(ds::apps::to_string(AnnotationKind::Box)), "Box");
    EXPECT_EQ(std::string(ds::apps::to_string(AnnotationKind::Text)), "Text");
}

}  // namespace
