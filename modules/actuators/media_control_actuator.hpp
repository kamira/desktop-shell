// E3-03 媒體播放控制致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「控制媒體播放」這組副作用（播放 / 暫停 / 上一首 / 下一首 / 停止 / 切換
// 播放暫停 / 設定音量），以具名命令掛上 E6-01 命令匯流排
// （`media.play` / `media.pause` / `media.next` / `media.prev` / `media.stop` /
// `media.toggle` / `media.volume`）。呼叫端只需 命令 id + 具名參數即可觸發，
// 不需相依本致動器或任何 OS 媒體控制 API。
//
// 與 E2-13（媒體播放中繼資料 provider）對稱：**E2-13 讀狀態、E3-03 下控制**。
// 兩者共用同一份平台中立播放狀態詞彙 `ds::sysinfo::PlaybackState`
// （Stopped / Paused / Playing），故控制動作產生的狀態，與 E2-13 回報的狀態，
// 說的是同一種語言（消費者可直接對照，見契約測試）。本致動器**消費** E2-13 的
// `PlaybackState` / `state_rank` / `to_string`，不自造播放狀態模型。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層 / 子系統 actuators），消費 E6-01
// 契約（與已合併的 E3-05 音量致動器採同一「注入式後端 + null 樣式」範式）。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 媒體控制 API**（不含 MediaRemote /
//     MPRemoteCommandCenter / MPRIS / SMTC 等）。所有播放狀態交由可抽換的
//     `MediaControlBackend` 承接；預設 `NullMediaControlBackend` 以純記憶體狀態模擬
//     （播放狀態 + 曲目索引 + 音量），供測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實
//     後端（win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實媒體 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（命令、經 E6-01 分派、toggle 語意、結果回報、音量夾限）與後端解耦：
// 意圖判定與參數驗證在致動器層完成，後端只承接「已合法化」的原語，因此可完全以單元
// 測試驗證。
#ifndef DS_ACTUATORS_E3_03_MEDIA_CONTROL_ACTUATOR_HPP
#define DS_ACTUATORS_E3_03_MEDIA_CONTROL_ACTUATOR_HPP

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "command_bus.hpp"     // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）
#include "media_metadata.hpp"  // E2-13：重用平台中立播放狀態詞彙 PlaybackState（PUBLIC 相依）

namespace ds::actuators {

// 與 E2-13 對稱：直接重用其平台中立播放狀態，不自造模型。
using ds::sysinfo::PlaybackState;

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// 命名加 `media_control_` 前綴，避免與同命名空間其他致動器的版本函式衝突。
const char* media_control_contract_version() noexcept;

// 七個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdMediaPlay   = "media.play";    // 無參數，開始 / 恢復播放
inline constexpr const char* kCmdMediaPause  = "media.pause";   // 無參數，暫停
inline constexpr const char* kCmdMediaNext   = "media.next";    // 無參數，下一首
inline constexpr const char* kCmdMediaPrev   = "media.prev";    // 無參數，上一首
inline constexpr const char* kCmdMediaStop   = "media.stop";    // 無參數，停止
inline constexpr const char* kCmdMediaToggle = "media.toggle";  // 無參數，切換 播放/暫停
inline constexpr const char* kCmdMediaVolume = "media.volume";  // 必填 level（int 0–100，可選命令）

// 媒體音量合法範圍（相對值域，非絕對座標）：0 = 靜音位階，100 = 最大。
inline constexpr int kMediaVolumeMin = 0;
inline constexpr int kMediaVolumeMax = 100;
// 「未設定音量」哨兵（null 後端初值；表示尚未經 media.volume 指定過）。
inline constexpr int kMediaVolumeUnset = -1;

// 把任意整數夾限至 [kMediaVolumeMin, kMediaVolumeMax]。致動器層唯一的音量「合法化」點。
inline int clamp_media_volume(int level) noexcept {
    return std::max(kMediaVolumeMin, std::min(kMediaVolumeMax, level));
}

// ---------------------------------------------------------------------------
// MediaControlState — 平台中立地描述一次媒體控制後端的內省快照。
//
// 不含任何 OS handle / 平台會話語意；只承載「播放狀態 + 曲目索引 + 音量」。
// track_index 讓 next / prev 的效果可觀測（相對序，非絕對座標）；volume 為
// kMediaVolumeUnset 表示尚未經 media.volume 指定過。
// ---------------------------------------------------------------------------
struct MediaControlState {
    PlaybackState playback = PlaybackState::Stopped;  // 目前播放狀態（詞彙同 E2-13）
    int track_index = 0;                              // 曲目相對索引（>=0）
    int volume = kMediaVolumeUnset;                   // 目前音量（-1 = 未設定）

