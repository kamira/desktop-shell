// E2-06 儲存容量 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：讀各磁碟容量快照、以使用率 = used/total（夾 [0,1]）算使用率、用 E2-01 記憶體內
// 實作把各磁碟建成四個平行指標的可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新
// （容量推入歷史）。無 `#ifdef`、無系統呼叫、無真實磁碟 API（無 statvfs / GetDiskFreeSpace /
// mach）——換平台一行不動。
#include "storage_capacity.hpp"

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 使用率演算法（自由函式，獨立可測）
// ---------------------------------------------------------------------------
double disk_usage_ratio(std::uint64_t used, std::uint64_t total) noexcept {
    if (total == 0) {
        return 0.0;  // 無容量 / 尚未讀到 → 無從判斷，不謊報
    }
    if (used > total) {
        return 1.0;  // 保留區塊等造成 used>total（理論不該發生）→ 夾到滿載
    }
    return static_cast<double>(used) / static_cast<double>(total);
}

double DiskCapacity::usage_ratio() const noexcept {
    return disk_usage_ratio(used_bytes, total_bytes);
}

// ---------------------------------------------------------------------------
// StorageCapacityProvider
// ---------------------------------------------------------------------------
void StorageCapacityProvider::make_slot(MetricSlot& slot, ds::metrics::MetricId id,
                                        std::string name, std::string unit,
                                        ds::metrics::MetricRange range) {
    slot.metric = std::make_shared<ds::metrics::InMemoryMetric>(
        std::move(id), std::move(name), std::move(unit), range);
    slot.insts.clear();
}

void StorageCapacityProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。使用率 %：有界 [0,100]（可正規化到 [0,1]
    // 供長條 / 直方圖）；三個容量欄位：單位 "B"、下界 0、上無界。
    make_slot(usage_, kMetricUsage, kNameUsage, kUnitPercent,
              ds::metrics::MetricRange::bounded(0.0, 100.0));
    make_slot(total_, kMetricTotal, kNameTotal, kUnitBytes,
              ds::metrics::MetricRange::at_least(0.0));
    make_slot(used_, kMetricUsed, kNameUsed, kUnitBytes,
              ds::metrics::MetricRange::at_least(0.0));
    make_slot(free_, kMetricFree, kNameFree, kUnitBytes,
              ds::metrics::MetricRange::at_least(0.0));

    // 以目前一份容量快照填初值，並依其磁碟數建立各磁碟實例（初建亦把有效值推入歷史，
    // 與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(usage_.metric);
    registry.register_metric(total_.metric);
    registry.register_metric(used_.metric);
    registry.register_metric(free_.metric);
}

void StorageCapacityProvider::sample() {
    if (!usage_.metric) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void StorageCapacityProvider::write_value(ds::metrics::InMemoryMetricInstance* inst,
                                          double number, bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = MetricValue::of(number);
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void StorageCapacityProvider::apply(const std::vector<DiskCapacity>& disks, bool to_history) {
    // 必要時動態擴增磁碟實例（新取樣磁碟數多於既有）——四槽同步新增同一 instance_id / label，
    // unique_ptr 持有，既有參照不失效。以列舉順序（索引）對齊。
    for (std::size_t i = disk_ids_.size(); i < disks.size(); ++i) {
        const DiskCapacity& d = disks[i];
        disk_ids_.push_back(d.id);
        usage_.insts.push_back(&usage_.metric->add_instance(d.id, d.name, history_));
        total_.insts.push_back(&total_.metric->add_instance(d.id, d.name, history_));
        used_.insts.push_back(&used_.metric->add_instance(d.id, d.name, history_));
        free_.insts.push_back(&free_.metric->add_instance(d.id, d.name, history_));
    }

    // 逐磁碟寫值（四槽平行）。
    for (std::size_t i = 0; i < disk_ids_.size(); ++i) {
        if (i < disks.size()) {
            const DiskCapacity& d = disks[i];
            const bool ok = d.valid;
            write_value(usage_.insts[i], d.usage_ratio() * kPercentScale, ok, to_history);
            write_value(total_.insts[i], static_cast<double>(d.total_bytes), ok, to_history);
            write_value(used_.insts[i], static_cast<double>(d.used_bytes), ok, to_history);
            write_value(free_.insts[i], static_cast<double>(d.free_bytes), ok, to_history);
        } else {
            // 本次取樣少了這顆磁碟（卸載 / 無讀值）→ 四槽皆設未知、不推歷史。
            write_value(usage_.insts[i], 0.0, /*valid=*/false, to_history);
            write_value(total_.insts[i], 0.0, /*valid=*/false, to_history);
            write_value(used_.insts[i], 0.0, /*valid=*/false, to_history);
            write_value(free_.insts[i], 0.0, /*valid=*/false, to_history);
        }
    }
}

}  // namespace ds::sysinfo
