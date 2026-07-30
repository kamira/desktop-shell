// E2-24 Wi-Fi 狀態 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「Wi-Fi 連線狀態」透過 **E2-01 的 MetricProvider 介面** 掛成一組指標——
// **SSID**、**訊號強度**（RSSI dBm 與 0–100%）、**連線速率**（Mbps）、**頻道 / 頻段**、
// **安全類型**、**是否已連線**。連線狀態會變動，故以 **E2-02 的採集頻率分級** 決定採樣
// 節奏。這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者——
// 它**消費 E2-01 / E2-02 契約、不自造指標模型或排程器**（範式與 E2-03 CPU 負載、
// E2-08 網路流量一致）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實 Wi-Fi API**（無 CoreWLAN /
//     `airport` / `CWInterface` / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作
//     抽象 `WifiSource`，提供者一行不動。
//   - **無差分**：Wi-Fi 狀態是**直接讀**的瞬時快照（不同於 CPU / 網路的累積計數差分）；
//     來源每次 `sample()` 直接回一份 `WifiStatus`。訊號百分比可由 RSSI dBm 換算——此換算
//     抽成獨立可測的純算術自由函式 `rssi_to_percent`（夾到 [0,100]）。
//   - **誠實無讀值**：整體無讀值（尚未取樣 / 感測失敗 / source 為 null）時所有面向皆
//     `valid==false`（未知），不謊報 0。**未連線**時「是否已連線」面向仍有效（值 =
//     未連線），但**連線相依面向**（SSID / RSSI / 速率 / 頻道 / 安全）則為未知——沒有連線
//     即無這些讀值可言，誠實表達為未知而非塞假值。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：把各面向各掛成一個指標，其單一可列舉
// 實例 = Wi-Fi 介面（"wifi0"）：
//   - "wifi.connected" / "Wi-Fi Connected" / unit ""     / bounded[0,1]     / 1|0（+文字）
//   - "wifi.ssid"      / "Wi-Fi SSID"      / unit ""     / unbounded        / 文字表述
//   - "wifi.rssi"      / "Wi-Fi Signal (RSSI)" / "dBm"   / bounded[-100,0]  / RSSI dBm
//   - "wifi.signal"    / "Wi-Fi Signal"    / unit "%"    / bounded[0,100]   / 訊號百分比
//   - "wifi.rate"      / "Wi-Fi Link Rate" / unit "Mbps" / at_least 0       / 連線速率
//   - "wifi.channel"   / "Wi-Fi Channel"   / unit ""     / at_least 0       / 頻道（+頻段文字）
//   - "wifi.security"  / "Wi-Fi Security"  / unit ""     / unbounded        / 安全類型（+文字）
// 各指標共用同一 Wi-Fi 介面實例，配合 E2-02 週期採集把數值面向（RSSI / 訊號 / 速率 /
// 頻道）鋪成時序歷史，供折線 / sparkline 類元件直接鋪繪。
#ifndef DS_MODULES_E2_24_WIFI_STATUS_HPP
#define DS_MODULES_E2_24_WIFI_STATUS_HPP

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// WifiBand：頻段（平台中立列舉）
// ---------------------------------------------------------------------------
// 頻道所屬頻段。Unknown = 無讀值 / 未知。
enum class WifiBand {
    Unknown = 0,
    Band2_4GHz,
    Band5GHz,
    Band6GHz,
};

// 診斷 / 顯示用穩定字串（"unknown"/"2.4 GHz"/"5 GHz"/"6 GHz"）。
const char* to_string(WifiBand band) noexcept;

// ---------------------------------------------------------------------------
// WifiSecurity：安全類型（平台中立列舉）
// ---------------------------------------------------------------------------
// 由弱到強粗略排序（ordinal 供指標數值維度用；主表述為文字）。Unknown = 未知。
enum class WifiSecurity {
    Unknown = 0,
    Open,        // 無加密
    WEP,
    WPA,
    WPA2,
    WPA3,
    Enterprise,  // 802.1X 企業級
};

// 診斷 / 顯示用穩定字串（"unknown"/"open"/"wep"/"wpa"/"wpa2"/"wpa3"/"enterprise"）。
const char* to_string(WifiSecurity security) noexcept;

