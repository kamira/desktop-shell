// E3-03 媒體播放控制致動器 — gtest 契約測試。
//
// 覆蓋：七個具名命令註冊到 E6-01 匯流排、play/pause/next/prev/stop/toggle 經 dispatch
// 分派、null 後端狀態轉換一致、toggle 於三態間切換、next/prev 曲目索引移動與 0 夾住、
// media.volume 夾限與參數驗證（缺 / 型別錯回 Failed，不崩潰）、未知命令回 NotFound、
// 重複註冊回滾、unregister、直接呼叫處理器、與 E2-13 播放狀態詞彙對照、契約版本標記。
#include "media_control_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "media_metadata.hpp"  // E2-13：對照播放狀態詞彙

using ds::actuators::clamp_media_volume;
using ds::actuators::kCmdMediaNext;
using ds::actuators::kCmdMediaPause;
using ds::actuators::kCmdMediaPlay;
using ds::actuators::kCmdMediaPrev;
using ds::actuators::kCmdMediaStop;
using ds::actuators::kCmdMediaToggle;
using ds::actuators::kCmdMediaVolume;
using ds::actuators::kMediaVolumeMax;
using ds::actuators::kMediaVolumeMin;
using ds::actuators::kMediaVolumeUnset;
using ds::actuators::MediaControlActuator;
using ds::actuators::MediaControlBackend;
using ds::actuators::MediaControlState;
using ds::actuators::NullMediaControlBackend;
using ds::actuators::PlaybackState;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;
using ds::command::CommandValue;

namespace {

// 便捷：讀取一個成功結果的 int 回傳值。
int result_int(const ds::command::CommandResult& r) {
    EXPECT_TRUE(r.value.as_int().has_value());
    return static_cast<int>(r.value.as_int().value_or(-999));
}

}  // namespace

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_03_Register, RegistersAllSevenCommands) {
    CommandBus bus;
    MediaControlActuator actuator;  // 預設綁 NullMediaControlBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdMediaPlay));
    EXPECT_TRUE(bus.has_command(kCmdMediaPause));
    EXPECT_TRUE(bus.has_command(kCmdMediaNext));
    EXPECT_TRUE(bus.has_command(kCmdMediaPrev));
    EXPECT_TRUE(bus.has_command(kCmdMediaStop));
    EXPECT_TRUE(bus.has_command(kCmdMediaToggle));
    EXPECT_TRUE(bus.has_command(kCmdMediaVolume));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(7));
}

TEST(E3_03_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    MediaControlActuator a1;
    MediaControlActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的七個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(7));
}

