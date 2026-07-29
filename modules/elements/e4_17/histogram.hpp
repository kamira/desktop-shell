// E4-17 直方圖 — 視覺直方圖 / 時序長條元件的「渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：把一序列數值（如歷史 CPU / GPU 用量）映射成直方圖 / 時序長條的**渲染描述模型**——
// 每個 bin / 樣本 → 一根長條（bar），含 y 軸縮放、可選滾動視窗（只保留最新 N 筆）、可選閾值線。
// 這是最終 CPU/GPU Widget（歷史用量直方圖）的視覺元件基礎。
//
// 相位 1**不做真實繪製**：本單元只產出**渲染描述**（`HistogramRenderModel`）供後續相位的繪製層
// 消費——不觸碰任何 OS / 繪圖 API、無 `#ifdef` / `win32` / `cocoa`。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 長條**不以像素 (x,y) 座標**表達，而以**正規化幅值** `magnitude ∈ [0,1]`（該樣本佔 y 軸
//     值域的比例）＋**清單順序**（list index = 時間序，最舊→最新）表達；順序是語意排序而非數字
//     z-order / 座標。
//   - 閾值線同樣以正規化幅值 [0,1] 表達。
//   - `HistogramRange{min,max}` 是**資料值域**（用量的語意範圍，非螢幕座標），用於 y 軸縮放。
//   - 合成不透明度沿用上游 E1-03 `AlphaProfile`（opacity 為比例 [0,1]，非座標），描述此半透明
//     桌面 widget 應如何被合成——仍符合 NFR-02。
//
// 不靜默失敗（NFR-04 精神）：
//   - 空序列 → 明確以 `HistogramRenderModel::empty=true` 且 `bars` 為空回報，不靜默假裝有資料。
//   - 超出值域的樣本 → **夾限**至 [min,max] 並在該 bar 標記 `clamped=true`（明確，不靜默吞掉）。
//   - 非有限值（NaN/Inf）、無效值域（min>=max）等前置條件違反 → 回 `HistogramStatus::Invalid`，
//     **不**部分套用、不靜默改成預設值。
//
// 設定以上游 E7-03 宣告式「段落變數」驅動：呼叫端可用一份宣告式文件（`${...}` 變數先由 E7-03
// `expand()` 展開）設定值域 / 容量 / 閾值 / 序列——見 `configure_from_document()`。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_17_HISTOGRAM_HPP
#define DS_ELEMENTS_E4_17_HISTOGRAM_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaProfile / AlphaMode（合成描述）
#include "document.hpp"       // E7-01（經 E7-03 傳遞）：Value / Document
#include "section_vars.hpp"   // E7-03（上游，可讀不可改）：段落變數 expand()（宣告式設定驅動）

namespace ds::elements {

// 直方圖樣本值型別（資料值域，如 CPU 用量百分比）。
using HistogramSample = double;

// 直方圖預設滾動視窗容量：0 = 無上限（不丟棄任何樣本）。
inline constexpr std::size_t kHistogramUnbounded = 0;

// y 軸縮放用的**資料值域**（非螢幕座標）。magnitude = (value - min) / (max - min)。
struct HistogramRange {
    double min = 0.0;
    double max = 1.0;

    bool operator==(const HistogramRange& o) const noexcept {
        return min == o.min && max == o.max;
    }
    bool operator!=(const HistogramRange& o) const noexcept { return !(*this == o); }
};

// 操作結果碼 —— 與上游各層 Status 同精神（明確、不靜默）。
enum class HistogramStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（非有限值、無效值域 min>=max 等）；不部分套用
};

// 渲染描述中的**一根長條**——純資料、正規化、無絕對座標。
struct HistogramBar {
    double value = 0.0;      // 原始樣本值（資料值域，供繪製層需要時參照）。
    float magnitude = 0.0f;  // 正規化幅值 [0,1]：該樣本佔 y 軸值域的比例（NFR-02：比例非座標）。
    bool clamped = false;    // 原始值是否落在 [min,max] 之外而被夾限（明確標記，不靜默）。
};

// 渲染描述中的**可選閾值線**（如「用量 80% 警戒線」）。
struct HistogramThreshold {
    bool present = false;    // 是否設有閾值線。
    double value = 0.0;      // 閾值的資料值域值。
    float magnitude = 0.0f;  // 正規化幅值 [0,1]（NFR-02：比例非座標）。
    bool clamped = false;    // 閾值是否落在值域之外而被夾限。
};

// 直方圖的**渲染描述模型**——供後續相位的繪製層消費。相位 1 不含任何真實繪製。
// 所有位置皆為正規化幅值 / 清單順序（NFR-02：無絕對座標 / 無數字 z-order）。
struct HistogramRenderModel {
    HistogramRange range;                 // 回傳目前 y 軸值域（資料值域）。
    std::vector<HistogramBar> bars;       // 長條，依時間序（清單順序 = 最舊→最新）。
    HistogramThreshold threshold;         // 閾值線（present=false 表未設定）。
    ds::kernel::AlphaProfile composite;   // 合成描述（此半透明 widget 如何被合成；opacity 為比例）。
    bool empty = true;                    // 空序列明確旗標（bars 為空時為 true）。
};

