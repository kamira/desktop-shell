// E2-27 螢幕像素取樣 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、null 後端無讀值、注入像素驗亮度 / 色碼值、
// 具名 anchor（無絕對座標）→ 實例、anchor 去重、消費者只走 E2-01 抽象介面、
// 採集頻率沿用 E2-02 SamplingTier（預設 Low）+ 掛進 SamplingScheduler 除頻、
// PixelColor 亮度 / 色碼工具、null source 保守不崩、重複註冊保守拒絕。
// 相位 1：只驗介面 + null 後端行為，不含任何平台分支。
#include "screen_pixel.hpp"

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
using ds::sysinfo::NullPixelSampleSource;
using ds::sysinfo::PixelColor;
using ds::sysinfo::PixelSampleSource;
using ds::sysinfo::ScreenAnchor;
using ds::sysinfo::ScreenPixelProvider;

namespace {

// 建一個注入了「中心=橘、左上=白」兩個假取樣點的來源。
std::shared_ptr<NullPixelSampleSource> makeFakeSource() {
    auto src = std::make_shared<NullPixelSampleSource>();
    src->set_pixel(ScreenAnchor::Center, PixelColor::rgb(0xFF, 0x88, 0x00));   // 橘
    src->set_pixel(ScreenAnchor::TopLeft, PixelColor::rgb(0xFF, 0xFF, 0xFF));  // 白
    return src;
}

std::vector<ScreenAnchor> centerAndTopLeft() {
    return {ScreenAnchor::Center, ScreenAnchor::TopLeft};
}

// ===========================================================================
// PixelColor 工具：亮度 / 色碼 / 相等
// ===========================================================================
TEST(PixelColor, LuminanceAndHex) {
    // 白 = 亮度 1.0、"#FFFFFF"。
    const auto white = PixelColor::rgb(0xFF, 0xFF, 0xFF);
    EXPECT_DOUBLE_EQ(white.luminance(), 1.0);
    EXPECT_EQ(white.hex(), "#FFFFFF");

    // 黑 = 亮度 0.0、"#000000"。
    const auto black = PixelColor::rgb(0, 0, 0);
    EXPECT_DOUBLE_EQ(black.luminance(), 0.0);
    EXPECT_EQ(black.hex(), "#000000");

    // 純綠 = Rec.601 luma 0.587、"#00FF00"。
    const auto green = PixelColor::rgb(0, 0xFF, 0);
    EXPECT_DOUBLE_EQ(green.luminance(), 0.587);
    EXPECT_EQ(green.hex(), "#00FF00");
}

TEST(PixelColor, Equality) {
    EXPECT_EQ(PixelColor::rgb(1, 2, 3), PixelColor::rgb(1, 2, 3));
    EXPECT_NE(PixelColor::rgb(1, 2, 3), PixelColor::rgb(1, 2, 4));
}

// ===========================================================================
// 具名 anchor：穩定字串 / 標籤（無絕對座標，NFR-02）
// ===========================================================================
TEST(ScreenAnchor, StableStrings) {
    EXPECT_EQ(std::string(ds::sysinfo::to_string(ScreenAnchor::Center)), "center");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(ScreenAnchor::TopLeft)), "top-left");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(ScreenAnchor::BottomRight)), "bottom-right");
    EXPECT_EQ(std::string(ds::sysinfo::to_label(ScreenAnchor::Center)), "Center");
    EXPECT_EQ(std::string(ds::sysinfo::to_label(ScreenAnchor::TopLeft)), "Top-Left");
}

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(ScreenPixelProvider, ProviderIdIsStable) {
    ScreenPixelProvider p{std::make_shared<NullPixelSampleSource>(), centerAndTopLeft()};
    EXPECT_EQ(p.provider_id(), "sysinfo.screen.pixel");
    EXPECT_EQ(std::string(ScreenPixelProvider::kMetricId), "screen.pixel");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(ScreenPixelProvider, IsAMetricProvider) {
    auto p = std::make_shared<ScreenPixelProvider>(makeFakeSource(), centerAndTopLeft());
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.screen.pixel");
}

// ===========================================================================
// 註冊到 E2-01 registry
// ===========================================================================
TEST(ScreenPixelProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft()};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(registry.contains("screen.pixel"));
    EXPECT_EQ(registry.size(), 1u);

    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->name(), "Screen Pixel Color");
    EXPECT_EQ(metric->unit(), "");
    // range = bounded(0,1)（相對亮度正規化）。
    const auto r = metric->range();
    EXPECT_TRUE(r.is_bounded());
    EXPECT_DOUBLE_EQ(*r.min, 0.0);
    EXPECT_DOUBLE_EQ(*r.max, 1.0);
}

