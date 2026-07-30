// content/widgets/c2_07/media_control_widget.cpp — C2-07 媒體控制 widget 實作
#include "media_control_widget.hpp"

#include <utility>

namespace ds::widgets {

namespace {

using ds::command::CommandArgs;
using ds::elements::ImageDimensions;
using ds::elements::ImageElement;
using ds::elements::MemoryImageSource;
using ds::metrics::Metric;
using ds::metrics::MetricInstance;
using ds::metrics::MetricValue;
using ds::sysinfo::MediaMetadataProvider;
using ds::sysinfo::PlaybackState;

// 把 E2-13 "media.state" 欄位的數值位階（state_rank：0/1/2）換算回 PlaybackState。
// 沿用 E2-13 的位階詞彙，不自造對映。非精確整數（理論上不會發生，數值恆由 state_rank 寫入）
// 亦保守夾在合法範圍內，不越界。
PlaybackState state_from_rank(double rank) {
    if (rank >= 2.0) return PlaybackState::Playing;
    if (rank >= 1.0) return PlaybackState::Paused;
    return PlaybackState::Stopped;
}

}  // namespace

const char* to_string(MediaControlStatus s) noexcept {
    switch (s) {
        case MediaControlStatus::Ok:
            return "Ok";
        case MediaControlStatus::Invalid:
            return "Invalid";
        case MediaControlStatus::Unavailable:
            return "Unavailable";
    }
    return "Invalid";
}

MediaControlWidget::MediaControlWidget(
    std::string id, ds::kernel::KernelBackend& backend, ds::kernel::LayerStack& layers,
    std::shared_ptr<ds::metrics::MetricRegistry> registry,
    std::shared_ptr<ds::actuators::MediaControlActuator> actuator)
    : id_(std::move(id)),
      base_(id_, backend, layers),
      registry_(std::move(registry)),
      actuator_(std::move(actuator)),
      play_button_(id_ + ".play"),
      pause_button_(id_ + ".pause"),
      prev_button_(id_ + ".prev"),
      next_button_(id_ + ".next") {
    // 控制元素（E4-04 按鈕）直接觸發注入的 E3-03 actuator——`this->actuator_` 於呼叫當下讀取，
    // 故建構後若能力仍未注入（nullptr）亦安全（no-op，不崩）。
    play_button_.set_on_click([this]() {
        if (actuator_) actuator_->handle_play(CommandArgs{});
    });
    pause_button_.set_on_click([this]() {
        if (actuator_) actuator_->handle_pause(CommandArgs{});
    });
    prev_button_.set_on_click([this]() {
        if (actuator_) actuator_->handle_prev(CommandArgs{});
    });
    next_button_.set_on_click([this]() {
        if (actuator_) actuator_->handle_next(CommandArgs{});
    });

    apply_no_media();  // 初始狀態：尚未 refresh() 過，視為「無媒體」（誠實預設，不假裝有資料）。
}

void MediaControlWidget::sync_buttons() {
    model_.play_button = play_button_.render_model();
    model_.pause_button = pause_button_.render_model();
    model_.prev_button = prev_button_.render_model();
    model_.next_button = next_button_.render_model();
}

void MediaControlWidget::apply_no_media() {
    model_.has_media = false;
    model_.playback = PlaybackState::Stopped;
    model_.title.clear();
    model_.artist.clear();
    model_.album.clear();

    ImageElement empty_art;  // 未設定來源 → has_source=false（誠實表達「無封面」）。
    model_.artwork = empty_art.render_model();

    sync_buttons();
}

MediaControlStatus MediaControlWidget::refresh() {
    if (!registry_) {
        apply_no_media();
        return MediaControlStatus::Ok;
    }

    const std::shared_ptr<Metric> metric = registry_->get(MediaMetadataProvider::kMetricId);
    if (!metric) {
        apply_no_media();
        return MediaControlStatus::Ok;
    }

    const MetricInstance* state_inst = metric->find_instance(MediaMetadataProvider::kFieldState);
    const MetricInstance* title_inst = metric->find_instance(MediaMetadataProvider::kFieldTitle);
    const MetricInstance* artist_inst = metric->find_instance(MediaMetadataProvider::kFieldArtist);
    const MetricInstance* album_inst = metric->find_instance(MediaMetadataProvider::kFieldAlbum);
    const MetricInstance* artwork_inst =
        metric->find_instance(MediaMetadataProvider::kFieldArtwork);
    if (state_inst == nullptr || title_inst == nullptr || artist_inst == nullptr ||
        album_inst == nullptr || artwork_inst == nullptr) {
        // 結構性錯誤：指標存在但欄位不齊全——不套用，既有 render_model() 保留。
        return MediaControlStatus::Invalid;
    }

    const MetricValue title_val = title_inst->value();
    if (!title_val.valid) {
        // 與 E2-13 "無播放" 語意一致：title 欄位 valid==false 即無播放中媒體工作階段。
        apply_no_media();
        return MediaControlStatus::Ok;
    }

    const MetricValue state_val = state_inst->value();
    const MetricValue artist_val = artist_inst->value();
    const MetricValue album_val = album_inst->value();
    const MetricValue artwork_val = artwork_inst->value();

    model_.has_media = true;
    model_.playback = state_val.valid ? state_from_rank(state_val.number) : PlaybackState::Stopped;
    model_.title = title_val.text.value_or("");
    model_.artist = artist_val.valid ? artist_val.text.value_or("") : "";
    model_.album = album_val.valid ? album_val.text.value_or("") : "";

    const std::string artwork_ref = artwork_val.valid ? artwork_val.text.value_or("") : "";
    ImageElement art;
    if (!artwork_ref.empty()) {
        MemoryImageSource source(artwork_ref, ImageDimensions{kArtworkSize, kArtworkSize});
        art.set_source(source);
        art.set_target(id_ + ".artwork");
    }
    model_.artwork = art.render_model();

    sync_buttons();
    return MediaControlStatus::Ok;
}

bool MediaControlWidget::has_control() const noexcept {
    return actuator_ != nullptr && actuator_->backend() != nullptr;
}

MediaControlStatus MediaControlWidget::trigger(MediaControlButtonBridge& button) {
    if (!has_control()) {
        return MediaControlStatus::Unavailable;
    }
    button.click();  // 模擬一次 press+release（觸發 on_click，見建構子綁定）。
    sync_buttons();
    return MediaControlStatus::Ok;
}

MediaControlStatus MediaControlWidget::play() { return trigger(play_button_); }
MediaControlStatus MediaControlWidget::pause() { return trigger(pause_button_); }
MediaControlStatus MediaControlWidget::next() { return trigger(next_button_); }
MediaControlStatus MediaControlWidget::prev() { return trigger(prev_button_); }

ds::actuators::MediaControlState MediaControlWidget::actuator_state() const {
    if (!actuator_) return ds::actuators::MediaControlState{};
    return actuator_->current_state();
}

}  // namespace ds::widgets
