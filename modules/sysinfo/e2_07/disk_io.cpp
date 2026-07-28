// E2-07 儲存 IO 吞吐與佇列 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：兩次取樣差分算吞吐率 / IOPS（佇列深度為瞬時值直接帶出）、以 E2-01 記憶體內實作
// 把每顆磁碟建成可列舉實例、掛上五個指標，並支援 sample() 重新讀取更新（速率推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實 IO API（無 IOKit / /proc/diskstats / mach）——換平台一行
// 不動。
#include "disk_io.hpp"

#include <algorithm>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 差分演算法（自由函式，獨立可測）
// ---------------------------------------------------------------------------
double counter_rate(std::uint64_t prev, std::uint64_t curr, double dt) noexcept {
    if (dt <= 0.0) {
        return 0.0;  // 無經過時間 / 時間戳未推進，無從判斷速率
    }
    if (curr < prev) {
        return 0.0;  // 累積值理應單調；回繞 / 重置 → 保守回 0
    }
    return static_cast<double>(curr - prev) / dt;
}

DiskIoRates rates_from_delta(const DiskIoCounters& prev, const DiskIoCounters& curr,
                             double dt) noexcept {
    DiskIoRates r;
    r.read_bps = counter_rate(prev.read_bytes, curr.read_bytes, dt);
    r.write_bps = counter_rate(prev.write_bytes, curr.write_bytes, dt);
    r.read_iops = counter_rate(prev.read_ops, curr.read_ops, dt);
    r.write_iops = counter_rate(prev.write_ops, curr.write_ops, dt);
    r.queue_depth = curr.queue_depth;  // 瞬時值，直接取當前快照（不差分）
    return r;
}

DiskIoUsageSample usage_from_delta(const DiskIoSample& prev, const DiskIoSample& curr) {
    DiskIoUsageSample out;
    // 對齊到兩份的共同磁碟數（較小者）——磁碟數可在取樣間變動。
    const std::size_t n = std::min(prev.disk_count(), curr.disk_count());
    if (n == 0) {
        return out;  // 無可差分的磁碟 → 無讀值（valid==false）
    }
    const double dt = curr.timestamp - prev.timestamp;
    out.disks.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        DiskIoReading rd;
        // 身分取自當前快照（列舉輸出以當前狀態為準）。
        rd.id = curr.disks[i].id;
        rd.label = curr.disks[i].label;
        rd.rates = rates_from_delta(prev.disks[i].counters, curr.disks[i].counters, dt);
        out.disks.push_back(std::move(rd));
    }
    out.valid = true;  // 有 >=1 顆共同磁碟可差分
    return out;
}

