// E2-09 電源與電池狀態 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「電源 / 電池狀態」透過 **E2-01 的 MetricProvider 介面**掛成一組指標，並以
// **E2-02 的採集頻率分級**決定採樣節奏。這是「新增指標 = 新增 MetricProvider、掛件一行
// 不動」機制的又一個具體提供者——它**消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。
// 本單元是最終「電池 / 電源 Widget」的核心資料來源，故介面刻意乾淨、易組合（狀態值模型與
// 提供者分離、來源可注入、可模擬無電池桌機）。
//
// 電源 / 電池指標（見單元規格）：
//   - 電池電量(%)          —— power.battery.level（% [0,100]）
//   - 充電狀態             —— power.battery.state（charging / discharging / full / no-battery）
//   - 是否接電源            —— power.ac.online（1 = 接 AC，0 = 未接）
//   - 剩餘時間估計(分鐘)    —— power.battery.time_remaining（min）
//   - 循環次數（可選）      —— power.battery.cycles
//   - 健康度（可選）        —— power.battery.health（% [0,100]）
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實電源 API**（無 IOKit /
//     IOPowerSources / GetSystemPowerStatus / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）
//     另實作抽象 PowerStatSource，提供者一行不動。
//   - 狀態值模型（PowerStatus）平台中立、可注入、可完全單元測試：塞入固定 / 序列狀態，
//     驗電量 / 充電狀態轉換 / 接電源 / 剩餘時間 / 無電池(桌機) / 0 · 100 邊界 / 無讀值 invalid /
//     經 E2-02 採樣。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：每個維度為一個單一實例指標
// （kSingleInstanceId），各保留時序歷史，配合 E2-02 週期採集鋪成序列。無讀值時以
// valid==false（未知）誠實表達，而非塞假值——無電池桌機的電量 / 剩餘時間即為未知，但
// 充電狀態誠實為 "no-battery"、接電源為真。
#ifndef DS_MODULES_E2_09_POWER_STATUS_HPP
#define DS_MODULES_E2_09_POWER_STATUS_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// ChargeState：充電狀態（平台中立列舉）
// ---------------------------------------------------------------------------
// 四態（見單元規格）。底層碼供掛成數值指標（power.battery.state 的 number 維度），
// 文字表述供顯示 / 列舉。碼刻意穩定（跨平台、跨後端一致），不隨列舉順序調整而變。
enum class ChargeState {
    NoBattery = 0,    // 無電池（如桌機）
    Discharging = 1,  // 放電中（靠電池）
    Charging = 2,     // 充電中（接 AC 且未滿）
    Full = 3,         // 已充滿（接 AC）
};

// 診斷用穩定字串（"no-battery" / "discharging" / "charging" / "full"）。
const char* to_string(ChargeState state) noexcept;

// 人類可讀顯示標籤（"No Battery" / "Discharging" / "Charging" / "Full"），供 MetricValue.text。
std::string charge_state_label(ChargeState state);

// 充電狀態的數值碼（供 power.battery.state 指標的 number 維度）。
inline double charge_state_code(ChargeState state) noexcept {
    return static_cast<double>(static_cast<int>(state));
}

// ---------------------------------------------------------------------------
// PowerStatus：某一時刻的電源 / 電池狀態快照（平台中立值模型）
// ---------------------------------------------------------------------------
// 提供者面對的統一狀態形狀（無論來自真實後端或注入式假來源）：
//   - valid              false = 「目前無讀值」（尚未取樣 / 感測失敗）。消費者據此顯示為
//                        未知，而非把 0 誤當真實讀值。valid==true 表「成功讀到電源系統」。
//   - on_ac_power        是否接外部電源（AC）。
//   - state              充電狀態（含 NoBattery——桌機成功讀到但無電池）。
//   - percent            電池電量 % [0,100]；nullopt = 無電池 / 電量未知。
//   - minutes_remaining  剩餘時間估計(分鐘)；nullopt = 未知（計算中 / 已滿 / 無電池）。
//   - cycle_count        循環次數（可選）；nullopt = 來源未提供。
//   - health_percent     健康度 % [0,100]（可選）；nullopt = 來源未提供。
//
// 各電池專屬欄位以 optional 表達「有無此讀值」，故無電池桌機自然表達為：valid==true、
// state==NoBattery、on_ac_power==true、其餘 optional 皆 nullopt。
struct PowerStatus {
    bool valid = false;
    bool on_ac_power = false;
    ChargeState state = ChargeState::NoBattery;
    std::optional<double> percent;
    std::optional<int> minutes_remaining;
    std::optional<int> cycle_count;
    std::optional<double> health_percent;