// ---------------------------------------------------------------------------
// rssi_to_percent：RSSI dBm → 訊號百分比 [0,100]（自由函式，獨立可測；純算術）
// ---------------------------------------------------------------------------
// 常見線性近似：-100 dBm（極弱）→ 0%，-50 dBm（極強）→ 100%，其間線性；超界夾到
// [0,100]。平台中立、無真實 Wi-Fi API——僅供來源未直接提供百分比時換算。
//   percent = clamp(2 * (dbm + 100), 0, 100)
double rssi_to_percent(double dbm) noexcept;

// ---------------------------------------------------------------------------
// WifiStatus：某一時刻的 Wi-Fi 連線狀態快照（提供者面對的統一形狀）
// ---------------------------------------------------------------------------
// 平台中立值：由 WifiSource 給出，不含任何取值方式。
//   - valid == false      → **整體無讀值**（尚未取樣 / 感測失敗）：所有面向皆未知。
//   - connected == false  → 未連線：「是否已連線」有效（值 = 未連線），但 SSID / RSSI /
//                           速率 / 頻道 / 安全等**連線相依面向**為未知（無連線即無讀值）。
//   - signal_percent      → 訊號百分比 [0,100]；來源未給時可由 rssi_to_percent(rssi_dbm) 補。
struct WifiStatus {
    bool connected = false;              // 是否已連線
    std::string ssid;                    // 連線的網路名稱（未連線 / 未知時為空）
    double rssi_dbm = 0.0;               // 訊號強度（dBm，負值，越接近 0 越強）
    double signal_percent = 0.0;         // 訊號強度（0–100%）
    double link_rate_mbps = 0.0;         // 連線速率（Mbps）
    int channel = 0;                     // 頻道（0 = 未知）
    WifiBand band = WifiBand::Unknown;   // 頻段
    WifiSecurity security = WifiSecurity::Unknown;  // 安全類型
    bool valid = false;                  // false = 整體無讀值（保守預設）

    // 明確的「無讀值」（保守預設；所有面向未知）。
    static WifiStatus unknown() { return WifiStatus{}; }

    // 「已連線」快照工廠：填入 SSID / RSSI / 速率 / 頻道 / 頻段 / 安全，訊號百分比由
    // rssi_to_percent(rssi_dbm) 換算，connected=true、valid=true。
    static WifiStatus connected_to(std::string ssid, double rssi_dbm, double link_rate_mbps,
                                   int channel, WifiBand band, WifiSecurity security);

    // 「未連線但有讀值」快照（valid=true、connected=false，其餘為預設 / 空）。
    static WifiStatus not_connected() {
        WifiStatus s;
        s.valid = true;
        return s;
    }

