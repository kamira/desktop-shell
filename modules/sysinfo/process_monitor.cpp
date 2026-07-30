// E2-19 行程監看 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：讀來源行程列表、依 CPU / 記憶體排序取 top-N、以 E2-01 記憶體內實作把每個行程
// 建成可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新（排序維度值推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實行程 API（無 sysctl / /proc / EnumProcesses）
// ——換平台一行不動。
#include "process_monitor.hpp"

#include <algorithm>
#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// ProcessStatus
// ---------------------------------------------------------------------------
const char* to_string(ProcessStatus status) noexcept {
    switch (status) {
        case ProcessStatus::Running:
            return "running";
        case ProcessStatus::Sleeping:
            return "sleeping";
        case ProcessStatus::Stopped:
            return "stopped";
        case ProcessStatus::Zombie:
            return "zombie";
        case ProcessStatus::Unknown:
            return "unknown";
    }
    return "unknown";  // 不可達；防禦編譯器對 enum 完整性的保守假設
}

// ---------------------------------------------------------------------------
// 排序 / top-N（自由函式）
// ---------------------------------------------------------------------------
double process_sort_value(const ProcessInfo& p, ProcessSortKey key) noexcept {
    switch (key) {
        case ProcessSortKey::Cpu:
            return p.cpu_percent;
        case ProcessSortKey::Memory:
            return static_cast<double>(p.memory_bytes);
    }
    return 0.0;  // 不可達
}

std::vector<ProcessInfo> top_processes(const std::vector<ProcessInfo>& procs,
                                       ProcessSortKey key, std::size_t top_n) {
    std::vector<ProcessInfo> out = procs;
    // 依排序維度由高到低；同值以 pid 遞增為次序（決定性穩定輸出）。
    std::sort(out.begin(), out.end(),
              [key](const ProcessInfo& a, const ProcessInfo& b) {
                  const double va = process_sort_value(a, key);
                  const double vb = process_sort_value(b, key);
                  if (va != vb) return va > vb;  // 高值在前
                  return a.pid < b.pid;          // 平手：pid 小在前
              });
    // top_n==0 → 全部；否則截斷到前 N。
    if (top_n != ProcessMonitorProvider::kAllProcesses && out.size() > top_n) {
        out.resize(top_n);
    }
    return out;
}

// ---------------------------------------------------------------------------
// NullProcessSource
// ---------------------------------------------------------------------------
ProcessSample NullProcessSource::sample() {
    if (sequence_.empty()) {
        return ProcessSample::unknown();  // 空序列 → 無讀值（null 期誠實預設）
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    ProcessSample s;
    s.processes = sequence_[idx];
    s.valid = true;
    return s;
}

// ---------------------------------------------------------------------------
// ProcessMonitorProvider
// ---------------------------------------------------------------------------
double ProcessMonitorProvider::exposed_value(const ProcessInfo& p) const noexcept {
    switch (sort_key_) {
        case ProcessSortKey::Cpu:
            return p.cpu_percent;  // 已是 %
        case ProcessSortKey::Memory:
            return static_cast<double>(p.memory_bytes) / kBytesPerMiB;  // bytes → MB
    }
    return 0.0;  // 不可達
}

void ProcessMonitorProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。指標身分 / 單位 / 範圍依排序維度：
    //   Cpu    → "proc.cpu"、"%"、bounded[0,100]（可正規化供長條 / 直方圖）。
    //   Memory → "proc.memory"、"MB"、at_least(0)（下界 0、上無界）。
    if (sort_key_ == ProcessSortKey::Cpu) {
        metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
            kCpuMetricId, kCpuMetricName, kCpuUnit,
            ds::metrics::MetricRange::bounded(0.0, 100.0));
    } else {
        metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
            kMemoryMetricId, kMemoryMetricName, kMemoryUnit,
            ds::metrics::MetricRange::at_least(0.0));
    }

    // 以目前一份行程列表填初值、依 top-N 建立實例（初建亦把有效值推入歷史，與後續採集
    // 路徑一致）。source 為 null / 無讀值 → 無實例（保守而不崩）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void ProcessMonitorProvider::sample() {
    if (!metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void ProcessMonitorProvider::apply(const ProcessSample& sample, bool to_history) {
    using ds::metrics::MetricValue;

    has_reading_ = sample.valid;

    if (!sample.valid) {
        // 無讀值：所有既有實例設為未知且**不**推歷史（不污染序列），誠實不謊報。
        last_top_.clear();
        for (std::uint64_t pid : order_) {
            inst_by_pid_[pid]->set_value(MetricValue::unknown());
        }
        return;
    }

    // 排序取 top-N（決定性）；快取供 processes() 消費者取完整欄位。
    last_top_ = top_processes(sample.processes, sort_key_, top_n_);

    // 記錄本次出現的 pid，供稍後把缺席的既有實例設為未知。
    // （用小 vector 線性查找即可——top-N 通常不大。）
    std::vector<std::uint64_t> present;
    present.reserve(last_top_.size());

    for (const ProcessInfo& p : last_top_) {
        present.push_back(p.pid);
        const MetricValue v =
            MetricValue::of(exposed_value(p), std::string(to_string(p.status)));

        auto it = inst_by_pid_.find(p.pid);
        if (it == inst_by_pid_.end()) {
            // 新出現的 pid → 動態新增實例（unique_ptr 持有，既有參照不失效）。
            //   instance_id = pid、label = 行程名。
            auto& inst = metric_->add_instance(std::to_string(p.pid), p.name, history_);
            inst_by_pid_.emplace(p.pid, &inst);
            order_.push_back(p.pid);
            if (to_history) {
                inst.update(v);  // 推入歷史（update 於 valid 值才推）
            } else {
                inst.set_value(v);
            }
        } else {
            if (to_history) {
                it->second->update(v);
            } else {
                it->second->set_value(v);
            }
        }
    }

    // 本次未出現的既有 pid（掉出 top-N / 已結束）→ 設未知、不推歷史（誠實表達缺席）。
    for (std::uint64_t pid : order_) {
        if (std::find(present.begin(), present.end(), pid) == present.end()) {
            inst_by_pid_[pid]->set_value(MetricValue::unknown());
        }
    }
}

}  // namespace ds::sysinfo
