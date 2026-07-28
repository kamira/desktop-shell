// E2-15 音訊峰值與頻譜 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊三個指標到 E2-01 registry、peak/rms 單一實例 + 每頻段可列舉實例、
// peak_of / rms_of 純算術（含正弦樣本）、linear_to_db（含靜音下限）、頻段能量陣列、頻段數
// 變動、0/靜音邊界、無讀值 invalid、經 E2-02 頻率採樣（除頻排程）、null 來源行為、序列來源、
// 範圍 bounded[0,1]、消費者只走 E2-01 抽象介面、重複註冊保守拒絕。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "audio_levels.hpp"

#include <gtest/gtest.h>

#include <cmath>
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
using ds::sysinfo::AudioLevelProvider;
using ds::sysinfo::AudioLevelSample;
using ds::sysinfo::AudioLevelSource;
using ds::sysinfo::NullAudioLevelSource;
using ds::sysinfo::linear_to_db;
using ds::sysinfo::peak_of;
using ds::sysinfo::rms_of;

namespace {

constexpr double kEps = 1e-9;
constexpr double kPi = 3.14159265358979323846;

// 一份有效電平快照（peak / rms / bands）。
AudioLevelSample makeLevel(double peak, double rms, std::vector<double> bands) {
    AudioLevelSample s;
    s.valid = true;
    s.peak = peak;
    s.rms = rms;
    s.bands = std::move(bands);
    return s;
}

// 固定電平來源（單一快照）。
std::shared_ptr<NullAudioLevelSource> makeFixedSource(AudioLevelSample s) {
    return std::make_shared<NullAudioLevelSource>(std::move(s));
}

// 生成 N 點滿幅正弦（振幅 1.0）：peak≈1.0、rms≈0.707。
std::vector<double> makeSine(std::size_t n) {
    std::vector<double> pcm;
    pcm.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        pcm.push_back(std::sin(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n)));
    }
    return pcm;
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(AudioLevelProvider, ProviderIdAndMetricIdsStable) {
    AudioLevelProvider p{std::make_shared<NullAudioLevelSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.audio");
    EXPECT_EQ(std::string(AudioLevelProvider::kPeakMetricId), "audio.peak");
    EXPECT_EQ(std::string(AudioLevelProvider::kRmsMetricId), "audio.rms");
    EXPECT_EQ(std::string(AudioLevelProvider::kSpectrumMetricId), "audio.spectrum");
}

TEST(AudioLevelProvider, IsMetricProvider) {
    AudioLevelProvider p{std::make_shared<NullAudioLevelSource>()};
    MetricProvider& mp = p;  // 可上轉為 E2-01 契約
    EXPECT_EQ(mp.provider_id(), "sysinfo.audio");
}

TEST(AudioLevelProvider, SamplingTierDefaultsHighOverridable) {
    AudioLevelProvider def{std::make_shared<NullAudioLevelSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::High);

    AudioLevelProvider low{std::make_shared<NullAudioLevelSource>(),
                           AudioLevelProvider::kDefaultHistory, SamplingTier::Low};
    EXPECT_EQ(low.sampling_tier(), SamplingTier::Low);
}

// ===========================================================================
// 註冊三個指標：peak / rms 單一實例 + 頻譜每頻段實例
// ===========================================================================
TEST(AudioLevelProvider, RegistersThreeMetrics) {
    auto src = makeFixedSource(makeLevel(0.8, 0.5, {0.1, 0.2, 0.3, 0.4}));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 3u);
    EXPECT_TRUE(reg.contains("audio.peak"));
    EXPECT_TRUE(reg.contains("audio.rms"));
    EXPECT_TRUE(reg.contains("audio.spectrum"));
}