    bool operator==(const WifiStatus& o) const {
        return valid == o.valid && connected == o.connected && ssid == o.ssid &&
               rssi_dbm == o.rssi_dbm && signal_percent == o.signal_percent &&
               link_rate_mbps == o.link_rate_mbps && channel == o.channel &&
               band == o.band && security == o.security;
    }
    bool operator!=(const WifiStatus& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// WifiSource：Wi-Fi 狀態的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 WifiStatus。實作決定其來源（注入 / 真實後端）。
// sample() **非 const**：序列型來源每次取樣會推進內部游標，故取樣具副作用。
class WifiSource {
public:
    virtual ~WifiSource() = default;

    // 取一份目前 Wi-Fi 狀態快照。無讀值時回 WifiStatus::unknown()。
    virtual WifiStatus sample() = 0;

protected:
    WifiSource() = default;
    WifiSource(const WifiSource&) = default;
    WifiSource& operator=(const WifiSource&) = default;
};

// ---------------------------------------------------------------------------
// NullWifiSource：相位 1 的 null / 假 Wi-Fi 來源
// ---------------------------------------------------------------------------
// **不接任何真實 Wi-Fi API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入**固定**
// 一份狀態，或一整條**序列**（模擬連線狀態隨時間變動 / 掉線）。序列型：每次 sample() 回下
// 一份；列盡則持續回最後一份（穩定，不走出界）。真實查詢留待後端相位——本類永不含平台呼叫。
class NullWifiSource : public WifiSource {
public:
    NullWifiSource() = default;
    explicit NullWifiSource(WifiStatus fixed) { sequence_.push_back(std::move(fixed)); }
    explicit NullWifiSource(std::vector<WifiStatus> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫為單一固定狀態（重置游標）。
    void set_status(WifiStatus status) {
        sequence_.clear();
        sequence_.push_back(std::move(status));
        cursor_ = 0;
    }
    // 注入 / 覆寫整條狀態序列（重置游標到起點）。
    void set_sequence(std::vector<WifiStatus> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份狀態快照到序列尾。
    void push_status(WifiStatus s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }
    // 回到「無讀值」預設（清空序列，null 期誠實語意）。
    void clear() {
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份狀態快照；列盡回最後一份；空列回 WifiStatus::unknown()。
    WifiStatus sample() override;

private:
    std::vector<WifiStatus> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// WifiStatusProvider：把 Wi-Fi 狀態掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上七個面向指標
// （connected / ssid / rssi / signal / rate / channel / security），各以單一 Wi-Fi 介面
// 為可列舉實例。因狀態會變動，本提供者建議以 **E2-02 的週期分級**採集：呼叫端把（各）
// metric_id 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample()
// 重新讀來源並更新各實例。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，
// 完全不觸及本型別。
class WifiStatusProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.wifi";

    // 單一 Wi-Fi 介面實例的穩定識別碼與顯示名。
    static constexpr const char* kInstanceId = "wifi0";
    static constexpr const char* kInstanceLabel = "Wi-Fi";

    // 七個面向指標的識別碼。
    static constexpr const char* kConnectedMetricId = "wifi.connected";
    static constexpr const char* kSsidMetricId = "wifi.ssid";
    static constexpr const char* kRssiMetricId = "wifi.rssi";
    static constexpr const char* kSignalMetricId = "wifi.signal";
    static constexpr const char* kRateMetricId = "wifi.rate";
    static constexpr const char* kChannelMetricId = "wifi.channel";
    static constexpr const char* kSecurityMetricId = "wifi.security";

    static constexpr const char* kRssiUnit = "dBm";
    static constexpr const char* kSignalUnit = "%";
    static constexpr const char* kRateUnit = "Mbps";

    // 面向枚舉（七個指標的內部索引，決定列舉 / 註冊順序）。
    enum Facet {
        kConnected = 0,
        kSsid,
        kRssi,
        kSignal,
        kRate,
        kChannel,
        kSecurity,
        kFacetCount,
    };

    // 各實例歷史環的預設容量（配合 E2-02 週期採集把數值面向鋪成時序）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：連線狀態變動不快，屬常規頻率（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個 Wi-Fi 來源建構。source 為 null 時，提供者仍會掛上七個指標，其 Wi-Fi 實例以
    // 「未知」（valid==false）呈現（保守而不崩、不謊報 0）。history 為各實例歷史環容量、
    // tier 為建議採集分級。
    explicit WifiStatusProvider(std::shared_ptr<WifiSource> source,
                                std::size_t history = kDefaultHistory,
                                ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 七個面向指標的識別碼（註冊順序）。
    static std::vector<ds::metrics::MetricId> metric_ids();

    // 對註冊表掛上七個指標：取一份狀態、建單一 Wi-Fi 介面實例、填初值，並保留指標參照供
    // 日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新狀態寫入各實例（有效數值面向推入歷史）。呼叫端在 E2-02 排程器判定
    // 本指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    void sample();

    // 目前一份狀態（供診斷 / 測試）：source_ 為 null 時視為「無讀值」。
    WifiStatus current_status() const {
        return source_ ? source_->sample() : WifiStatus::unknown();
    }

private:
    // 把一份狀態寫入各實例。to_history 為真時有效**數值**面向推入歷史（採集路徑）。
    void apply(const WifiStatus& status, bool to_history);

    // 目前狀態快照：source_ 為 null 時視為「無讀值」。
    WifiStatus current() {
        return source_ ? source_->sample() : WifiStatus::unknown();
    }

    // 把一個數值 + 可選文字寫入某面向實例。valid 決定是否為未知；to_history 且 valid 時
    // 推入歷史（數值面向鋪時序）；categorical（無數值時序意義的）面向以 push_history=false
    // 只設值不污染歷史。
    void write(Facet facet, const ds::metrics::MetricValue& v, bool to_history,
               bool push_history);

    std::shared_ptr<WifiSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有七個指標（與 registry 共享同一物件，故更新對消費者可見）。
    std::array<std::shared_ptr<ds::metrics::InMemoryMetric>, kFacetCount> metrics_{};
    // 每個指標的單一 Wi-Fi 介面實例（非擁有指標，壽命由 metrics_ 保證）。
    std::array<ds::metrics::InMemoryMetricInstance*, kFacetCount> insts_{};
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_24_WIFI_STATUS_HPP