// ===========================================================================
// 注入像素 → 驗亮度 + 色碼值（每 anchor 一實例）
// ===========================================================================
TEST(ScreenPixelProvider, InjectedPixelsYieldLuminanceAndHex) {
    MetricRegistry registry;
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);

    // 兩個具名取樣點 = 兩個實例（決定性順序 = 建構 anchor 順序）。
    ASSERT_EQ(metric->instance_count(), 2u);
    EXPECT_EQ(metric->instance(0).instance_id(), "center");
    EXPECT_EQ(metric->instance(0).label(), "Center");
    EXPECT_EQ(metric->instance(1).instance_id(), "top-left");

    // 中心=橘 (#FF8800)：value.number = 亮度、text = 色碼。
    const auto orange = PixelColor::rgb(0xFF, 0x88, 0x00);
    const auto v0 = metric->instance(0).value();
    EXPECT_TRUE(v0.valid);
    EXPECT_DOUBLE_EQ(v0.number, orange.luminance());
    ASSERT_TRUE(v0.text.has_value());
    EXPECT_EQ(*v0.text, "#FF8800");

    // 左上=白：亮度 1.0、"#FFFFFF"。
    const auto v1 = metric->instance(1).value();
    EXPECT_TRUE(v1.valid);
    EXPECT_DOUBLE_EQ(v1.number, 1.0);
    ASSERT_TRUE(v1.text.has_value());
    EXPECT_EQ(*v1.text, "#FFFFFF");

    // 有讀值者亮度已推入歷史環（供環境光趨勢 / sparkline）。
    EXPECT_EQ(metric->instance(1).history().size(), 1u);
    EXPECT_DOUBLE_EQ(metric->instance(1).history().latest(), 1.0);
}

// find_instance 依 anchor 字串尋得（E2-01 便利查詢，走抽象介面）。
TEST(ScreenPixelProvider, FindInstanceByAnchorId) {
    MetricRegistry registry;
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);

    const auto* center = metric->find_instance("center");
    ASSERT_NE(center, nullptr);
    EXPECT_EQ(center->label(), "Center");
    EXPECT_EQ(metric->find_instance("bottom-right"), nullptr);  // 未設定的 anchor
}

// ===========================================================================
// null 後端：無讀值 → 實例值為未知（不掃描 / 不抓螢幕）
// ===========================================================================
TEST(ScreenPixelProvider, NullBackendYieldsUnknownValues) {
    // 預設 null 後端：任何 anchor 皆無讀值。
    NullPixelSampleSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.sample(ScreenAnchor::Center).has_value());

    MetricRegistry registry;
    ScreenPixelProvider provider{std::make_shared<NullPixelSampleSource>(), centerAndTopLeft()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);
    // 指標仍被掛上、實例仍存在（消費者可查到「兩個取樣點、目前未知」）。
    ASSERT_EQ(metric->instance_count(), 2u);
    const auto v = metric->instance(0).value();
    EXPECT_FALSE(v.valid);                          // 未知
    EXPECT_EQ(metric->instance(0).history().size(), 0u);  // 未知不污染歷史
}

// source 為 null 指標亦保守不崩：掛上指標、實例值為未知。
TEST(ScreenPixelProvider, NullSourcePointerIsSafe) {
    MetricRegistry registry;
    ScreenPixelProvider provider{nullptr, centerAndTopLeft()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);
    ASSERT_EQ(metric->instance_count(), 2u);
    EXPECT_FALSE(metric->instance(0).value().valid);
}

// 空 anchor 清單 → 掛上 0 實例的指標（保守，不崩）。
TEST(ScreenPixelProvider, EmptyAnchorsYieldZeroInstances) {
    MetricRegistry registry;
    ScreenPixelProvider provider{makeFakeSource(), {}};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 0u);
}

// ===========================================================================
// anchor 去重（保留首次出現順序）
// ===========================================================================
TEST(ScreenPixelProvider, DuplicateAnchorsAreDeduped) {
    ScreenPixelProvider provider{
        makeFakeSource(),
        {ScreenAnchor::Center, ScreenAnchor::TopLeft, ScreenAnchor::Center}};
    // 去重後只剩兩個，順序 = 首次出現。
    ASSERT_EQ(provider.anchors().size(), 2u);
    EXPECT_EQ(provider.anchors()[0], ScreenAnchor::Center);
    EXPECT_EQ(provider.anchors()[1], ScreenAnchor::TopLeft);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    EXPECT_EQ(registry.get("screen.pixel")->instance_count(), 2u);
}

