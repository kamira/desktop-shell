// tests/c2/test_c2_07.cpp — C2-07 媒體控制 widget（gtest）
//
// 涵蓋：C1-01 基底組裝與初始「無媒體」預設、refresh()（經注入 E2-13 MetricRegistry 讀
// "media.nowplaying" 指標：無 registry / 無指標時降級、有效媒體讀取曲名/演出者/專輯/播放狀態、
// E4-02 封面渲染描述含具名參照與目標 surface、空封面參照時 has_source=false、無媒體降級
// （has_media 轉 false 不清空之前 render_model 以外的既有邏輯，即時反映最新一次注入）、指標存在
// 但缺必要欄位實例時 Invalid）、play/pause/next/prev（經 E3-03 注入的 NullMediaControlBackend，
// 驗證後端狀態確實變動、E4-04 四顆按鈕的 on_click 確實觸發）、能力閘控（無 actuator / actuator
// 後端為 null 時 Unavailable，不崩潰）。
#include "media_control_widget.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ds::actuators::MediaControlActuator;
using ds::actuators::MediaControlBackend;
using ds::actuators::NullMediaControlBackend;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::metrics::InMemoryMetric;
using ds::metrics::MetricRange;
using ds::metrics::MetricRegistry;
using ds::profiles::SkinState;
using ds::sysinfo::MediaMetadata;
using ds::sysinfo::MediaMetadataProvider;
using ds::sysinfo::NullMediaSource;
using ds::sysinfo::PlaybackState;
using ds::widgets::MediaControlStatus;
using ds::widgets::MediaControlWidget;

namespace {

// 測試固定件：以 defaults() 能力矩陣建構後端 / 圖層堆疊；預設注入一個空 MetricRegistry 與一個
// 預設綁 NullMediaControlBackend 的 MediaControlActuator（兩者皆可於個別測試以其他值覆寫）。
struct Fixture {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    bool backend_initialized_ = backend.init();  // CHG-20260803-11：成員依宣告順序初始化，故此行在其後成員建構前完成（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    std::shared_ptr<MetricRegistry> registry = std::make_shared<MetricRegistry>();
    std::shared_ptr<MediaControlActuator> actuator = std::make_shared<MediaControlActuator>();
};

// 把一份媒體中繼資料掛上一個新 registry（經 E2-13 MediaMetadataProvider + 注入式
// NullMediaSource），回傳 registry 與其 source（供之後以 source->set_* / clear() 變動）。
struct RegisteredMedia {
    std::shared_ptr<MetricRegistry> registry = std::make_shared<MetricRegistry>();
    std::shared_ptr<NullMediaSource> source = std::make_shared<NullMediaSource>();
    MediaMetadataProvider provider{source};
};

RegisteredMedia make_media_registry(const MediaMetadata& meta) {
    RegisteredMedia rm;
    rm.source->set_metadata(meta);
    rm.provider.register_metrics(*rm.registry);
    return rm;
}

}  // namespace

// -----------------------------------------------------------------------------
// 建構 / C1-01 基底組裝 / 初始「無媒體」預設
// -----------------------------------------------------------------------------

TEST(MediaControlWidget, ConstructedHasAssembledBaseAndNoMediaDefaults) {
    Fixture f;
    MediaControlWidget widget{"widget.media.home", f.backend, f.layers, f.registry, f.actuator};

    EXPECT_EQ(widget.id(), std::string("widget.media.home"));
    EXPECT_EQ(widget.base().id(), std::string("widget.media.home"));
    EXPECT_EQ(widget.base().state(), SkinState::Unloaded);

    EXPECT_FALSE(widget.has_media());
    const auto& model = widget.render_model();
    EXPECT_EQ(model.playback, PlaybackState::Stopped);
    EXPECT_TRUE(model.title.empty());
    EXPECT_FALSE(model.artwork.has_source);
    // 四顆按鈕於建構時已組裝，初始皆為 Normal 態。
    EXPECT_EQ(model.play_button.state, ds::widgets::MediaButtonState::Normal);
    EXPECT_EQ(model.pause_button.state, ds::widgets::MediaButtonState::Normal);
    EXPECT_EQ(model.prev_button.state, ds::widgets::MediaButtonState::Normal);
    EXPECT_EQ(model.next_button.state, ds::widgets::MediaButtonState::Normal);
}

