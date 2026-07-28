// E2-08 網路流量 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「網路流量」透過 **E2-01 的 MetricProvider 介面** 掛成一組指標——每個網路
// 介面的 **上傳/下載速率**（bytes/sec）、**累積收發位元組**、**收發封包數**，並支援
// **多介面**列舉。速率以 **E2-02 的採集頻率分級** 決定採樣節奏。這是「新增指標 = 新增
// MetricProvider、掛件一行不動」機制的又一個具體提供者——它**消費 E2-01 / E2-02 契約、
// 不自造指標模型或排程器**（範式與 E2-03 CPU 負載一致）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實網路 API**（無 `getifaddrs`
//     / `/proc/net/dev` / mach / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作
//     抽象來源，提供者一行不動。
//   - **速率由累積計數差分**：作業系統原生網路統計為單調遞增的累積 rx/tx 位元組與封包
//     計數；速率 = (位元組Δ / 時間Δ) bytes/sec。此差分邏輯抽成獨立可測的自由函式
//     `rate_from_delta`（純算術），並由 `DifferencingNetworkStatSource` 封裝
//     （把一個「累積計數來源」轉成「流量來源」）。
//   - **累積計數直接可讀**：累積位元組 / 封包數本身即有意義（無須差分），故一有讀值即
//     有效；**只有速率**需要兩次取樣差分，**首次差分速率回 invalid**（誠實：單一時刻無從
//     算速率）。整體無讀值時所有面向誠實 invalid（不謊報 0）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：把六個面向各掛成一個指標，
// 其可列舉實例 = 每個網路介面（如 "en0" / "en1"）：
//   - "net.rx.rate"    / "Network RX Rate"    / unit "B/s"     / range at_least 0
//   - "net.tx.rate"    / "Network TX Rate"    / unit "B/s"     / range at_least 0
//   - "net.rx.bytes"   / "Network RX Bytes"   / unit "B"       / range at_least 0
//   - "net.tx.bytes"   / "Network TX Bytes"   / unit "B"       / range at_least 0
//   - "net.rx.packets" / "Network RX Packets" / unit "packets" / range at_least 0
//   - "net.tx.packets" / "Network TX Packets" / unit "packets" / range at_least 0
// 六個指標共用同一組介面實例（同名、同列舉順序 = 首見序），配合 E2-02 週期採集鋪成
// 時序歷史，供折線 / sparkline 類元件直接鋪繪。
#ifndef DS_MODULES_E2_08_NETWORK_TRAFFIC_HPP
#define DS_MODULES_E2_08_NETWORK_TRAFFIC_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// NetInterfaceCounters：單一介面的**累積**計數（平台中立值）
// ---------------------------------------------------------------------------
// 作業系統原生網路統計多為單調遞增的累積計數（getifaddrs / /proc/net/dev 皆然）。
// 此處只描述其平台中立形狀，**不含任何取值方式**：
//   - rx_bytes / tx_bytes     累積收 / 發位元組。
//   - rx_packets / tx_packets 累積收 / 發封包數。
// 速率須由**兩次取樣差分**得出（見 rate_from_delta）：單一時刻的累積值本身無速率意義，
// 但累積計數本身即有意義（可直接呈現）。
struct NetInterfaceCounters {
    std::uint64_t rx_bytes = 0;
    std::uint64_t tx_bytes = 0;
    std::uint64_t rx_packets = 0;
    std::uint64_t tx_packets = 0;

    bool operator==(const NetInterfaceCounters& o) const {
        return rx_bytes == o.rx_bytes && tx_bytes == o.tx_bytes &&
               rx_packets == o.rx_packets && tx_packets == o.tx_packets;
    }
    bool operator!=(const NetInterfaceCounters& o) const { return !(*this == o); }
};

// 某一介面在快照中的一筆累積計數（含介面名，供差分時**按名對齊**）。
struct NetInterfaceCounterEntry {
    std::string name;                 // 介面穩定名（如 "en0"）
    NetInterfaceCounters counters;