TEST(AudioLevelProvider, PeakAndRmsAreSingleInstance) {
    auto src = makeFixedSource(makeLevel(0.8, 0.5, {0.25, 0.75}));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto peak = reg.get("audio.peak");
    auto rms = reg.get("audio.rms");
    ASSERT_NE(peak, nullptr);
    ASSERT_NE(rms, nullptr);
    // 各恰一個實例（單一實例語意）。
    ASSERT_EQ(peak->instance_count(), 1u);
    ASSERT_EQ(rms->instance_count(), 1u);
    EXPECT_TRUE(peak->is_single());
    EXPECT_EQ(peak->instance(0).instance_id(), "peak");
    EXPECT_EQ(peak->instance(0).label(), "Peak");
    EXPECT_EQ(rms->instance(0).instance_id(), "rms");
    EXPECT_EQ(rms->instance(0).label(), "RMS");

    EXPECT_NEAR(peak->single().value().number, 0.8, kEps);
    EXPECT_NEAR(rms->single().value().number, 0.5, kEps);
}

TEST(AudioLevelProvider, SpectrumEnumeratesPerBand) {
    auto src = makeFixedSource(makeLevel(1.0, 0.7, {0.1, 0.2, 0.3, 0.4}));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto spec = reg.get("audio.spectrum");
    ASSERT_NE(spec, nullptr);
    ASSERT_EQ(spec->instance_count(), 4u);
    EXPECT_EQ(p.band_count(), 4u);
    EXPECT_EQ(spec->instance(0).instance_id(), "band0");
    EXPECT_EQ(spec->instance(0).label(), "Band 0");
    EXPECT_EQ(spec->instance(3).instance_id(), "band3");
    EXPECT_EQ(spec->instance(3).label(), "Band 3");

    EXPECT_NEAR(spec->find_instance("band0")->value().number, 0.1, kEps);
    EXPECT_NEAR(spec->find_instance("band3")->value().number, 0.4, kEps);
}

// peak / rms 實例附 dB 文字表述（number=線性、text=dB）。
TEST(AudioLevelProvider, LevelInstanceCarriesDbText) {
    auto src = makeFixedSource(makeLevel(1.0, 0.5, {}));  // peak full-scale = 0 dB
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    const auto pk = reg.get("audio.peak")->single().value();
    ASSERT_TRUE(pk.valid);
    ASSERT_TRUE(pk.text.has_value());
    EXPECT_EQ(*pk.text, "0.0 dB");  // 線性 1.0 → 0 dB
}

// ===========================================================================
// peak_of / rms_of 純算術（含正弦樣本）
// ===========================================================================
TEST(PeakOf, MaxAbsoluteAndEmpty) {
    EXPECT_NEAR(peak_of({0.1, -0.9, 0.3}), 0.9, kEps);  // 取絕對值最大
    EXPECT_NEAR(peak_of({}), 0.0, kEps);                // 空塊 → 0
    EXPECT_NEAR(peak_of({-2.0, 0.5}), 1.0, kEps);       // 超界夾到 1
}

TEST(RmsOf, RootMeanSquareAndEmpty) {
    // 全 0.5 → rms = 0.5。
    EXPECT_NEAR(rms_of({0.5, 0.5, 0.5, 0.5}), 0.5, kEps);
    EXPECT_NEAR(rms_of({}), 0.0, kEps);  // 空塊 → 0
}

TEST(PeakRms, FromFullScaleSine) {
    // 滿幅正弦：peak≈1.0、rms≈1/sqrt(2)≈0.7071。
    auto sine = makeSine(1024);
    EXPECT_NEAR(peak_of(sine), 1.0, 1e-3);
    EXPECT_NEAR(rms_of(sine), 1.0 / std::sqrt(2.0), 1e-3);
}

// null 來源 set_pcm（如正弦樣本）：peak / rms 由純算術得出、頻段直接注入。
TEST(NullAudioLevelSource, SetPcmComputesPeakRmsInjectsBands) {
    auto src = std::make_shared<NullAudioLevelSource>();
    src->set_pcm(makeSine(1024), {0.2, 0.4});
    AudioLevelSample s = src->sample();
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.peak, 1.0, 1e-3);
    EXPECT_NEAR(s.rms, 1.0 / std::sqrt(2.0), 1e-3);
    ASSERT_EQ(s.band_count(), 2u);
    EXPECT_NEAR(s.bands[0], 0.2, kEps);
    EXPECT_NEAR(s.bands[1], 0.4, kEps);
}