// -----------------------------------------------------------------------------
// refresh — 讀 E2-13 指標
// -----------------------------------------------------------------------------

TEST(MediaControlWidget, RefreshWithNullRegistryDegradesToNoMedia) {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    auto actuator = std::make_shared<MediaControlActuator>();
    MediaControlWidget widget{"widget.media.a", backend, layers, nullptr, actuator};

    EXPECT_EQ(widget.refresh(), MediaControlStatus::Ok);
    EXPECT_FALSE(widget.has_media());
    EXPECT_FALSE(widget.render_model().artwork.has_source);
}

TEST(MediaControlWidget, RefreshWithEmptyRegistryDegradesToNoMedia) {
    Fixture f;
    MediaControlWidget widget{"widget.media.b", f.backend, f.layers, f.registry, f.actuator};

    // registry_ 非 null 但尚未掛上 "media.nowplaying" 指標。
    EXPECT_EQ(widget.refresh(), MediaControlStatus::Ok);
    EXPECT_FALSE(widget.has_media());
}

TEST(MediaControlWidget, RefreshReadsValidMediaMetadata) {
    Fixture f;
    RegisteredMedia rm = make_media_registry(MediaMetadata{
        /*has_media=*/true, PlaybackState::Playing, "Song A", "Artist A", "Album A",
        /*position=*/10.0, /*duration=*/200.0, "art/cover-a"});
    MediaControlWidget widget{"widget.media.c", f.backend, f.layers, rm.registry, f.actuator};

    EXPECT_EQ(widget.refresh(), MediaControlStatus::Ok);
    EXPECT_TRUE(widget.has_media());
    const auto& model = widget.render_model();
    EXPECT_EQ(model.playback, PlaybackState::Playing);
    EXPECT_EQ(model.title, std::string("Song A"));
    EXPECT_EQ(model.artist, std::string("Artist A"));
    EXPECT_EQ(model.album, std::string("Album A"));

    // E4-02 封面渲染描述：具名參照 + 具名目標 surface。
    EXPECT_TRUE(model.artwork.has_source);
    EXPECT_EQ(model.artwork.source_reference, std::string("art/cover-a"));
    EXPECT_EQ(model.artwork.target, std::string("widget.media.c.artwork"));
}

TEST(MediaControlWidget, RefreshWithEmptyArtworkRefHasNoSource) {
    Fixture f;
    RegisteredMedia rm = make_media_registry(
        MediaMetadata{true, PlaybackState::Paused, "Song B", "Artist B", "Album B", 0.0, 0.0, ""});
    MediaControlWidget widget{"widget.media.d", f.backend, f.layers, rm.registry, f.actuator};

    EXPECT_EQ(widget.refresh(), MediaControlStatus::Ok);
    EXPECT_TRUE(widget.has_media());
    EXPECT_FALSE(widget.render_model().artwork.has_source);
}

TEST(MediaControlWidget, RefreshDegradesWhenSourceHasNoMedia) {
    Fixture f;
    RegisteredMedia rm = make_media_registry(
        MediaMetadata{true, PlaybackState::Playing, "Song C", "Artist C", "Album C", 5.0, 100.0,
                      "art/cover-c"});
    MediaControlWidget widget{"widget.media.e", f.backend, f.layers, rm.registry, f.actuator};
    ASSERT_EQ(widget.refresh(), MediaControlStatus::Ok);
    ASSERT_TRUE(widget.has_media());

    // 媒體工作階段結束：來源回到「無播放」並重新取樣，指標各欄位轉為 valid==false。
    rm.source->clear();
    rm.provider.sample();

    EXPECT_EQ(widget.refresh(), MediaControlStatus::Ok);
    EXPECT_FALSE(widget.has_media());
    EXPECT_TRUE(widget.render_model().title.empty());
    EXPECT_FALSE(widget.render_model().artwork.has_source);
}

