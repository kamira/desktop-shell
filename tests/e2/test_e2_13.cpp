// E2-13 媒體播放中繼資料 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、null 來源「無播放」語意、
// 注入播放中→取得各欄位（曲名/演出者/專輯/進度/總長/封面/狀態）、
// 暫停/停止狀態轉換、進度更新（歷史累積）、透過 E2-02 頻率分級採集（除頻排程）、
// 消費者只走 E2-01 抽象介面、null source 保守不崩、範圍 unbounded、重複註冊保守拒絕。
// 相位 1：只驗介面 + null 來源行為，不含任何平台分支。
#include "media_metadata.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"
#include "sampling.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;
using ds::sysinfo::MediaMetadata;
using ds::sysinfo::MediaMetadataProvider;
using ds::sysinfo::MediaSource;
using ds::sysinfo::NullMediaSource;
using ds::sysinfo::PlaybackState;

namespace {

// 一份「播放中」的平台中立假中繼資料。
MediaMetadata makePlaying() {
    MediaMetadata m;
    m.has_media = true;
    m.state = PlaybackState::Playing;
    m.title = "Clair de Lune";
    m.artist = "Claude Debussy";
    m.album = "Suite bergamasque";
    m.position_seconds = 42.0;
    m.duration_seconds = 300.0;
    m.artwork_ref = "artwork://cache/abc123";
    return m;
}

std::shared_ptr<NullMediaSource> makePlayingSource() {
    return std::make_shared<NullMediaSource>(makePlaying());
}

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(MediaMetadataProvider, ProviderIdIsStable) {
    MediaMetadataProvider p{std::make_shared<NullMediaSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.media");
    EXPECT_EQ(std::string(MediaMetadataProvider::kMetricId), "media.nowplaying");
    EXPECT_EQ(std::string(MediaMetadataProvider::kMetricName), "Now Playing");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(MediaMetadataProvider, IsAMetricProvider) {
    auto p = std::make_shared<MediaMetadataProvider>(std::make_shared<NullMediaSource>());
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.media");
}

// 建議採集分級來自 E2-02（預設 Normal）。
TEST(MediaMetadataProvider, DefaultSamplingTierIsNormal) {
    MediaMetadataProvider p{std::make_shared<NullMediaSource>()};
    EXPECT_EQ(p.sampling_tier(), SamplingTier::Normal);
    // 可覆寫分級（如前景放大時拉高）。
    MediaMetadataProvider hi{std::make_shared<NullMediaSource>(),
                             MediaMetadataProvider::kDefaultPositionHistory,
                             SamplingTier::High};
    EXPECT_EQ(hi.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 註冊到 E2-01 registry
// ===========================================================================
TEST(MediaMetadataProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    MediaMetadataProvider provider{makePlayingSource()};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);  // 掛上一個指標
    EXPECT_TRUE(registry.contains("media.nowplaying"));
    EXPECT_EQ(registry.size(), 1u);

    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->name(), "Now Playing");
    EXPECT_EQ(metric->unit(), "");
    // 固定七欄：狀態/曲名/演出者/專輯/進度/總長/封面。
    EXPECT_EQ(metric->instance_count(), 7u);
}

// 欄位以決定性順序列舉，鍵與提供者常數一致。
TEST(MediaMetadataProvider, FieldsEnumeratedDeterministically) {
    MetricRegistry registry;
    MediaMetadataProvider provider{makePlayingSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);

    EXPECT_EQ(metric->instance(0).instance_id(), "media.state");
    EXPECT_EQ(metric->instance(1).instance_id(), "media.title");
    EXPECT_EQ(metric->instance(2).instance_id(), "media.artist");
    EXPECT_EQ(metric->instance(3).instance_id(), "media.album");
    EXPECT_EQ(metric->instance(4).instance_id(), "media.position");
    EXPECT_EQ(metric->instance(5).instance_id(), "media.duration");
    EXPECT_EQ(metric->instance(6).instance_id(), "media.artwork");
    EXPECT_EQ(metric->instance(0).label(), "Playback State");
    EXPECT_EQ(metric->instance(6).label(), "Artwork");
}

// ===========================================================================
// 注入播放中 → 取得各欄位（走 E2-01 抽象介面）
// ===========================================================================
TEST(MediaMetadataProvider, PlayingFieldsAreReadable) {
    MetricRegistry registry;
    MediaMetadataProvider provider{makePlayingSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);

    // 狀態：數值位階 2 + 文字 "playing"。
    const auto* st = metric->find_instance("media.state");
    ASSERT_NE(st, nullptr);
    EXPECT_TRUE(st->value().valid);
    EXPECT_DOUBLE_EQ(st->value().number, 2.0);
    ASSERT_TRUE(st->value().text.has_value());
    EXPECT_EQ(*st->value().text, "playing");

    // 文字欄位：曲名 / 演出者 / 專輯 / 封面。
    ASSERT_TRUE(metric->find_instance("media.title")->value().text.has_value());
    EXPECT_EQ(*metric->find_instance("media.title")->value().text, "Clair de Lune");
    EXPECT_EQ(*metric->find_instance("media.artist")->value().text, "Claude Debussy");
    EXPECT_EQ(*metric->find_instance("media.album")->value().text, "Suite bergamasque");
    EXPECT_EQ(*metric->find_instance("media.artwork")->value().text, "artwork://cache/abc123");

    // 數值欄位：進度 42s、總長 300s。
    const auto* pos = metric->find_instance("media.position");
    ASSERT_NE(pos, nullptr);
    EXPECT_TRUE(pos->value().valid);
    EXPECT_DOUBLE_EQ(pos->value().number, 42.0);
    const auto* dur = metric->find_instance("media.duration");
    ASSERT_NE(dur, nullptr);
    EXPECT_DOUBLE_EQ(dur->value().number, 300.0);
}

// ===========================================================================
// 無播放狀態（null 來源預設 / 明確 none）
// ===========================================================================
TEST(MediaMetadataProvider, NoPlaybackYieldsUnknownFields) {
    // 預設 null 來源 = 無播放。
    NullMediaSource src;
    EXPECT_FALSE(src.has_media());
    EXPECT_EQ(src.snapshot().state, PlaybackState::Stopped);

    MetricRegistry registry;
    MediaMetadataProvider provider{std::make_shared<NullMediaSource>()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);

    // 指標仍掛上、仍列舉七欄（決定性）。
    EXPECT_EQ(metric->instance_count(), 7u);

    // 狀態欄位恆有效：stopped（位階 0）。
    const auto* st = metric->find_instance("media.state");
    ASSERT_NE(st, nullptr);
    EXPECT_TRUE(st->value().valid);
    EXPECT_DOUBLE_EQ(st->value().number, 0.0);
    EXPECT_EQ(*st->value().text, "stopped");

    // 其餘欄位以「未知」誠實表達（valid==false），不塞假值。
    EXPECT_FALSE(metric->find_instance("media.title")->value().valid);
    EXPECT_FALSE(metric->find_instance("media.artist")->value().valid);
    EXPECT_FALSE(metric->find_instance("media.album")->value().valid);
    EXPECT_FALSE(metric->find_instance("media.position")->value().valid);
    EXPECT_FALSE(metric->find_instance("media.duration")->value().valid);
    EXPECT_FALSE(metric->find_instance("media.artwork")->value().valid);
}

// source 為 null 指標亦保守不崩，掛上「無播放」指標。
TEST(MediaMetadataProvider, NullSourcePointerIsSafe) {
    MetricRegistry registry;
    MediaMetadataProvider provider{nullptr};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 7u);
    EXPECT_EQ(*metric->find_instance("media.state")->value().text, "stopped");
    EXPECT_FALSE(metric->find_instance("media.title")->value().valid);
}

// sample() 在未 register_metrics 時為 no-op（不崩）。
TEST(MediaMetadataProvider, SampleBeforeRegisterIsNoop) {
    MediaMetadataProvider provider{makePlayingSource()};
    provider.sample();  // 尚未註冊 → 無指標可更新，不得崩
    SUCCEED();
}

// ===========================================================================
// 狀態轉換：playing → paused → stopped（經 sample 反映）
// ===========================================================================
TEST(MediaMetadataProvider, StateTransitionsReflectAfterSample) {
    auto src = makePlayingSource();
    MetricRegistry registry;
    MediaMetadataProvider provider{src};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);
    const auto* st = metric->find_instance("media.state");
    ASSERT_NE(st, nullptr);

    // 初始 playing。
    EXPECT_EQ(*st->value().text, "playing");

    // 暫停：來源改狀態 → sample 反映。
    src->set_state(PlaybackState::Paused);
    provider.sample();
    EXPECT_DOUBLE_EQ(st->value().number, 1.0);
    EXPECT_EQ(*st->value().text, "paused");
    // 暫停仍有媒體：文字欄位仍可讀。
    EXPECT_TRUE(metric->find_instance("media.title")->value().valid);

    // 停止且無媒體：非狀態欄位轉未知。
    src->set_metadata(MediaMetadata::none());
    provider.sample();
    EXPECT_DOUBLE_EQ(st->value().number, 0.0);
    EXPECT_EQ(*st->value().text, "stopped");
    EXPECT_FALSE(metric->find_instance("media.title")->value().valid);
}

// ===========================================================================
// 進度更新：sample 逐次把進度推入歷史環
// ===========================================================================
TEST(MediaMetadataProvider, PositionUpdatesAccumulateInHistory) {
    auto src = makePlayingSource();
    MetricRegistry registry;
    MediaMetadataProvider provider{src};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);
    const auto* pos = metric->find_instance("media.position");
    ASSERT_NE(pos, nullptr);

