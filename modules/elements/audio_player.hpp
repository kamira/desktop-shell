// E4-26 音效播放 — 平台中立介面
//
// 桌面外殼的 UI 元件（按鈕點擊、通知、錯誤提示等）需要播放短音效。
// 本單元屬 module 層（子系統 elements，非視覺），提供平台中立的音效播放抽象：
//   - `SoundId` / `SoundClip`：音效資源的識別碼與引用（由 `SoundLibrary` 註冊配發）。
//   - `AudioPlayer`：播放介面（play / stop / stop_all / set_volume / volume / is_playing）。
//   - `NullAudioPlayer`：null 後端——記錄呼叫並維護播放狀態，但**不驅動任何真實音訊裝置**、
//     不出聲。可完全以單元測試驗證狀態轉換（誰在播、音量多少、呼叫次序）。
//
// 因此本單元不含 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支，也不綁真實音訊 API。
// 真實輸出後端（CoreAudio / WASAPI 等）由後續相位另實作同一 `AudioPlayer` 介面。
#ifndef DS_ELEMENTS_E4_26_AUDIO_PLAYER_HPP
#define DS_ELEMENTS_E4_26_AUDIO_PLAYER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ds::elements {

// 音效資源識別碼。由 SoundLibrary::register_clip() 配發，單調遞增且不重用；
// 0 保留為「無效 / 未註冊音效」。
using SoundId = std::uint64_t;

// 保留的無效音效識別碼。
inline constexpr SoundId kInvalidSoundId = 0;

// 一個已註冊的音效資源引用。name 為平台中立的邏輯資源鍵（如 "click"、"error"），
// 實際的檔案 / 位元組來源由真實後端在後續相位解析——本單元只保管引用，不載入音訊資料。
struct SoundClip {
    SoundId id = kInvalidSoundId;
    std::string name;
};

// 音效資源登錄簿：把邏輯資源鍵註冊為 SoundClip 並配發 SoundId。
// 純資料操作，不觸碰任何音訊裝置。
class SoundLibrary {
public:
    SoundLibrary() = default;

    // 註冊一個音效資源，回傳新配發的 SoundId。
    // name 為空字串則不註冊並回傳 kInvalidSoundId（無效）。
    // 同名可重複註冊（各自得到不同 id）——由呼叫端決定是否去重。
    SoundId register_clip(std::string name);

    // 該 id 是否為本登錄簿已註冊的音效。
    bool contains(SoundId id) const noexcept;

    // 查詢 SoundClip；未註冊回 nullptr。
    const SoundClip* find(SoundId id) const noexcept;

    // 已註冊音效數。
    std::size_t size() const noexcept { return clips_.size(); }

private:
    std::vector<SoundClip> clips_;
    SoundId next_id_ = 1;
};

// 音效播放介面（平台中立）。真實後端與 null 後端皆實作此介面。
//
// 語意約定：
//   - play(id)：開始播放某音效；id 必須為有效且已註冊，否則回 false。
//   - stop(id)：停止某正在播放的音效；未在播放回 false。
//   - stop_all()：停止全部正在播放的音效。
//   - set_volume(v)：設定主音量，v 會被夾到 [0, 1]。
//   - volume()：目前主音量。
//   - is_playing(id)：某音效目前是否正在播放。
class AudioPlayer {
public:
    virtual ~AudioPlayer() = default;

    virtual bool play(SoundId id) = 0;
    virtual bool stop(SoundId id) = 0;
    virtual void stop_all() = 0;
    virtual void set_volume(float volume) = 0;
    virtual float volume() const = 0;
    virtual bool is_playing(SoundId id) const = 0;
};

// 呼叫種類，供 null 後端記錄呼叫序列以驗證狀態轉換。
enum class AudioCall {
    Play,
    Stop,
    StopAll,
    SetVolume,
};

// null 後端記錄的單筆呼叫。
struct AudioCallRecord {
    AudioCall kind;
    SoundId id;      // Play / Stop 的目標；其餘為 kInvalidSoundId
    float volume;    // 記錄當下的主音量（SetVolume 為夾限後的值）
    bool accepted;   // 該呼叫是否實際生效（如 play 未註冊 id 則為 false）
};

// null 音效後端：實作 AudioPlayer，維護「誰正在播放 / 主音量 / 呼叫記錄」等純狀態，
// 但**不出聲、不碰任何真實音訊裝置**。專供單元測試驗證播放邏輯的狀態轉換。
class NullAudioPlayer : public AudioPlayer {
public:
    // 綁定一個資源登錄簿以驗證 play 的 id 是否已註冊。
    // 登錄簿須在本播放器存續期間有效（僅保存參考，不取得所有權）。
    explicit NullAudioPlayer(const SoundLibrary& library);

    // 開始播放。id 無效或未註冊則回 false（不改狀態、記錄 accepted=false）。
    // 對已在播放的同一 id 再次 play 視為「重新觸發」：計次 +1、維持播放中、回 true。
    bool play(SoundId id) override;

    // 停止某正在播放的音效。未在播放（含未註冊）回 false。
    bool stop(SoundId id) override;

    // 停止全部正在播放的音效。
    void stop_all() override;

    // 設定主音量，夾到 [0, 1]。
    void set_volume(float volume) override;

    float volume() const override { return volume_; }

    bool is_playing(SoundId id) const override;

    // ── 測試用內省介面（不屬 AudioPlayer 介面） ──

    // 某音效自建構起累計被 play() 成功觸發的次數。
    std::size_t play_count(SoundId id) const noexcept;

    // 目前正在播放的相異音效數。
    std::size_t playing_count() const noexcept { return playing_.size(); }

    // 完整呼叫記錄（依發生次序）。
    const std::vector<AudioCallRecord>& call_log() const noexcept { return log_; }

private:
    struct PlayState {
        SoundId id;
        std::size_t count;  // 累計觸發次數
    };

    // 回傳 playing_ 中該 id 的索引；不存在回 playing_.size()。
    std::size_t index_of(SoundId id) const noexcept;

    const SoundLibrary& library_;
    std::vector<PlayState> playing_;   // 目前正在播放者
    std::vector<PlayState> history_;    // 累計計次（含已停止者）
    std::vector<AudioCallRecord> log_;
    float volume_ = 1.0f;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_26_AUDIO_PLAYER_HPP
