// E2-15 音訊峰值與頻譜 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「音訊視覺化資料」——目前輸出/輸入音訊的**峰值電平**（peak）、**RMS 電平**、
// 以及**頻譜頻段能量**（N 個 band 的強度，供等化器類視覺化）——透過 **E2-01 統一指標介面**
// 掛成指標，並以 **E2-02 採集頻率分級**決定採樣節奏（音訊視覺化宜跟手，屬週期高頻採集）。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者——它
// **消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。本單元是最終「音訊等化器 / 電平表
// Widget」的核心資料來源，故介面刻意乾淨、易組合（電平計算與提供者分離、來源可注入）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）——一個提供者掛上三個指標：
//   - "audio.peak"     單一實例 "peak"（label "Peak"）：峰值電平，線性振幅 [0,1]，
//                      value.text 附 dB 表述（如 "-6.0 dB"）。range=bounded[0,1]。
//   - "audio.rms"      單一實例 "rms"（label "RMS"）：RMS 電平，線性振幅 [0,1]，同附 dB。
//   - "audio.spectrum" **每頻段可列舉實例** "band0"/"band1"/…（label "Band 0"/…）：各頻段
//                      能量 [0,1]，供等化器逐段鋪繪。range=bounded[0,1]。
// 「峰值/RMS 單一實例 + 每頻段可列舉實例」即單元名在 E2-01「可列舉實例」要素上的自然對應。
// 各實例保留時序歷史（history_capacity>0），配合 E2-02 週期採集鋪成波形/等化器序列。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實音訊 API**（無 CoreAudio /
//     AudioUnit / WASAPI / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作抽象來源，
//     提供者一行不動。
//   - 電平計算為平台中立、可注入、可完全單元測試的純算術：
//       * `peak_of` / `rms_of`：由一塊 PCM 樣本（如注入的正弦樣本）算峰值 / RMS。
//       * `linear_to_db`：線性振幅 → 分貝（含靜音下限 floor，避免 -∞）。
//     頻段能量（頻譜）本身需 FFT / 分析器（相位 2+ 後端或分析單元提供），相位 1 **直接注入**
//     頻段陣列——本層不做 FFT。
#ifndef DS_MODULES_E2_15_AUDIO_LEVELS_HPP
#define DS_MODULES_E2_15_AUDIO_LEVELS_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// AudioLevelSample：某一時刻的音訊電平快照（平台中立值）
// ---------------------------------------------------------------------------
// 提供者面對的統一電平形狀（無論來自 PCM 分析或真實後端）：
//   - peak   峰值電平，線性振幅 [0,1]（1.0 = 滿刻度 full-scale）。
//   - rms    RMS 電平，線性振幅 [0,1]。
//   - bands  各頻段能量 [0,1]（bands[i] = 第 i 個等化器頻段）。頻段數可在取樣間變動。
//   - valid  false = 「目前無讀值」（尚未取樣 / 無音訊裝置 / 感測失敗）。消費者據此顯示為
//            未知，而非把 0 誤當成「真實的靜音」讀值。
struct AudioLevelSample {
    double peak = 0.0;
    double rms = 0.0;
    std::vector<double> bands;
    bool valid = false;

    std::size_t band_count() const noexcept { return bands.size(); }

    // 明確的「無讀值」（保守預設）。
    static AudioLevelSample unknown() { return AudioLevelSample{}; }

