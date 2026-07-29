// E4-03 長條 / 進度 / 量表 — 視覺元件的「渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：把一個（或多個）數值依「範圍」映射成長條圖 / 進度條 / 量表(gauge) 的**渲染描述**：
//   - Bar（長條）：值 → 長度**比例** [0,1]（依 min/max 範圍映射）。
//   - Progress（進度）：0–100% 的完成度（本質即 [0,1] 比例，以百分比呈現）。
//   - Gauge（量表）：值 → **角度 / 弧**（依起始角 + 角度跨距把比例映射為指針角度）。
//
// **相位 1 不做真實繪製**：本單元只產出一份「渲染描述」(`RenderModel`)——形狀種類、填充比例、
// 量表角度、具名顏色語彙、文字標籤、以及「在**具名 surface** 上的**具名佈局槽(slot)**」——
// 供後續相位的繪製層消費。本層純邏輯、平台中立：無 `#ifdef` / win32 / cocoa / 真實繪圖 API。
//
// **NFR-02 鐵律**：渲染描述**不含絕對座標、不含數字 z-order**。位置一律以**具名**表達
//   （`surface` 具名 SurfaceId + `slot` 具名佈局槽），尺寸 / 進度一律以**比例** [0,1] 或
//   **語意角度**（量表的值→角度，是量表本身的語意輸出，非版面座標）表達。故無 x/y/寬/高像素、
//   無整數層級。
//
// 上游消費（已合併，可讀不可改）：
//   - E1-03（`ds::kernel`，經 alpha_surface.hpp 帶入）：以**具名 `SurfaceId`** 指涉元件所在
//     surface（NFR-02：不用數字 handle）。元件「渲染」到一個具名（可帶 per-pixel alpha 的）surface 上。
//   - E7-03（`ds::format`，section_vars.hpp）：以宣告式「變數段落」文件驅動元件設定——
//     設定值可用 `${var}` 引用段落內宣告的變數，展開由 E7-03 `expand()` 完成後再填入元件。
//
// 錯誤不靜默（NFR-04 精神）：
//   - 值超出範圍 → **夾限**（clamp，屬正常行為，非錯誤）。
//   - 無效輸入 → **明確報錯**：非有限值（NaN/Inf）之值、退化範圍（min>=max 或界非有限）、
//     退化量表弧（sweep 為 0 或非有限）→ 擲出 `std::invalid_argument`；設定文件型別不符 →
//     回傳非 Ok 的 `ConfigStatus`（絕不靜默吞掉）。
#ifndef DS_ELEMENTS_E4_03_BAR_GAUGE_HPP
#define DS_ELEMENTS_E4_03_BAR_GAUGE_HPP

#include <string>
#include <utility>

#include "alpha_surface.hpp"   // E1-03（上游）：ds::kernel::SurfaceId（具名 surface）
#include "section_vars.hpp"    // E7-03（上游）：ds::format::Value / Document / expand（段落變數）

namespace ds::elements {

// 元件種類（具名，非數字）。
enum class ElementKind {
    Bar,       // 長條：值 → 長度比例
    Progress,  // 進度：0–100%
    Gauge,     // 量表：值 → 角度 / 弧
};

// 長條 / 進度的排列方向（具名，非座標）。
enum class Orientation {
    Horizontal,  // 水平（左→右填充）
    Vertical,    // 垂直（下→上填充）
};

// 數值範圍 [min, max]。合法 iff min、max 皆為有限值且 min < max。
struct Range {
    double min = 0.0;
    double max = 1.0;

    // 是否為合法範圍（界皆有限且 min < max）。
    bool valid() const noexcept;
};

// 一個元件的「渲染描述模型」——純資料，供繪製層消費。
//
// **NFR-02**：本結構刻意**不含任何絕對座標 / 像素尺寸 / 數字 z-order**。
//   - 位置：`surface`（具名 SurfaceId）+ `slot`（具名佈局槽，如 "content" / "header"）。
//   - 尺寸 / 進度：`fill_ratio` ∈ [0,1]（比例）。
//   - 量表：`angle_degrees`（值→角度，量表語意輸出）落在 [start, start+sweep] 弧上。
//   - 顏色：具名 / 語意色彩語彙（如 "accent" / "muted"），非 RGB 座標數字。
struct RenderModel {
    ElementKind kind = ElementKind::Bar;

