// apps/c4_02/screenshot_annotator.cpp — C4-02 截圖與標註實作
//
// 純組裝邏輯：呼叫 E3-11 `ScreenCaptureBackend::capture()` 擷取區域、呼叫 E4-20
// `build_shape_path` / `VectorGraphic` 把箭頭 / 框標註轉為向量渲染描述、呼叫 E4-30
// `DimOverlayElement` 的 show/hide/add_cutout/remove_cutout 組出選取框覆蓋。無平台分支、
// 無真實螢幕擷取、無真實影像合成 / 檔案寫入。
#include "screenshot_annotator.hpp"

#include <cmath>     // std::isfinite
#include <utility>   // std::move

namespace ds::apps {

const char* to_string(CaptureStatus s) noexcept {
    switch (s) {
        case CaptureStatus::Ok:
            return "Ok";
        case CaptureStatus::NoBackend:
            return "NoBackend";
        case CaptureStatus::InvalidRegion:
            return "InvalidRegion";
    }
    return "unknown";
}

const char* to_string(AnnotateStatus s) noexcept {
    switch (s) {
        case AnnotateStatus::Ok:
            return "Ok";
        case AnnotateStatus::NoCapture:
            return "NoCapture";
        case AnnotateStatus::Invalid:
            return "Invalid";
    }
    return "unknown";
}

const char* to_string(AnnotationKind k) noexcept {
    switch (k) {
        case AnnotationKind::Arrow:
            return "Arrow";
        case AnnotationKind::Box:
            return "Box";
        case AnnotationKind::Text:
            return "Text";
    }
    return "unknown";
}

ScreenshotAnnotatorApp::ScreenshotAnnotatorApp(
    std::shared_ptr<ds::actuators::ScreenCaptureBackend> backend,
    ds::elements::DimOverlayElement& selection)
    : backend_(std::move(backend)), selection_(selection) {}

CaptureStatus ScreenshotAnnotatorApp::capture(const ds::actuators::CaptureRegion& region,
                                              const std::string& save_path) {
    if (!region.valid()) {
        return CaptureStatus::InvalidRegion;  // 不動既有擷取狀態（不靜默清空）
    }
    if (!backend_) {
        return CaptureStatus::NoBackend;  // 同樣不動既有狀態
    }

    ds::actuators::CaptureSpec spec;
    spec.kind = ds::actuators::CaptureKind::Region;
    spec.region = region;
    spec.save_path = save_path;

    const ds::actuators::CaptureResult result = backend_->capture(spec);
    if (!result.ok()) {
        // 防禦式檢查：null 後端遵循請求區域尺寸、區域已驗證正寬高，恆不致此；未來替換
        // 為真實後端時避免靜默吞下空結果。
        return CaptureStatus::InvalidRegion;
    }

    capture_ = result;
    has_capture_ = true;
    annotations_.clear();  // 新擷取開新的標註 session，不殘留前次標註。

    if (selection_active_) {
        // 換了新擷取畫面：舊選取框挖洞名對應前次擷取，不再有效，統一收起避免殘留誤導。
        selection_.remove_cutout(kSelectionRegionName);
        selection_.hide();
        selection_active_ = false;
    }
    return CaptureStatus::Ok;
}

AnnotateStatus ScreenshotAnnotatorApp::annotate_arrow(ds::elements::Point from,
                                                       ds::elements::Point to) {
    if (!has_capture_) {
        return AnnotateStatus::NoCapture;
    }

    ds::elements::VectorPath path;
    const ds::elements::VectorStatus built =
        ds::elements::build_shape_path(ds::elements::Shape::make_line(from, to), path);
    if (built != ds::elements::VectorStatus::Ok) {
        return AnnotateStatus::Invalid;  // 零長度 / 非有限座標：委派 E4-20 判定，不部分套用。
    }

    ds::elements::VectorGraphic graphic;
    graphic.set_path(path);
    ds::elements::StrokeStyle stroke;
    stroke.enabled = true;
    stroke.width = kAnnotationStrokeWidth;
    graphic.set_stroke(stroke);  // 常數寬度恆為正、有限，set_stroke 恆回 Ok。

    Annotation a;
    a.kind = AnnotationKind::Arrow;
    a.render = graphic.render_model();
    a.anchor = from;
    annotations_.push_back(std::move(a));
    return AnnotateStatus::Ok;
}

AnnotateStatus ScreenshotAnnotatorApp::annotate_box(ds::elements::Point origin, double width,
                                                     double height) {
    if (!has_capture_) {
        return AnnotateStatus::NoCapture;
    }

    ds::elements::VectorPath path;
    const ds::elements::VectorStatus built = ds::elements::build_shape_path(
        ds::elements::Shape::make_rect(origin, width, height), path);
    if (built != ds::elements::VectorStatus::Ok) {
        return AnnotateStatus::Invalid;  // 零 / 負寬高 / 非有限值：委派 E4-20 判定。
    }

    ds::elements::VectorGraphic graphic;
    graphic.set_path(path);
    ds::elements::StrokeStyle stroke;
    stroke.enabled = true;
    stroke.width = kAnnotationStrokeWidth;
    graphic.set_stroke(stroke);

    Annotation a;
    a.kind = AnnotationKind::Box;
    a.render = graphic.render_model();
    a.anchor = origin;
    annotations_.push_back(std::move(a));
    return AnnotateStatus::Ok;
}

AnnotateStatus ScreenshotAnnotatorApp::annotate_text(ds::elements::Point position,
                                                      std::string text) {
    if (!has_capture_) {
        return AnnotateStatus::NoCapture;
    }
    if (text.empty()) {
        return AnnotateStatus::Invalid;  // 空文字標註：報錯不靜默。
    }
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return AnnotateStatus::Invalid;  // 非有限位置座標：報錯不靜默。
    }

    Annotation a;
    a.kind = AnnotationKind::Text;
    a.anchor = position;
    a.text = std::move(text);
    // a.render 保持預設（VectorRenderModel{}，empty=true）：文字不經向量路徑（本層無字形渲染）。
    annotations_.push_back(std::move(a));
    return AnnotateStatus::Ok;
}

ds::elements::DimStatus ScreenshotAnnotatorApp::show_selection() {
    if (!has_capture_) {
        return ds::elements::DimStatus::Invalid;  // 無畫面可框選。
    }

    const ds::elements::DimStatus shown = selection_.show();
    if (shown != ds::elements::DimStatus::Ok) {
        return shown;  // 能力不可用等：委派 E4-30 降級，不改動 selection_active_。
    }

    const ds::elements::DimStatus cut = selection_.add_cutout(kSelectionRegionName);
    if (cut != ds::elements::DimStatus::Ok) {
        return cut;
    }
    selection_active_ = true;
    return ds::elements::DimStatus::Ok;
}

ds::elements::DimStatus ScreenshotAnnotatorApp::hide_selection() {
    if (selection_active_) {
        selection_.remove_cutout(kSelectionRegionName);
        selection_active_ = false;
    }
    return selection_.hide();  // 隱藏恆安全，不需能力閘控（委派 E4-30）。
}

bool ScreenshotAnnotatorApp::selection_visible() const { return selection_.visible(); }

ExportResult ScreenshotAnnotatorApp::export_result() const {
    ExportResult r;
    if (!has_capture_) {
        return r;  // ok=false，其餘欄位維持預設值，不假裝有資料。
    }
    r.ok = true;
    r.width = capture_.width;
    r.height = capture_.height;
    r.image_ref = capture_.image_ref;
    r.path = capture_.path;
    r.annotation_count = annotations_.size();
    return r;
}

}  // namespace ds::apps
