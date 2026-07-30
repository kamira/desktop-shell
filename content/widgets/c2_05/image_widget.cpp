// content/widgets/c2_05/image_widget.cpp — C2-05 圖片 widget 實作（組裝型 artifact 單元）
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。宣告式設定（E7-01）先整批驗證，通過後才提交清單並委派 E4-02
// `set_source`（全有或全無，不靜默）；未設定時（空清單）一切查詢 / 切換操作安全降級。
#include "image_widget.hpp"

#include <cmath>  // std::isfinite

namespace ds::widgets {

namespace {

// 宣告式設定的欄位名（NFR-02：圖片來源以具名參照承載，非數字 handle）。
constexpr const char* kKeyImages = "images";
constexpr const char* kKeyImageRef = "ref";
constexpr const char* kKeyImageWidth = "width";
constexpr const char* kKeyImageHeight = "height";
constexpr const char* kKeyFit = "fit";

// 解讀單一圖片項（Map { ref, width, height }）。不合法回 false（不靜默）。
bool parse_image_item(const ds::format::Value& item, std::string& ref_out,
                      ds::elements::ImageDimensions& dims_out) {
    if (!item.is_map()) {
        return false;
    }
    const ds::format::Value* ref_v = item.find(kKeyImageRef);
    if (ref_v == nullptr || !ref_v->is_string() || ref_v->as_string().empty()) {
        return false;
    }
    const ds::format::Value* width_v = item.find(kKeyImageWidth);
    if (width_v == nullptr || !width_v->is_number() || !std::isfinite(width_v->as_number())) {
        return false;
    }
    const ds::format::Value* height_v = item.find(kKeyImageHeight);
    if (height_v == nullptr || !height_v->is_number() || !std::isfinite(height_v->as_number())) {
        return false;
    }
    const int width = static_cast<int>(width_v->as_number());
    const int height = static_cast<int>(height_v->as_number());
    if (width <= 0 || height <= 0) {
        return false;
    }
    ref_out = ref_v->as_string();
    dims_out = ds::elements::ImageDimensions{width, height};
    return true;
}

// 具名縮放模式字串 → E4-02 ScaleMode（NFR-02：具名，非數字係數）。未知回 false（不靜默）。
bool parse_scale_mode(const std::string& name, ds::elements::ScaleMode& out) {
    if (name == "fill") {
        out = ds::elements::ScaleMode::Fill;
    } else if (name == "fit") {
        out = ds::elements::ScaleMode::Fit;
    } else if (name == "stretch") {
        out = ds::elements::ScaleMode::Stretch;
    } else if (name == "center") {
        out = ds::elements::ScaleMode::Center;
    } else if (name == "tile") {
        out = ds::elements::ScaleMode::Tile;
    } else {
        return false;
    }
    return true;
}

}  // namespace

const char* to_string(ImageWidgetStatus s) noexcept {
    switch (s) {
        case ImageWidgetStatus::Ok:
            return "Ok";
        case ImageWidgetStatus::Invalid:
            return "Invalid";
    }
    return "unknown";
}

ImageWidget::ImageWidget(ds::profiles::SkinProfile& skin) : skin_(skin) {
    // 渲染輸出綁定所掛載 C1-01 基底的具名 surface id（NFR-02）。`skin_.id()` 於其生命週期內
    // 不變，故僅需綁定一次；若 id 為空（防禦性——SkinProfile 允許建構時傳空 id），set_target
    // 回 Invalid 且安全 no-op（目標維持未綁定），不影響本 widget 其餘功能。
    image_.set_target(skin_.id());
}

void ImageWidget::apply_current() {
    if (refs_.empty()) {
        image_.clear_source();
        return;
    }
    // refs_/dims_/current_ 已由呼叫端（configure/set_image/next）驗證並維持不變量：
    // current_ < refs_.size()。set_source 對已驗證非空參照 + 正尺寸恆回 Ok（防禦性不再檢查）。
    const ds::elements::MemoryImageSource source(refs_[current_], dims_[current_]);
    image_.set_source(source);
}

ImageWidgetStatus ImageWidget::configure(const ds::format::Value& definition) {
    if (!definition.is_map()) {
        return ImageWidgetStatus::Invalid;  // 宣告式定義須為 Map
    }

    const ds::format::Value* images_v = definition.find(kKeyImages);
    if (images_v == nullptr || !images_v->is_list() || images_v->as_list().empty()) {
        return ImageWidgetStatus::Invalid;  // images 缺失 / 非 List / 空清單 → 不套用
    }

    // --- 整批驗證圖片清單（不部分套用）---
    std::vector<std::string> refs_new;
    std::vector<ds::elements::ImageDimensions> dims_new;
    refs_new.reserve(images_v->size());
    dims_new.reserve(images_v->size());
    for (const ds::format::Value& item : images_v->as_list()) {
        std::string ref;
        ds::elements::ImageDimensions dim;
        if (!parse_image_item(item, ref, dim)) {
            return ImageWidgetStatus::Invalid;  // 任一圖片項不合法 → 整批不套用
        }
        refs_new.push_back(std::move(ref));
        dims_new.push_back(dim);
    }

    // --- 驗證選填欄位（fit），未給則沿用目前縮放模式 ---
    ds::elements::ScaleMode fit_new = image_.scale_mode();
    bool have_fit = false;
    if (const ds::format::Value* fit_v = definition.find(kKeyFit)) {
        if (!fit_v->is_string() || !parse_scale_mode(fit_v->as_string(), fit_new)) {
            return ImageWidgetStatus::Invalid;  // 非字串 / 未知具名值 → 不套用
        }
        have_fit = true;
    }

    // --- 全數驗證通過：提交新清單，重置索引至第 0 張，載入 E4-02（全有或全無）---
    refs_ = std::move(refs_new);
    dims_ = std::move(dims_new);
    current_ = 0;
    apply_current();
    if (have_fit) {
        image_.set_scale_mode(fit_new);  // 已預先驗證合法，恆 Ok
    }
    return ImageWidgetStatus::Ok;
}

ImageWidgetStatus ImageWidget::set_image(std::size_t index) {
    if (refs_.empty() || index >= refs_.size()) {
        return ImageWidgetStatus::Invalid;  // 未設定清單或索引越界 → 不套用，不改變目前所選
    }
    current_ = index;
    apply_current();
    return ImageWidgetStatus::Ok;
}

void ImageWidget::next() {
    if (refs_.empty()) {
        return;  // 空來源：安全 no-op，不靜默改狀態
    }
    current_ = (current_ + 1) % refs_.size();  // 抵達尾端循環繞回第 0 張
    apply_current();
}

std::size_t ImageWidget::image_count() const noexcept { return refs_.size(); }
std::size_t ImageWidget::current_index() const noexcept { return current_; }
ds::elements::ScaleMode ImageWidget::fit_mode() const noexcept { return image_.scale_mode(); }

ds::elements::ImageRenderModel ImageWidget::render_model() const { return image_.render_model(); }

}  // namespace ds::widgets
