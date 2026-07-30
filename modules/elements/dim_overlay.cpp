// E4-30 全螢幕調光覆蓋 — 實作
//
// 純邏輯：覆蓋層狀態機 + 上游整合（E1-03 半透明 alpha surface、E1-01 具名頂層），
// 不含任何平台分支或真實繪圖 API。強度 / 顏色夾限，非有限輸入報錯不靜默，能力閘控（NFR-03）。
#include "dim_overlay.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::isfinite
#include <utility>    // std::move

namespace ds::elements {

namespace {

// 正規化 [0,1]：非有限值視為無效（回 false）；有效則 clamp 至 [0,1] 寫入 out。
bool normalize_unit(float in, float& out) {
    if (!std::isfinite(in)) {
        return false;
    }
    out = std::clamp(in, 0.0f, 1.0f);
    return true;
}

}  // namespace

DimOverlayElement::DimOverlayElement(ds::kernel::AlphaSurfaceService& alpha,
                                     ds::kernel::LayerStack& layers,
                                     ds::kernel::SurfaceId surface_id)
    : alpha_(alpha), layers_(layers), surface_id_(std::move(surface_id)) {}

DimStatus DimOverlayElement::ensure_surface() {
    if (created_) {
        return DimStatus::Ok;
    }
    // NFR-03：建立半透明覆蓋層需 per-pixel alpha 能力。不可用 → 降級（不建立任何狀態）。
    if (!alpha_.supported()) {
        return DimStatus::Unsupported;
    }

    // 覆蓋層 profile：置於具名頂層、短暫生命週期、穿透輸入 / 命中（純調光層不搶輸入）。
    ds::kernel::SurfaceProfile profile;
    profile.layer = ds::kernel::SurfaceLayer::Topmost;
    profile.input = ds::kernel::InputPolicy::PassThrough;
    profile.hit = ds::kernel::HitPolicy::Transparent;
    profile.lifecycle = ds::kernel::SurfaceLifecycle::Ephemeral;

    // 以 E1-03 建立帶不透明度的 per-pixel alpha surface（intensity → opacity）。
    ds::kernel::AlphaProfile alpha_profile;
    alpha_profile.mode = ds::kernel::AlphaMode::PerPixel;  // 逐像素：支援挖洞（不規則透明）
    alpha_profile.opacity = intensity_;

    const ds::kernel::AlphaStatus a = alpha_.create_alpha_surface(surface_id_, profile, alpha_profile);
    if (a != ds::kernel::AlphaStatus::Ok) {
        return (a == ds::kernel::AlphaStatus::Unsupported) ? DimStatus::Unsupported
                                                           : DimStatus::Invalid;
    }

    // 以 E1-01 把覆蓋層置於**具名頂層**（NFR-02：具名圖層置頂，非數字 z-order）。
    const ds::kernel::LayerAssign la = layers_.assign(surface_id_, ds::kernel::SurfaceLayer::Topmost);
    if (la == ds::kernel::LayerAssign::RejectedNoCapability ||
        la == ds::kernel::LayerAssign::RejectedEmptyId) {
        // 圖層置頂失敗：回滾已建立的 alpha surface，不留半份狀態。
        alpha_.destroy_alpha_surface(surface_id_);
        return DimStatus::Unsupported;
    }

    created_ = true;
    return DimStatus::Ok;
}

void DimOverlayElement::sync_alpha() {
    if (!created_) {
        return;  // 尚未建立底層 surface：純元件狀態，稍後 ensure_surface 時再帶入。
    }
    // best-effort 反映不透明度；能力 / id 問題由上游回碼表達，此處不改變元件對外語意。
    alpha_.set_opacity(surface_id_, intensity_);
}

DimStatus DimOverlayElement::set_intensity(float intensity) {
    float clamped = 0.0f;
    if (!normalize_unit(intensity, clamped)) {
        return DimStatus::Invalid;  // 非有限值：報錯不靜默
    }
    intensity_ = clamped;
    sync_alpha();
    return DimStatus::Ok;
}

DimStatus DimOverlayElement::set_color(const DimColor& color) {
    DimColor c;
    if (!normalize_unit(color.r, c.r) ||
        !normalize_unit(color.g, c.g) ||
        !normalize_unit(color.b, c.b)) {
        return DimStatus::Invalid;  // 任一通道非有限值：報錯不靜默
    }
    color_ = c;
    return DimStatus::Ok;
}

DimStatus DimOverlayElement::show() {
    const DimStatus s = ensure_surface();
    if (s != DimStatus::Ok) {
        return s;  // 能力不可用等：不標記可見（降級）
    }
    visible_ = true;
    sync_alpha();
    return DimStatus::Ok;
}

DimStatus DimOverlayElement::hide() {
    visible_ = false;  // 隱藏恆為安全，不需能力閘控
    return DimStatus::Ok;
}

DimStatus DimOverlayElement::fade_to(float target) {
    float clamped = 0.0f;
    if (!normalize_unit(target, clamped)) {
        return DimStatus::Invalid;  // 非有限目標：報錯不靜默
    }
    // 改動半透明狀態 → 先確保底層 surface（經能力閘控）。
    const DimStatus s = ensure_surface();
    if (s != DimStatus::Ok) {
        return s;
    }

    if (clamped > intensity_) {
        fade_ = DimFade::In;
    } else if (clamped < intensity_) {
        fade_ = DimFade::Out;
    } else {
        fade_ = DimFade::None;
    }
    fade_target_ = clamped;
    intensity_ = clamped;             // 相位 1：宣告式，直接抵達目標強度
    visible_ = (clamped > 0.0f);      // 淡出至 0 = 隱藏；淡入至 >0 = 顯示
    sync_alpha();
    return DimStatus::Ok;
}

DimStatus DimOverlayElement::add_cutout(const std::string& region) {
    if (region.empty()) {
        return DimStatus::Invalid;  // 空名：報錯不靜默
    }
    // 挖洞屬 per-pixel alpha 語意（不規則透明）→ 需能力閘控（NFR-03）。
    if (!alpha_.supported()) {
        return DimStatus::Unsupported;
    }
    if (has_cutout(region)) {
        return DimStatus::Ok;  // 重複名：冪等忽略
    }
    cutouts_.push_back(region);
    return DimStatus::Ok;
}

bool DimOverlayElement::remove_cutout(const std::string& region) {
    for (auto it = cutouts_.begin(); it != cutouts_.end(); ++it) {
        if (*it == region) {
            cutouts_.erase(it);
            return true;
        }
    }
    return false;  // 未知名：不崩潰
}

bool DimOverlayElement::has_cutout(const std::string& region) const {
    for (const std::string& c : cutouts_) {
        if (c == region) {
            return true;
        }
    }
    return false;
}

DimRenderModel DimOverlayElement::render_model() const {
    DimRenderModel m;
    m.visible = visible_;
    m.intensity = intensity_;
    m.effective_opacity = visible_ ? intensity_ : 0.0f;  // 隱藏時有效不透明度為 0
    m.color = color_;
    // NFR-02：以 E1-01 的**具名頂層**名表達置頂，不出現數字 z-order。
    m.layer_name = ds::kernel::layer_name(ds::kernel::SurfaceLayer::Topmost);
    m.fade = fade_;
    m.fade_target = fade_target_;
    m.cutouts = cutouts_;
    m.alpha_supported = alpha_.supported();
    return m;
}

}  // namespace ds::elements
