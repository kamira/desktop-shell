// E2-17 主機板感測器（溫度 / 風扇 / 電壓）— sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「主機板 / 硬體感測器指標」——溫度(°C，多點) / 風扇轉速(RPM) / 電壓軌(V)
// ——透過 **E2-01 的 MetricProvider 介面**掛成三個指標，並以 **E2-02 的採集頻率分級**
// 決定採樣節奏。每一顆感測器（某一溫度點 / 某一風扇 / 某一電壓軌）為對應指標貢獻一個
// **可列舉實例（附名稱與類型）**；三類感測器各自獨立列舉、數量可在取樣間變動。這是
// 「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者——它
// **消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實感測器 API**（無 SMC /
//     lm-sensors / IOKit / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作抽象來源
//     `BoardSensorSource`，提供者一行不動。
//   - 來源以**可注入的 BoardSensorSource 抽象**表達：一次 sample() 列舉全部感測器，
//     依類型分三組（溫度 / 風扇 / 電壓），每顆給一份讀值（附名稱、帶 valid 旗標可獨立缺讀）。
//     相位 1 只有 `NullBoardSensorSource`（注入固定 / 序列資料），預設回「無感測器 / 無讀值」。
//   - **無讀值誠實回 invalid、不謊報 0**：某顆感測器無讀值即以 E2-01 `MetricValue` 的
//     valid==false（未知）表達，消費者據此顯示為未知，而非把 0 誤當真實讀值。
//
// 關於 E10-01（第三個宣告相依）：E10-01 為**本機 IPC 訊息投遞**（行程內佇列 + 發布訂閱），
// **非**感測器 / 裝置列舉基礎設施，故本單元的感測器列舉自帶可注入抽象（如上），不依賴其
// 公開型別；CMakeLists 仍連結 e10_01 以忠實反映宣告的相依邊（見該檔說明）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - "board.temp"    — name "Board Temperature"、unit "°C"、range at_least(0)（溫度下界 0、
//                       上不設假界——溫度無自然上限）
//   - "board.fan"     — name "Board Fan Speed"、unit "RPM"、range at_least(0)（轉速 >= 0、
//                       上不設假界）
//   - "board.voltage" — name "Board Voltage"、unit "V"、range unbounded（電壓軌可為負軌
//                       如 -12V，故上下皆不設界；需正規化的消費者自處理 nullopt）
//   三類感測器各有自己的一組可列舉實例：溫度 "temp0" / "temp1" / …、風扇 "fan0" / …、
//   電壓 "volt0" / …（label 取感測器名稱，未命名則以 "Temp 0" / "Fan 0" / "Voltage 0" 補位）。
//   各實例保留時序歷史（history_capacity>0），配合 E2-02 週期採集鋪成序列。無感測器（如 null
//   期預設）時每個指標為 0 實例——誠實表達。
#ifndef DS_MODULES_E2_17_BOARD_SENSORS_HPP
#define DS_MODULES_E2_17_BOARD_SENSORS_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// SensorType：主機板感測器的類型（溫度 / 風扇 / 電壓）
// ---------------------------------------------------------------------------
// 類型決定該顆感測器屬哪個指標、單位為何。列舉實例「附名稱與類型」即靠本列舉表達類型。
enum class SensorType {
    Temperature,  // °C
    Fan,          // RPM
    Voltage,      // V
};

// 該類型的單位字串（"°C" / "RPM" / "V"）。供提供者掛指標與診斷用。
const char* unit_for(SensorType type) noexcept;

// 診斷用穩定字串（"temperature" / "fan" / "voltage"）。
const char* to_string(SensorType type) noexcept;

// ---------------------------------------------------------------------------
// SensorReading：單一顆感測器的一份讀值（平台中立值）
// ---------------------------------------------------------------------------
// 一顆感測器 = 一個名稱（如 "CPU" / "System" / "+12V" / "CPU Fan"）+ 一個數值 + valid 旗標。
// name 可為空（未命名——提供者以類型 + 序號補位）。value 的單位依所屬類型（°C / RPM / V）。
// valid==false = 無讀值（該顆感測器存在但此刻讀不到）。
// 刻意保持為 aggregate（可 `{name, value, valid}` 初始化），另附靜態工廠與相等運算子。
struct SensorReading {
    std::string name;        // 感測器名稱（可空）
    double value = 0.0;      // 讀值（°C / RPM / V，依類型）
    bool valid = false;      // false = 無讀值（未知），預設保守