    // 初建已推一筆（42s）。
    EXPECT_EQ(pos->history().size(), 1u);
    EXPECT_DOUBLE_EQ(pos->value().number, 42.0);
    EXPECT_DOUBLE_EQ(pos->history().latest(), 42.0);

    // 逐次推進進度並採集。
    src->set_position(43.0);
    provider.sample();
    src->set_position(44.0);
    provider.sample();

    EXPECT_EQ(pos->history().size(), 3u);
    EXPECT_DOUBLE_EQ(pos->value().number, 44.0);
    EXPECT_DOUBLE_EQ(pos->history().at(0), 42.0);
    EXPECT_DOUBLE_EQ(pos->history().at(1), 43.0);
    EXPECT_DOUBLE_EQ(pos->history().at(2), 44.0);

    // 總長不入歷史（相對靜態）。
    EXPECT_EQ(metric->find_instance("media.duration")->history().capacity(), 0u);
}

// 無播放時進度轉未知且不污染歷史序列。
TEST(MediaMetadataProvider, NoPlaybackDoesNotPushHistory) {
    auto src = makePlayingSource();
    MediaMetadataProvider provider{src};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    const auto* pos = metric->find_instance("media.position");
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->history().size(), 1u);  // 初建一筆

    src->clear();  // 無播放
    provider.sample();
    EXPECT_FALSE(pos->value().valid);
    EXPECT_EQ(pos->history().size(), 1u);  // 未新增（不污染）
}

