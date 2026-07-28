// E2-20 網路連通性 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「網路連通性」透過 **E2-01 的 MetricProvider 介面** 掛成一組指標——
//   - **是否連上網際網路**（online，bool）
//   - **延遲 / ping**（latency，ms）
//   - **封包遺失率**（loss，%）
//   - **對特定目標的可達性**（reachable，bool，支援多目標）
//   - **DNS 是否可解析**（dns，bool）
// 連通性隨時間變動，故以 **E2-02 的採集頻率分級** 決定採樣節奏。這是「新增指標 = 新增
// MetricProvider、掛件一行不動」機制的又一個具體提供者——它**消費 E2-01 / E2-02 契約、
// 不自造指標模型或排程器**（範式與 E2-08 網路流量、E2-03 CPU 負載一致）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實網路探測**（無 socket / ping
//     / `getaddrinfo` / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作抽象來源，
//     提供者一行不動。
//   - **誠實 invalid**：連通性各面向可各自無讀值（如尚未探測到延遲，但可達性已知）。故
//     每個面向各帶自己的 valid 旗標；整體無讀值時所有面向誠實 invalid（不謊報 0 / 不謊報
//     online）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - "net.connectivity.online"    / "Network Online"   / unit ""   / range bounded(0,1)
//        單一實例（整體網際網路連通性）。
//   - "net.connectivity.reachable" / "Target Reachable" / unit ""   / range bounded(0,1)
//   - "net.connectivity.latency"   / "Network Latency"  / unit "ms" / range at_least(0)
//   - "net.connectivity.loss"      / "Packet Loss"      / unit "%"  / range bounded(0,100)
//   - "net.connectivity.dns"       / "DNS Resolvable"   / unit ""   / range bounded(0,1)
//        以上四者以**每個探測目標**（如 "8.8.8.8" / "dns.google"）為可列舉實例，
//        共用同一組目標實例（同名、同列舉順序 = 首見序），配合 E2-02 週期採集鋪成
//        時序歷史，供折線 / sparkline 類元件直接鋪繪。
// bool 面向以 1.0 / 0.0 表達（配合 range bounded(0,1)，可正規化）。
#ifndef DS_MODULES_E2_20_CONNECTIVITY_HPP
#define DS_MODULES_E2_20_CONNECTIVITY_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// ConnectivityTarget：對**單一探測目標**的連通性讀值（平台中立值）
// ---------------------------------------------------------------------------
// 每個目標（如 "8.8.8.8"、"dns.google"）帶四個面向的讀值 + 各自的 valid 旗標：
//   - reachable      該目標是否可達（bool）。
//   - latency_ms     來回延遲 / ping（ms）。
//   - loss_pct       封包遺失率（%，0..100）。
//   - dns_resolvable 該目標名是否可經 DNS 解析（bool）。
// 各面向獨立帶 valid：例如目標可達（reachable_valid）但尚未量到延遲（latency_valid==false）
// 是合法且誠實的狀態；無讀值的面向不謊報 0。
struct ConnectivityTarget {
    std::string name;                // 目標穩定名（如 "8.8.8.8" / "dns.google"）
    bool reachable = false;
    bool reachable_valid = false;
    double latency_ms = 0.0;
    bool latency_valid = false;
    double loss_pct = 0.0;
    bool loss_valid = false;
    bool dns_resolvable = false;
    bool dns_valid = false;

