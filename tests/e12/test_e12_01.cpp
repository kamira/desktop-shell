// E12-01 idle 記憶體與 CPU 門檻 — 契約測試（gtest）
//
// 涵蓋：低於門檻→閒置降頻、超過門檻→活躍升頻、遲滯(hysteresis)防抖動、
// 與 E2-02 tier 對應、邊界值、門檻設定驗證。
// 平台中立純邏輯：資源讀值全為注入的百分比數值，無真實資源 API / 無平台分支。
#include "idle_threshold.hpp"

#include <gtest/gtest.h>

using ds::common::ActivityLevel;
using ds::common::IdleThresholdPolicy;
using ds::common::lower_tier;
using ds::common::to_string;
using ds::metrics::SamplingTier;

namespace {

// --- 自由函式 / 預設 -------------------------------------------------------

TEST(ActivityLevelString, StableDiagnostics) {
    EXPECT_STREQ("active", to_string(ActivityLevel::Active));
    EXPECT_STREQ("idle", to_string(ActivityLevel::Idle));
}

TEST(LowerTier, DowngradesOneStepAndClampsAtLow) {
    EXPECT_EQ(SamplingTier::Normal, lower_tier(SamplingTier::High));
    EXPECT_EQ(SamplingTier::Low, lower_tier(SamplingTier::Normal));
    EXPECT_EQ(SamplingTier::Low, lower_tier(SamplingTier::Low));            // 夾底
    EXPECT_EQ(SamplingTier::OnDemand, lower_tier(SamplingTier::OnDemand));  // 非週期維持
}

TEST(Defaults, InitialLevelActiveAndDefaultThresholds) {
    IdleThresholdPolicy p;
    EXPECT_EQ(ActivityLevel::Active, p.level());  // 保守初態
    EXPECT_DOUBLE_EQ(30.0, p.cpu_active_threshold());
    EXPECT_DOUBLE_EQ(70.0, p.mem_active_threshold());
    EXPECT_DOUBLE_EQ(10.0, p.hysteresis_band());
    // 閒置門檻 = 活躍門檻 − 帶寬。
    EXPECT_DOUBLE_EQ(20.0, p.cpu_idle_threshold());
    EXPECT_DOUBLE_EQ(60.0, p.mem_idle_threshold());
}

TEST(Defaults, FactoryEqualsDefaultConstruction) {
    IdleThresholdPolicy p = IdleThresholdPolicy::defaults();
    EXPECT_DOUBLE_EQ(30.0, p.cpu_active_threshold());
    EXPECT_DOUBLE_EQ(70.0, p.mem_active_threshold());
}

// --- 低於門檻 → 閒置降頻 ---------------------------------------------------

TEST(Evaluate, BothBelowIdleThresholdGoesIdle) {
    IdleThresholdPolicy p;  // cpu_idle=20, mem_idle=60
    EXPECT_EQ(ActivityLevel::Idle, p.evaluate(5.0, 10.0));
    EXPECT_EQ(ActivityLevel::Idle, p.level());
}

TEST(Evaluate, IdleRecommendsAndDowngradesTier) {
    IdleThresholdPolicy p;
    p.evaluate(1.0, 1.0);  // → Idle
    // 預設映射 Idle→Low。
    EXPECT_EQ(SamplingTier::Low, p.recommended_tier());
    // 閒置把基準分級降一階（與 E2-02 整合）。
    EXPECT_EQ(SamplingTier::Normal, p.adjust_tier(SamplingTier::High));
    EXPECT_EQ(SamplingTier::Low, p.adjust_tier(SamplingTier::Normal));
}

// --- 超過門檻 → 活躍升頻 ---------------------------------------------------

TEST(Evaluate, CpuAboveThresholdGoesActive) {
    IdleThresholdPolicy p;
    p.reset(ActivityLevel::Idle);
    EXPECT_EQ(ActivityLevel::Active, p.evaluate(50.0, 5.0));  // CPU 高、記憶體低 → OR 進活躍
}

TEST(Evaluate, MemAboveThresholdGoesActive) {
    IdleThresholdPolicy p;
    p.reset(ActivityLevel::Idle);
    EXPECT_EQ(ActivityLevel::Active, p.evaluate(1.0, 90.0));  // 記憶體高 → 活躍
}

TEST(Evaluate, ActiveRecommendsAndPreservesTier) {
    IdleThresholdPolicy p;
    p.evaluate(80.0, 80.0);  // → Active
    EXPECT_EQ(ActivityLevel::Active, p.level());
    EXPECT_EQ(SamplingTier::High, p.recommended_tier());          // 預設 Active→High
    EXPECT_EQ(SamplingTier::High, p.adjust_tier(SamplingTier::High));  // 活躍原樣回
    EXPECT_EQ(SamplingTier::Normal, p.adjust_tier(SamplingTier::Normal));
}

// --- 遲滯（hysteresis）防抖動 ----------------------------------------------

TEST(Hysteresis, DeadBandHoldsCurrentState) {
    IdleThresholdPolicy p;  // active=30/70, idle=20/60
    // 從 Active 起，讀值落在死區（CPU 25 介於 20..30、記憶體 65 介於 60..70）→ 維持 Active。
    p.reset(ActivityLevel::Active);
    EXPECT_EQ(ActivityLevel::Active, p.evaluate(25.0, 65.0));
    // 同一死區讀值，若現態為 Idle → 維持 Idle（遲滯的另一半）。
    p.reset(ActivityLevel::Idle);
    EXPECT_EQ(ActivityLevel::Idle, p.evaluate(25.0, 65.0));
}

TEST(Hysteresis, NoThrashingAcrossDeadBand) {
    IdleThresholdPolicy p;
    p.reset(ActivityLevel::Idle);
    // 在死區內來回抖動的讀值序列不應翻轉等級（防 thrashing）。
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(ActivityLevel::Idle, p.evaluate(22.0, 61.0));
        EXPECT_EQ(ActivityLevel::Idle, p.evaluate(28.0, 68.0));
    }
    // 明確越過上緣才切活躍。
    EXPECT_EQ(ActivityLevel::Active, p.evaluate(31.0, 10.0));
    // 明確跌破下緣才切回閒置。
    EXPECT_EQ(ActivityLevel::Idle, p.evaluate(19.0, 59.0));
}