// ===========================================================================
// linear_to_db（含靜音下限）
// ===========================================================================
TEST(LinearToDb, FullScaleHalfAndSilence) {
    EXPECT_NEAR(linear_to_db(1.0), 0.0, kEps);           // full-scale → 0 dB
    EXPECT_NEAR(linear_to_db(0.5), -6.0206, 1e-3);       // 半幅 ≈ -6 dB
    EXPECT_NEAR(linear_to_db(0.0), -120.0, kEps);        // 靜音 → 下限（非 -∞）
    EXPECT_NEAR(linear_to_db(0.0, -80.0), -80.0, kEps);  // 可調下限
    // 極小值夾到下限。
    EXPECT_NEAR(linear_to_db(1e-12), -120.0, kEps);
}

// ===========================================================================
// 頻段數變動
// ===========================================================================
TEST(AudioLevelProvider, BandCountGrowsAcrossSamples) {
    auto src = makeFixedSource(makeLevel(0.5, 0.3, {0.1, 0.2}));  // 2 頻段
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.band_count(), 2u);
    auto spec = reg.get("audio.spectrum");
    const auto* b0_before = spec->find_instance("band0");

    src->set_sample(makeLevel(0.5, 0.3, {0.1, 0.2, 0.3, 0.4}));  // 增為 4 頻段
    p.sample();
    EXPECT_EQ(p.band_count(), 4u);
    EXPECT_EQ(spec->instance_count(), 4u);
    // 既有 band0 參照仍有效（unique_ptr 持有實例）。
    EXPECT_EQ(spec->find_instance("band0"), b0_before);
    EXPECT_NE(spec->find_instance("band3"), nullptr);
}

TEST(AudioLevelProvider, BandCountDecreaseMarksMissingUnknown) {
    auto src = makeFixedSource(makeLevel(0.5, 0.3, {0.1, 0.2, 0.3, 0.4}));  // 4 頻段
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.band_count(), 4u);

    src->set_sample(makeLevel(0.5, 0.3, {0.1, 0.2}));  // 降為 2 頻段
    p.sample();
    auto spec = reg.get("audio.spectrum");
    // 實例數不縮減（4 頻段實例仍在）。
    EXPECT_EQ(p.band_count(), 4u);
    EXPECT_TRUE(spec->find_instance("band0")->value().valid);
    EXPECT_TRUE(spec->find_instance("band1")->value().valid);
    EXPECT_FALSE(spec->find_instance("band2")->value().valid);  // 消失 → 未知
    EXPECT_FALSE(spec->find_instance("band3")->value().valid);
}

// ===========================================================================
// 0 / 靜音邊界
// ===========================================================================
TEST(AudioLevelProvider, SilenceBoundaryValidZero) {
    // 靜音但**有讀值**：peak=rms=0、頻段全 0（valid==true，非未知）。
    auto src = makeFixedSource(makeLevel(0.0, 0.0, {0.0, 0.0, 0.0}));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    const auto pk = reg.get("audio.peak")->single().value();
    ASSERT_TRUE(pk.valid);  // 靜音是真實讀值，非未知
    EXPECT_NEAR(pk.number, 0.0, kEps);
    ASSERT_TRUE(pk.text.has_value());
    EXPECT_EQ(*pk.text, "-120.0 dB");  // 靜音 → dB 下限

    auto spec = reg.get("audio.spectrum");
    for (std::size_t i = 0; i < spec->instance_count(); ++i) {
        EXPECT_TRUE(spec->instance(i).value().valid);
        EXPECT_NEAR(spec->instance(i).value().number, 0.0, kEps);
    }
}

