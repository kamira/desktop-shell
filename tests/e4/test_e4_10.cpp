// E4-10 閒置自動動畫觸發 — gtest 測試
//
// 涵蓋：閒置達門檻觸發、活動重置（notify_activity 使閒置計時歸零並暫停後續觸發）、
// E5-09 機率決定觸發與否（機率 0 / 1 邊界 + 精確門檻）、多動畫各自獨立評估、
// advance 推進語意（未達門檻不消耗亂數 / dt=0 安全 no-op / 跨多評估間隔一次推進多次觸發）、
// 決定性（注入腳本亂數重現觸發序列）、登記 / 取消登記、閒置門檻設定驗證。
#include "idle_animation_trigger.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "frame_animation_element.hpp"
#include "random_source.hpp"

using ds::elements::FrameAnimationElement;
using ds::elements::FrameAnimationStatus;
using ds::elements::IdleAnimationStatus;
using ds::elements::IdleAnimationTrigger;
using ds::elements::ImageDimensions;
using ds::elements::ImageSource;
using ds::elements::MemoryImageSource;
using ds::elements::RegistrationId;
using ds::events::RandomSource;

namespace {

// 假亂數來源：回傳預定序列，超出後夾在最後一個值——測試對觸發做確定斷言（同 E5-09 慣例）。
class ScriptedRandomSource : public RandomSource {
public:
    explicit ScriptedRandomSource(std::vector<double> values) : values_(std::move(values)) {}

    double next_unit() override {
        if (values_.empty()) {
            return 0.0;
        }
        const double v = values_[index_ < values_.size() ? index_ : values_.size() - 1];
        ++index_;
        return v;
    }

    std::size_t draws() const { return index_; }

private:
    std::vector<double> values_;
    std::size_t index_ = 0;
};

// 兩幀的最小可用幀序列：res://a, res://b，皆 8x8。
std::vector<MemoryImageSource> two_frames() {
    return {
        MemoryImageSource("res://a", ImageDimensions{8, 8}),
        MemoryImageSource("res://b", ImageDimensions{8, 8}),
    };
}

std::vector<std::reference_wrapper<const ImageSource>> as_refs(
    const std::vector<MemoryImageSource>& frames) {
    std::vector<std::reference_wrapper<const ImageSource>> refs;
    refs.reserve(frames.size());
    for (const auto& f : frames) {
        refs.emplace_back(f);
    }
    return refs;
}

FrameAnimationElement make_ready_animation() {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = two_frames();
    [[maybe_unused]] FrameAnimationStatus st = anim.set_frames(as_refs(frames));
    return anim;
}

}  // namespace

// --- 閒置達門檻觸發 ---
TEST(IdleAnimationTrigger, IdleThresholdReachedTriggersAnimation) {
    IdleAnimationTrigger trig(/*idle_threshold=*/5);
    FrameAnimationElement anim = make_ready_animation();
    const RegistrationId id = trig.register_animation(anim, /*eval_interval=*/1, /*probability=*/1.0);
    ASSERT_NE(id, 0u);
    EXPECT_FALSE(anim.is_playing());

    EXPECT_EQ(trig.advance(4), 0u);  // 未達閒置門檻：不評估、不觸發
    EXPECT_FALSE(trig.is_idle());
    EXPECT_FALSE(anim.is_playing());
    EXPECT_EQ(trig.total_trigger_count(), 0u);

    EXPECT_EQ(trig.advance(1), 1u);  // 第 5 tick 達門檻，同一次 advance 內立即評估並觸發（機率 1）
    EXPECT_TRUE(trig.is_idle());
    EXPECT_TRUE(anim.is_playing());
    EXPECT_EQ(anim.current_frame(), 0u);  // 觸發即 reset 回第 0 幀
    EXPECT_EQ(trig.total_trigger_count(), 1u);
}

// --- 活動重置：notify_activity 歸零閒置計時，暫停後續評估直到再次達門檻 ---
TEST(IdleAnimationTrigger, NotifyActivityResetsIdleTimerAndPausesTriggering) {
    IdleAnimationTrigger trig(5);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, 1, 1.0);

    EXPECT_EQ(trig.advance(4), 0u);
    trig.notify_activity();
    EXPECT_EQ(trig.idle_elapsed(), 0u);
    EXPECT_FALSE(trig.is_idle());

    // 重置後再走 4 tick：若未重置，原本 4+4=8 早已達門檻；重置後仍不足。
    EXPECT_EQ(trig.advance(4), 0u);
    EXPECT_FALSE(anim.is_playing());

    EXPECT_EQ(trig.advance(1), 1u);  // 重置後累計滿 5 tick 才達門檻觸發
    EXPECT_TRUE(anim.is_playing());
}