TEST(E3_03_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    MediaControlActuator actuator{std::shared_ptr<MediaControlBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_03_Register, UnregisterRemovesAllSeven) {
    CommandBus bus;
    MediaControlActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(7));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 經匯流排分派 — play / pause / stop 狀態轉換
// ---------------------------------------------------------------------------
TEST(E3_03_Dispatch, PlayPauseStopViaBus) {
    CommandBus bus;
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r_play = bus.dispatch(kCmdMediaPlay);
    EXPECT_EQ(r_play.status, CommandStatus::Ok);
    EXPECT_EQ(backend->state(), PlaybackState::Playing);
    // 回傳值為 state_rank，訊息為 to_string（同 E2-13 詞彙）。
    EXPECT_EQ(result_int(r_play), ds::sysinfo::state_rank(PlaybackState::Playing));
    EXPECT_EQ(r_play.message, std::string(ds::sysinfo::to_string(PlaybackState::Playing)));

    auto r_pause = bus.dispatch(kCmdMediaPause);
    EXPECT_EQ(r_pause.status, CommandStatus::Ok);
    EXPECT_EQ(backend->state(), PlaybackState::Paused);

    auto r_stop = bus.dispatch(kCmdMediaStop);
    EXPECT_EQ(r_stop.status, CommandStatus::Ok);
    EXPECT_EQ(backend->state(), PlaybackState::Stopped);
}

// ---------------------------------------------------------------------------
// next / prev — 曲目索引移動與 0 夾住
// ---------------------------------------------------------------------------
TEST(E3_03_Dispatch, NextPrevMoveTrackIndexAndClampAtZero) {
    CommandBus bus;
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_EQ(backend->track_index(), 0);
    bus.dispatch(kCmdMediaNext);
    bus.dispatch(kCmdMediaNext);
    EXPECT_EQ(backend->track_index(), 2);
    bus.dispatch(kCmdMediaPrev);
    EXPECT_EQ(backend->track_index(), 1);
    // 於 0 夾住：連續 prev 不為負。
    bus.dispatch(kCmdMediaPrev);
    bus.dispatch(kCmdMediaPrev);
    EXPECT_EQ(backend->track_index(), 0);
    // next / prev 不改變播放狀態（換曲不改「播放 / 暫停」語意）。
    bus.dispatch(kCmdMediaPlay);
    bus.dispatch(kCmdMediaNext);
    EXPECT_EQ(backend->state(), PlaybackState::Playing);
}

// ---------------------------------------------------------------------------
// toggle — 三態間切換
// ---------------------------------------------------------------------------
TEST(E3_03_Toggle, TogglesBetweenPlayAndPause) {
    CommandBus bus;
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 初始 Stopped → toggle 應開始播放。
    ASSERT_EQ(backend->state(), PlaybackState::Stopped);
    auto r1 = bus.dispatch(kCmdMediaToggle);
    EXPECT_EQ(backend->state(), PlaybackState::Playing);
    EXPECT_EQ(result_int(r1), ds::sysinfo::state_rank(PlaybackState::Playing));
    // Playing → toggle → Paused。
    bus.dispatch(kCmdMediaToggle);
    EXPECT_EQ(backend->state(), PlaybackState::Paused);
    // Paused → toggle → Playing。
    bus.dispatch(kCmdMediaToggle);
    EXPECT_EQ(backend->state(), PlaybackState::Playing);
}

TEST(E3_03_Toggle, FromStoppedGoesToPlaying) {
    auto backend = std::make_shared<NullMediaControlBackend>(PlaybackState::Stopped);
    MediaControlActuator actuator{backend};
    actuator.handle_toggle(CommandArgs{});
    EXPECT_EQ(backend->state(), PlaybackState::Playing);
}

// ---------------------------------------------------------------------------
// media.volume — 夾限與參數驗證
// ---------------------------------------------------------------------------
TEST(E3_03_Volume, SetsAndClampsVolume) {
    CommandBus bus;
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    EXPECT_EQ(backend->volume(), kMediaVolumeUnset);  // 初值未設定

    auto r = bus.dispatch(kCmdMediaVolume, CommandArgs{}.set("level", CommandValue{42}));
    EXPECT_EQ(r.status, CommandStatus::Ok);
    EXPECT_EQ(result_int(r), 42);
    EXPECT_EQ(backend->volume(), 42);

    // 超上限夾至 100、超下限夾至 0（皆不失敗）。
    auto r_hi = bus.dispatch(kCmdMediaVolume, CommandArgs{}.set("level", CommandValue{500}));
    EXPECT_EQ(result_int(r_hi), kMediaVolumeMax);
    auto r_lo = bus.dispatch(kCmdMediaVolume, CommandArgs{}.set("level", CommandValue{-7}));
    EXPECT_EQ(result_int(r_lo), kMediaVolumeMin);
}

TEST(E3_03_Volume, MissingOrWrongTypeLevelFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 缺 level → Failed，不改後端狀態。
    auto r_miss = bus.dispatch(kCmdMediaVolume, CommandArgs{});
    EXPECT_EQ(r_miss.status, CommandStatus::Failed);
    EXPECT_EQ(backend->volume(), kMediaVolumeUnset);

    // 型別錯（字串）→ Failed。
    auto r_type = bus.dispatch(kCmdMediaVolume,
                               CommandArgs{}.set("level", CommandValue{std::string("loud")}));
    EXPECT_EQ(r_type.status, CommandStatus::Failed);
    EXPECT_EQ(backend->volume(), kMediaVolumeUnset);
}

TEST(E3_03_Volume, ClampHelperBoundaries) {
    EXPECT_EQ(clamp_media_volume(-1), kMediaVolumeMin);
    EXPECT_EQ(clamp_media_volume(0), 0);
    EXPECT_EQ(clamp_media_volume(100), 100);
    EXPECT_EQ(clamp_media_volume(101), kMediaVolumeMax);
}

// ---------------------------------------------------------------------------
// 未知命令 — 匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_03_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    MediaControlActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    auto r = bus.dispatch("media.nonexistent");
    EXPECT_EQ(r.status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// null 後端狀態轉換序列一致
// ---------------------------------------------------------------------------
TEST(E3_03_NullBackend, StateTransitionSequenceConsistent) {
    NullMediaControlBackend backend;
    EXPECT_EQ(backend.state(), PlaybackState::Stopped);
    backend.play();
    EXPECT_EQ(backend.state(), PlaybackState::Playing);
    backend.pause();
    EXPECT_EQ(backend.state(), PlaybackState::Paused);
    backend.set_state(PlaybackState::Playing);
    EXPECT_EQ(backend.state(), PlaybackState::Playing);
    backend.stop();
    EXPECT_EQ(backend.state(), PlaybackState::Stopped);

    // 完整內省快照一致。
    backend.play();
    backend.next();
    backend.set_volume(70);
    const MediaControlState snap = backend.snapshot();
    EXPECT_EQ(snap.playback, PlaybackState::Playing);
    EXPECT_EQ(snap.track_index, 1);
    EXPECT_EQ(snap.volume, 70);
}

TEST(E3_03_NullBackend, InjectedInitialStateHonored) {
    NullMediaControlBackend backend{PlaybackState::Paused, 5, 33};
    EXPECT_EQ(backend.state(), PlaybackState::Paused);
    EXPECT_EQ(backend.track_index(), 5);
    EXPECT_EQ(backend.volume(), 33);
    // 負初始曲目夾為 0；超界初始音量夾限。
    NullMediaControlBackend b2{PlaybackState::Stopped, -3, 250};
    EXPECT_EQ(b2.track_index(), 0);
    EXPECT_EQ(b2.volume(), kMediaVolumeMax);
}

// ---------------------------------------------------------------------------
// 直接呼叫處理器（不經匯流排）
// ---------------------------------------------------------------------------
TEST(E3_03_Handlers, DirectInvocationEquivalent) {
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    actuator.handle_play(CommandArgs{});
    EXPECT_EQ(actuator.playback_state(), PlaybackState::Playing);
    actuator.handle_stop(CommandArgs{});
    EXPECT_EQ(actuator.playback_state(), PlaybackState::Stopped);

    MediaControlState st = actuator.current_state();
    EXPECT_EQ(st.playback, PlaybackState::Stopped);
}

TEST(E3_03_Handlers, NullBackendHandlersReturnFailed) {
    MediaControlActuator actuator{std::shared_ptr<MediaControlBackend>{}};
    EXPECT_EQ(actuator.handle_play(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_pause(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_next(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_prev(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_stop(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_toggle(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(
        actuator.handle_volume(CommandArgs{}.set("level", CommandValue{10})).status,
        CommandStatus::Failed);
    // 無後端內省回預設。
    EXPECT_EQ(actuator.current_state(), MediaControlState{});
    EXPECT_EQ(actuator.playback_state(), PlaybackState::Stopped);
}

// ---------------------------------------------------------------------------
// 與 E2-13 對照（可選）：控制端與讀取端共用同一播放狀態詞彙
// ---------------------------------------------------------------------------
TEST(E3_03_Symmetry, SharesPlaybackVocabularyWithE2_13) {
    // E3-03 控制端把後端推到 Playing；E2-13 讀取端（NullMediaSource）以同一 enum 回報。
    auto backend = std::make_shared<NullMediaControlBackend>();
    MediaControlActuator actuator{backend};
    actuator.handle_play(CommandArgs{});

    // E2-13 讀取側：以相同 PlaybackState 表達「正在播放」。
    ds::sysinfo::MediaMetadata meta;
    meta.has_media = true;
    meta.state = backend->state();  // 直接沿用控制端狀態（同型別、無轉換）
    ds::sysinfo::NullMediaSource source{meta};

    // 兩端說的是同一種語言：控制端狀態 == 讀取端快照狀態。
    EXPECT_EQ(source.snapshot().state, PlaybackState::Playing);
    EXPECT_EQ(source.snapshot().state, actuator.playback_state());
    EXPECT_EQ(ds::sysinfo::state_rank(actuator.playback_state()),
              ds::sysinfo::state_rank(source.snapshot().state));
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_03_Contract, VersionTag) {
    EXPECT_STREQ(ds::actuators::media_control_contract_version(), "e3_03/1.0.0");
}