    bool operator==(const NetInterfaceCounterEntry& o) const {
        return name == o.name && counters == o.counters;
    }
    bool operator!=(const NetInterfaceCounterEntry& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// NetCountersSample：某一時刻**全介面**的累積計數快照 + 單調時戳
// ---------------------------------------------------------------------------
// timestamp 為平台中立的**單調秒數**（由呼叫端 / 來源注入，非真實時鐘）：差分時以兩份
// 快照的時戳差為分母算速率。介面集合可在取樣間變動（上 / 下線）；差分按**介面名**對齊。
struct NetCountersSample {
    double timestamp = 0.0;                         // 單調秒數（平台中立）
    std::vector<NetInterfaceCounterEntry> interfaces;

    std::size_t interface_count() const noexcept { return interfaces.size(); }
    bool empty() const noexcept { return interfaces.empty(); }
};

// ---------------------------------------------------------------------------
// NetTrafficSample：某一時刻的流量快照（提供者面對的統一形狀）
// ---------------------------------------------------------------------------
// 每介面一筆：速率（bytes/sec，來自差分或來源直接給）+ 累積計數（直接可讀）。
//   - rate_valid false = 速率無讀值（首次差分 / 無前一份 / 時間差<=0）；此時累積計數仍
//     可有效（cumulative 直接可讀）。
//   - valid false = **整體無讀值**（尚未取樣 / 感測失敗）：所有面向皆未知。
struct NetTrafficSample {
    struct Iface {
        std::string name;
        double rx_rate = 0.0;   // bytes/sec
        double tx_rate = 0.0;   // bytes/sec
        std::uint64_t rx_bytes = 0;      // 累積
        std::uint64_t tx_bytes = 0;
        std::uint64_t rx_packets = 0;
        std::uint64_t tx_packets = 0;
        bool rate_valid = false;         // 速率是否已差分（首次 / 無前份 → false）

        bool operator==(const Iface& o) const {
            return name == o.name && rx_rate == o.rx_rate && tx_rate == o.tx_rate &&
                   rx_bytes == o.rx_bytes && tx_bytes == o.tx_bytes &&
                   rx_packets == o.rx_packets && tx_packets == o.tx_packets &&
                   rate_valid == o.rate_valid;
        }
        bool operator!=(const Iface& o) const { return !(*this == o); }
    };

    std::vector<Iface> interfaces;
    bool valid = false;   // false = 整體無讀值（保守預設）

    std::size_t interface_count() const noexcept { return interfaces.size(); }

    // 明確的「無讀值」（保守預設）。
    static NetTrafficSample unknown() { return NetTrafficSample{}; }

    // 按名尋找介面；找不到回 nullptr。
    const Iface* find(const std::string& name) const;
};

// ---------------------------------------------------------------------------
// 差分演算法（自由函式，獨立可測；平台中立純算術）
// ---------------------------------------------------------------------------
// 單面向速率：bytes/sec = (currΔ / dt_seconds)。
//   - dt_seconds <= 0（無經過時間）→ 0（無從判斷，不謊報）。
//   - 計數器重置（curr < prev，累積值理應單調）→ 0（保守，避免負值 / 爆量）。
double rate_from_delta(std::uint64_t prev, std::uint64_t curr, double dt_seconds) noexcept;

// 由**單一**累積快照建流量快照：累積計數直接填入、速率 0 且 rate_valid==false
// （尚無前一份可差分），valid==true（有讀值）。供差分來源首次取樣用。
NetTrafficSample traffic_from_snapshot(const NetCountersSample& curr);

// 由兩份累積快照（prev→curr）算流量：
//   - 逐介面（以 curr 為準，按名於 prev 尋對應）：累積計數取自 curr；若找到 prev 對應且
//     dt>0 → rx/tx 速率由 rate_from_delta 算出、rate_valid=true；否則速率 0、rate_valid
//     =false（新上線介面 / 無經過時間 → 只有累積、暫無速率）。
//   - valid = true（已有兩份取樣）。curr 無介面時 interfaces 為空但 valid 仍 true。
NetTrafficSample traffic_from_delta(const NetCountersSample& prev, const NetCountersSample& curr);

// ---------------------------------------------------------------------------
// NetworkStatSource：網路流量的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 NetTrafficSample。實作決定其來源（來源直接給
// 速率 / 累積計數差分 / 真實後端）。sample() **非 const**：差分型來源每次取樣會推進內部
// 「上一份」狀態，故取樣具副作用。
class NetworkStatSource {
public:
    virtual ~NetworkStatSource() = default;

    // 取一份目前流量快照。無讀值時回 NetTrafficSample::unknown()。
    virtual NetTrafficSample sample() = 0;

protected:
    NetworkStatSource() = default;
    NetworkStatSource(const NetworkStatSource&) = default;
    NetworkStatSource& operator=(const NetworkStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullNetworkStatSource：相位 1 的 null / 假「直接流量」來源
// ---------------------------------------------------------------------------
// **不接任何真實網路 API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入固定流量
// 快照供測試與假感測器情境（此即「來源直接給流量」路徑）。真實查詢留待後端相位——本類
// 永不含平台呼叫。
class NullNetworkStatSource : public NetworkStatSource {
public:
    NullNetworkStatSource() = default;
    explicit NullNetworkStatSource(NetTrafficSample fixed) : fixed_(std::move(fixed)) {}

    // 注入 / 覆寫整份流量快照。
    void set_traffic(NetTrafficSample traffic) { fixed_ = std::move(traffic); }

    // 便利：新增 / 覆寫一個介面的完整讀值（速率 + 累積），rate_valid=true、整體 valid=true。
    void set_interface(const std::string& name, double rx_rate, double tx_rate,
                       std::uint64_t rx_bytes, std::uint64_t tx_bytes,
                       std::uint64_t rx_packets, std::uint64_t tx_packets);

    // 回到「無讀值」預設（null 期誠實語意）。
    void clear() { fixed_ = NetTrafficSample::unknown(); }

    // 目前注入的快照（唯讀）。
    const NetTrafficSample& traffic() const noexcept { return fixed_; }

    // 回目前注入的流量快照（決定性；無副作用，直接讀路徑）。
    NetTrafficSample sample() override { return fixed_; }

private:
    NetTrafficSample fixed_ = NetTrafficSample::unknown();
};

// ---------------------------------------------------------------------------
// NetCounterSource：**累積計數**的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 給出各介面的累積 rx/tx 位元組與封包快照（含時戳，尚未差分）。真實後端（相位 2+）於此
// 讀 getifaddrs / /proc/net/dev；相位 1 只有注入式 NullNetCounterSource。差分邏輯不在此
// 層——由 DifferencingNetworkStatSource 承擔（關注點分離，兩者各自可測）。
class NetCounterSource {
public:
    virtual ~NetCounterSource() = default;

    // 讀一份目前的累積計數快照。
    virtual NetCountersSample read() = 0;

protected:
    NetCounterSource() = default;
    NetCounterSource(const NetCounterSource&) = default;
    NetCounterSource& operator=(const NetCounterSource&) = default;
};

// ---------------------------------------------------------------------------
// NullNetCounterSource：相位 1 的 null / 假累積計數來源
// ---------------------------------------------------------------------------
// **不接任何真實網路 API**。持一列注入的累積計數快照（模擬時間推進下的累積值 + 時戳），
// 每次 read() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。空列預設回空快照。
class NullNetCounterSource : public NetCounterSource {
public:
    NullNetCounterSource() = default;
    explicit NullNetCounterSource(std::vector<NetCountersSample> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫整條累積計數序列（重置游標到起點）。
    void set_sequence(std::vector<NetCountersSample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份累積計數快照到序列尾。
    void push_sample(NetCountersSample s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份累積計數快照；列盡回最後一份；空列回空快照。
    NetCountersSample read() override;

private:
    std::vector<NetCountersSample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// DifferencingNetworkStatSource：把「累積計數來源」轉成「流量來源」的差分轉接器
// ---------------------------------------------------------------------------
// 消費一個 NetCounterSource，內部保存「上一份」累積計數；每次 sample() 讀新一份、與上一份
// **差分**（traffic_from_delta）得速率，並直接帶出累積計數。首次 sample()（尚無上一份可比）
// 回 traffic_from_snapshot（累積有效、速率 rate_valid==false）——**速率**差分至少需兩份
// 取樣。這正是「速率由累積計數差分」的封裝，且本身是個乾淨可組合的 NetworkStatSource
// （提供者不必知道背後是差分或直接流量）。
class DifferencingNetworkStatSource : public NetworkStatSource {
public:
    explicit DifferencingNetworkStatSource(std::shared_ptr<NetCounterSource> counters)
        : counters_(std::move(counters)) {}

    // 是否已有「上一份」基準（即已至少取樣一次）。
    bool primed() const noexcept { return primed_; }

    // 丟棄已保存的基準（下一次 sample() 又回「速率需兩份」語意）。
    void reset() noexcept { primed_ = false; prev_ = NetCountersSample{}; }

    // 讀新一份累積計數、與上一份差分得速率。首次（或 reset 後首次）速率回未知（累積仍有效）。
    NetTrafficSample sample() override;

private:
    std::shared_ptr<NetCounterSource> counters_;
    NetCountersSample prev_;
    bool primed_ = false;
};

// ---------------------------------------------------------------------------
// NetworkTrafficProvider：把網路流量掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上六個指標
// （rx/tx 速率、rx/tx 累積位元組、rx/tx 封包數），各以每網路介面為可列舉實例。因速率會
// 隨時間變動，本提供者建議以 **E2-02 的週期分級**採集：呼叫端把（各）metric_id 與
// sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新讀來源
// 並更新各實例。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及
// 本型別。
class NetworkTrafficProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.net";

    // 六個面向指標的識別碼。
    static constexpr const char* kRxRateMetricId = "net.rx.rate";
    static constexpr const char* kTxRateMetricId = "net.tx.rate";
    static constexpr const char* kRxBytesMetricId = "net.rx.bytes";
    static constexpr const char* kTxBytesMetricId = "net.tx.bytes";
    static constexpr const char* kRxPacketsMetricId = "net.rx.packets";
    static constexpr const char* kTxPacketsMetricId = "net.tx.packets";

    static constexpr const char* kRateUnit = "B/s";
    static constexpr const char* kBytesUnit = "B";
    static constexpr const char* kPacketsUnit = "packets";

    // 面向枚舉（六個指標的內部索引，決定列舉 / 註冊順序）。
    enum Facet {
        kRxRate = 0,
        kTxRate,
        kRxBytes,
        kTxBytes,
        kRxPackets,
        kTxPackets,
        kFacetCount,
    };

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成流量序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：網路流量常規變動，屬常規頻率（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個網路流量來源建構。source 為 null 時，提供者仍會掛上六個指標，且無任何介面
    // 實例（保守而不崩、不謊報 0）。history 為各實例歷史環容量、tier 為建議採集分級。
    explicit NetworkTrafficProvider(std::shared_ptr<NetworkStatSource> source,
                                    std::size_t history = kDefaultHistory,
                                    ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 六個面向指標的識別碼（註冊順序）。
    static std::vector<ds::metrics::MetricId> metric_ids();

    // 對註冊表掛上六個指標：取一份流量、依其介面建每介面實例、填初值，並保留指標參照供
    // 日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新流量寫入各實例（有效值推入歷史）。呼叫端在 E2-02 排程器判定本指標
    // 該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣出現新介面，會**動態新增**該介面於全部六個指標的實例（既有參照不失效）；
    // 若某介面本次消失，其實例設為未知（不污染歷史）。
    void sample();

    // 目前已知的網路介面實例數。register_metrics 前為 0。
    std::size_t interface_count() const noexcept { return iface_names_.size(); }

private:
    // 把一份流量寫入各實例。to_history 為真時有效值推入歷史（採集路徑），為假時只設值。
    // 必要時動態新增介面實例。
    void apply(const NetTrafficSample& traffic, bool to_history);

    // 目前流量快照：source_ 為 null 時視為「無讀值」。
    NetTrafficSample current() {
        return source_ ? source_->sample() : NetTrafficSample::unknown();
    }

    // 確保某介面名已有實例（於全部六個指標）；回其索引。必要時動態新增。
    std::size_t ensure_interface(const std::string& name);

    // 把一個數值寫入某實例（valid 決定是否為未知 / 是否推歷史）。
    void write_value(ds::metrics::InMemoryMetricInstance* inst, double number, bool valid,
                     bool to_history);

    std::shared_ptr<NetworkStatSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有六個指標（與 registry 共享同一物件，故更新對消費者可見）。
    std::array<std::shared_ptr<ds::metrics::InMemoryMetric>, kFacetCount> metrics_{};
    // 每個指標的介面實例（與 iface_names_ 同序，非擁有指標，壽命由 metrics_ 保證）。
    std::array<std::vector<ds::metrics::InMemoryMetricInstance*>, kFacetCount> insts_{};
    // 已知介面名（首見序）+ 名→索引。
    std::vector<std::string> iface_names_;
    std::unordered_map<std::string, std::size_t> iface_index_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_08_NETWORK_TRAFFIC_HPP