// ===========================================================================
// 透過 E2-02 頻率分級採集（除頻排程驅動 sample）
// ===========================================================================
TEST(MediaMetadataProvider, SampledViaE2_02Scheduler) {
    auto src = makePlayingSource();
    MetricRegistry registry;
    MediaMetadataProvider provider{src};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    const auto* pos = metric->find_instance("media.position");
    ASSERT_NE(pos, nullptr);

    // 以 E2-02 排程器登記需求：頻率 = 提供者建議分級（Normal，預設間隔 8 tick）。
    SamplingScheduler scheduler;
    const auto demand = scheduler.add_demand(MediaMetadataProvider::kMetricId,
                                             provider.sampling_tier());
    EXPECT_NE(demand, 0u);
    EXPECT_TRUE(scheduler.tracks(MediaMetadataProvider::kMetricId));
    ASSERT_TRUE(scheduler.effective_interval(MediaMetadataProvider::kMetricId).has_value());
    const auto interval = *scheduler.effective_interval(MediaMetadataProvider::kMetricId);
    EXPECT_EQ(interval, 8u);  // Normal 預設間隔

    const std::size_t before = pos->history().size();  // 初建 1

    // 推進 32 個 tick；每當排程器判定該指標到期就前進進度並採集。
    std::size_t samples = 0;
    double clock_pos = 42.0;
    for (ds::metrics::Tick t = 1; t <= 32; ++t) {
        const auto due = scheduler.advance(t);
        bool due_here = false;
        for (const auto& id : due) {
            if (id == MediaMetadataProvider::kMetricId) due_here = true;
        }
        if (due_here) {
            clock_pos += 1.0;
            src->set_position(clock_pos);
            provider.sample();
            ++samples;
        }
    }

    // Normal 間隔 8：32 tick 內於 t=8,16,24,32 各採一次 = 4 次。
    EXPECT_EQ(samples, 4u);
    EXPECT_EQ(pos->history().size(), before + 4u);
    EXPECT_DOUBLE_EQ(pos->value().number, 46.0);  // 42 + 4 次各 +1

    // 撤銷需求後停止追蹤。
    EXPECT_TRUE(scheduler.remove_demand(demand));
    EXPECT_FALSE(scheduler.tracks(MediaMetadataProvider::kMetricId));
}