    // 具名佈局（NFR-02）。
    ds::kernel::SurfaceId surface;  // 具名 surface；空字串 = 尚未綁定 surface。
    std::string slot;               // surface 上的具名佈局槽；空字串 = 預設 / 未指定。

    Orientation orientation = Orientation::Horizontal;

    // 值於範圍內的正規化比例 [0,1]（已夾限）。長條長度 / 進度完成度 / 量表掃掠皆由此驅動。
    double fill_ratio = 0.0;

    // --- 量表專屬（kind==Gauge 時有效）---
    bool has_angle = false;         // 是否為角度型（量表）渲染。
    double start_angle_degrees = 0.0;  // 弧起始角。
    double sweep_degrees = 0.0;        // 弧角度跨距（可正可負）。
    double angle_degrees = 0.0;        // 值映射後的指針角度 = start + fill_ratio*sweep。

    // 具名 / 語意顏色語彙（非數字座標）。
    std::string fill_color = "accent";   // 填充 / 前景色語彙。
    std::string track_color = "muted";   // 軌道 / 背景色語彙。

    // 文字標籤。
    bool show_label = true;   // 是否顯示標籤。
    std::string label;        // 呈現文字（自動由值格式化，或以 set_label 覆寫）。
};

// -----------------------------------------------------------------------------
// 設定載入狀態（自 E7-03 宣告式文件填入元件時）。不靜默失敗。
// -----------------------------------------------------------------------------
enum class ConfigStatus {
    Ok,             // 成功套用。
    NotAMap,        // 設定根不是 Map。
    ResolveError,   // E7-03 變數展開失敗（未定義 / 循環 / 未終止引用等）。
    InvalidField,   // 某存在的欄位型別不符或值非法（如 min>=max、orientation 非具名值）。
};

// -----------------------------------------------------------------------------
// RangedElement —— 長條 / 量表共用的「值 ↔ 範圍」核心。
// -----------------------------------------------------------------------------
//
// 持有一個 `Range` 與目前值。值以**原樣**保存；對外的 `value()` / `ratio()` 皆回傳**夾限後**
// 的結果，故變更範圍會自動就其重新夾限。無效輸入（非有限值 / 退化範圍）擲例外（不靜默）。
class RangedElement {
public:
    RangedElement() = default;
    RangedElement(double min, double max);  // 界非有限或 min>=max → std::invalid_argument。

    // 設定範圍。界非有限或 min>=max → std::invalid_argument（原範圍不變）。
    void set_range(double min, double max);
    const Range& range() const noexcept { return range_; }

    // 設定值（原樣保存）。非有限值（NaN/Inf）→ std::invalid_argument（原值不變）。
    // 值超出範圍不報錯——由 value() / ratio() 於取用時夾限。
    void set_value(double v);

    // 原樣（未夾限）值。
    double raw_value() const noexcept { return raw_value_; }
    // 夾限至 [min,max] 後的有效值。
    double value() const noexcept;
    // 值於範圍內的正規化比例 [0,1]（已夾限）。
    double ratio() const noexcept;

    // 排列方向。
    void set_orientation(Orientation o) noexcept { orientation_ = o; }
    Orientation orientation() const noexcept { return orientation_; }

    // 具名佈局（NFR-02）。
    void set_surface(ds::kernel::SurfaceId id) { surface_ = std::move(id); }
    const ds::kernel::SurfaceId& surface() const noexcept { return surface_; }
    void set_slot(std::string slot) { slot_ = std::move(slot); }
    const std::string& slot() const noexcept { return slot_; }

    // 顏色語彙。
    void set_fill_color(std::string c) { fill_color_ = std::move(c); }
    const std::string& fill_color() const noexcept { return fill_color_; }
    void set_track_color(std::string c) { track_color_ = std::move(c); }
    const std::string& track_color() const noexcept { return track_color_; }