TEST(Hysteresis, ZeroBandDegeneratesToSingleThreshold) {
    IdleThresholdPolicy p;
    p.set_hysteresis(0.0);  // 閒置門檻 = 活躍門檻
    EXPECT_DOUBLE_EQ(p.cpu_active_threshold(), p.cpu_idle_threshold());
    p.reset(ActivityLevel::Active);
    // 帶寬 0：低於門檻即閒置（無死區）。CPU 29.9 < 30 且 mem 低 → Idle。
    EXPECT_EQ(ActivityLevel::Idle, p.evaluate(29.9, 10.0));
}

// --- 邊界值 ---------------------------------------------------------------

TEST(Boundary, AtActiveThresholdIsActive) {
    IdleThresholdPolicy p;  // active cpu=30
    p.reset(ActivityLevel::Idle);
    // 恰等於活躍門檻 → 活躍（>= 語意）。
    EXPECT_EQ(ActivityLevel::Active, p.evaluate(30.0, 5.0));
}

TEST(Boundary, AtIdleThresholdHoldsNotIdle) {
    IdleThresholdPolicy p;  // idle cpu=20, mem=60
    p.reset(ActivityLevel::Active);
    // 恰等於閒置門檻：非「< 門檻」，故不進閒置 → 維持 Active（死區邊界）。
    EXPECT_EQ(ActivityLevel::Active, p.evaluate(20.0, 60.0));
}

TEST(Boundary, JustBelowIdleThresholdGoesIdle) {
    IdleThresholdPolicy p;
    p.reset(ActivityLevel::Active);
    EXPECT_EQ(ActivityLevel::Idle, p.evaluate(19.999, 59.999));
}

// --- 門檻設定驗證 ---------------------------------------------------------

TEST(ThresholdValidation, ClampsOutOfRangeToPercentBounds) {
    IdleThresholdPolicy p;
    p.set_thresholds(150.0, -5.0);  // 超界
    EXPECT_DOUBLE_EQ(100.0, p.cpu_active_threshold());  // >100 夾 100
    EXPECT_DOUBLE_EQ(0.0, p.mem_active_threshold());    // <0 夾 0
}

TEST(ThresholdValidation, NegativeHysteresisClampedToZero) {
    IdleThresholdPolicy p;
    p.set_hysteresis(-20.0);
    EXPECT_DOUBLE_EQ(0.0, p.hysteresis_band());
}

TEST(ThresholdValidation, LargeBandClampsIdleThresholdAtZero) {
    IdleThresholdPolicy p;
    p.set_thresholds(30.0, 70.0);
    p.set_hysteresis(100.0);  // 帶寬 > 活躍門檻
    EXPECT_DOUBLE_EQ(0.0, p.cpu_idle_threshold());  // 夾到 0，不變負
    EXPECT_DOUBLE_EQ(0.0, p.mem_idle_threshold());
}

TEST(ThresholdValidation, SettersAreChainable) {
    IdleThresholdPolicy p;
    p.set_thresholds(40.0, 80.0).set_hysteresis(5.0);
    EXPECT_DOUBLE_EQ(40.0, p.cpu_active_threshold());
    EXPECT_DOUBLE_EQ(80.0, p.mem_active_threshold());
    EXPECT_DOUBLE_EQ(35.0, p.cpu_idle_threshold());
}

// --- 與 E2-02 tier 對應 / 自訂映射 ----------------------------------------

TEST(TierMapping, CustomActiveIdleTiers) {
    IdleThresholdPolicy p;
    p.set_tier_mapping(SamplingTier::Normal, SamplingTier::OnDemand);
    p.evaluate(80.0, 80.0);  // Active
    EXPECT_EQ(SamplingTier::Normal, p.recommended_tier());
    p.evaluate(1.0, 1.0);  // Idle
    EXPECT_EQ(SamplingTier::OnDemand, p.recommended_tier());
}

TEST(TierMapping, AdjustTierOnDemandStaysOnDemandWhenIdle) {
    IdleThresholdPolicy p;
    p.evaluate(1.0, 1.0);  // Idle
    EXPECT_EQ(SamplingTier::OnDemand, p.adjust_tier(SamplingTier::OnDemand));
    EXPECT_EQ(SamplingTier::Low, p.adjust_tier(SamplingTier::Low));  // 夾底
}

// --- 端到端：閒置↔活躍轉換直接驅動採集分級 --------------------------------

TEST(Integration, LevelTransitionDrivesSamplingTier) {
    IdleThresholdPolicy p;
    const SamplingTier base = SamplingTier::High;

    // 活躍期：跟手，維持 High。
    p.evaluate(90.0, 20.0);
    EXPECT_EQ(SamplingTier::High, p.adjust_tier(base));

    // 進入閒置：降頻到 Normal（護 idle 門檻）。
    p.evaluate(2.0, 3.0);
    EXPECT_EQ(SamplingTier::Normal, p.adjust_tier(base));

    // 回到活躍：恢復 High。
    p.evaluate(95.0, 5.0);
    EXPECT_EQ(SamplingTier::High, p.adjust_tier(base));
}

}  // namespace