// --- 活動重置：閒置觸發後再度活動，觸發評估隨之暫停 ---
TEST(IdleAnimationTrigger, NotifyActivityDuringIdleStopsFurtherTriggersUntilIdleAgain) {
    IdleAnimationTrigger trig(2);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, 1, 1.0);

    // dt=2 對 interval=1 的任務是 2 個評估機會（單次 advance 可跨多個間隔，見 E5-09/E5-04
    // 文件），機率 1 全觸發。
    EXPECT_EQ(trig.advance(2), 2u);
    EXPECT_EQ(trig.total_trigger_count(), 2u);
    anim.pause();  // 觀察用：清掉播放旗標以便下方斷言不再被觸發覆蓋

    trig.notify_activity();
    EXPECT_FALSE(trig.is_idle());
    EXPECT_EQ(trig.advance(1), 0u);  // 尚未重新達門檻：不評估、不觸發
    EXPECT_FALSE(anim.is_playing());
}

// --- E5-09 機率決定：機率 0 恆不觸發，但每次評估仍固定抽一個亂數 ---
TEST(IdleAnimationTrigger, ProbabilityZeroNeverFiresEvenWhenIdle) {
    auto rng = std::make_shared<ScriptedRandomSource>(std::vector<double>{0.0, 0.0, 0.0, 0.0, 0.0});
    IdleAnimationTrigger trig(1, rng);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, 1, 0.0);

    EXPECT_EQ(trig.advance(1), 0u);  // 立即閒置（門檻 1），評估一次但機率 0 不觸發
    EXPECT_EQ(trig.advance(4), 0u);  // 再 4 次評估機會，仍全不觸發
    EXPECT_FALSE(anim.is_playing());
    EXPECT_EQ(rng->draws(), 5u);     // 5 次評估 → 5 次抽值，即使機率為 0
}

// --- E5-09 機率決定：精確門檻（u < p 觸發、u >= p 不觸發）搭配腳本亂數 ---
TEST(IdleAnimationTrigger, ProbabilityThresholdDeterminesEachEvaluation) {
    auto rng = std::make_shared<ScriptedRandomSource>(std::vector<double>{0.9, 0.1, 0.9, 0.1});
    IdleAnimationTrigger trig(1, rng);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, 1, 0.5);

    std::vector<bool> fired_each_tick;
    for (int i = 0; i < 4; ++i) {
        const std::size_t fires = trig.advance(1);
        fired_each_tick.push_back(fires == 1u);
        anim.pause();  // 每輪後清掉播放旗標，讓下一輪的斷言只反映該輪是否觸發
    }
    EXPECT_EQ(fired_each_tick, (std::vector<bool>{false, true, false, true}));
    EXPECT_EQ(trig.total_trigger_count(), 2u);
}

// --- 多動畫：各自獨立評估、獨立觸發 ---
TEST(IdleAnimationTrigger, MultipleAnimationsTriggerIndependently) {
    IdleAnimationTrigger trig(1);
    FrameAnimationElement anim_fires = make_ready_animation();
    FrameAnimationElement anim_silent = make_ready_animation();
    trig.register_animation(anim_fires, 1, 1.0);
    trig.register_animation(anim_silent, 1, 0.0);
    EXPECT_EQ(trig.animation_count(), 2u);

    EXPECT_EQ(trig.advance(1), 1u);  // 兩個任務各評估一次，只有機率 1 的那個觸發
    EXPECT_TRUE(anim_fires.is_playing());
    EXPECT_FALSE(anim_silent.is_playing());
    EXPECT_EQ(trig.total_trigger_count(), 1u);
}

// --- 登記 / 取消登記 ---
TEST(IdleAnimationTrigger, RegisterAnimationRejectsZeroInterval) {
    IdleAnimationTrigger trig(1);
    FrameAnimationElement anim = make_ready_animation();
    const RegistrationId id = trig.register_animation(anim, /*eval_interval=*/0, 1.0);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(trig.animation_count(), 0u);
}

