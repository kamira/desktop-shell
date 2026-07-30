// E4-29 折線圖 — 折線圖元件的「渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：把一序列數值（如歷史 CPU / GPU 用量）映射成**折線圖**的渲染描述模型——每個樣本
// → 一個折線點（點與點依序連線），支援 y 軸縮放、可選滾動視窗（只保留最新 N 筆）、可選
// **多序列**（同一圖表疊加多條線）、可選**平滑**（簡單移動平均）、可選**填充下方區域**。
// 這是最終 CPU/GPU Widget（用量折線圖）的視覺元件基礎。
//
// 與上游 E4-17（直方圖）同屬「時序資料元件」，沿用其**序列/render_model 風格**：純邏輯
// 建構者（累積樣本）＋不可變渲染描述模型（`render_model()` 一次求值）。y 軸值域與操作結果碼
// 直接沿用 E4-17 的型別（`HistogramRange` / `HistogramStatus`，語意相同：資料值域 / Ok-Invalid）。
//
// 相位 1**不做真實繪製**：本單元只產出**渲染描述**（`LineChartRenderModel`）供後續相位的
// 繪製層消費——不觸碰任何 OS / 繪圖 API、無 `#ifdef` / `win32` / `cocoa`。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 折線點**不以像素 (x,y) 座標**表達，而以**正規化座標** `x, y ∈ [0,1]`（相對圖表區）表達：
//     `x` = 該點在序列中的水平位置比例（`index / (count-1)`；單點序列時為 0）；
//     `y` = 該樣本佔 y 軸值域的比例（同 E4-17 的 magnitude 概念，語意為「幅值比例」而非螢幕座標）。
//   - 多序列時，序列彼此的疊加順序以**清單順序**（`render_model().series` 的位置）表達，
//     不是數字 z-order。
//   - y 軸值域沿用上游 E4-17 `HistogramRange`（同款資料值域型別：`min` / `max`，用於 y 軸縮放，
//     非螢幕座標）。
//   - 填充底線恆為正規化幅值 0（對應 `range.min`，圖表區底部之比例位置），非像素座標。
//   - 合成不透明度沿用上游 E1-03 `AlphaProfile`（opacity 為比例 [0,1]，非座標）。
//
// 不靜默失敗（NFR-04 精神）：
//   - 空序列 → 該序列 `LineChartSeriesModel::empty=true` 且 `points` 為空；圖表整體
//     `LineChartRenderModel::empty=true` 僅當**所有**序列皆空（不靜默假裝有資料）。
//   - 超出值域的樣本 → **夾限**至 [0,1] 並在該點標記 `clamped=true`（原始 `value` 仍保留）。
//   - 非有限值（NaN/Inf）、無效值域（min>=max）等前置條件違反 → 回 `LineChartStatus::Invalid`，
//     **不**部分套用、不靜默改成預設值。
//   - 對不存在的具名序列操作、或以空字串 / 重複名稱新增序列 → `LineChartStatus::Invalid`。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_29_LINE_CHART_HPP
#define DS_ELEMENTS_E4_29_LINE_CHART_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（經 e4_17 傳遞，可讀不可改）：AlphaProfile / AlphaMode（合成描述）
#include "histogram.hpp"      // E4-17（上游，可讀不可改）：沿用 HistogramRange / HistogramStatus

namespace ds::elements {

// 折線圖樣本值型別（資料值域，如 CPU 用量百分比）。與 E4-17 HistogramSample 同精神。
using LineChartSample = double;

// y 軸值域沿用 E4-17 的資料值域型別（語意相同：min/max 為資料值域，非螢幕座標）。
using LineChartRange = HistogramRange;

// 操作結果碼 —— 沿用 E4-17 同款語意（Ok / Invalid；明確、不靜默、不部分套用）。
using LineChartStatus = HistogramStatus;

// 折線圖預設滾動視窗容量：0 = 無上限（不丟棄任何樣本）。各序列共用同一容量設定。
inline constexpr std::size_t kLineChartUnbounded = 0;

// 渲染描述中的**一個折線點**——純資料、正規化座標，無絕對像素座標。
struct LineChartPoint {
    double value = 0.0;       // 原始樣本值（資料值域，供繪製層需要時參照）。
    float x = 0.0f;           // 正規化水平位置 [0,1]：index/(count-1)（單點序列時為 0）。
    float y = 0.0f;           // 正規化幅值 [0,1]：該樣本佔 y 軸值域的比例（NFR-02：比例非座標）。
    float smoothed_y = 0.0f;  // 平滑後的正規化幅值；未啟用平滑時等於 `y`。
    bool clamped = false;     // 原始值是否落在 [min,max] 之外而被夾限（明確標記，不靜默）。
};

// 渲染描述中**一序列的下方填充**設定（底線恆對應 y 軸值域下界）。
struct LineChartFill {
    bool enabled = false;   // 是否填充折線與底線之間的區域。
    float baseline = 0.0f;  // 填充底線的正規化幅值（恆為 0：對應 range.min，圖表區底部之比例）。
};

// 渲染描述中的**一條序列**（折線）——依清單順序疊加（NFR-02：順序非數字 z-order）。
struct LineChartSeriesModel {
    std::string name;                    // 序列名稱（預設序列為空字串 ""）。
    std::vector<LineChartPoint> points;  // 折線點，依時間序（清單順序 = 最舊→最新）。
    LineChartFill fill;                  // 此序列的填充描述（跟隨圖表層級設定）。
    bool empty = true;                   // 本序列是否無樣本（明確旗標，不靜默）。
};

// 折線圖的**渲染描述模型**——供後續相位的繪製層消費。相位 1 不含任何真實繪製。
struct LineChartRenderModel {
    LineChartRange range;                     // 目前 y 軸值域（資料值域）。
    std::vector<LineChartSeriesModel> series; // 序列列表；series[0] 恆為預設序列（名稱 ""）。
    bool smoothed = false;                    // 是否套用了平滑（圖表層級設定）。
    ds::kernel::AlphaProfile composite;       // 合成描述（此半透明 widget 如何被合成；opacity 為比例）。
    bool empty = true;                        // 所有序列皆空時為 true（明確，不靜默）。
};

// ---------------------------------------------------------------------------
// LineChartElement —— 折線圖渲染描述模型的建構者。
//
// 累積一或多序列樣本（可選滾動視窗只保留最新 N 筆，各序列共用同一容量），依具名值域做
// y 軸縮放，可選簡單移動平均平滑、可選下方填充與合成不透明度，最後產出 `LineChartRenderModel`
// 供繪製層消費。純邏輯、平台中立、無真實繪製。序列 0（預設序列，名稱 `""`）永遠存在；
// `add_series()` 可新增具名序列做多序列疊加。
// ---------------------------------------------------------------------------
class LineChartElement {
public:
    LineChartElement();