TEST(MediaControlWidget, RefreshInvalidWhenMetricMissingRequiredFieldInstances) {
    Fixture f;
    MediaControlWidget widget{"widget.media.f", f.backend, f.layers, f.registry, f.actuator};

    // 手工建構一個結構不齊全的 "media.nowplaying" 指標：只掛狀態欄位，缺 title/artist/album/
    // artwork 實例——模擬指標存在但形狀不合法。
    auto metric = std::make_shared<InMemoryMetric>(MediaMetadataProvider::kMetricId, "Now Playing",
                                                    "", MetricRange::unbounded());
    metric->add_instance(MediaMetadataProvider::kFieldState, "Playback State", 0);
    ASSERT_TRUE(f.registry->register_metric(metric));

    EXPECT_EQ(widget.refresh(), MediaControlStatus::Invalid);
    // 不套用：既有（初始「無媒體」）render_model 保留。
    EXPECT_FALSE(widget.has_media());
}

// -----------------------------------------------------------------------------
// play / pause / next / prev — E4-04 按鈕觸發 E3-03 actuator（注入後端）
// -----------------------------------------------------------------------------

TEST(MediaControlWidget, PlayTriggersInjectedActuatorBackend) {
    Fixture f;
    MediaControlWidget widget{"widget.media.g", f.backend, f.layers, f.registry, f.actuator};

    EXPECT_EQ(widget.play(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.actuator_state().playback, PlaybackState::Playing);
    // 按鈕於一次 press+release 後回到 Normal 態（渲染描述有同步）。
    EXPECT_EQ(widget.render_model().play_button.state, ds::widgets::MediaButtonState::Normal);
}

TEST(MediaControlWidget, PauseTriggersInjectedActuatorBackend) {
    Fixture f;
    MediaControlWidget widget{"widget.media.h", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(widget.play(), MediaControlStatus::Ok);

    EXPECT_EQ(widget.pause(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.actuator_state().playback, PlaybackState::Paused);
}

TEST(MediaControlWidget, NextAndPrevMoveTrackIndexOnInjectedBackend) {
    Fixture f;
    MediaControlWidget widget{"widget.media.i", f.backend, f.layers, f.registry, f.actuator};

    EXPECT_EQ(widget.next(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.next(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.actuator_state().track_index, 2);

    EXPECT_EQ(widget.prev(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.actuator_state().track_index, 1);

    // NullMediaControlBackend 的 prev 於 0 夾住，不為負。
    EXPECT_EQ(widget.prev(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.prev(), MediaControlStatus::Ok);
    EXPECT_EQ(widget.actuator_state().track_index, 0);
}

// -----------------------------------------------------------------------------
// 能力閘控（NFR-03）——未注入 actuator / 後端為 null 時 Unavailable，不崩潰
// -----------------------------------------------------------------------------

TEST(MediaControlWidget, ControlsUnavailableWithoutInjectedActuator) {
    Fixture f;
    MediaControlWidget widget{"widget.media.j", f.backend, f.layers, f.registry, nullptr};

    EXPECT_FALSE(widget.has_control());
    EXPECT_EQ(widget.play(), MediaControlStatus::Unavailable);
    EXPECT_EQ(widget.pause(), MediaControlStatus::Unavailable);
    EXPECT_EQ(widget.next(), MediaControlStatus::Unavailable);
    EXPECT_EQ(widget.prev(), MediaControlStatus::Unavailable);
    // 未注入時查詢仍安全回預設值，不崩潰。
    EXPECT_EQ(widget.actuator_state().playback, PlaybackState::Stopped);
}

TEST(MediaControlWidget, ControlsUnavailableWhenActuatorBackendIsNull) {
    Fixture f;
    auto actuator_no_backend =
        std::make_shared<MediaControlActuator>(std::shared_ptr<MediaControlBackend>(nullptr));
    MediaControlWidget widget{"widget.media.k", f.backend, f.layers, f.registry,
                              actuator_no_backend};

    EXPECT_FALSE(widget.has_control());
    EXPECT_EQ(widget.play(), MediaControlStatus::Unavailable);
}

// -----------------------------------------------------------------------------
// to_string（NFR-02：具名結果）
// -----------------------------------------------------------------------------

TEST(MediaControlWidget, ToStringNamesAllStatuses) {
    EXPECT_STREQ(ds::widgets::to_string(MediaControlStatus::Ok), "Ok");
    EXPECT_STREQ(ds::widgets::to_string(MediaControlStatus::Invalid), "Invalid");
    EXPECT_STREQ(ds::widgets::to_string(MediaControlStatus::Unavailable), "Unavailable");
}
