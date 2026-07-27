// E5-09 機率排程器 — 單元測試（gtest）
//
// 以可注入亂數來源驗證：固定種子下觸發序列決定性可重現、機率分佈合理、
// 每次評估固定抽一個亂數、無效間隔拒絕、機率夾限（0 永不觸發 / 1 每次觸發）、
// 多任務獨立、取消後不再觸發、回呼中取消安全、事件內容正確。全程不依賴真實時鐘或熵。
#include "probabilistic_scheduler.hpp"
#include "random_source.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

using ds::events::ProbabilisticScheduler;
using ds::events::RandomSource;
using ds::events::ScheduledEvent;
using ds::events::SeededRandomSource;
using ds::events::SubscriptionId;

namespace {

// 假亂數來源：回傳預定序列，超出後夾在最後一個值 —— 讓測試對觸發做確定斷言。
class ScriptedRandomSource : public RandomSource {
public:
    explicit ScriptedRandomSource(std::vector<double> values)
        : values_(std::move(values)) {}

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

// 無效間隔（0）不建立任務，回傳無效 id 0。
TEST(ProbabilisticScheduler, RejectsZeroInterval) {
    ProbabilisticScheduler sched(123);
    const SubscriptionId id = sched.schedule(0, 1.0, [](const ScheduledEvent&) {});
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(sched.task_count(), 0u);
}

// 機率 1.0：每次評估機會（每 interval）必觸發。
TEST(ProbabilisticScheduler, ProbabilityOneFiresEveryInterval) {
    ProbabilisticScheduler sched(1);
    int fired = 0;
    sched.schedule(5, 1.0, [&](const ScheduledEvent&) { ++fired; });

    EXPECT_EQ(sched.advance(4), 0u);  // 未達間隔
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(sched.advance(1), 1u);  // 第 5 tick 評估並觸發
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(sched.advance(15), 3u);  // 跨 3 個間隔 → 評估 3 次全觸發
    EXPECT_EQ(fired, 4);
}

// 機率 0.0：永不觸發（但仍每次評估抽一個亂數 —— 消耗序列穩定）。
TEST(ProbabilisticScheduler, ProbabilityZeroNeverFires) {
    auto rng = std::make_shared<ScriptedRandomSource>(std::vector<double>{0.0, 0.0, 0.0});
    ProbabilisticScheduler sched(rng);
    int fired = 0;
    sched.schedule(1, 0.0, [&](const ScheduledEvent&) { ++fired; });

    EXPECT_EQ(sched.advance(3), 0u);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(rng->draws(), 3u);  // 3 次評估 → 3 次抽值，即使機率為 0
}

// 機率門檻：u < p 觸發、u >= p 不觸發。以腳本亂數精確斷言。
TEST(ProbabilisticScheduler, FiresWhenDrawBelowProbability) {
    // p=0.5：序列 0.2(觸發) 0.7(不) 0.5(不，非嚴格小於) 0.49(觸發)。
    auto rng = std::make_shared<ScriptedRandomSource>(
        std::vector<double>{0.2, 0.7, 0.5, 0.49});
    ProbabilisticScheduler sched(rng);
    std::vector<std::uint64_t> fire_counts;
    sched.schedule(1, 0.5, [&](const ScheduledEvent& e) { fire_counts.push_back(e.fire_count); });

    EXPECT_EQ(sched.advance(4), 2u);
    ASSERT_EQ(fire_counts.size(), 2u);
    EXPECT_EQ(fire_counts[0], 1u);  // 第一次觸發
    EXPECT_EQ(fire_counts[1], 2u);  // 第二次觸發（fire_count 連續遞增）
}

// 事件內容正確：id / interval / probability / now / eval_count / fire_count。
TEST(ProbabilisticScheduler, EventCarriesCorrectData) {
    auto rng = std::make_shared<ScriptedRandomSource>(std::vector<double>{0.9, 0.1});
    ProbabilisticScheduler sched(rng);
    std::vector<ScheduledEvent> events;
    const SubscriptionId id =
        sched.schedule(3, 0.5, [&](const ScheduledEvent& e) { events.push_back(e); });

    // 第 3 tick：抽 0.9 不觸發（eval 1）；第 6 tick：抽 0.1 觸發（eval 2, fire 1）。
    sched.advance(6);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].id, id);
    EXPECT_EQ(events[0].interval, 3u);
    EXPECT_DOUBLE_EQ(events[0].probability, 0.5);
    EXPECT_EQ(events[0].now, 6u);
    EXPECT_EQ(events[0].eval_count, 2u);
    EXPECT_EQ(events[0].fire_count, 1u);
}

// 固定種子決定性可重現：兩台相同種子的排程器，同樣推進 → 完全相同的觸發序列。
TEST(ProbabilisticScheduler, DeterministicWithFixedSeed) {
    auto run = [](std::uint64_t seed) {
        ProbabilisticScheduler sched(seed);
        std::vector<std::uint64_t> fires;
        sched.schedule(1, 0.5, [&](const ScheduledEvent& e) { fires.push_back(e.eval_count); });
        sched.advance(200);
        return fires;
    };

    const auto a = run(0xC0FFEE);
    const auto b = run(0xC0FFEE);
    const auto c = run(0xBEEF);

    EXPECT_EQ(a, b);       // 同種子 → 完全相同
    EXPECT_FALSE(a.empty());
    EXPECT_NE(a, c);       // 不同種子 → 序列不同（極高機率）
}

