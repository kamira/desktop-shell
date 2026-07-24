// E4-26 音效播放 — 實作
//
// 純邏輯：資源登錄與 null 後端狀態機，不含任何平台分支或真實音訊 API。
#include "audio_player.hpp"

#include <utility>

namespace ds::elements {

// ── SoundLibrary ──

SoundId SoundLibrary::register_clip(std::string name) {
    if (name.empty()) {
        return kInvalidSoundId;  // 空名：拒絕註冊
    }
    const SoundId id = next_id_++;
    clips_.push_back(SoundClip{id, std::move(name)});
    return id;
}

const SoundClip* SoundLibrary::find(SoundId id) const noexcept {
    if (id == kInvalidSoundId) {
        return nullptr;
    }
    for (const auto& clip : clips_) {
        if (clip.id == id) {
            return &clip;
        }
    }
    return nullptr;
}

bool SoundLibrary::contains(SoundId id) const noexcept {
    return find(id) != nullptr;
}

// ── NullAudioPlayer ──

NullAudioPlayer::NullAudioPlayer(const SoundLibrary& library) : library_(library) {}

std::size_t NullAudioPlayer::index_of(SoundId id) const noexcept {
    for (std::size_t i = 0; i < playing_.size(); ++i) {
        if (playing_[i].id == id) {
            return i;
        }
    }
    return playing_.size();
}

bool NullAudioPlayer::play(SoundId id) {
    const bool accepted = library_.contains(id);
    if (accepted) {
        // 更新目前播放集合。
        const std::size_t at = index_of(id);
        if (at == playing_.size()) {
            playing_.push_back(PlayState{id, 0});
        }

        // 更新累計計次（歷史）。
        bool found_hist = false;
        for (auto& h : history_) {
            if (h.id == id) {
                ++h.count;
                found_hist = true;
                break;
            }
        }
        if (!found_hist) {
            history_.push_back(PlayState{id, 1});
        }
    }
    log_.push_back(AudioCallRecord{AudioCall::Play, id, volume_, accepted});
    return accepted;
}

bool NullAudioPlayer::stop(SoundId id) {
    const std::size_t at = index_of(id);
    const bool accepted = (at != playing_.size());
    if (accepted) {
        playing_.erase(playing_.begin() + static_cast<std::ptrdiff_t>(at));
    }
    log_.push_back(AudioCallRecord{AudioCall::Stop, id, volume_, accepted});
    return accepted;
}

void NullAudioPlayer::stop_all() {
    playing_.clear();
    log_.push_back(AudioCallRecord{AudioCall::StopAll, kInvalidSoundId, volume_, true});
}

void NullAudioPlayer::set_volume(float volume) {
    if (volume < 0.0f) {
        volume = 0.0f;
    } else if (volume > 1.0f) {
        volume = 1.0f;
    }
    volume_ = volume;
    log_.push_back(AudioCallRecord{AudioCall::SetVolume, kInvalidSoundId, volume_, true});
}

bool NullAudioPlayer::is_playing(SoundId id) const {
    return index_of(id) != playing_.size();
}

std::size_t NullAudioPlayer::play_count(SoundId id) const noexcept {
    for (const auto& h : history_) {
        if (h.id == id) {
            return h.count;
        }
    }
    return 0;
}

}  // namespace ds::elements
