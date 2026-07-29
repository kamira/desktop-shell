// E4-02 圖片元件 — 渲染描述模型實作（見 image_element.hpp 規格）。
#include "image_element.hpp"

#include <cmath>  // std::isfinite

namespace ds::elements {

namespace {

// 把不透明度 clamp 至 [0,1]（呼叫前已保證為有限值）。
float clamp_opacity(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// 正規化裁切合法性：各分量須有限、寬高 > 0、邊界落在 [0,1] 且不溢出（x+width、y+height <= 1）。
bool valid_crop(const CropRect& c) {
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.width) ||
        !std::isfinite(c.height)) {
        return false;
    }
    if (!(c.width > 0.0) || !(c.height > 0.0)) {
        return false;  // 退化：零 / 負裁切
    }
    if (c.x < 0.0 || c.y < 0.0) {
        return false;  // 起點超出來源左 / 上界
    }
    if (c.x + c.width > 1.0 || c.y + c.height > 1.0) {
        return false;  // 溢出來源右 / 下界（比例須落在 [0,1]）
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 載入 / 卸載來源
// ---------------------------------------------------------------------------

ImageStatus ImageElement::set_source(const ImageSource& source) {
    if (!source.valid()) {
        return ImageStatus::Invalid;  // 無效來源不靜默（不改變既有來源）
    }
    const ImageDimensions dims = source.dimensions();
    if (dims.width <= 0 || dims.height <= 0) {
        return ImageStatus::Invalid;  // 尺寸為零 / 負：明確報錯，不部分套用
    }
    source_reference_ = source.reference();
    source_dimensions_ = dims;
    has_source_ = true;
    return ImageStatus::Ok;
}

void ImageElement::clear_source() noexcept {
    has_source_ = false;
    source_reference_.clear();
    source_dimensions_ = ImageDimensions{};
}

// ---------------------------------------------------------------------------
// 縮放模式
// ---------------------------------------------------------------------------

ImageStatus ImageElement::set_scale_mode(ScaleMode mode) noexcept {
    scale_mode_ = mode;
    return ImageStatus::Ok;
}

// ---------------------------------------------------------------------------
// 裁切
// ---------------------------------------------------------------------------

ImageStatus ImageElement::set_crop(const CropRect& crop) {
    if (!valid_crop(crop)) {
        return ImageStatus::Invalid;  // 非法裁切不套用
    }
    crop_ = crop;
    return ImageStatus::Ok;
}

// ---------------------------------------------------------------------------
// 透明度
// ---------------------------------------------------------------------------

ImageStatus ImageElement::set_opacity(float opacity) {
    if (!std::isfinite(opacity)) {
        return ImageStatus::Invalid;  // NaN / Inf：不靜默改成預設值
    }
    alpha_.opacity = clamp_opacity(opacity);
    return ImageStatus::Ok;
}

// ---------------------------------------------------------------------------
// 目標具名 surface
// ---------------------------------------------------------------------------

ImageStatus ImageElement::set_target(const ds::kernel::SurfaceId& target) {
    if (target.empty()) {
        return ImageStatus::Invalid;  // 空目標名不套用（NFR-02：必須具名）
    }
    target_ = target;
    return ImageStatus::Ok;
}

// ---------------------------------------------------------------------------
// 渲染描述
// ---------------------------------------------------------------------------

ImageRenderModel ImageElement::render_model() const {
    ImageRenderModel model;
    model.has_source = has_source_;
    model.source_reference = source_reference_;
    model.source_dimensions = source_dimensions_;
    model.scale_mode = scale_mode_;
    model.crop = crop_;
    model.alpha = alpha_;
    model.target = target_;
    return model;
}

}  // namespace ds::elements
