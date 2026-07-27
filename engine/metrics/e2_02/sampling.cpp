// E2-02 採集頻率分級與除頻 — 實作（engine 層 / 平台中立純邏輯）
//
// 無 `#ifdef`、無系統呼叫、無真實時鐘：一切以邏輯 tick 表達，可完全單元測試。
#include "sampling.hpp"

#include <algorithm>

namespace ds::metrics {

// ---------------------------------------------------------------------------
// SamplingTier 自由函式
// ---------------------------------------------------------------------------
int tier_rank(SamplingTier tier) noexcept {
    switch (tier) {
        case SamplingTier::High:     return 3;
        case SamplingTier::Normal:   return 2;
        case SamplingTier::Low:      return 1;
        case SamplingTier::OnDemand: return 0;
    }
    return 0;  // 不可達；防禦性
}

SamplingTier higher_tier(SamplingTier a, SamplingTier b) noexcept {
    return tier_rank(b) > tier_rank(a) ? b : a;  // 位階相同回 a
}

const char* to_string(SamplingTier tier) noexcept {
    switch (tier) {
        case SamplingTier::High:     return "high";
        case SamplingTier::Normal:   return "normal";
        case SamplingTier::Low:      return "low";
        case SamplingTier::OnDemand: return "on-demand";
    }
    return "unknown";  // 不可達
}

// ---------------------------------------------------------------------------
// SamplingPolicy
// ---------------------------------------------------------------------------
SamplingPolicy::SamplingPolicy() = default;

SamplingPolicy SamplingPolicy::defaults() { return SamplingPolicy{}; }

std::optional<Tick> SamplingPolicy::interval(SamplingTier tier) const noexcept {
    switch (tier) {
        case SamplingTier::High:     return high_;
        case SamplingTier::Normal:   return normal_;
        case SamplingTier::Low:      return low_;
        case SamplingTier::OnDemand: return std::nullopt;  // 非週期
    }
    return std::nullopt;  // 不可達
}

SamplingPolicy& SamplingPolicy::set_interval(SamplingTier tier, Tick ticks) noexcept {
    const Tick t = ticks == 0 ? 1 : ticks;  // 間隔 0 無意義，夾到 1
    switch (tier) {
        case SamplingTier::High:     high_ = t;   break;
        case SamplingTier::Normal:   normal_ = t; break;
        case SamplingTier::Low:      low_ = t;    break;
        case SamplingTier::OnDemand: break;       // 恆非週期，忽略
    }
    return *this;
}

// ---------------------------------------------------------------------------
// SamplingScheduler
// ---------------------------------------------------------------------------
SamplingScheduler::SamplingScheduler(SamplingPolicy policy) : policy_(policy) {}

std::optional<SamplingTier> SamplingScheduler::effective_tier_of(const MetricState& st) const {
    if (st.demands.empty()) return std::nullopt;
    SamplingTier best = st.demands.front().second;
    for (const auto& d : st.demands) best = higher_tier(best, d.second);
    return best;  // 除頻：所有需求中最高頻者
}

std::optional<Tick> SamplingScheduler::interval_of(const MetricState& st) const {
    const std::optional<SamplingTier> tier = effective_tier_of(st);
    if (!tier) return std::nullopt;
    return policy_.interval(*tier);  // OnDemand → nullopt（非週期）
}

DemandId SamplingScheduler::add_demand(const MetricId& id, SamplingTier tier) {
    const DemandId did = next_demand_id_++;
    auto it = metrics_.find(id);
    if (it == metrics_.end()) {
        // 新指標：首採排在「下一次 advance 到達目前 tick」時。
        MetricState st;
        st.order = next_order_++;
        st.demands.emplace_back(did, tier);
        st.next_due = now_;
        metrics_.emplace(id, std::move(st));
    } else {
        MetricState& st = it->second;
        st.demands.emplace_back(did, tier);
        // 若新需求把有效頻率拉高（間隔變短），相應提前下次採集時機。
        const std::optional<Tick> iv = interval_of(st);
        if (iv.has_value()) {
            const Tick candidate = now_ + *iv;
            if (candidate < st.next_due) st.next_due = candidate;
        }
    }
    demand_index_.emplace(did, id);
    return did;
}

bool SamplingScheduler::remove_demand(DemandId demand) {
    auto di = demand_index_.find(demand);
    if (di == demand_index_.end()) return false;
    const MetricId id = di->second;
    demand_index_.erase(di);

    auto it = metrics_.find(id);
    if (it != metrics_.end()) {
        auto& demands = it->second.demands;
        demands.erase(std::remove_if(demands.begin(), demands.end(),
                                     [demand](const std::pair<DemandId, SamplingTier>& d) {
                                         return d.first == demand;
                                     }),
                      demands.end());
        if (demands.empty()) metrics_.erase(it);  // 最後一筆撤銷 → 停止追蹤
        // 否則：有效頻率可能下降；不動 next_due，下次採集後即以新間隔續行。
    }
    return true;
}

std::optional<SamplingTier> SamplingScheduler::effective_tier(const MetricId& id) const {
    auto it = metrics_.find(id);
    if (it == metrics_.end()) return std::nullopt;
    return effective_tier_of(it->second);
}

std::optional<Tick> SamplingScheduler::effective_interval(const MetricId& id) const {
    auto it = metrics_.find(id);
    if (it == metrics_.end()) return std::nullopt;
    return interval_of(it->second);
}

std::size_t SamplingScheduler::demand_count(const MetricId& id) const {
    auto it = metrics_.find(id);
    return it == metrics_.end() ? 0 : it->second.demands.size();
}

bool SamplingScheduler::tracks(const MetricId& id) const {
    return metrics_.find(id) != metrics_.end();
}

std::optional<Tick> SamplingScheduler::next_due(const MetricId& id) const {
    auto it = metrics_.find(id);
    if (it == metrics_.end()) return std::nullopt;
    if (!interval_of(it->second).has_value()) return std::nullopt;  // 非週期
    return it->second.next_due;
}

void SamplingScheduler::request_now(const MetricId& id) {
    auto it = metrics_.find(id);
    if (it == metrics_.end()) return;  // 未追蹤：no-op
    it->second.pending_now = true;
}

std::vector<MetricId> SamplingScheduler::advance(Tick now) {
    std::vector<MetricId> due;
    if (now < now_) return due;  // 單調時間；倒退不推進

    // 收集此刻到期者：pending_now（強制）或 週期指標 next_due <= now。
    std::vector<std::pair<std::size_t, MetricId>> ordered;  // (order, id) 供決定性排序
    for (auto& kv : metrics_) {
        MetricState& st = kv.second;
        const std::optional<Tick> iv = interval_of(st);
        const bool periodic_due = iv.has_value() && st.next_due <= now;
        if (st.pending_now || periodic_due) {
            ordered.emplace_back(st.order, kv.first);
        }
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const std::pair<std::size_t, MetricId>& a,
                 const std::pair<std::size_t, MetricId>& b) { return a.first < b.first; });

    for (auto& o : ordered) {
        MetricState& st = metrics_[o.second];
        st.pending_now = false;
        const std::optional<Tick> iv = interval_of(st);
        if (iv.has_value()) {
            // 下次時機 = now + 間隔。跨越多個間隔只採一份（不補採、不爆量）。
            st.next_due = now + *iv;
        }
        due.push_back(o.second);
    }

    now_ = now;
    return due;
}

}  // namespace ds::metrics
