// E2-27 螢幕像素取樣 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「某個螢幕位置的像素顏色」透過 **E2-01 的 MetricProvider 介面**掛成一個指標，
// 供環境光（ambient light）／取色（colour picking）類掛件消費。取樣的頻率性質沿用
// **E2-02 的採集分級**（SamplingTier）——像素／環境光變動不快，預設 Low 頻。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者，
// **消費 E2-01 + E2-02 契約、不自造指標模型、不自造時鐘**。
//
// 位置以**具名 anchor 表達，絕不用絕對座標**（NFR-02）：取樣點是 `ScreenAnchor` 列舉
// （center / top-left / …），而非 x/y 像素座標，故核心 API 不含任何絕對座標或數字 z-order。
// 具名 anchor 的實際像素落點，留待真實後端相位依當時螢幕幾何解析。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null 後端**：不真的抓螢幕。null 後端預設回「無讀值」，或回
//     注入的假像素供測試與假感測器情境。真實螢幕擷取（win32 / cocoa）留待後端相位。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "screen.pixel"
//   - name  = "Screen Pixel Color"
//   - unit  = ""（值為正規化亮度，無單位）
//   - range = bounded(0,1)（相對亮度正規化到 [0,1]，長條／環境光類元件可直接畫成比例）
//   - **可列舉實例 = 各具名取樣點**：每個 anchor 一個 MetricInstance，
//     instance_id = anchor 穩定字串（如 "center"）、label = 人類可讀（如 "Center"）、
//     value = 相對亮度(number, [0,1]) + 十六進位色碼文字（如 "#FF8800"，供取色），
//     history = 近期亮度環（供環境光趨勢 / sparkline）。
#ifndef DS_MODULES_E2_27_SCREEN_PIXEL_HPP
#define DS_MODULES_E2_27_SCREEN_PIXEL_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 契約（上游，可讀不可改）：採集分級 + 除頻排程

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// PixelColor：一個像素的平台中立顏色（8-bit RGBA）
// ---------------------------------------------------------------------------
// 跨平台一致的最小顏色描述。刻意不含任何平台專屬色彩空間欄位，維持 module 層中立。
struct PixelColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;  // 不透明度；預設完全不透明

    static PixelColor rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return PixelColor{r, g, b, 255};
    }

    // 相對亮度（Rec. 601 luma），正規化到 [0,1]。供環境光 / 亮度型消費者。
    double luminance() const noexcept {
        return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
    }

    // "#RRGGBB"（大寫十六進位，不含 alpha）。供取色型消費者。
    std::string hex() const;

    bool operator==(const PixelColor& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const PixelColor& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// ScreenAnchor：具名取樣位置（**不用絕對座標**，NFR-02）
// ---------------------------------------------------------------------------
// 以九宮格具名 anchor 表達取樣點：核心 API 不含任何 x/y 像素座標或數字 z-order。
// 具名 anchor 對應的實際像素落點，由真實後端相位依當時螢幕幾何解析（本層不碰）。
enum class ScreenAnchor {
    Center,
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// anchor 的穩定字串識別碼（如 "center"、"top-left"）。跨平台一致，用作 instance_id。
const char* to_string(ScreenAnchor anchor) noexcept;

// anchor 的人類可讀標籤（如 "Center"、"Top-Left"）。用作 instance label。
const char* to_label(ScreenAnchor anchor) noexcept;

// ---------------------------------------------------------------------------
// PixelSampleSource：於某具名 anchor 取樣像素的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台後端（相位 2+）實作它以擷取螢幕；相位 1 只有 null 後端。提供者只依賴此抽象
// 介面，故換後端時提供者一行不動。回 nullopt = 「目前無讀值」（未取樣 / 擷取失敗）。
class PixelSampleSource {
public:
    virtual ~PixelSampleSource() = default;

    // 取某具名 anchor 目前的像素顏色。無讀值回 nullopt（消費者據此顯示為未知）。
    virtual std::optional<PixelColor> sample(ScreenAnchor anchor) const = 0;

protected:
    PixelSampleSource() = default;
    PixelSampleSource(const PixelSampleSource&) = default;
    PixelSampleSource& operator=(const PixelSampleSource&) = default;
};

// ---------------------------------------------------------------------------
// NullPixelSampleSource：相位 1 的 null 後端
// ---------------------------------------------------------------------------
// **不抓螢幕**。預設對任何 anchor 回 nullopt（Mac / null 期的誠實預設 = 無讀值）；
// 可用 set_pixel 注入某 anchor 的假像素供測試與假感測器情境。真實擷取留待後端相位。
class NullPixelSampleSource : public PixelSampleSource {
public:
    NullPixelSampleSource() = default;

    // 注入 / 覆寫某 anchor 的假像素。
    void set_pixel(ScreenAnchor anchor, PixelColor color);
    // 清掉某 anchor 的注入值（回到「無讀值」）。有清到回 true。
    bool clear(ScreenAnchor anchor);
    // 清空全部注入值（回到 null 期預設語意：處處無讀值）。
    void clear_all() noexcept;

    // 目前有注入值的 anchor 數。
    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

    // 無讀值回 nullopt；有注入則回該像素。
    std::optional<PixelColor> sample(ScreenAnchor anchor) const override;

private:
    // 小量取樣點，線性表即可（決定性、無雜湊順序問題）。
    std::vector<std::pair<ScreenAnchor, PixelColor>> entries_;
};

// ---------------------------------------------------------------------------
// ScreenPixelProvider：把具名螢幕取樣點的像素顏色掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "screen.pixel"，其可列舉實例即各具名取樣 anchor（每 anchor 一實例）。取樣頻率性質
// 沿用 **E2-02 的 SamplingTier**（預設 Low）；register_demand() 可把本指標的採集需求
// 掛進 E2-02 的 SamplingScheduler（除頻合併由排程器負責）。消費者（掛件）只透過 E2-01
// 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class ScreenPixelProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "screen.pixel";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.screen.pixel";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Screen Pixel Color";
    // 每個取樣點保留的近期亮度歷史環容量（供環境光趨勢 / sparkline）。
    static constexpr std::size_t kDefaultHistoryCapacity = 32;

    // 以取樣後端 + 一組具名 anchor 建構。
    //   - source 為 null → 提供者仍保守掛上指標；各實例值為「未知」，不崩。
    //   - anchors 重複者去重（保留首次出現順序）；空清單 → 掛上 0 實例的指標。
    //   - tier：本指標的採集分級（E2-02），預設 Low（像素 / 環境光變動不快）。
    explicit ScreenPixelProvider(
        std::shared_ptr<PixelSampleSource> source,
        std::vector<ScreenAnchor> anchors,
        ds::metrics::SamplingTier tier = ds::metrics::SamplingTier::Low,
        std::size_t history_capacity = kDefaultHistoryCapacity);

    std::string provider_id() const override { return kProviderId; }

    // 本指標的採集分級（E2-02）。供呼叫端 / 診斷查詢。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 去重後的取樣 anchor 清單（決定性順序 = 建構時首次出現順序）。
    const std::vector<ScreenAnchor>& anchors() const noexcept { return anchors_; }

    // 把本指標的採集需求掛進 E2-02 的排程器（以本提供者的分級）。回傳票根供日後撤銷。
    // 這是「消費 E2-02」的實際掛接：多個消費者對同一指標的需求由排程器除頻合併。
    ds::metrics::DemandId register_demand(ds::metrics::SamplingScheduler& scheduler) const;

    // 對註冊表掛上 "screen.pixel" 指標：每個具名 anchor 建一個實例，取樣後端當下的像素
    // 化為（相對亮度, 色碼文字）。無讀值的 anchor 實例值為未知。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

private:
    std::shared_ptr<PixelSampleSource> source_;
    std::vector<ScreenAnchor> anchors_;  // 已去重
    ds::metrics::SamplingTier tier_;
    std::size_t history_capacity_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_27_SCREEN_PIXEL_HPP