// ===========================================================================
// 無讀值 invalid
// ===========================================================================
TEST(AudioLevelProvider, NoReadingIsInvalidNotZero) {
    // NullAudioLevelSource 預設（未注入）→ 無讀值。
    auto src = std::make_shared<NullAudioLevelSource>();
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_FALSE(reg.get("audio.peak")->single().value().valid);
    EXPECT_FALSE(reg.get("audio.rms")->single().value().valid);
    EXPECT_EQ(p.band_count(), 0u);  // 無讀值 → 無頻段實例（不謊報 0 個頻段能量）

    // 之後注入資料 → sample() 後可讀。
    src->set_sample(makeLevel(0.9, 0.6, {0.5}));
    p.sample();
    EXPECT_TRUE(reg.get("audio.peak")->single().value().valid);
    EXPECT_NEAR(reg.get("audio.peak")->single().value().number, 0.9, kEps);
    EXPECT_EQ(p.band_count(), 1u);
}

TEST(AudioLevelSample, UnknownDefault) {
    AudioLevelSample u = AudioLevelSample::unknown();
    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.band_count(), 0u);
}

// 無讀值不污染歷史（invalid 不推入歷史序列）。
TEST(AudioLevelProvider, InvalidDoesNotPolluteHistory) {
    auto src = std::make_shared<NullAudioLevelSource>();
    src->set_sample(makeLevel(0.8, 0.5, {0.3}));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 首採：有效 → 歷史 1 筆

    src->clear();  // 回無讀值
    p.sample();    // 無讀值 → 不推歷史
    const auto& hist = reg.get("audio.peak")->single().history();
    EXPECT_EQ(hist.size(), 1u);  // 仍 1 筆（未被無讀值污染）
    EXPECT_FALSE(reg.get("audio.peak")->single().value().valid);
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(AudioLevelProvider, SampledViaE2_02Scheduler) {
    // 一列電平序列（peak 隨時間變動），每 tick 採一份。
    std::vector<AudioLevelSample> seq;
    for (int k = 0; k < 8; ++k) {
        seq.push_back(makeLevel(0.5, 0.4, {0.2, 0.6}));
    }
    auto src = std::make_shared<NullAudioLevelSource>(std::move(seq));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 首採消耗第 0 份 → 歷史 1 筆

    SamplingScheduler sched;  // 預設 High 間隔=1
    sched.add_demand(AudioLevelProvider::kPeakMetricId, p.sampling_tier());

    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 4; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == AudioLevelProvider::kPeakMetricId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 4);

    // register(1) + 4 次採樣 = 歷史 5 筆，各 0.5。
    const auto& hist = reg.get("audio.peak")->single().history();
    EXPECT_EQ(hist.size(), 5u);
    EXPECT_NEAR(hist.latest(), 0.5, kEps);
}

// 除頻：多消費者同一頻譜指標合併，最高頻者供給。
TEST(AudioLevelProvider, DeFrequencyCoalescesDemands) {
    SamplingScheduler sched;
    auto d_low = sched.add_demand(AudioLevelProvider::kSpectrumMetricId, SamplingTier::Low);
    sched.add_demand(AudioLevelProvider::kSpectrumMetricId, SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(AudioLevelProvider::kSpectrumMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(AudioLevelProvider::kSpectrumMetricId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(AudioLevelProvider::kSpectrumMetricId), 2u);

    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(AudioLevelProvider::kSpectrumMetricId));
    EXPECT_EQ(*sched.effective_tier(AudioLevelProvider::kSpectrumMetricId), SamplingTier::High);
}

// ===========================================================================
// null 來源行為 / 序列來源
// ===========================================================================
TEST(AudioLevelProvider, NullSourcePointerIsConservative) {
    // source 為 null 指標：仍掛上三個指標，各未知、無頻段、不崩。
    AudioLevelProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 3u);
    EXPECT_FALSE(reg.get("audio.peak")->single().value().valid);
    EXPECT_FALSE(reg.get("audio.rms")->single().value().valid);
    EXPECT_EQ(reg.get("audio.spectrum")->instance_count(), 0u);
    EXPECT_EQ(p.band_count(), 0u);
    p.sample();  // 不崩
    EXPECT_FALSE(reg.get("audio.peak")->single().value().valid);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(AudioLevelProvider, SampleBeforeRegisterIsNoop) {
    AudioLevelProvider p{makeFixedSource(makeLevel(0.5, 0.4, {0.3}))};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.band_count(), 0u);
}

