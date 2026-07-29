// apps/c4_03/color_picker.cpp — C4-03 取色器實作
//
// 純組裝邏輯：呼叫 E2-27 `PixelSampleSource::sample()` 取色、呼叫 E4-30
// `DimOverlayElement` 的 show/hide/add_cutout/remove_cutout 組出放大鏡。無平台分支、
// 無真實螢幕擷取、無真實系統剪貼簿。
#include "color_picker.hpp"

#include <cstdio>
#include <utility>

namespace ds::apps {

const char* to_string(PickStatus s) noexcept {
    switch (s) {
        case PickStatus::Ok:
            return "Ok";
        case PickStatus::NoSource:
            return "NoSource";
        case PickStatus::NoReading:
            return "NoReading";
    }
    return "unknown";
}

namespace {

// "rgb(R, G, B)" —— 供顯示用的另一種色值格式（十進位，非十六進位）。
std::string format_rgb(const ds::sysinfo::PixelColor& c) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "rgb(%u, %u, %u)", static_cast<unsigned>(c.r),
                  static_cast<unsigned>(c.g), static_cast<unsigned>(c.b));
    return std::string(buf);
}

// 放大鏡挖洞的具名區域（NFR-02：具名，非絕對座標）—— 每個取樣點各自一個穩定區域名。
std::string magnifier_region(ds::sysinfo::ScreenAnchor anchor) {
    return std::string(kMagnifierRegionPrefix) + ds::sysinfo::to_string(anchor);
}

}  // namespace

ColorPickerApp::ColorPickerApp(std::shared_ptr<ds::sysinfo::PixelSampleSource> source,
                                ds::elements::DimOverlayElement& magnifier)
    : source_(std::move(source)), magnifier_(magnifier) {}

PickStatus ColorPickerApp::pick_at(ds::sysinfo::ScreenAnchor anchor) {
    if (!source_) {
        return PickStatus::NoSource;  // 無來源：不動既有取色狀態（不靜默清空）。
    }
    std::optional<ds::sysinfo::PixelColor> px = source_->sample(anchor);
    if (!px) {
        return PickStatus::NoReading;  // 無效點：目前無讀值，不動既有取色狀態。
    }

    current_.anchor = anchor;
    current_.color = *px;
    current_.hex = px->hex();
    current_.rgb_text = format_rgb(*px);
    has_pick_ = true;
    return PickStatus::Ok;
}

ds::elements::DimStatus ColorPickerApp::magnify() {
    if (!has_pick_) {
        return ds::elements::DimStatus::Invalid;  // 尚未取色，無點可放大鏡。
    }

    const std::string region = magnifier_region(current_.anchor);
    if (!active_cutout_.empty() && active_cutout_ != region) {
        // 取樣點與上次不同：先清掉舊挖洞，避免殘留區域累積。
        magnifier_.remove_cutout(active_cutout_);
        active_cutout_.clear();
    }

    const ds::elements::DimStatus shown = magnifier_.show();
    if (shown != ds::elements::DimStatus::Ok) {
        return shown;  // 能力不可用等：委派 E4-30 降級，不改動 active_cutout_。
    }

    const ds::elements::DimStatus cut = magnifier_.add_cutout(region);
    if (cut != ds::elements::DimStatus::Ok) {
        return cut;
    }
    active_cutout_ = region;
    return ds::elements::DimStatus::Ok;
}

ds::elements::DimStatus ColorPickerApp::dismiss_magnifier() {
    if (!active_cutout_.empty()) {
        magnifier_.remove_cutout(active_cutout_);
        active_cutout_.clear();
    }
    return magnifier_.hide();  // 隱藏恆安全，不需能力閘控（委派 E4-30）。
}

bool ColorPickerApp::magnifier_visible() const { return magnifier_.visible(); }

bool ColorPickerApp::copy() {
    if (!has_pick_) {
        return false;  // 尚未取色：no-op，不靜默覆寫剪貼簿。
    }
    clipboard_ = current_.hex;
    return true;
}

}  // namespace ds::apps