// 機率分佈合理：p=0.5、大量評估 → 觸發比例接近 0.5。
TEST(ProbabilisticScheduler, DistributionIsReasonable) {
    ProbabilisticScheduler sched(42);
    int fired = 0;
    sched.schedule(1, 0.5, [&](const ScheduledEvent&) { ++fired; });

    const int n = 20000;
    sched.advance(static_cast<std::uint64_t>(n));

    const double ratio = static_cast<double>(fired) / n;
    EXPECT_NEAR(ratio, 0.5, 0.03);  // 兩萬次評估容差 3%
}

// 多任務獨立：各自的間隔 / 機率 / 計數互不干擾。
TEST(ProbabilisticScheduler, MultipleTasksIndependent) {
    ProbabilisticScheduler sched(7);
    int fired_a = 0;
    int fired_b = 0;
    sched.schedule(1, 1.0, [&](const ScheduledEvent&) { ++fired_a; });  // 每 tick 必觸發
    sched.schedule(2, 1.0, [&](const ScheduledEvent&) { ++fired_b; });  // 每 2 ticks 觸發

    EXPECT_EQ(sched.task_count(), 2u);
    const std::size_t total = sched.advance(10);
    EXPECT_EQ(fired_a, 10);            // 10 次
    EXPECT_EQ(fired_b, 5);             // 5 次
    EXPECT_EQ(total, 15u);            // advance 回傳跨任務總觸發數
}

// 取消後不再評估 / 觸發。
TEST(ProbabilisticScheduler, CancelStopsFiring) {
    ProbabilisticScheduler sched(1);
    int fired = 0;
    const SubscriptionId id = sched.schedule(1, 1.0, [&](const ScheduledEvent&) { ++fired; });

    EXPECT_EQ(sched.advance(3), 3u);
    EXPECT_EQ(fired, 3);

    EXPECT_TRUE(sched.cancel(id));
    EXPECT_EQ(sched.task_count(), 0u);
    EXPECT_EQ(sched.advance(5), 0u);  // 取消後不再觸發
    EXPECT_EQ(fired, 3);

    EXPECT_FALSE(sched.cancel(id));    // 重複取消未知 id
    EXPECT_FALSE(sched.cancel(999));   // 未知 id
}

// 於回呼中取消自身 / 他者：不破壞本次派發、之後不再觸發。
TEST(ProbabilisticScheduler, CancelInsideCallbackIsSafe) {
    ProbabilisticScheduler sched(1);
    SubscriptionId self = 0;
    int fired = 0;
    self = sched.schedule(1, 1.0, [&](const ScheduledEvent&) {
        ++fired;
        sched.cancel(self);  // 首次觸發即取消自己
    });

    EXPECT_EQ(sched.advance(1), 1u);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(sched.task_count(), 0u);
    EXPECT_EQ(sched.advance(10), 0u);  // 已取消
    EXPECT_EQ(fired, 1);
}

// 機率夾限：越界 / 無效機率被安全夾至 [0,1]。
TEST(ProbabilisticScheduler, ProbabilityClamped) {
    ProbabilisticScheduler sched(5);
    int high = 0;
    int low = 0;
    sched.schedule(1, 5.0, [&](const ScheduledEvent&) { ++high; });   // >1 → 夾成 1，每次觸發
    sched.schedule(1, -3.0, [&](const ScheduledEvent&) { ++low; });   // <0 → 夾成 0，永不觸發

    sched.advance(8);
    EXPECT_EQ(high, 8);
    EXPECT_EQ(low, 0);
}

// 注入 / 替換亂數來源：set_random_source 後改用新序列。
TEST(ProbabilisticScheduler, ReplaceRandomSource) {
    auto never = std::make_shared<ScriptedRandomSource>(std::vector<double>{0.99});
    ProbabilisticScheduler sched(never);
    int fired = 0;
    sched.schedule(1, 0.5, [&](const ScheduledEvent&) { ++fired; });

    EXPECT_EQ(sched.advance(3), 0u);  // 0.99 >= 0.5 從不觸發
    EXPECT_EQ(fired, 0);

    sched.set_random_source(std::make_shared<ScriptedRandomSource>(std::vector<double>{0.01}));
    EXPECT_EQ(sched.advance(2), 2u);  // 0.01 < 0.5 每次觸發
    EXPECT_EQ(fired, 2);
}

// dt=0 不推進、不評估；now() 反映累計推進。
TEST(ProbabilisticScheduler, ZeroAdvanceIsNoOp) {
    ProbabilisticScheduler sched(1);
    int fired = 0;
    sched.schedule(1, 1.0, [&](const ScheduledEvent&) { ++fired; });

    EXPECT_EQ(sched.advance(0), 0u);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(sched.now(), 0u);

    EXPECT_EQ(sched.tick(), 1u);  // tick() == advance(1)
    EXPECT_EQ(sched.now(), 1u);
}

}  // namespace