// NullAudioLevelSource：序列逐份推進、列盡回最後一份、空列回無讀值。
TEST(NullAudioLevelSource, SequenceAdvanceExhaustionAndEmpty) {
    NullAudioLevelSource src{std::vector<AudioLevelSample>{
        makeLevel(0.1, 0.1, {}),
        makeLevel(0.2, 0.2, {}),
    }};
    EXPECT_NEAR(src.sample().peak, 0.1, kEps);  // 第 0 份
    EXPECT_NEAR(src.sample().peak, 0.2, kEps);  // 第 1 份
    EXPECT_NEAR(src.sample().peak, 0.2, kEps);  // 列盡 → 持續回最後一份

    NullAudioLevelSource empty;
    EXPECT_FALSE(empty.sample().valid);  // 空列 → 無讀值

    src.reset();  // 游標回起點
    EXPECT_NEAR(src.sample().peak, 0.1, kEps);
}

TEST(NullAudioLevelSource, ClearReturnsToUnknown) {
    NullAudioLevelSource src{makeLevel(0.7, 0.5, {0.3})};
    EXPECT_TRUE(src.sample().valid);
    src.clear();
    EXPECT_FALSE(src.sample().valid);  // 回到無讀值
    EXPECT_TRUE(src.empty());
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(AudioLevelProvider, RangesAreBoundedZeroToOne) {
    AudioLevelProvider p{makeFixedSource(makeLevel(0.5, 0.5, {0.5}))};
    MetricRegistry reg;
    reg.add_provider(p);
    for (const char* id : {"audio.peak", "audio.rms", "audio.spectrum"}) {
        auto m = reg.get(id);
        ASSERT_NE(m, nullptr);
        auto r = m->range();
        ASSERT_TRUE(r.is_bounded());
        EXPECT_NEAR(*r.min, 0.0, kEps);
        EXPECT_NEAR(*r.max, 1.0, kEps);
    }
    // 0.5 正規化到 0.5。
    auto norm = reg.get("audio.spectrum")->range().normalized(0.5);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 0.5, kEps);
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪等化器頻段，不觸及具體型別。
TEST(AudioLevelProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeFixedSource(makeLevel(0.9, 0.6, {0.1, 0.2, 0.3, 0.4}));
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> spec = reg.get("audio.spectrum");
    ASSERT_NE(spec, nullptr);
    double sum = 0.0;
    for (std::size_t i = 0; i < spec->instance_count(); ++i) {
        const auto& inst = spec->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum, 1.0, kEps);  // 0.1+0.2+0.3+0.4
}

TEST(AudioLevelProvider, DuplicateRegistrationRejected) {
    AudioLevelProvider p1{makeFixedSource(makeLevel(0.5, 0.4, {0.3}))};
    AudioLevelProvider p2{makeFixedSource(makeLevel(0.9, 0.8, {0.7}))};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 3u);
    // 第二個提供者掛同一組 id → 三個皆保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 3u);
}

// 值超界時提供者夾到 [0,1]（來源給了超界值也保守）。
TEST(AudioLevelProvider, OutOfRangeValuesClamped) {
    auto src = makeFixedSource(makeLevel(1.5, -0.2, {2.0}));  // 超界
    AudioLevelProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_NEAR(reg.get("audio.peak")->single().value().number, 1.0, kEps);
    EXPECT_NEAR(reg.get("audio.rms")->single().value().number, 0.0, kEps);
    EXPECT_NEAR(reg.get("audio.spectrum")->find_instance("band0")->value().number, 1.0, kEps);
}