// on-demand 分級：不週期採集，靠 request_now 觸發（驗提供者分級可搭 E2-02 隨需路徑）。
TEST(MediaMetadataProvider, OnDemandTierUsesRequestNow) {
    auto src = makePlayingSource();
    MediaMetadataProvider provider{src, MediaMetadataProvider::kDefaultPositionHistory,
                                   SamplingTier::OnDemand};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    SamplingScheduler scheduler;
    scheduler.add_demand(MediaMetadataProvider::kMetricId, provider.sampling_tier());
    // OnDemand 無週期間隔。
    EXPECT_FALSE(scheduler.effective_interval(MediaMetadataProvider::kMetricId).has_value());

    // 純推進 tick 不會使其到期。
    bool any = false;
    for (ds::metrics::Tick t = 1; t <= 20; ++t) {
        for (const auto& id : scheduler.advance(t)) {
            if (id == MediaMetadataProvider::kMetricId) any = true;
        }
    }
    EXPECT_FALSE(any);

    // request_now 後下一次 advance 到期一次。
    scheduler.request_now(MediaMetadataProvider::kMetricId);
    const auto due = scheduler.advance(21);
    ASSERT_EQ(due.size(), 1u);
    EXPECT_EQ(due[0], MediaMetadataProvider::kMetricId);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(MediaMetadataProvider, MetricRangeIsUnbounded) {
    MetricRegistry registry;
    MediaMetadataProvider provider{makePlayingSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);
    const auto r = metric->range();
    EXPECT_FALSE(r.is_bounded());
}

// 消費者只走 E2-01 registry / Metric 介面，全程無 sysinfo 型別。
TEST(MediaMetadataProvider, ConsumerWalksViaAbstractContractOnly) {
    MetricRegistry registry;
    MediaMetadataProvider provider{makePlayingSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    std::size_t total = 0;
    for (const auto& m : registry.all()) {
        total += m->instance_count();  // 全程無 sysinfo 型別
    }
    EXPECT_EQ(total, 7u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫既有）。
TEST(MediaMetadataProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    MediaMetadataProvider p1{makePlayingSource()};
    MediaMetadataProvider p2{std::make_shared<NullMediaSource>()};

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);  // 同 id "media.nowplaying" 被拒
    auto metric = registry.get("media.nowplaying");
    ASSERT_NE(metric, nullptr);
    // 既有指標未被覆寫：仍為 p1 的播放中資料。
    EXPECT_EQ(*metric->find_instance("media.title")->value().text, "Clair de Lune");
}

// ===========================================================================
// 值模型單元（PlaybackState / MediaMetadata）
// ===========================================================================
TEST(PlaybackState, RankAndString) {
    EXPECT_EQ(ds::sysinfo::state_rank(PlaybackState::Stopped), 0);
    EXPECT_EQ(ds::sysinfo::state_rank(PlaybackState::Paused), 1);
    EXPECT_EQ(ds::sysinfo::state_rank(PlaybackState::Playing), 2);
    EXPECT_EQ(std::string(ds::sysinfo::to_string(PlaybackState::Stopped)), "stopped");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(PlaybackState::Paused)), "paused");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(PlaybackState::Playing)), "playing");
}

TEST(MediaMetadata, NoneAndEquality) {
    const auto none = MediaMetadata::none();
    EXPECT_FALSE(none.has_media);
    EXPECT_EQ(none.state, PlaybackState::Stopped);

    const auto a = makePlaying();
    auto b = makePlaying();
    EXPECT_EQ(a, b);
    b.position_seconds = 99.0;
    EXPECT_NE(a, b);
    EXPECT_NE(a, none);
}

TEST(NullMediaSource, InjectionApi) {
    NullMediaSource src;
    EXPECT_FALSE(src.has_media());

    src.set_metadata(makePlaying());
    EXPECT_TRUE(src.has_media());
    EXPECT_EQ(src.snapshot().title, "Clair de Lune");

    src.set_state(PlaybackState::Paused);
    EXPECT_EQ(src.snapshot().state, PlaybackState::Paused);

    src.set_position(120.0);
    EXPECT_DOUBLE_EQ(src.snapshot().position_seconds, 120.0);

    src.clear();
    EXPECT_FALSE(src.has_media());
    EXPECT_EQ(src.snapshot(), MediaMetadata::none());
}

TEST(NullMediaSource, ConstructorInjection) {
    NullMediaSource src{makePlaying()};
    EXPECT_TRUE(src.has_media());
    EXPECT_EQ(src.snapshot().artist, "Claude Debussy");
}

}  // namespace