    bool operator==(const AudioLevelSample& o) const {
        return valid == o.valid && peak == o.peak && rms == o.rms && bands == o.bands;
    }
    bool operator!=(const AudioLevelSample& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// 電平計算（自由函式，獨立可測；平台中立純算術）
// ---------------------------------------------------------------------------
// 峰值：一塊 PCM 樣本中絕對值最大者，夾到 [0,1]。空塊回 0.0（無訊號）。
double peak_of(const std::vector<double>& pcm) noexcept;

// RMS：一塊 PCM 樣本的均方根 = sqrt(mean(x^2))，夾到 [0,1]。空塊回 0.0。
double rms_of(const std::vector<double>& pcm) noexcept;

// 線性振幅 [0,1] → 分貝（20*log10）。full-scale 1.0 → 0 dB。
// 靜音 / 極小值以 floor_db 為下限（避免 -∞；預設 -120 dB）；負振幅取絕對值。
double linear_to_db(double amplitude, double floor_db = -120.0) noexcept;

// ---------------------------------------------------------------------------
// AudioLevelSource：音訊電平的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 AudioLevelSample。實作決定其來源（PCM 分析 /
// 真實後端）。sample() **非 const**：序列型來源每次取樣會推進內部游標，故取樣具副作用。
class AudioLevelSource {
public:
    virtual ~AudioLevelSource() = default;

    // 取一份目前音訊電平快照。無讀值時回 AudioLevelSample::unknown()。
    virtual AudioLevelSample sample() = 0;

protected:
    AudioLevelSource() = default;
    AudioLevelSource(const AudioLevelSource&) = default;
    AudioLevelSource& operator=(const AudioLevelSource&) = default;
};

// ---------------------------------------------------------------------------
// NullAudioLevelSource：相位 1 的 null / 假音訊來源
// ---------------------------------------------------------------------------
// **不接任何真實音訊 API**。持一列注入的電平快照（模擬時間推進下的波形 / 等化器動畫），
// 每次 sample() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。空列預設回無讀值
// （Mac / null 期的誠實預設）。真實查詢留待後端相位——本類永不含平台呼叫。
class NullAudioLevelSource : public AudioLevelSource {
public:
    NullAudioLevelSource() = default;
    explicit NullAudioLevelSource(AudioLevelSample fixed)
        : sequence_{std::move(fixed)} {}
    explicit NullAudioLevelSource(std::vector<AudioLevelSample> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫**單一固定**電平快照（sample() 恆回此份；重置游標）。
    void set_sample(AudioLevelSample s) {
        sequence_.assign(1, std::move(s));
        cursor_ = 0;
    }

    // 注入 / 覆寫整條電平序列（重置游標到起點）。
    void set_sequence(std::vector<AudioLevelSample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }

    // 追加一份電平快照到序列尾。
    void push_sample(AudioLevelSample s) { sequence_.push_back(std::move(s)); }

    // 便利：由一塊 PCM 樣本（如正弦樣本）算 peak / rms，並注入指定頻段能量，設為單一固定
    // 快照。這即「注入固定 / 序列，如正弦樣本」路徑——峰值 / RMS 由純算術得出、頻段直接給。
    void set_pcm(const std::vector<double>& pcm, std::vector<double> bands = {});

    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    // 回到「無讀值」預設（null 期誠實語意）。
    void clear() {
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份電平快照；列盡回最後一份；空列回無讀值。
    AudioLevelSample sample() override;

private:
    std::vector<AudioLevelSample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// AudioLevelProvider：把音訊峰值 / RMS / 頻譜掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上三個指標：
// "audio.peak"（單一實例）、"audio.rms"（單一實例）、"audio.spectrum"（每頻段可列舉實例）。
// 因音訊電平會隨時間快速變動，本提供者建議以 **E2-02 的週期分級**採集（宜跟手，預設 High）：
// 呼叫端把 metric id 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫
// sample() 重新讀來源並更新各實例（電平推入歷史）。消費者（掛件）只透過 E2-01 的
// MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class AudioLevelProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.audio";

    // 三個指標識別碼 / 顯示名。
    static constexpr const char* kPeakMetricId = "audio.peak";
    static constexpr const char* kRmsMetricId = "audio.rms";
    static constexpr const char* kSpectrumMetricId = "audio.spectrum";
    static constexpr const char* kPeakMetricName = "Audio Peak";
    static constexpr const char* kRmsMetricName = "Audio RMS";
    static constexpr const char* kSpectrumMetricName = "Audio Spectrum";

    // 單一實例的穩定識別碼 / 顯示名。
    static constexpr const char* kPeakInstance = "peak";
    static constexpr const char* kPeakLabel = "Peak";
    static constexpr const char* kRmsInstance = "rms";
    static constexpr const char* kRmsLabel = "RMS";

    // 電平 / 頻段能量皆為線性 [0,1]（無單位；峰值 / RMS 另以 value.text 附 dB 表述）。
    static constexpr const char* kUnit = "";

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成波形 / 等化器序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：音訊視覺化宜跟手，屬高頻（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::High;

    // 以一個音訊電平來源建構。source 為 null 時，提供者仍會掛上三個指標，且各實例以
    // 「未知」（valid==false）呈現、無頻段實例（保守而不崩、不謊報 0）。history 為各實例
    // 歷史環容量、tier 為建議採集分級。
    explicit AudioLevelProvider(std::shared_ptr<AudioLevelSource> source,
                                std::size_t history = kDefaultHistory,
                                ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上三個指標：取一份電平、建 peak / rms 單一實例 + 每頻段實例、填初值，並保留
    // 指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新電平寫入各實例（電平推入歷史）。呼叫端在 E2-02 排程器判定本指標該
    // 採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣的頻段數多於既有頻段實例，會**動態新增**頻段實例（既有參照不失效）；
    // 若較少，多出的頻段實例設為未知（不污染歷史）。
    void sample();

    // 目前的頻段實例數。register_metrics 前為 0。
    std::size_t band_count() const noexcept { return band_insts_.size(); }

private:
    // 把一份電平寫入各實例。to_history 為真時電平推入歷史（採集路徑），為假時只設值不動歷史。
    // 必要時動態擴增頻段實例。
    void apply(const AudioLevelSample& lvl, bool to_history);

    // 目前電平快照：source_ 為 null 時視為「無讀值」。
    AudioLevelSample current() {
        return source_ ? source_->sample() : AudioLevelSample::unknown();
    }

    // 把一個線性振幅 [0,1] 寫入電平實例（number=線性、text=dB；valid 決定是否為未知 /
    // 是否推歷史）。
    void write_level(ds::metrics::InMemoryMetricInstance* inst, double amplitude, bool valid,
                     bool to_history);
    // 把一個頻段能量 [0,1] 寫入頻段實例（無 dB 文字）。
    void write_band(ds::metrics::InMemoryMetricInstance* inst, double energy, bool valid,
                    bool to_history);

    std::shared_ptr<AudioLevelSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。非擁有指標指向 metric 內的實例（其壽命由 metric 保證，unique_ptr 持有，故
    // 新增更多頻段實例時既有參照不失效）。
    std::shared_ptr<ds::metrics::InMemoryMetric> peak_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> rms_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> spectrum_metric_;
    ds::metrics::InMemoryMetricInstance* peak_inst_ = nullptr;
    ds::metrics::InMemoryMetricInstance* rms_inst_ = nullptr;
    std::vector<ds::metrics::InMemoryMetricInstance*> band_insts_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_15_AUDIO_LEVELS_HPP
