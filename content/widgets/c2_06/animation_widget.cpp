// content/widgets/c2_06/animation_widget.cpp — C2-06 動畫 / 序列圖 widget 實作（組裝型 artifact 單元）
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。宣告式設定（E7-01）先整批驗證，通過後才委派 E4-07 套用（全有或全無，
// 不靜默）；未設定時（空幀）一切查詢 / 播放操作安全降級。
#include "animation_widget.hpp"

#include <cmath>          // std::isfinite
#include <functional>     // std::reference_wrapper
#include <string>
#include <vector>

namespace ds::widgets {

namespace {

// 宣告式設定的欄位名（NFR-02：幀來源以具名參照承載，非數字 handle）。
constexpr const char* kKeyFrames = "frames";
constexpr const char* kKeyFrameRef = "ref";
constexpr const char* kKeyFrameWidth = "width";
constexpr const char* kKeyFrameHeight = "height";
constexpr const char* kKeyFps = "fps";
constexpr const char* kKeyLoop = "loop";

// 解讀單一幀項（Map { ref, width, height }）。不合法回 false（不靜默）。
bool parse_frame(const ds::format::Value& item, std::string& ref_out,
                 ds::elements::ImageDimensions& dims_out) {
    if (!item.is_map()) {
        return false;
    }
    const ds::format::Value* ref_v = item.find(kKeyFrameRef);
    if (ref_v == nullptr || !ref_v->is_string() || ref_v->as_string().empty()) {
        return false;
    }
    const ds::format::Value* width_v = item.find(kKeyFrameWidth);
    if (width_v == nullptr || !width_v->is_number() || !std::isfinite(width_v->as_number())) {
        return false;
    }
    const ds::format::Value* height_v = item.find(kKeyFrameHeight);
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

}  // namespace

const char* to_string(AnimationWidgetStatus s) noexcept {
    switch (s) {
        case AnimationWidgetStatus::Ok:
            return "Ok";
        case AnimationWidgetStatus::Invalid:
            return "Invalid";
    }
    return "unknown";
}

AnimationWidget::AnimationWidget(ds::profiles::SkinProfile& skin) : skin_(skin) {
    // 渲染輸出綁定所掛載 C1-01 基底的具名 surface id（NFR-02）。`skin_.id()` 於其生命週期內
    // 不變，故僅需綁定一次；若 id 為空（防禦性——SkinProfile 允許建構時傳空 id），set_target
    // 回 Invalid 且安全 no-op（目標維持未綁定），不影響本 widget 其餘功能。
    animation_.set_target(skin_.id());
}

AnimationWidgetStatus AnimationWidget::configure(const ds::format::Value& definition) {
    if (!definition.is_map()) {
        return AnimationWidgetStatus::Invalid;  // 宣告式定義須為 Map
    }

    const ds::format::Value* frames_v = definition.find(kKeyFrames);
    if (frames_v == nullptr || !frames_v->is_list() || frames_v->as_list().empty()) {
        return AnimationWidgetStatus::Invalid;  // frames 缺失 / 非 List / 空清單 → 不套用
    }

    // --- 整批驗證幀清單（不部分套用）---
    std::vector<std::string> refs;
    std::vector<ds::elements::ImageDimensions> dims;
    refs.reserve(frames_v->size());
    dims.reserve(frames_v->size());
    for (const ds::format::Value& item : frames_v->as_list()) {
        std::string ref;
        ds::elements::ImageDimensions dim;
        if (!parse_frame(item, ref, dim)) {
            return AnimationWidgetStatus::Invalid;  // 任一幀項不合法 → 整批不套用
        }
        refs.push_back(std::move(ref));
        dims.push_back(dim);
    }

    // --- 驗證選填欄位（fps / loop），未給則沿用目前值 ---
    double fps_new = animation_.fps();
    bool have_fps = false;
    if (const ds::format::Value* fps_v = definition.find(kKeyFps)) {
        const double candidate = fps_v->is_number() ? fps_v->as_number() : 0.0;
        if (!fps_v->is_number() || !std::isfinite(candidate) || candidate <= 0.0) {
            return AnimationWidgetStatus::Invalid;  // 非正 / 非有限 / 非數字 → 不套用
        }
        fps_new = candidate;
        have_fps = true;
    }

    bool loop_new = animation_.loop();
    bool have_loop = false;
    if (const ds::format::Value* loop_v = definition.find(kKeyLoop)) {
        if (!loop_v->is_bool()) {
            return AnimationWidgetStatus::Invalid;  // 型別不符 → 不套用
        }
        loop_new = loop_v->as_bool();
        have_loop = true;
    }

    // --- 全數驗證通過：建構幀來源並委派 E4-07 set_frames（其本身亦全有或全無）---
    std::vector<ds::elements::MemoryImageSource> sources;
    sources.reserve(refs.size());
    for (std::size_t i = 0; i < refs.size(); ++i) {
        sources.emplace_back(refs[i], dims[i]);
    }
    std::vector<std::reference_wrapper<const ds::elements::ImageSource>> source_refs;
    source_refs.reserve(sources.size());
    for (const auto& s : sources) {
        source_refs.emplace_back(s);
    }

    if (animation_.set_frames(source_refs) != ds::elements::FrameAnimationStatus::Ok) {
        // 已於上方逐項預先驗證（非空參照 + 正尺寸），理論上不會落入此分支；防禦性不靜默。
        return AnimationWidgetStatus::Invalid;
    }
    if (have_fps) {
        animation_.set_fps(fps_new);  // 已預先驗證合法，恆 Ok
    }
    if (have_loop) {
        animation_.set_loop(loop_new);
    }
    return AnimationWidgetStatus::Ok;
}

void AnimationWidget::play() noexcept { animation_.play(); }
void AnimationWidget::pause() noexcept { animation_.pause(); }
bool AnimationWidget::is_playing() const noexcept { return animation_.is_playing(); }

void AnimationWidget::advance(ds::elements::Tick dt) { animation_.advance(dt); }

void AnimationWidget::reset() noexcept { animation_.reset(); }

std::size_t AnimationWidget::current_frame() const noexcept { return animation_.current_frame(); }
std::size_t AnimationWidget::frame_count() const noexcept { return animation_.frame_count(); }
double AnimationWidget::fps() const noexcept { return animation_.fps(); }
bool AnimationWidget::loop() const noexcept { return animation_.loop(); }
bool AnimationWidget::is_finished() const noexcept { return animation_.is_finished(); }

ds::elements::ImageRenderModel AnimationWidget::render_model() const {
    return animation_.render_model();
}

}  // namespace ds::widgets