// ---------------------------------------------------------------------------
// HistogramElement —— 直方圖渲染描述模型的建構者。
//
// 累積一序列樣本（可選滾動視窗只保留最新 N 筆），依具名值域做 y 軸縮放，可選閾值線與合成
// 不透明度，最後產出 `HistogramRenderModel` 供繪製層消費。純邏輯、平台中立、無真實繪製。
// ---------------------------------------------------------------------------
class HistogramElement {
public:
    HistogramElement() = default;

    // --- 值域設定（y 軸縮放）---
    // 設定資料值域 [min,max]。min/max 非有限值或 min>=max → Invalid（不套用）。成功 → Ok。
    HistogramStatus set_range(double min, double max);
    HistogramStatus set_range(const HistogramRange& r) { return set_range(r.min, r.max); }
    const HistogramRange& range() const noexcept { return range_; }

    // --- 滾動視窗容量 ---
    // 設定只保留最新 N 筆樣本的容量；0（kHistogramUnbounded）= 無上限。
    // 若目前樣本數超過新容量，立即丟棄較舊者只留最新 N 筆。恆成功回 Ok。
    HistogramStatus set_capacity(std::size_t capacity);
    std::size_t capacity() const noexcept { return capacity_; }

    // --- 閾值線 ---
    // 設定閾值線的資料值域值。非有限值 → Invalid（不套用）。成功 → Ok。
    HistogramStatus set_threshold(double value);
    // 移除閾值線。
    void clear_threshold() noexcept { has_threshold_ = false; threshold_ = 0.0; }
    bool has_threshold() const noexcept { return has_threshold_; }

    // --- 合成不透明度（沿用 E1-03 AlphaProfile；opacity 為比例，符合 NFR-02）---
    // 設定此半透明 widget 的合成描述。opacity 非有限值 → Invalid（不套用）；否則夾限至 [0,1]。
    HistogramStatus set_composite(const ds::kernel::AlphaProfile& composite);
    const ds::kernel::AlphaProfile& composite() const noexcept { return composite_; }

    // --- 序列輸入 ---
    // 追加一筆樣本（最新）。非有限值 → Invalid（不追加）。若設有容量且超出，丟棄最舊者（滾動視窗）。
    HistogramStatus push_sample(double v);
    // 以整組數值取代序列（最舊→最新）。任一元素非有限值 → Invalid 且**完全不套用**（不部分寫入）。
    // 若設有容量且元素數超出，只保留最新 N 筆。成功 → Ok。
    HistogramStatus set_series(const std::vector<double>& values);
    // 清空所有樣本（保留值域 / 容量 / 閾值 / 合成設定）。
    void clear() noexcept { samples_.clear(); }

    std::size_t sample_count() const noexcept { return samples_.size(); }
    const std::vector<double>& samples() const noexcept { return samples_; }

    // --- 渲染描述 ---
    // 產出目前狀態的渲染描述模型：每筆樣本 → 一根正規化長條（超範圍夾限並標記），
    // 附閾值線（若有）與合成描述。空序列 → empty=true 且 bars 為空（明確，不靜默）。
    HistogramRenderModel render_model() const;

private:
    // 依目前容量把 samples_ 修剪為只保留最新 N 筆（capacity_==0 則不修剪）。
    void trim_to_capacity();

    HistogramRange range_{0.0, 1.0};
    std::size_t capacity_ = kHistogramUnbounded;
    bool has_threshold_ = false;
    double threshold_ = 0.0;
    ds::kernel::AlphaProfile composite_{};  // 預設：PerPixel、opacity=1.0（半透明桌面 widget）。
    std::vector<double> samples_;
};

// ---------------------------------------------------------------------------
// 宣告式設定驅動（消費上游 E7-03 段落變數）
// ---------------------------------------------------------------------------

// 設定套用錯誤 —— 明確可讀，不靜默。
struct HistogramConfigError {
    std::string message;
};

// 依一份**已解析**的宣告式 Value（Map）設定一個 HistogramElement。可辨識鍵（皆可選）：
//   range:     { min: <num>, max: <num> }   —— y 軸值域
//   capacity:  <int >= 0>                    —— 滾動視窗容量（0 = 無上限）
//   threshold: <num>                         —— 閾值線
//   series:    [ <num>, ... ]                —— 初始樣本序列（最舊→最新）
// 任一欄位型別錯誤 / 值非法（如非 Map 的 range、負容量、無效值域）→ 回 false 並填 err（不靜默）。
// config 非 Map → false。未出現的鍵維持 out 現況。成功 → true。
bool configure(const ds::format::Value& config, HistogramElement& out, HistogramConfigError& err);

// 依一份宣告式 Document 設定 HistogramElement：**先以 E7-03 `expand()` 展開段落變數**
//（文件內 `vars:` 段落 + `${...}` 引用），再把展開後的 root 交給 `configure()`。
// 這示範「宣告一次、處處引用」的 E7-03 設定驅動。展開失敗（未定義 / 循環引用等）或設定
// 欄位非法 → 回 false 並填 err（帶上游訊息，不靜默）。成功 → true。
bool configure_from_document(const ds::format::Document& doc, HistogramElement& out,
                             HistogramConfigError& err);

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_17_HISTOGRAM_HPP
