// E4-07 幀序列動畫 — 實作
//
// 純邏輯：以注入式 tick 推進「累計幀進度」，並委由 E4-02 `ImageElement` 顯示目前幀。
// 不含任何平台分支或真實計時器 / 繪製後端；不新增絕對座標 / 數字 z-order（NFR-02）。
#include "frame_animation_element.hpp"

#include <cmath>

namespace ds::elements {

FrameAnimationStatus FrameAnimationElement::set_frames(
    const std::vector<std::reference_wrapper<const ImageSource>>& frames) {
    if (frames.empty()) {
        return FrameAnimationStatus::Invalid;  // 空幀序列不靜默（不套用，維持既有幀序列）
    }

    std::vector<MemoryImageSource> next;
    next.reserve(frames.size());
    for (const ImageSource& src : frames) {
        if (!src.valid()) {
            return FrameAnimationStatus::Invalid;  // 任一幀無效來源：整批不套用（不部分套用）
        }
        const ImageDimensions dims = src.dimensions();
        if (dims.width <= 0 || dims.height <= 0) {
            return FrameAnimationStatus::Invalid;  // 尺寸為零 / 負：明確報錯，不部分套用
        }
        next.emplace_back(src.reference(), dims);
    }

    frames_ = std::move(next);
    index_ = 0;
    progress_ = 0.0;
    finished_ = false;
    sync_current_source();
    return FrameAnimationStatus::Ok;
}

FrameAnimationStatus FrameAnimationElement::set_fps(double fps) {
    if (!std::isfinite(fps) || fps <= 0.0) {
        return FrameAnimationStatus::Invalid;  // 非正 / 非有限 fps：不靜默改值
    }
    fps_ = fps;
    return FrameAnimationStatus::Ok;
}

void FrameAnimationElement::advance(Tick dt) {
    if (frames_.empty() || !playing_) {
        return;  // 空幀序列或暫停中：安全 no-op
    }
    if (!loop_ && finished_) {
        return;  // 單次模式已播完：安全 no-op（維持在最後一幀）
    }

    progress_ += fps_ * static_cast<double>(dt);
    const double count = static_cast<double>(frames_.size());

    if (loop_) {
        // 以幀數為模（wrap），恆落在 [0, count) 內循環，不丟脈衝、不漂移。
        progress_ = std::fmod(progress_, count);
        if (progress_ < 0.0) {
            progress_ += count;  // fps() > 0 且 dt 為無號整數，理論上不會發生；保守處理。
        }
        index_ = static_cast<std::size_t>(progress_);
    } else {
        const double last = count - 1.0;
        if (progress_ >= last) {
            progress_ = last;  // 夾在最後一幀，不超出
            index_ = frames_.size() - 1;
            finished_ = true;
        } else {
            index_ = static_cast<std::size_t>(progress_);
        }
    }

    sync_current_source();
}

ds::render::AnimationId FrameAnimationElement::attach(ds::render::AnimationDriver& driver) {
    return driver.add([this](const ds::render::AnimationFrame& frame) { advance(frame.dt); });
}

bool FrameAnimationElement::is_finished() const noexcept {
    if (frames_.empty()) {
        return true;  // 無可播放內容：視為已完成（承 E4-11 空文字立即完成的精神）
    }
    if (loop_) {
        return false;  // 循環模式恆不完成
    }
    return finished_;
}

void FrameAnimationElement::reset() noexcept {
    index_ = 0;
    progress_ = 0.0;
    finished_ = false;
    sync_current_source();
}

void FrameAnimationElement::sync_current_source() {
    if (frames_.empty()) {
        image_.clear_source();
        return;
    }
    // frames_[index_] 已於 set_frames 驗證過合法性，此處 set_source 恆成功。
    image_.set_source(frames_[index_]);
}

}  // namespace ds::elements