    bool operator==(const MediaControlState& o) const noexcept {
        return playback == o.playback && track_index == o.track_index && volume == o.volume;
    }
    bool operator!=(const MediaControlState& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// MediaControlBackend — 執行實際媒體控制副作用的抽象後端。
//
// 相位 1 僅提供 NullMediaControlBackend；相位 2 由平台後端以真實媒體控制 API 實作。
// 介面刻意最小：play / pause / next / prev / stop / set_state 六組播放原語 + 音量存取。
// 契約保證：傳入 set_volume(level) 的 level 已由致動器夾限至 [0,100]（後端可信任）；
// toggle 的「切換」語意由致動器層依 state() 判定後，改呼 play() / pause()（後端不需懂 toggle）。
// ---------------------------------------------------------------------------
class MediaControlBackend {
public:
    virtual ~MediaControlBackend() = default;

    virtual void play() = 0;                       // 開始 / 恢復播放
    virtual void pause() = 0;                      // 暫停
    virtual void next() = 0;                       // 下一首
    virtual void prev() = 0;                       // 上一首
    virtual void stop() = 0;                       // 停止
    virtual void set_state(PlaybackState st) = 0;  // 直接設定播放狀態
    virtual PlaybackState state() const = 0;       // 查詢目前播放狀態

    // 設定音量位階（呼叫端保證已夾限至 [0,100]）。
    virtual void set_volume(int level) = 0;
    // 查詢目前音量（kMediaVolumeUnset 表示尚未設定過）。
    virtual int volume() const = 0;
};

// ---------------------------------------------------------------------------
// NullMediaControlBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體狀態模擬。
//
// 讓致動器在無真實平台後端時仍可完整跑通（play → 狀態變 Playing、pause → Paused、
// stop → Stopped、next / prev 移動曲目索引、toggle 切換、音量設定 / 查詢一致），
// 並讓測試 / 診斷驗證狀態一致性。初值可注入（預設停止、曲目 0、音量未設定）。
// **不接任何真實媒體控制 API**——本類永不含平台呼叫。
// ---------------------------------------------------------------------------
class NullMediaControlBackend : public MediaControlBackend {
public:
    NullMediaControlBackend() = default;
    explicit NullMediaControlBackend(PlaybackState initial_state, int initial_track = 0,
                                     int initial_volume = kMediaVolumeUnset)
        : state_(initial_state),
          track_(initial_track < 0 ? 0 : initial_track),
          volume_(initial_volume == kMediaVolumeUnset ? kMediaVolumeUnset
                                                      : clamp_media_volume(initial_volume)) {}

    void play() override { state_ = PlaybackState::Playing; }
    void pause() override { state_ = PlaybackState::Paused; }
    void stop() override { state_ = PlaybackState::Stopped; }
    // next / prev 移動曲目相對索引；prev 於 0 夾住（不為負）。狀態不變（真實播放器
    // 換曲不改變「正在播放 / 暫停」語意）。
    void next() override { ++track_; }
    void prev() override { if (track_ > 0) --track_; }
    void set_state(PlaybackState st) override { state_ = st; }
    PlaybackState state() const override { return state_; }

    void set_volume(int level) override { volume_ = clamp_media_volume(level); }
    int volume() const override { return volume_; }

    // 內省：完整狀態（供測試 / 診斷）。
    MediaControlState snapshot() const noexcept {
        return MediaControlState{state_, track_, volume_};
    }
    int track_index() const noexcept { return track_; }

private:
    PlaybackState state_ = PlaybackState::Stopped;  // 記憶體模擬的播放狀態
    int track_ = 0;                                 // 記憶體模擬的曲目相對索引（>=0）
    int volume_ = kMediaVolumeUnset;                // 記憶體模擬的音量（-1 = 未設定）
};

// ---------------------------------------------------------------------------
// MediaControlActuator — 把具名媒體控制命令掛上 E6-01 命令匯流排的致動器。
//
// 建構時綁定一個 MediaControlBackend（相位 1 為 NullMediaControlBackend）。
// register_on(bus) 將 media.play / pause / next / prev / stop / toggle / volume 註冊到
// 匯流排；呼叫端之後只需 bus.dispatch("media.play") 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/get_int 保護）：
//   - media.play / pause / next / prev / stop：無參數；施作對應原語。回目前播放狀態。
//   - media.toggle：無參數；Playing → pause()、其餘（Paused / Stopped）→ play()。回切換後狀態。
//   - media.volume：必填 `level`（int）；超出 [0,100] 自動夾限（不失敗）。回夾限後音量。
// 缺 / 型別錯的必填參數 → 回 CommandResult{Failed}（不崩潰、不丟例外）。
// 播放類命令成功結果的 value = 播放狀態位階（int，state_rank；同 E2-13 語意）、
// message = 狀態字串（to_string）；media.volume 成功結果的 value = 夾限後音量（int）。
// ---------------------------------------------------------------------------
class MediaControlActuator {
public:
    explicit MediaControlActuator(std::shared_ptr<MediaControlBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullMediaControlBackend（相位 1）。
    MediaControlActuator() : backend_(std::make_shared<NullMediaControlBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<MediaControlBackend>& backend() const noexcept { return backend_; }

    // 將七個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）則回滾
    // 已註冊者並回 false（不留半掛狀態，不遮蔽既有致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_play = bus.register_command(
            kCmdMediaPlay, [self](const ds::command::CommandArgs& a) { return self->handle_play(a); });
        const bool ok_pause = bus.register_command(
            kCmdMediaPause, [self](const ds::command::CommandArgs& a) { return self->handle_pause(a); });
        const bool ok_next = bus.register_command(
            kCmdMediaNext, [self](const ds::command::CommandArgs& a) { return self->handle_next(a); });
        const bool ok_prev = bus.register_command(
            kCmdMediaPrev, [self](const ds::command::CommandArgs& a) { return self->handle_prev(a); });
        const bool ok_stop = bus.register_command(
            kCmdMediaStop, [self](const ds::command::CommandArgs& a) { return self->handle_stop(a); });
        const bool ok_toggle = bus.register_command(
            kCmdMediaToggle, [self](const ds::command::CommandArgs& a) { return self->handle_toggle(a); });
        const bool ok_volume = bus.register_command(
            kCmdMediaVolume, [self](const ds::command::CommandArgs& a) { return self->handle_volume(a); });
        if (ok_play && ok_pause && ok_next && ok_prev && ok_stop && ok_toggle && ok_volume) {
            return true;
        }
        // 回滾：只移除本次成功掛上的。
        if (ok_play) bus.unregister(kCmdMediaPlay);
        if (ok_pause) bus.unregister(kCmdMediaPause);
        if (ok_next) bus.unregister(kCmdMediaNext);
        if (ok_prev) bus.unregister(kCmdMediaPrev);
        if (ok_stop) bus.unregister(kCmdMediaStop);
        if (ok_toggle) bus.unregister(kCmdMediaToggle);
        if (ok_volume) bus.unregister(kCmdMediaVolume);
        return false;
    }

    // 從匯流排移除七個具名命令。回傳確有移除的數量（0..7）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdMediaPlay) ? 1 : 0;
        n += bus.unregister(kCmdMediaPause) ? 1 : 0;
        n += bus.unregister(kCmdMediaNext) ? 1 : 0;
        n += bus.unregister(kCmdMediaPrev) ? 1 : 0;
        n += bus.unregister(kCmdMediaStop) ? 1 : 0;
        n += bus.unregister(kCmdMediaToggle) ? 1 : 0;
        n += bus.unregister(kCmdMediaVolume) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_play(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->play();
        return ok_with_state();
    }
    ds::command::CommandResult handle_pause(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->pause();
        return ok_with_state();
    }
    ds::command::CommandResult handle_next(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->next();
        return ok_with_state();
    }
    ds::command::CommandResult handle_prev(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->prev();
        return ok_with_state();
    }
    ds::command::CommandResult handle_stop(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->stop();
        return ok_with_state();
    }

    // toggle 語意在致動器層判定（後端不需懂 toggle）：Playing → 暫停，其餘 → 播放。
    ds::command::CommandResult handle_toggle(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        if (backend_->state() == PlaybackState::Playing) {
            backend_->pause();
        } else {
            backend_->play();
        }
        return ok_with_state();
    }

    // media.volume（可選命令）：必填 level（int），夾限至 [0,100]（不失敗）。
    ds::command::CommandResult handle_volume(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        if (!args.has("level")) {
            return ds::command::CommandResult::make_failed("media.volume: missing 'level'");
        }
        const auto level = args.get_int("level");
        if (!level) {
            return ds::command::CommandResult::make_failed(
                "media.volume: 'level' must be an integer");
        }
        const int clamped = clamp_media_volume(static_cast<int>(*level));
        backend_->set_volume(clamped);
        return ds::command::CommandResult::make_ok(ds::command::CommandValue{clamped}, "volume");
    }

    // 便捷查詢：目前完整狀態（播放狀態 + 曲目索引 + 音量）。不經匯流排，供呼叫端 / 測試內省。
    MediaControlState current_state() const {
        if (!backend_) return MediaControlState{};
        return MediaControlState{backend_->state(), track_index_or_zero(), backend_->volume()};
    }

    // 便捷查詢：目前播放狀態（詞彙同 E2-13；供與 E2-13 讀取端對照）。
    PlaybackState playback_state() const {
        return backend_ ? backend_->state() : PlaybackState::Stopped;
    }

private:
    static ds::command::CommandResult no_backend() {
        return ds::command::CommandResult::make_failed("media: no backend bound");
    }

    // 播放類成功結果統一帶「目前播放狀態位階」為回傳值（int，同 E2-13 的 state_rank），
    // 訊息帶狀態字串（to_string）供診斷。
    ds::command::CommandResult ok_with_state() const {
        const PlaybackState st = backend_->state();
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{ds::sysinfo::state_rank(st)}, ds::sysinfo::to_string(st));
    }

    // track_index 僅 null 後端保證可查；抽象後端無此概念時回 0（保守內省）。
    int track_index_or_zero() const {
        if (auto* null_be = dynamic_cast<const NullMediaControlBackend*>(backend_.get())) {
            return null_be->track_index();
        }
        return 0;
    }

    std::shared_ptr<MediaControlBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_03_MEDIA_CONTROL_ACTUATOR_HPP