// ---------------------------------------------------------------------------
// NullIoStatSource
// ---------------------------------------------------------------------------
DiskIoSample NullIoStatSource::read() {
    if (sequence_.empty()) {
        return DiskIoSample{};  // 空列 → 空快照
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// DifferencingIoRateSource
// ---------------------------------------------------------------------------
DiskIoUsageSample DifferencingIoRateSource::sample() {
    if (!stats_) {
        return DiskIoUsageSample::unknown();  // 無累積計數來源 → 無讀值
    }
    DiskIoSample curr = stats_->read();
    if (!primed_) {
        // 首次取樣：只有一份，尚無可差分的基準 → 無讀值。保存為基準。
        prev_ = std::move(curr);
        primed_ = true;
        return DiskIoUsageSample::unknown();
    }
    DiskIoUsageSample usage = usage_from_delta(prev_, curr);
    prev_ = std::move(curr);  // 推進基準供下次差分
    return usage;
}

// ---------------------------------------------------------------------------
// DiskIoProvider
// ---------------------------------------------------------------------------
void DiskIoProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::InMemoryMetric;
    using ds::metrics::MetricRange;

    // 沿用 E2-01 的記憶體內實作，不自造指標模型。吞吐率 / IOPS / 佇列深度皆 >= 0、上無界
    // （at_least(0)）——不同儲存裝置速率跨度極大，不強設上限。
    read_bytes_metric_ = std::make_shared<InMemoryMetric>(
        kReadBytesId, kReadBytesName, kBytesUnit, MetricRange::at_least(0.0));
    write_bytes_metric_ = std::make_shared<InMemoryMetric>(
        kWriteBytesId, kWriteBytesName, kBytesUnit, MetricRange::at_least(0.0));
    read_iops_metric_ = std::make_shared<InMemoryMetric>(
        kReadIopsId, kReadIopsName, kIopsUnit, MetricRange::at_least(0.0));
    write_iops_metric_ = std::make_shared<InMemoryMetric>(
        kWriteIopsId, kWriteIopsName, kIopsUnit, MetricRange::at_least(0.0));
    queue_metric_ = std::make_shared<InMemoryMetric>(
        kQueueId, kQueueName, kQueueUnit, MetricRange::at_least(0.0));

    // 以目前一份速率填初值，並依其磁碟建立每磁碟實例（初建亦把有效值推入歷史，與後續採集
    // 路徑一致）。差分型來源首次回 unknown（無磁碟），故此時通常尚無實例——磁碟於下一次
    // sample() 差分成功後才populated（誠實：差分至少需兩份）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(read_bytes_metric_);
    registry.register_metric(write_bytes_metric_);
    registry.register_metric(read_iops_metric_);
    registry.register_metric(write_iops_metric_);
    registry.register_metric(queue_metric_);
}

void DiskIoProvider::sample() {
    if (!read_bytes_metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

std::size_t DiskIoProvider::ensure_slot(const std::string& id, const std::string& label) {
    auto it = slot_index_.find(id);
    if (it != slot_index_.end()) {
        return it->second;
    }
    // 新磁碟：於五個指標各新增一個同 id/label 的實例（unique_ptr 持有，既有參照不失效）。
    DiskSlot slot;
    slot.id = id;
    slot.read_bytes = &read_bytes_metric_->add_instance(id, label, history_);
    slot.write_bytes = &write_bytes_metric_->add_instance(id, label, history_);
    slot.read_iops = &read_iops_metric_->add_instance(id, label, history_);
    slot.write_iops = &write_iops_metric_->add_instance(id, label, history_);
    slot.queue = &queue_metric_->add_instance(id, label, history_);
    const std::size_t idx = slots_.size();
    slots_.push_back(slot);
    slot_index_.emplace(id, idx);
    return idx;
}

void DiskIoProvider::write_value(ds::metrics::InMemoryMetricInstance* inst, double value,
                                 bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = MetricValue::of(value);
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void DiskIoProvider::apply(const DiskIoUsageSample& usage, bool to_history) {
    // 記錄本次出現的磁碟槽，未出現的既有磁碟稍後設未知。
    std::vector<bool> seen(slots_.size(), false);

    if (usage.valid) {
        for (const DiskIoReading& rd : usage.disks) {
            const std::size_t idx = ensure_slot(rd.id, rd.label);
            if (idx >= seen.size()) seen.resize(idx + 1, false);  // 新槽
            seen[idx] = true;
            const DiskSlot& slot = slots_[idx];
            write_value(slot.read_bytes, rd.rates.read_bps, /*valid=*/true, to_history);
            write_value(slot.write_bytes, rd.rates.write_bps, /*valid=*/true, to_history);
            write_value(slot.read_iops, rd.rates.read_iops, /*valid=*/true, to_history);
            write_value(slot.write_iops, rd.rates.write_iops, /*valid=*/true, to_history);
            write_value(slot.queue, rd.rates.queue_depth, /*valid=*/true, to_history);
        }
    }

    // 本次未出現的既有磁碟（下線 / 無讀值 / usage 無效）→ 五個實例設未知、不推歷史。
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (i < seen.size() && seen[i]) continue;
        const DiskSlot& slot = slots_[i];
        write_value(slot.read_bytes, 0.0, /*valid=*/false, to_history);
        write_value(slot.write_bytes, 0.0, /*valid=*/false, to_history);
        write_value(slot.read_iops, 0.0, /*valid=*/false, to_history);
        write_value(slot.write_iops, 0.0, /*valid=*/false, to_history);
        write_value(slot.queue, 0.0, /*valid=*/false, to_history);
    }
}

}  // namespace ds::sysinfo