    // 明確的「無讀值」（保守預設）。
    static PowerStatus unknown() { return PowerStatus{}; }

    // 無電池桌機：成功讀到電源系統，但無電池（電量 / 剩餘時間等為未知）。
    // 預設接 AC（桌機恆接市電）；可傳 false 表示斷電情境。
    static PowerStatus no_battery(bool on_ac = true) {
        PowerStatus s;
        s.valid = true;
        s.on_ac_power = on_ac;
        s.state = ChargeState::NoBattery;
        return s;
    }

    // 便利：一顆電池的常見讀值（電量 + 充電狀態 + 是否接電源 + 可選剩餘分鐘）。
    static PowerStatus battery(double percent_value, ChargeState st, bool on_ac,
                               std::optional<int> minutes = std::nullopt) {
        PowerStatus s;
        s.valid = true;
        s.on_ac_power = on_ac;
        s.state = st;
        s.percent = percent_value;
        s.minutes_remaining = minutes;
        return s;
    }

    bool operator==(const PowerStatus& o) const {
        return valid == o.valid && on_ac_power == o.on_ac_power && state == o.state &&
               percent == o.percent && minutes_remaining == o.minutes_remaining &&
               cycle_count == o.cycle_count && health_percent == o.health_percent;
    }
    bool operator!=(const PowerStatus& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// PowerStatSource：電源 / 電池狀態的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 PowerStatus。實作決定其來源（注入式假來源 /
// 真實後端）。sample() **非 const**：序列型來源每次取樣會推進內部游標，故取樣具副作用。
class PowerStatSource {
public:
    virtual ~PowerStatSource() = default;

    // 取一份目前電源 / 電池狀態快照。無讀值時回 PowerStatus::unknown()。
    virtual PowerStatus sample() = 0;

protected:
    PowerStatSource() = default;
    PowerStatSource(const PowerStatSource&) = default;
    PowerStatSource& operator=(const PowerStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullPowerStatSource：相位 1 的 null / 假「固定狀態」來源
// ---------------------------------------------------------------------------
// **不接任何真實電源 API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入固定
// PowerStatus 供測試與假感測器情境（含以 no_battery() 模擬桌機）。真實查詢留待後端相位
// ——本類永不含平台呼叫。
class NullPowerStatSource : public PowerStatSource {
public:
    NullPowerStatSource() = default;
    explicit NullPowerStatSource(PowerStatus fixed) : fixed_(std::move(fixed)) {}

    // 注入 / 覆寫整份狀態快照。
    void set_status(PowerStatus status) { fixed_ = std::move(status); }

    // 回到「無讀值」預設（null 期誠實語意）。
    void clear() { fixed_ = PowerStatus::unknown(); }

    // 目前注入的狀態（唯讀）。
    const PowerStatus& status() const noexcept { return fixed_; }

    // 回目前注入的狀態快照（決定性；無副作用，直接讀路徑）。
    PowerStatus sample() override { return fixed_; }

private:
    PowerStatus fixed_ = PowerStatus::unknown();
};

// ---------------------------------------------------------------------------
// SequencedPowerStatSource：相位 1 的 null / 假「序列狀態」來源
// ---------------------------------------------------------------------------
// **不接任何真實電源 API**。持一列注入的狀態快照（模擬時間推進下的狀態演變，如充電狀態
// 轉換、電量下降），每次 sample() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。
// 空列預設回無讀值。用於驗充電狀態轉換 / 電量序列 / 經 E2-02 週期採樣鋪成歷史。
class SequencedPowerStatSource : public PowerStatSource {
public:
    SequencedPowerStatSource() = default;
    explicit SequencedPowerStatSource(std::vector<PowerStatus> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫整條狀態序列（重置游標到起點）。
    void set_sequence(std::vector<PowerStatus> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份狀態快照到序列尾。
    void push_status(PowerStatus s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份狀態快照；列盡回最後一份；空列回無讀值。
    PowerStatus sample() override;

private:
    std::vector<PowerStatus> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// PowerProvider：把電源 / 電池狀態掛成一組指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上一組**單一實例**指標
// （電量 / 充電狀態 / 接電源 / 剩餘時間 / 循環次數 / 健康度）——各維度單位 / 值域不同，故各
// 為獨立指標（非同一指標的多實例）。因電源狀態變動慢，本提供者建議以 **E2-02 的低頻分級**
// 採集（`SamplingTier::Low`，E2-02 註解即以「電池健康」為低頻例）：呼叫端把各 metric_id 與
// sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新讀來源
// 並更新各指標。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及
// 本型別。
class PowerProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.power";

    // 各指標識別碼（穩定、跨平台一致）。
    static constexpr const char* kLevelId = "power.battery.level";
    static constexpr const char* kStateId = "power.battery.state";
    static constexpr const char* kAcId = "power.ac.online";
    static constexpr const char* kTimeId = "power.battery.time_remaining";
    static constexpr const char* kCyclesId = "power.battery.cycles";
    static constexpr const char* kHealthId = "power.battery.health";

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：電源狀態變動慢，屬低頻（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Low;

    // 接 AC 的數值碼（power.ac.online 的 number 維度）：1 = 接、0 = 未接。
    static constexpr double kAcOnline = 1.0;
    static constexpr double kAcOffline = 0.0;

    // 以一個電源狀態來源建構。source 為 null 時，提供者仍會掛上全部指標，且各以「未知」
    // （valid==false）呈現（保守而不崩、不謊報）。history 為各實例歷史環容量、tier 為建議
    // 採集分級。
    explicit PowerProvider(std::shared_ptr<PowerStatSource> source,
                           std::size_t history = kDefaultHistory,
                           ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上全部電源 / 電池指標：取一份狀態、建各單一實例指標、填初值，並保留指標
    // 參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新狀態寫入各指標（有效數值推入歷史）。呼叫端在 E2-02 排程器判定本組
    // 指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    void sample();

    // 目前注入來源的狀態快照（source 為 null 時視為無讀值）。診斷 / 測試便利。
    PowerStatus current() {
        return source_ ? source_->sample() : PowerStatus::unknown();
    }

private:
    // 一個單一實例指標 + 其唯一實例參照（供更新）。
    struct MetricSlot {
        std::shared_ptr<ds::metrics::InMemoryMetric> metric;
        ds::metrics::InMemoryMetricInstance* inst = nullptr;
    };

    // 建一個單一實例指標並登記到 registry（沿用 E2-01 記憶體內實作，不自造模型）。
    MetricSlot make_metric(ds::metrics::MetricRegistry& registry,
                           const char* id, std::string name, std::string unit,
                           ds::metrics::MetricRange range);

    // 把一個 MetricValue 寫入某槽的實例：valid 決定是否為未知 / 是否推歷史。
    void write(MetricSlot& slot, const ds::metrics::MetricValue& v, bool to_history);

    // 把一份狀態寫入全部指標。to_history 為真時有效值推入歷史（採集路徑），為假時只設值。
    void apply(const PowerStatus& status, bool to_history);

    std::shared_ptr<PowerStatSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。
    MetricSlot level_;
    MetricSlot state_;
    MetricSlot ac_;
    MetricSlot time_;
    MetricSlot cycles_;
    MetricSlot health_;
    bool registered_ = false;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_09_POWER_STATUS_HPP
