// W1-08 其一：原始讀數 → ProcessSample 的純邏輯層（實作）
//
// 本檔不含任何 win32 呼叫，見 process_policy.hpp 的檔頭說明。
#include "process_policy.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace ds::sysinfo::win32 {

const char* to_string(SampleOutcome outcome) noexcept {
    switch (outcome) {
        case SampleOutcome::Ok:
            return "ok";
        case SampleOutcome::BaselineOnly:
            return "baseline_only";
        case SampleOutcome::EnumerationFailed:
            return "enumeration_failed";
        case SampleOutcome::NoTimeElapsed:
            return "no_time_elapsed";
        case SampleOutcome::InvalidCpuCount:
            return "invalid_cpu_count";
        case SampleOutcome::NoUsableEntries:
            return "no_usable_entries";
    }
    return "unknown";
}

double cpu_percent_from_delta(Ticks100ns busy_delta, Ticks100ns wall_delta,
                              std::uint32_t cpus) noexcept {
    // 守門：這兩個條件由呼叫端在更前面就擋掉並回報具名原因。走到這裡表示呼叫端漏判，
    // 故回一個**不可能是合法 CPU% 的值**（-1.0），而不是 0.0 —— 0.0 是合法的 CPU%，
    // 會讓「算不出來」與「很閒」無從分辨（K-002）。
    if (wall_delta == 0 || cpus == 0) {
        return -1.0;
    }

    const double capacity = static_cast<double>(wall_delta) * static_cast<double>(cpus);
    double percent = 100.0 * static_cast<double>(busy_delta) / capacity;

    // clamp 到 E2-19 契約宣告的值域 [0,100]。量測誤差（行程時間與取樣時刻來自不同粒度的
    // 時鐘）讓滿載行程算出 100.4% 是正常的，不是故障；真正的故障在上面已被擋掉。
    if (percent < 0.0) {
        percent = 0.0;
    } else if (percent > 100.0) {
        percent = 100.0;
    }
    return percent;
}

ProcessStatus status_from_raw(const RawProcessEntry& entry) noexcept {
    switch (entry.liveness) {
        case RawLiveness::Running:
            return ProcessStatus::Running;
        case RawLiveness::Exited:
            return ProcessStatus::Zombie;
        case RawLiveness::Unknown:
            break;
    }
    return ProcessStatus::Unknown;
}

void ProcessDeltaTracker::reset() noexcept {
    baseline_.clear();
    prev_wall_100ns_ = 0;
    has_baseline_ = false;
    last_outcome_ = SampleOutcome::BaselineOnly;
    last_error_.clear();
    skipped_last_ = 0;
}

ProcessSample ProcessDeltaTracker::fail(SampleOutcome outcome, std::string reason) {
    last_outcome_ = outcome;
    last_error_ = std::move(reason);
    // 一律回明確的「無讀值」。空的 processes + valid==false ——
    // 消費者（E2-19 的 ProcessMonitorProvider）據此顯示為未知，而不是「沒有任何行程」。
    return ProcessSample::unknown();
}