TEST(IdleAnimationTrigger, UnregisterAnimationStopsFutureTriggers) {
    IdleAnimationTrigger trig(1);
    FrameAnimationElement anim = make_ready_animation();
    const RegistrationId id = trig.register_animation(anim, 1, 1.0);
    ASSERT_NE(id, 0u);
    EXPECT_TRUE(trig.unregister_animation(id));
    EXPECT_FALSE(trig.unregister_animation(id));  // 已移除，再取消回 false
    EXPECT_EQ(trig.animation_count(), 0u);

    EXPECT_EQ(trig.advance(1), 0u);  // 任務已取消：不再評估、不觸發
    EXPECT_FALSE(anim.is_playing());
}

// --- advance 推進語意：dt=0 安全 no-op ---
TEST(IdleAnimationTrigger, AdvanceZeroIsSafeNoOp) {
    IdleAnimationTrigger trig(1);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, 1, 1.0);

    EXPECT_EQ(trig.advance(0), 0u);
    EXPECT_EQ(trig.idle_elapsed(), 0u);
    EXPECT_FALSE(trig.is_idle());
    EXPECT_FALSE(anim.is_playing());
}

// --- advance 推進語意：閒置後單次 advance 跨越多個評估間隔 → 多次觸發機會 ---
TEST(IdleAnimationTrigger, AdvanceAfterIdleCanFireMultipleTimesAcrossIntervals) {
    IdleAnimationTrigger trig(1);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, /*eval_interval=*/1, /*probability=*/1.0);

    EXPECT_EQ(trig.advance(1), 1u);  // 達門檻並立即評估觸發一次
    EXPECT_EQ(trig.total_trigger_count(), 1u);

    EXPECT_EQ(trig.advance(3), 3u);  // 已在閒置中：3 tick = 3 個評估間隔 → 3 次觸發機會全中
    EXPECT_EQ(trig.total_trigger_count(), 4u);
}

// --- 閒置門檻設定驗證：0 不套用，不靜默改值 ---
TEST(IdleAnimationTrigger, SetIdleThresholdRejectsZero) {
    IdleAnimationTrigger trig(5);
    EXPECT_EQ(trig.set_idle_threshold(0), IdleAnimationStatus::Invalid);
    EXPECT_EQ(trig.idle_threshold(), 5u);  // 不套用，維持既有門檻

    EXPECT_EQ(trig.set_idle_threshold(10), IdleAnimationStatus::Ok);
    EXPECT_EQ(trig.idle_threshold(), 10u);
}

// --- 決定性：相同種子 + 相同操作序列 → 相同觸發結果，可重現 ---
TEST(IdleAnimationTrigger, DeterministicWithSameInjectedSeed) {
    auto run = [](std::uint64_t seed) {
        IdleAnimationTrigger trig(/*idle_threshold=*/2, seed);
        FrameAnimationElement anim = make_ready_animation();
        trig.register_animation(anim, /*eval_interval=*/1, /*probability=*/0.5);

        std::vector<std::size_t> fires_per_step;
        fires_per_step.push_back(trig.advance(2));   // 達門檻 + 首次評估
        fires_per_step.push_back(trig.advance(5));   // 再 5 次評估機會
        return std::make_pair(fires_per_step, trig.total_trigger_count());
    };

    const auto r1 = run(42);
    const auto r2 = run(42);
    EXPECT_EQ(r1, r2);  // 同種子、同操作序列 → 逐步觸發次數與總數完全一致，可重現
}

// --- 決定性：注入的假亂數來源在未達閒置門檻前完全不被消耗 ---
TEST(IdleAnimationTrigger, RandomSourceNotConsumedBeforeIdleThreshold) {
    auto rng = std::make_shared<ScriptedRandomSource>(std::vector<double>{0.0, 0.0, 0.0});
    IdleAnimationTrigger trig(/*idle_threshold=*/10, rng);
    FrameAnimationElement anim = make_ready_animation();
    trig.register_animation(anim, 1, 1.0);

    EXPECT_EQ(trig.advance(9), 0u);   // 未達門檻：排程器不推進
    EXPECT_EQ(rng->draws(), 0u);      // 因此完全未消耗任何亂數
    EXPECT_EQ(trig.advance(1), 1u);   // 第 10 tick 達門檻並觸發
    EXPECT_EQ(rng->draws(), 1u);      // 恰消耗一次（唯一一次評估）
}