    // --- 值域設定（y 軸縮放，所有序列共用）---
    // 設定資料值域 [min,max]。min/max 非有限值或 min>=max → Invalid（不套用）。成功 → Ok。
    LineChartStatus set_range(double min, double max);
    LineChartStatus set_range(const LineChartRange& r) { return set_range(r.min, r.max); }
    const LineChartRange& range() const noexcept { return range_; }

    // --- 滾動視窗容量（所有序列共用）---
    // 設定只保留最新 N 筆樣本的容量；0（kLineChartUnbounded）= 無上限。
    // 若任一序列目前樣本數超過新容量，立即修剪只留最新 N 筆。恆成功回 Ok。
    LineChartStatus set_capacity(std::size_t capacity);
    std::size_t capacity() const noexcept { return capacity_; }

    // --- 平滑（可選；簡單移動平均，窗口 3，邊界以可得鄰居平均）---
    void set_smoothing(bool enabled) noexcept { smoothing_enabled_ = enabled; }
    bool smoothing_enabled() const noexcept { return smoothing_enabled_; }

    // --- 下方填充（可選；底線恆為 y 軸值域下界，正規化幅值 0）---
    void set_fill(bool enabled) noexcept { fill_enabled_ = enabled; }
    bool fill_enabled() const noexcept { return fill_enabled_; }

    // --- 合成不透明度（沿用 E1-03 AlphaProfile；opacity 為比例，符合 NFR-02）---
    // 設定此半透明 widget 的合成描述。opacity 非有限值 → Invalid（不套用）；否則夾限至 [0,1]。
    LineChartStatus set_composite(const ds::kernel::AlphaProfile& composite);
    const ds::kernel::AlphaProfile& composite() const noexcept { return composite_; }

    // --- 預設序列（序列 0，名稱 ""）輸入 ---
    // 追加一筆樣本（最新）。非有限值 → Invalid（不追加）。若設有容量且超出，丟棄最舊者。
    LineChartStatus push_sample(double v);
    // 以整組數值取代預設序列（最舊→最新）。任一元素非有限值 → Invalid 且**完全不套用**。
    LineChartStatus set_series(const std::vector<double>& values);
    // 清空**所有**序列（含具名序列）的樣本；保留值域/容量/平滑/填充/合成/序列名單。
    void clear() noexcept;

    std::size_t sample_count() const noexcept { return series_[0].samples.size(); }
    const std::vector<double>& samples() const noexcept { return series_[0].samples; }

    // --- 多序列（可選）：具名序列 ---
    // 新增一條具名序列（初始為空）。name 為空字串（保留給預設序列）或已存在（含 ""）→ Invalid。
    LineChartStatus add_series(const std::string& name);
    std::size_t series_count() const noexcept { return series_.size(); }

    // 對指定具名序列輸入（找不到該序列 → Invalid，不追加/不套用；非有限值同樣 → Invalid）。
    LineChartStatus push_sample(const std::string& name, double v);
    LineChartStatus set_series(const std::string& name, const std::vector<double>& values);
    // 具名序列目前樣本數；找不到該序列回 0（與「存在但為空」的 0 需另以 series_count() 分辨存在性）。
    std::size_t sample_count(const std::string& name) const;

    // --- 渲染描述 ---
    // 產出目前狀態的渲染描述模型：每序列每筆樣本 → 一個正規化折線點（超範圍夾限並標記），
    // 依設定附平滑幅值與填充描述，以及合成描述。所有序列皆空 → empty=true（明確，不靜默）。
    LineChartRenderModel render_model() const;

private:
    struct SeriesData {
        std::string name;
        std::vector<double> samples;
    };

    SeriesData* find_series(const std::string& name);
    const SeriesData* find_series(const std::string& name) const;
    void trim_series_to_capacity(SeriesData& s);

    LineChartRange range_{0.0, 1.0};
    std::size_t capacity_ = kLineChartUnbounded;
    bool smoothing_enabled_ = false;
    bool fill_enabled_ = false;
    ds::kernel::AlphaProfile composite_{};  // 預設：PerPixel、opacity=1.0（半透明桌面 widget）。
    std::vector<SeriesData> series_;        // series_[0] 恆為預設序列（名稱 ""）。
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_29_LINE_CHART_HPP