ProcessSample ProcessDeltaTracker::accept(const RawSnapshot& raw) {
    // win32 層已跳過的筆數先記下；本層可能再追加（首見 / pid 重用）。
    skipped_last_ = raw.skipped;

    // 列舉失敗 ≠ 沒有行程。**保留既有基準點**：下一輪仍能差分，只是區間較長——
    // 那仍是一個真實區間的平均值，不是假值。
    if (!raw.enumerated) {
        return fail(SampleOutcome::EnumerationFailed,
                    "行程列舉失敗（EnumProcesses 回 FALSE）：無讀值，不等於沒有行程");
    }

    // 處理器數是 CPU% 的除數之一；讀不到就算不出使用率。
    if (raw.processor_count == 0) {
        return fail(SampleOutcome::InvalidCpuCount,
                    "邏輯處理器數讀不到（0）：CPU% 的除數不存在，無讀值");
    }

    // 列舉成功但一筆都讀不到。機器上至少有「我們自己」這個行程，故 0 筆必然是讀取側壞了
    // （權限不足 / 全數開不起來），不是「沒有任何行程」。
    if (raw.entries.empty()) {
        return fail(SampleOutcome::NoUsableEntries,
                    "列舉回報 " + std::to_string(raw.listed_pids) +
                        " 個 pid，但五欄位齊全者 0 筆（跳過 " +
                        std::to_string(raw.skipped) + " 筆）：無讀值");
    }

    // 本次的新基準點（僅含本次出現的 pid；已結束的行程自動從基準點消失，
    // 否則 pid 被重用時會拿到另一個行程的累計時間去差分）。
    std::unordered_map<std::uint64_t, Baseline> next;
    next.reserve(raw.entries.size());
    for (const auto& e : raw.entries) {
        next[e.pid] = Baseline{e.busy_100ns, e.created_100ns};
    }

    // 首次取樣：只建立基準點。CPU% 是差分式的，單一快照無從得知使用率——
    // K-002 的解法原文即「首次呼叫只建立基準點並回（不可能是合法值的）失敗值」。
    if (!has_baseline_) {
        baseline_ = std::move(next);
        prev_wall_100ns_ = raw.wall_100ns;
        has_baseline_ = true;
        skipped_last_ = raw.skipped + raw.entries.size();
        return fail(SampleOutcome::BaselineOnly,
                    "首次取樣：已建立 " + std::to_string(baseline_.size()) +
                        " 個行程的基準點，尚無可差分的區間 → 無讀值");
    }

    // 兩次取樣之間必須真的經過時間（K-002 第 1 層：差分恆為 0 的那個陷阱）。
    // 時間差為 0 時**不推進基準點**，讓下一輪拿到一個較長但合法的區間。
    const Ticks100ns wall_delta =
        raw.wall_100ns > prev_wall_100ns_ ? raw.wall_100ns - prev_wall_100ns_ : 0;
    if (wall_delta == 0) {
        return fail(SampleOutcome::NoTimeElapsed,
                    "兩次取樣之間時間差為 0：差分式 CPU% 無從計算 → 無讀值"
                    "（基準點保留，下一輪以較長區間差分）");
    }

    ProcessSample out;
    out.processes.reserve(raw.entries.size());
    std::size_t deferred = 0;

    for (const auto& e : raw.entries) {
        const auto it = baseline_.find(e.pid);

        // 本次首見 → 無基準點。pid 相同但行程建立時間不同 → **pid 被重用**，
        // 基準點屬於另一個行程，拿它去差分會算出憑空的使用率。
        // 兩者都是「本輪跳過、下一輪才回報」：拿「行程開始至今的平均」充數會印出一個
        // 看似合理的數字，比回 0 更難察覺（K-002 附記）。
        if (it == baseline_.end() || it->second.created_100ns != e.created_100ns) {
            ++deferred;
            continue;
        }

        // 同一個行程的累計 CPU 時間只會單調增加。倒退表示讀數不可信（而非「沒用到 CPU」），
        // 故跳過該筆而不回報 0.0。
        if (e.busy_100ns < it->second.busy_100ns) {
            ++deferred;
            continue;
        }

        const Ticks100ns busy_delta = e.busy_100ns - it->second.busy_100ns;
        const double cpu = cpu_percent_from_delta(busy_delta, wall_delta, raw.processor_count);
        if (cpu < 0.0) {
            // 守門已擋掉 wall_delta==0 / cpus==0，理論上不可達；仍不回報猜測值。
            ++deferred;
            continue;
        }

        ProcessInfo info;
        info.pid = e.pid;
        info.name = e.name;
        info.cpu_percent = cpu;
        info.memory_bytes = e.memory_bytes;
        info.status = status_from_raw(e);
        out.processes.push_back(std::move(info));
    }

    baseline_ = std::move(next);
    prev_wall_100ns_ = raw.wall_100ns;
    skipped_last_ = raw.skipped + deferred;

    // 全數延後（例如整張行程表在兩次取樣之間完全換過）→ 本輪沒有任何真實量測。
    if (out.processes.empty()) {
        return fail(SampleOutcome::NoUsableEntries,
                    "列舉回報 " + std::to_string(raw.listed_pids) + " 個 pid、讀到 " +
                        std::to_string(raw.entries.size()) +
                        " 筆，但全數無可用基準點（首見 / pid 重用 / 讀數倒退）→ 無讀值");
    }

    out.valid = true;
    last_outcome_ = SampleOutcome::Ok;
    last_error_.clear();
    return out;
}

}  // namespace ds::sysinfo::win32
