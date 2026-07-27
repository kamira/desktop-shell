// E5-09 機率排程器 — 實作
//
// 純邏輯：時間推進委派 E5-04 HeartbeatSource，機率判斷取自可注入的 RandomSource；
// 不含任何平台分支或真實計時器 / 熵源。
#include "probabilistic_scheduler.hpp"

#include <utility>

namespace ds::events {

namespace {
// 夾至 [0,1]；同時把 NaN 夾成 0（!(p>=0) 對 NaN 為真）—— 越界 / 無效輸入永遠安全。
double clamp_probability(double p) noexcept {
    if (!(p >= 0.0)) {
        return 0.0;
    }
    if (p > 1.0) {
        return 1.0;
    }
    return p;
}
}  // namespace

SubscriptionId ProbabilisticScheduler::schedule(Tick interval, double probability,
                                                ScheduledCallback cb) {
    if (interval == 0) {
        return 0;  // 無效間隔：不建立
    }
    // 掛一個心跳訂閱作為「評估機會」來源；回呼只轉呼 evaluate，任務 id 即心跳訂閱 id。
    const SubscriptionId id = heartbeat_.subscribe(
        interval, [this](const HeartbeatEvent& ev) { this->evaluate(ev.id, ev.now); });
    // interval > 0 時 subscribe 必配發有效 id。
    tasks_.emplace(id, Task{interval, clamp_probability(probability), /*eval_count=*/0,
                            /*fire_count=*/0, std::move(cb)});
    return id;
}

bool ProbabilisticScheduler::cancel(SubscriptionId id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return false;
    }
    tasks_.erase(it);
    heartbeat_.unsubscribe(id);
    return true;
}

std::size_t ProbabilisticScheduler::advance(Tick dt) {
    if (dt == 0) {
        return 0;
    }
    fires_this_advance_ = 0;
    // 心跳的兩階段派發保證：於回呼中 schedule / cancel 不會破壞本次遍歷。
    heartbeat_.advance(dt);
    return fires_this_advance_;
}

void ProbabilisticScheduler::evaluate(SubscriptionId id, Tick now) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) {
        return;  // 任務已於本次派發前被取消（心跳 pending 殘留）—— 防禦性略過。
    }
    Task& t = it->second;
    ++t.eval_count;

    // 每次評估固定抽一個亂數 —— 亂數消耗次序穩定，固定種子下決定性可重現。
    const double u = rng_->next_unit();
    if (u < t.probability) {
        ++t.fire_count;
        const ScheduledEvent ev{id, t.interval, t.probability, now, t.eval_count, t.fire_count};
        // 先複製回呼再呼叫：使用者回呼可能 cancel 本任務而使 it / t 失效。
        ScheduledCallback cb = t.cb;
        ++fires_this_advance_;
        if (cb) {
            cb(ev);
        }
    }
}

}  // namespace ds::events
