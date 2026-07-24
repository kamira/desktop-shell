// E4-26 音效播放 — 單元測試（gtest）
//
// 以 null 後端驗證播放邏輯的狀態轉換：
//   註冊 / 未註冊 id、play 成功與拒絕、is_playing、重播計次、stop、stop 未播放、
//   stop_all、音量夾限、預設音量、呼叫記錄序列與 accepted 標記、多音效獨立。
// 全程不驅動任何真實音訊裝置。
#include "audio_player.hpp"

#include <gtest/gtest.h>

using ds::elements::AudioCall;
using ds::elements::kInvalidSoundId;
using ds::elements::NullAudioPlayer;
using ds::elements::SoundId;
using ds::elements::SoundLibrary;

namespace {

// SoundLibrary：註冊配發遞增 id、空名拒絕、contains / find 正確。
TEST(SoundLibrary, RegisterAssignsMonotonicIds) {
    SoundLibrary lib;
    const SoundId a = lib.register_clip("click");
    const SoundId b = lib.register_clip("error");

    EXPECT_NE(a, kInvalidSoundId);
    EXPECT_NE(b, kInvalidSoundId);
    EXPECT_NE(a, b);
    EXPECT_EQ(lib.size(), 2u);
    EXPECT_TRUE(lib.contains(a));
    EXPECT_TRUE(lib.contains(b));
    ASSERT_NE(lib.find(a), nullptr);
    EXPECT_EQ(lib.find(a)->name, "click");
}

// 空名不註冊、回無效 id；未知 id 與 0 皆不 contains。
TEST(SoundLibrary, RejectsEmptyNameAndUnknownId) {
    SoundLibrary lib;
    EXPECT_EQ(lib.register_clip(""), kInvalidSoundId);
    EXPECT_EQ(lib.size(), 0u);
    EXPECT_FALSE(lib.contains(kInvalidSoundId));
    EXPECT_FALSE(lib.contains(999u));
    EXPECT_EQ(lib.find(999u), nullptr);
}

// 預設音量為 1.0，且一開始沒有任何音效在播放。
TEST(NullAudioPlayer, DefaultsToFullVolumeAndSilent) {
    SoundLibrary lib;
    NullAudioPlayer player(lib);
    EXPECT_FLOAT_EQ(player.volume(), 1.0f);
    EXPECT_EQ(player.playing_count(), 0u);
    EXPECT_TRUE(player.call_log().empty());
}

// play 已註冊 id：回 true、標記播放中、計次為 1、記錄 accepted。
TEST(NullAudioPlayer, PlayRegisteredMarksPlaying) {
    SoundLibrary lib;
    const SoundId click = lib.register_clip("click");
    NullAudioPlayer player(lib);

    EXPECT_TRUE(player.play(click));
    EXPECT_TRUE(player.is_playing(click));
    EXPECT_EQ(player.playing_count(), 1u);
    EXPECT_EQ(player.play_count(click), 1u);

    ASSERT_EQ(player.call_log().size(), 1u);
    EXPECT_EQ(player.call_log()[0].kind, AudioCall::Play);
    EXPECT_EQ(player.call_log()[0].id, click);
    EXPECT_TRUE(player.call_log()[0].accepted);
}

// play 未註冊 / 無效 id：回 false、不改播放狀態、記錄 accepted=false。
TEST(NullAudioPlayer, PlayUnregisteredRejected) {
    SoundLibrary lib;
    NullAudioPlayer player(lib);

    EXPECT_FALSE(player.play(42u));
    EXPECT_FALSE(player.play(kInvalidSoundId));
    EXPECT_EQ(player.playing_count(), 0u);
    EXPECT_FALSE(player.is_playing(42u));

    ASSERT_EQ(player.call_log().size(), 2u);
    EXPECT_FALSE(player.call_log()[0].accepted);
    EXPECT_FALSE(player.call_log()[1].accepted);
}

// 重播同一 id：計次累加，仍只算一個播放中。
TEST(NullAudioPlayer, RetriggerIncrementsCount) {
    SoundLibrary lib;
    const SoundId click = lib.register_clip("click");
    NullAudioPlayer player(lib);

    EXPECT_TRUE(player.play(click));
    EXPECT_TRUE(player.play(click));
    EXPECT_TRUE(player.play(click));
    EXPECT_EQ(player.play_count(click), 3u);
    EXPECT_EQ(player.playing_count(), 1u);
    EXPECT_TRUE(player.is_playing(click));
}

// stop 正在播放者：回 true、移出播放中，但累計計次保留。
TEST(NullAudioPlayer, StopPlayingRemovesButKeepsCount) {
    SoundLibrary lib;
    const SoundId click = lib.register_clip("click");
    NullAudioPlayer player(lib);

    player.play(click);
    EXPECT_TRUE(player.stop(click));
    EXPECT_FALSE(player.is_playing(click));
    EXPECT_EQ(player.playing_count(), 0u);
    EXPECT_EQ(player.play_count(click), 1u);
}

// stop 未在播放（或未註冊）：回 false。
TEST(NullAudioPlayer, StopNotPlayingRejected) {
    SoundLibrary lib;
    const SoundId click = lib.register_clip("click");
    NullAudioPlayer player(lib);

    EXPECT_FALSE(player.stop(click));   // 尚未播放
    EXPECT_FALSE(player.stop(999u));    // 未註冊
}

// stop_all 停止全部；記錄一筆 StopAll。
TEST(NullAudioPlayer, StopAllClearsEverything) {
    SoundLibrary lib;
    const SoundId a = lib.register_clip("a");
    const SoundId b = lib.register_clip("b");
    NullAudioPlayer player(lib);

    player.play(a);
    player.play(b);
    EXPECT_EQ(player.playing_count(), 2u);

    player.stop_all();
    EXPECT_EQ(player.playing_count(), 0u);
    EXPECT_FALSE(player.is_playing(a));
    EXPECT_FALSE(player.is_playing(b));
    EXPECT_EQ(player.call_log().back().kind, AudioCall::StopAll);
}

// 音量夾限到 [0,1]，並記錄夾限後的值。
TEST(NullAudioPlayer, VolumeClampsToUnitRange) {
    SoundLibrary lib;
    NullAudioPlayer player(lib);

    player.set_volume(0.5f);
    EXPECT_FLOAT_EQ(player.volume(), 0.5f);

    player.set_volume(-1.0f);
    EXPECT_FLOAT_EQ(player.volume(), 0.0f);

    player.set_volume(3.0f);
    EXPECT_FLOAT_EQ(player.volume(), 1.0f);

    EXPECT_EQ(player.call_log().back().kind, AudioCall::SetVolume);
    EXPECT_FLOAT_EQ(player.call_log().back().volume, 1.0f);
}

// 多音效彼此獨立：停止其一不影響另一。
TEST(NullAudioPlayer, MultipleSoundsAreIndependent) {
    SoundLibrary lib;
    const SoundId a = lib.register_clip("a");
    const SoundId b = lib.register_clip("b");
    NullAudioPlayer player(lib);

    player.play(a);
    player.play(b);
    EXPECT_TRUE(player.stop(a));
    EXPECT_FALSE(player.is_playing(a));
    EXPECT_TRUE(player.is_playing(b));
    EXPECT_EQ(player.playing_count(), 1u);
}

// 呼叫記錄按次序保存，涵蓋四種呼叫種類。
TEST(NullAudioPlayer, CallLogPreservesOrder) {
    SoundLibrary lib;
    const SoundId a = lib.register_clip("a");
    NullAudioPlayer player(lib);

    player.set_volume(0.8f);
    player.play(a);
    player.stop(a);
    player.stop_all();

    const auto& log = player.call_log();
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0].kind, AudioCall::SetVolume);
    EXPECT_EQ(log[1].kind, AudioCall::Play);
    EXPECT_EQ(log[2].kind, AudioCall::Stop);
    EXPECT_EQ(log[3].kind, AudioCall::StopAll);
}

}  // namespace
