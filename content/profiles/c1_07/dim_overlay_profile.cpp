// content/profiles/c1_07/dim_overlay_profile.cpp — C1-07 全螢幕調光層 profile 實作
#include "dim_overlay_profile.hpp"

#include <utility>

namespace ds::profiles {

const char* to_string(DimProfileKind kind) noexcept {
    switch (kind) {
        case DimProfileKind::Focus:
            return "Focus";
        case DimProfileKind::NightShift:
            return "NightShift";
        case DimProfileKind::PopupBackdrop:
            return "PopupBackdrop";
    }
    return "unknown";
}

const char* to_string(DimProfileState state) noexcept {
    switch (state) {
        case DimProfileState::Inactive:
            return "Inactive";
        case DimProfileState::Active:
            return "Active";
    }
    return "unknown";
}

DimPreset default_preset(DimProfileKind kind) noexcept {
    switch (kind) {
        case DimProfileKind::Focus:
            // 中度調光、純黑 —— 凸顯焦點視窗，不改變色溫。
            return DimPreset{0.6f, ds::elements::DimColor{0.0f, 0.0f, 0.0f}};
        case DimProfileKind::NightShift:
            // 低度調光、暖色調 —— 降低藍光觀感（r > g > b）。
            return DimPreset{0.3f, ds::elements::DimColor{0.15f, 0.08f, 0.0f}};
        case DimProfileKind::PopupBackdrop:
            // 較深調光、純黑 —— 清楚區隔彈窗與背景。
            return DimPreset{0.7f, ds::elements::DimColor{0.0f, 0.0f, 0.0f}};
    }
    return DimPreset{};  // 不可達（列舉已窮盡）；保守回退。
}

DimOverlayProfile::DimOverlayProfile(DimProfileKind kind,
                                     ds::kernel::AlphaSurfaceService& alpha,
                                     ds::kernel::LayerStack& layers,
                                     ds::kernel::SurfaceId surface_id)
    : kind_(kind), overlay_(alpha, layers, std::move(surface_id)) {
    // 建構即套用本情境的預設調光顏色 —— 純元件狀態更新，不需能力、不建立 surface。
    overlay_.set_color(default_preset(kind_).color);
}

ds::elements::DimStatus DimOverlayProfile::activate(float intensity) {
    // 每次 activate 皆重新宣告本情境的調光顏色語意（呼叫端可能於兩情境間切換強度前後
    // 改動過顏色；activate 是「套用本 profile」的入口，故重申預設顏色）。
    const ds::elements::DimStatus color_status = overlay_.set_color(default_preset(kind_).color);
    if (color_status != ds::elements::DimStatus::Ok) {
        return color_status;  // 預設顏色恆為有限值，理論上不可達；防禦性透傳。
    }
    return overlay_.fade_to(intensity);
}

ds::elements::DimStatus DimOverlayProfile::activate() {
    return activate(default_preset(kind_).intensity);
}

ds::elements::DimStatus DimOverlayProfile::deactivate() {
    return overlay_.fade_to(0.0f);
}

ds::elements::DimStatus DimOverlayProfile::fade(float target) {
    return overlay_.fade_to(target);
}

ds::elements::DimStatus DimOverlayProfile::add_cutout(const std::string& region) {
    return overlay_.add_cutout(region);
}

bool DimOverlayProfile::remove_cutout(const std::string& region) {
    return overlay_.remove_cutout(region);
}

bool DimOverlayProfile::has_cutout(const std::string& region) const {
    return overlay_.has_cutout(region);
}

std::size_t DimOverlayProfile::cutout_count() const noexcept {
    return overlay_.cutout_count();
}

DimProfileState DimOverlayProfile::state() const noexcept {
    return overlay_.visible() ? DimProfileState::Active : DimProfileState::Inactive;
}

bool DimOverlayProfile::is_active() const noexcept {
    return overlay_.visible();
}

float DimOverlayProfile::intensity() const noexcept {
    return overlay_.intensity();
}

const ds::elements::DimColor& DimOverlayProfile::color() const noexcept {
    return overlay_.color();
}

ds::kernel::SurfaceLayer DimOverlayProfile::layer() const noexcept {
    return overlay_.layer();
}

bool DimOverlayProfile::alpha_supported() const {
    return overlay_.alpha_supported();
}

ds::elements::DimRenderModel DimOverlayProfile::render_model() const {
    return overlay_.render_model();
}

}  // namespace ds::profiles