// ===========================================================================
// 採集頻率沿用 E2-02 SamplingTier
// ===========================================================================
TEST(ScreenPixelProvider, DefaultSamplingTierIsLow) {
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft()};
    EXPECT_EQ(provider.sampling_tier(), SamplingTier::Low);
}

TEST(ScreenPixelProvider, SamplingTierIsConfigurable) {
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft(),
                                 SamplingTier::High};
    EXPECT_EQ(provider.sampling_tier(), SamplingTier::High);
}

// 消費 E2-02：把採集需求掛進 SamplingScheduler，排程器以本分級追蹤本指標。
TEST(ScreenPixelProvider, RegistersDemandIntoScheduler) {
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft(),
                                 SamplingTier::Low};
    SamplingScheduler scheduler;
    const auto demand = provider.register_demand(scheduler);
    EXPECT_NE(demand, 0u);

    EXPECT_TRUE(scheduler.tracks("screen.pixel"));
    const auto tier = scheduler.effective_tier("screen.pixel");
    ASSERT_TRUE(tier.has_value());
    EXPECT_EQ(*tier, SamplingTier::Low);

    // 兩個消費者對同一指標的需求由排程器除頻合併：有效分級 = 最高頻者（High）。
    ScreenPixelProvider hi{makeFakeSource(), centerAndTopLeft(), SamplingTier::High};
    hi.register_demand(scheduler);
    EXPECT_EQ(scheduler.demand_count("screen.pixel"), 2u);
    const auto merged = scheduler.effective_tier("screen.pixel");
    ASSERT_TRUE(merged.has_value());
    EXPECT_EQ(*merged, SamplingTier::High);  // 除頻合併後取最高頻
}

// ===========================================================================
// 消費者範式：新增指標 = 新增提供者、消費者只走 E2-01 registry
// ===========================================================================
TEST(ScreenPixelProvider, ConsumerWalksViaAbstractContractOnly) {
    MetricRegistry registry;
    ScreenPixelProvider provider{makeFakeSource(), centerAndTopLeft()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 一個「掛件」風格的消費者：只認得 E2-01 的 Metric，數取樣點數量。
    std::size_t total = 0;
    for (const auto& m : registry.all()) {
        total += m->instance_count();  // 全程無 sysinfo 型別
    }
    EXPECT_EQ(total, 2u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫）。
TEST(ScreenPixelProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    ScreenPixelProvider p1{makeFakeSource(), centerAndTopLeft()};
    ScreenPixelProvider p2{std::make_shared<NullPixelSampleSource>(),
                           {ScreenAnchor::Center}};

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);  // 同 id "screen.pixel" 被拒
    // 既有指標未被覆寫：仍為 p1 的兩個實例。
    auto metric = registry.get("screen.pixel");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 2u);
}

// ===========================================================================
// null 後端注入 API：set_pixel / clear / clear_all
// ===========================================================================
TEST(NullPixelSampleSource, InjectionApi) {
    NullPixelSampleSource src;
    EXPECT_EQ(src.size(), 0u);

    src.set_pixel(ScreenAnchor::Center, PixelColor::rgb(1, 2, 3));
    EXPECT_EQ(src.size(), 1u);
    ASSERT_TRUE(src.sample(ScreenAnchor::Center).has_value());
    EXPECT_EQ(*src.sample(ScreenAnchor::Center), PixelColor::rgb(1, 2, 3));

    // 覆寫同 anchor 不新增條目。
    src.set_pixel(ScreenAnchor::Center, PixelColor::rgb(9, 9, 9));
    EXPECT_EQ(src.size(), 1u);
    EXPECT_EQ(*src.sample(ScreenAnchor::Center), PixelColor::rgb(9, 9, 9));

    // 清掉某 anchor。
    EXPECT_TRUE(src.clear(ScreenAnchor::Center));
    EXPECT_FALSE(src.clear(ScreenAnchor::Center));  // 已無
    EXPECT_TRUE(src.empty());

    src.set_pixel(ScreenAnchor::TopLeft, PixelColor::rgb(0, 0, 0));
    src.clear_all();
    EXPECT_TRUE(src.empty());
}

}  // namespace