    // 有效讀值（附名稱）。
    static SensorReading of(std::string name, double v) {
        return SensorReading{std::move(name), v, true};
    }
    // 具名但無讀值（感測器存在、此刻讀不到）。
    static SensorReading unknown(std::string name) {
        return SensorReading{std::move(name), 0.0, false};
    }

    bool operator==(const SensorReading& o) const {
        return valid == o.valid && value == o.value && name == o.name;
    }
    bool operator!=(const SensorReading& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// BoardSensorSample：某一時刻**全部主機板感測器**的讀值快照（依類型分三組）
// ---------------------------------------------------------------------------
// temperatures[i] / fans[i] / voltages[i] = 該類第 i 顆感測器的讀值。三類各自的感測器數
// 可在取樣間獨立變動（感測器上下線 / 驅動變化）——提供者據此動態增減各類實例。
struct BoardSensorSample {
    std::vector<SensorReading> temperatures;  // °C
    std::vector<SensorReading> fans;          // RPM
    std::vector<SensorReading> voltages;      // V

    std::size_t temperature_count() const noexcept { return temperatures.size(); }
    std::size_t fan_count() const noexcept { return fans.size(); }
    std::size_t voltage_count() const noexcept { return voltages.size(); }
    std::size_t total_count() const noexcept {
        return temperatures.size() + fans.size() + voltages.size();
    }
    bool empty() const noexcept { return total_count() == 0; }

    bool operator==(const BoardSensorSample& o) const {
        return temperatures == o.temperatures && fans == o.fans && voltages == o.voltages;
    }
    bool operator!=(const BoardSensorSample& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// BoardSensorSource：主機板感測器讀值的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 列舉全部感測器，回一份 BoardSensorSample。實作決定其
// 來源（注入式假來源 / 真實後端）。sample() **非 const**：序列型來源每次取樣會推進內部游標，
// 故取樣具副作用。
class BoardSensorSource {
public:
    virtual ~BoardSensorSource() = default;

    // 取一份目前的全感測器讀值快照。無感測器 / 無讀值時回空 / 未知快照。
    virtual BoardSensorSample sample() = 0;

protected:
    BoardSensorSource() = default;
    BoardSensorSource(const BoardSensorSource&) = default;
    BoardSensorSource& operator=(const BoardSensorSource&) = default;
};

// ---------------------------------------------------------------------------
// NullBoardSensorSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實感測器 API**。持一列注入的 BoardSensorSample（可為單份「固定」或多份
// 「序列」），每次 sample() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。空列（預設）
// 回空快照——Mac / null 期的誠實預設「無感測器 / 無讀值」。真實查詢留待後端相位——本類永不
// 含平台呼叫。
class NullBoardSensorSource : public BoardSensorSource {
public:
    NullBoardSensorSource() = default;

    // 固定來源：單份快照，每次 sample() 皆回它（列盡回最後一份 = 恆回同一份）。
    explicit NullBoardSensorSource(BoardSensorSample fixed) {
        sequence_.push_back(std::move(fixed));
    }

    // 序列來源：多份快照（模擬時間推進下的變動），每次 sample() 回下一份。
    explicit NullBoardSensorSource(std::vector<BoardSensorSample> sequence)
        : sequence_(std::move(sequence)) {}

    // 設為固定單份快照（重置游標）。
    void set_fixed(BoardSensorSample s) {
        sequence_.clear();
        sequence_.push_back(std::move(s));
        cursor_ = 0;
    }
    // 注入 / 覆寫整條序列（重置游標到起點）。
    void set_sequence(std::vector<BoardSensorSample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份快照到序列尾。
    void push_sample(BoardSensorSample s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }
    // 回到「無感測器 / 無讀值」預設（清空序列）。
    void clear() noexcept {
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份快照；列盡回最後一份；空列回空快照。
    BoardSensorSample sample() override;

private:
    std::vector<BoardSensorSample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// BoardSensorsProvider：把溫度 / 風扇 / 電壓掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上三個指標
// "board.temp" / "board.fan" / "board.voltage"，各以對應類型的每顆感測器為一個可列舉實例
// （附名稱）。因感測器讀值會隨時間變動，本提供者建議以 **E2-02 的週期分級**採集：呼叫端把各
// metric_id 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample()
// 重新讀來源並更新各實例。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，
// 完全不觸及本型別。
class BoardSensorsProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.board";

    // 三個指標識別碼 / 顯示名。
    static constexpr const char* kTempId = "board.temp";
    static constexpr const char* kFanId = "board.fan";
    static constexpr const char* kVoltageId = "board.voltage";
    static constexpr const char* kTempName = "Board Temperature";
    static constexpr const char* kFanName = "Board Fan Speed";
    static constexpr const char* kVoltageName = "Board Voltage";

    // 單位：溫度 °C、風扇 RPM、電壓 V。
    static constexpr const char* kTempUnit = "°C";
    static constexpr const char* kFanUnit = "RPM";
    static constexpr const char* kVoltageUnit = "V";

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：主機板感測器變動慢（溫度 / 電壓 / 轉速非跟手需求），屬低頻。
    static constexpr ds::metrics::SamplingTier kDefaultTier = ds::metrics::SamplingTier::Low;

    // 以一個感測器讀值來源建構。source 為 null 時，提供者仍會掛上三個指標，且皆以 0 實例
    // （無感測器）呈現（保守而不崩、不謊報）。history 為各實例歷史環容量、tier 為建議採集分級。
    explicit BoardSensorsProvider(std::shared_ptr<BoardSensorSource> source,
                                  std::size_t history = kDefaultHistory,
                                  ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端對三個 metric_id add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上三個指標：取一份讀值、依各類感測器數建立實例、填初值，並保留指標參照供
    // 日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新讀值寫入各實例（有效值推入歷史）。呼叫端在 E2-02 排程器判定本指標
    // 該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣的某類感測器數多於既有實例，會**動態新增**實例（既有參照不失效）；
    // 若較少，多出的實例設為未知（不污染歷史）。
    void sample();

    // 目前各類感測器的實例數。register_metrics 前皆為 0。
    std::size_t temperature_count() const noexcept { return temp_.insts.size(); }
    std::size_t fan_count() const noexcept { return fan_.insts.size(); }
    std::size_t voltage_count() const noexcept { return voltage_.insts.size(); }
    // 三類實例總數。
    std::size_t sensor_count() const noexcept {
        return temp_.insts.size() + fan_.insts.size() + voltage_.insts.size();
    }

private:
    // 一組（單一類型）指標 + 其實例向量 + 命名前綴，便於三類共用同一套更新邏輯。
    struct Group {
        std::shared_ptr<ds::metrics::InMemoryMetric> metric;
        std::vector<ds::metrics::InMemoryMetricInstance*> insts;
        const char* id_prefix;      // 實例 id 前綴（"temp" / "fan" / "volt"）
        const char* label_prefix;   // 未命名時的 label 前綴（"Temp" / "Fan" / "Voltage"）
    };

    // 把一份快照寫入三組。to_history 為真時有效值推入歷史（採集路徑），為假時只設值不動歷史。
    void apply(const BoardSensorSample& sample, bool to_history);

    // 把某類的一列讀值寫入對應 Group：必要時動態擴增實例（以讀值名稱命名），逐顆寫值；
    // 多出的既有實例（本次少了）設未知。
    void apply_group(Group& g, const std::vector<SensorReading>& readings, bool to_history);

    // 確保 Group 至少有 count 個實例（不足則以 readings 對應名稱建立；既有參照不失效）。
    void ensure_instances(Group& g, const std::vector<SensorReading>& readings,
                          std::size_t count);

    // 把一個純數值（valid 決定是否未知 / 是否推歷史）寫入某實例。
    void write_value(ds::metrics::InMemoryMetricInstance* inst, double number, bool valid,
                     bool to_history);

    // 目前讀值快照：source_ 為 null 時視為空（無感測器 / 無讀值）。
    BoardSensorSample current() { return source_ ? source_->sample() : BoardSensorSample{}; }

    std::shared_ptr<BoardSensorSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。三組各自獨立列舉；insts 為非擁有指標，指向各 metric 內的實例（unique_ptr 持有，
    // 新增更多實例時既有參照不失效）。
    Group temp_{nullptr, {}, "temp", "Temp"};
    Group fan_{nullptr, {}, "fan", "Fan"};
    Group voltage_{nullptr, {}, "volt", "Voltage"};
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_17_BOARD_SENSORS_HPP