    // 標籤：可顯示 / 隱藏，並可覆寫自動格式化的文字。
    void set_show_label(bool show) noexcept { show_label_ = show; }
    bool show_label() const noexcept { return show_label_; }
    // 覆寫標籤文字（空字串 = 回到自動由值格式化）。
    void set_label(std::string text) { label_override_ = std::move(text); }
    // 目前生效的標籤文字（覆寫優先，否則自動格式化）。
    std::string label() const;

protected:
    // 填入 RenderModel 的共用欄位（不含 kind / 量表角度）。
    void fill_common(RenderModel& m) const;
    // 是否已設定覆寫標籤（供子類決定自動標籤格式，如進度的 "%"）。
    bool has_label_override() const noexcept { return !label_override_.empty(); }

private:
    Range range_{};
    double raw_value_ = 0.0;
    Orientation orientation_ = Orientation::Horizontal;
    ds::kernel::SurfaceId surface_;
    std::string slot_;
    std::string fill_color_ = "accent";
    std::string track_color_ = "muted";
    bool show_label_ = true;
    std::string label_override_;
};

// -----------------------------------------------------------------------------
// BarElement —— 長條：值 → 長度比例。
// -----------------------------------------------------------------------------
class BarElement : public RangedElement {
public:
    using RangedElement::RangedElement;

    // 產出渲染描述（kind=Bar）。fill_ratio 為值於範圍內的夾限比例。
    RenderModel render_model() const;
};

// -----------------------------------------------------------------------------
// ProgressElement —— 進度：0–100%。
// -----------------------------------------------------------------------------
//
// 本質為固定範圍 [0,100] 的長條，以百分比操作 / 呈現。
class ProgressElement : public RangedElement {
public:
    ProgressElement();  // 範圍固定 [0,100]，起始 0%。

    // 設定完成百分比（夾限至 [0,100]）。非有限值 → std::invalid_argument。
    void set_percent(double percent);
    // 目前完成百分比（夾限後，[0,100]）。
    double percent() const noexcept;

    // 產出渲染描述（kind=Progress）。fill_ratio = percent/100；標籤預設如 "63%"。
    RenderModel render_model() const;
};

// -----------------------------------------------------------------------------
// GaugeElement —— 量表：值 → 角度 / 弧。
// -----------------------------------------------------------------------------
//
// 除值 / 範圍外，額外持有弧幾何：起始角 `start_angle` 與角度跨距 `sweep`（皆為度）。
// 值映射：angle = start_angle + ratio(value) * sweep。角度是量表的**語意輸出**（值→角度），
// 屬渲染描述之一環，非版面座標，故不違反 NFR-02。
class GaugeElement : public RangedElement {
public:
    GaugeElement();  // 預設範圍 [0,1]、起始角 135°、跨距 270°（常見圓弧量表）。
    GaugeElement(double min, double max);

    // 設定弧幾何（度）。sweep 為 0 或界非有限 → std::invalid_argument（原弧不變）。
    void set_arc(double start_angle_degrees, double sweep_degrees);
    double start_angle() const noexcept { return start_angle_; }
    double sweep() const noexcept { return sweep_; }

    // 值映射後的指針角度（度）= start_angle + ratio()*sweep。
    double angle() const noexcept;

    // 產出渲染描述（kind=Gauge，has_angle=true）。
    RenderModel render_model() const;

private:
    double start_angle_ = 135.0;
    double sweep_ = 270.0;
};

// -----------------------------------------------------------------------------
// E7-03 宣告式設定驅動（消費上游 E7-03 `expand`）。
// -----------------------------------------------------------------------------
//
// 各載入器接受一份**設定根** `ds::format::Value`（Map，可含 `vars:` 變數段落，其餘欄位可用
// `${name}` 引用之），先以 E7-03 `expand()` 展開變數段落，再把解析後的欄位填入元件。
//
// 展開失敗（未定義 / 循環 / 未終止引用）→ 回 `ResolveError`（帶訊息於 err，不靜默）。
// 根非 Map → `NotAMap`。某存在欄位型別不符 / 值非法 → `InvalidField`（帶訊息於 err）。
// 未出現的欄位一律沿用元件現值（缺欄非錯誤）。成功回 `Ok`。
//
// 認得的欄位：
//   Bar     : min, max, value, orientation("horizontal"|"vertical"),
//             fill_color, track_color, surface, slot, label, show_label
//   Progress: percent, fill_color, track_color, surface, slot, label, show_label
//   Gauge   : min, max, value, start_angle, sweep,
//             fill_color, track_color, surface, slot, label, show_label
ConfigStatus load_bar_config(const ds::format::Value& config_root, BarElement& out,
                             std::string& err);
ConfigStatus load_progress_config(const ds::format::Value& config_root, ProgressElement& out,
                                  std::string& err);
ConfigStatus load_gauge_config(const ds::format::Value& config_root, GaugeElement& out,
                               std::string& err);

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_03_BAR_GAUGE_HPP