    bool operator==(const ConnectivityTarget& o) const {
        return name == o.name && reachable == o.reachable &&
               reachable_valid == o.reachable_valid && latency_ms == o.latency_ms &&
               latency_valid == o.latency_valid && loss_pct == o.loss_pct &&
               loss_valid == o.loss_valid && dns_resolvable == o.dns_resolvable &&
               dns_valid == o.dns_valid;
    }
    bool operator!=(const ConnectivityTarget& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// ConnectivitySample：某一時刻的連通性快照（提供者面對的統一形狀）
// ---------------------------------------------------------------------------
//   - online / online_valid：整體網際網路是否連通（bool）。可由來源直接給，或由各目標
//     可達性推得（見 online_from_targets）。online_valid==false = 該面向無讀值。
//   - targets：各探測目標的逐目標讀值。目標集合可在取樣間變動（新增 / 移除）；提供者
//     按**目標名**對齊。
//   - valid：**整體無讀值**（尚未探測 / 感測失敗）——所有面向皆未知。
struct ConnectivitySample {
    bool online = false;
    bool online_valid = false;
    std::vector<ConnectivityTarget> targets;
    bool valid = false;   // false = 整體無讀值（保守預設）

    std::size_t target_count() const noexcept { return targets.size(); }

    // 明確的「無讀值」（保守預設）。
    static ConnectivitySample unknown() { return ConnectivitySample{}; }

    // 按名尋找目標；找不到回 nullptr。
    const ConnectivityTarget* find(const std::string& name) const;
};

// ---------------------------------------------------------------------------
// 純邏輯自由函式（獨立可測；平台中立純算術，無任何探測）
// ---------------------------------------------------------------------------
// 由封包收發數算遺失率（%）：loss% = (sent - received) / sent * 100。
//   - sent == 0（未送出任何探測）→ 0（無從判斷，不謊報）。
//   - received > sent（不該發生）→ 0（保守夾住，避免負遺失率）。
double loss_pct_from_counts(std::uint64_t sent, std::uint64_t received) noexcept;

// 由各目標可達性推「整體是否連上網際網路」的判定結果。
struct OnlineVerdict {
    bool online = false;
    bool valid = false;
};

// 由各目標可達性推「整體是否連上網際網路」：任一目標 reachable_valid 且 reachable == true
// 即視為 online。當**至少一個**目標帶有效可達性讀值時回一個「有效」結果（valid==true）；
// 若沒有任一目標帶有效可達性讀值，回「無讀值」（valid==false，即不謊報 online / offline）。
OnlineVerdict online_from_targets(const ConnectivitySample& sample) noexcept;

// ---------------------------------------------------------------------------
// ConnectivitySource：網路連通性的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 ConnectivitySample。實作決定其來源（注入式假
// 來源 / 真實後端探測）。相位 1 只有注入式 NullConnectivitySource——**永不含任何平台
// 探測呼叫**（socket / ping / getaddrinfo）。真實探測留待後端相位。
class ConnectivitySource {
public:
    virtual ~ConnectivitySource() = default;

    // 取一份目前連通性快照。無讀值時回 ConnectivitySample::unknown()。
    virtual ConnectivitySample sample() = 0;

protected:
    ConnectivitySource() = default;
    ConnectivitySource(const ConnectivitySource&) = default;
    ConnectivitySource& operator=(const ConnectivitySource&) = default;
};

// ---------------------------------------------------------------------------
// NullConnectivitySource：相位 1 的 null / 假連通性來源（可注入固定 / 序列）
// ---------------------------------------------------------------------------
// **不接任何真實網路探測**。內部持一列注入的快照，每次 sample() 回下一份；列盡則持續回
// 最後一份（穩定，不走出界）。空列預設回「無讀值」（Mac / null 期的誠實預設）。
//   - **固定**用法：set_sample / set_online / set_target 建一份固定快照（單元素序列，
//     sample() 每次回同一份）。
//   - **序列**用法：set_sequence / push_sample 注入多份，模擬連通性隨時間變化（上 / 下線、
//     延遲抖動），sample() 逐份推進。
class NullConnectivitySource : public ConnectivitySource {
public:
    NullConnectivitySource() = default;
    explicit NullConnectivitySource(ConnectivitySample fixed) { set_sample(std::move(fixed)); }
    explicit NullConnectivitySource(std::vector<ConnectivitySample> sequence)
        : sequence_(std::move(sequence)) {}

    // -- 序列用法 ---------------------------------------------------------
    // 注入 / 覆寫整條快照序列（重置游標到起點）。
    void set_sequence(std::vector<ConnectivitySample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份快照到序列尾。
    void push_sample(ConnectivitySample s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    // -- 固定用法（單元素序列，sample() 每次回同一份）---------------------
    // 設為固定快照（覆寫整條序列為此單份、游標歸零）。
    void set_sample(ConnectivitySample s) {
        sequence_.clear();
        sequence_.push_back(std::move(s));
        cursor_ = 0;
    }
    // 設定固定快照的整體 online（valid=true）。必要時先建立固定快照。
    void set_online(bool online);
    // 新增 / 覆寫固定快照中某目標的完整讀值（四面向皆 valid、整體 valid=true）。
    void set_target(const std::string& name, bool reachable, double latency_ms,
                    double loss_pct, bool dns_resolvable);
    // 回到「無讀值」預設（清空序列）。
    void clear() noexcept {
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份快照；列盡回最後一份；空列回「無讀值」。
    ConnectivitySample sample() override;

private:
    // 確保有一份可就地修改的固定快照（單元素序列）；回其參照並把游標歸零。
    ConnectivitySample& ensure_fixed();

    std::vector<ConnectivitySample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// ConnectivityProvider：把網路連通性掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上五個指標
// （online、reachable、latency、loss、dns）。其中 online 為**單一實例**（整體連通性），
// 其餘四者以**每個探測目標**為可列舉實例。因連通性隨時間變動，本提供者建議以 **E2-02 的
// 週期分級**採集：呼叫端把（各）metric_id 與 sampling_tier() 登記到 SamplingScheduler，
// 於排程器判定該採集時呼叫 sample() 重新讀來源並更新各實例。消費者（掛件）只透過 E2-01
// 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class ConnectivityProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.connectivity";

    // 五個面向指標的識別碼。
    static constexpr const char* kOnlineMetricId = "net.connectivity.online";
    static constexpr const char* kReachableMetricId = "net.connectivity.reachable";
    static constexpr const char* kLatencyMetricId = "net.connectivity.latency";
    static constexpr const char* kLossMetricId = "net.connectivity.loss";
    static constexpr const char* kDnsMetricId = "net.connectivity.dns";

    static constexpr const char* kLatencyUnit = "ms";
    static constexpr const char* kLossUnit = "%";
    static constexpr const char* kBoolUnit = "";

    // 整體 online 指標的單一實例識別碼 / 標籤。
    static constexpr const char* kInternetInstanceId = "internet";

    // 逐目標面向枚舉（決定 target 指標的列舉 / 註冊順序）。
    enum TargetFacet {
        kReachable = 0,
        kLatency,
        kLoss,
        kDns,
        kTargetFacetCount,
    };

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成連通性序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：連通性屬常規變動，預設常規頻率（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個連通性來源建構。source 為 null 時，提供者仍會掛上五個指標（online 有其單一
    // 實例、其餘四者無任何目標實例），且保守回無讀值（不崩、不謊報）。
    explicit ConnectivityProvider(std::shared_ptr<ConnectivitySource> source,
                                  std::size_t history = kDefaultHistory,
                                  ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 五個面向指標的識別碼（註冊順序：online、reachable、latency、loss、dns）。
    static std::vector<ds::metrics::MetricId> metric_ids();

    // 對註冊表掛上五個指標：online 建其單一實例，其餘四者依目前一份快照建每目標實例並
    // 填初值，並保留指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新連通性寫入各實例（有效值推入歷史）。呼叫端在 E2-02 排程器判定本指標
    // 該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣出現新目標，會**動態新增**該目標於四個逐目標指標的實例（既有參照不失效）；
    // 若某目標本次消失，其實例設為未知（不污染歷史）。
    void sample();

    // 目前已知的探測目標實例數。register_metrics 前為 0。
    std::size_t target_count() const noexcept { return target_names_.size(); }

private:
    // 把一份快照寫入各實例。to_history 為真時有效值推入歷史（採集路徑），為假時只設值。
    void apply(const ConnectivitySample& snap, bool to_history);

    // 目前連通性快照：source_ 為 null 時視為「無讀值」。
    ConnectivitySample current() {
        return source_ ? source_->sample() : ConnectivitySample::unknown();
    }

    // 確保某目標名已有實例（於四個逐目標指標）；回其索引。必要時動態新增。
    std::size_t ensure_target(const std::string& name);

    // 把一個數值寫入某實例（valid 決定是否為未知 / 是否推歷史）。
    void write_value(ds::metrics::InMemoryMetricInstance* inst, double number, bool valid,
                     bool to_history);

    std::shared_ptr<ConnectivitySource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有整體 online 指標 + 其單一實例（與 registry 共享同一物件）。
    std::shared_ptr<ds::metrics::InMemoryMetric> online_metric_;
    ds::metrics::InMemoryMetricInstance* online_inst_ = nullptr;

    // 四個逐目標指標 + 每指標的目標實例（與 target_names_ 同序，非擁有指標，壽命由
    // metrics_ 保證）。
    std::array<std::shared_ptr<ds::metrics::InMemoryMetric>, kTargetFacetCount> metrics_{};
    std::array<std::vector<ds::metrics::InMemoryMetricInstance*>, kTargetFacetCount> insts_{};

    // 已知目標名（首見序）+ 名→索引。
    std::vector<std::string> target_names_;
    std::unordered_map<std::string, std::size_t> target_index_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_20_CONNECTIVITY_HPP
