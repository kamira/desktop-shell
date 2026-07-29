// E4-18 徑向線 / 指針 — 視覺元件的「渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：一根從中心以**某角度**向外延伸的**徑向線 / 指針(needle/pointer)**——量表指針、
// 時鐘指針皆屬此語意。核心映射與 E4-03 的 Gauge 相同（值 → 角度），但輸出聚焦於「指針
// 本身」的幾何：一條線，而非一段弧 / 填充區——因此另外表達**長度比例**（相對於量表半徑，
// 指針有多長）與**粗細比例**（指針有多粗），供最終如 CPU/GPU Usage Widget 之類的量表
// Widget 疊在錶盤(dial)上組合使用。
//
// **相位 1 不做真實繪製**：本單元只產出一份「渲染描述」(`RenderModel`)——目前角度、
// 弧幾何（起始角 + 跨距，供呼叫端理解指針活動範圍）、長度比例、粗細比例、具名顏色語彙、
// 以及「在**具名 surface** 上的**具名佈局槽(slot)**」——供後續相位的繪製層消費。本層純
// 邏輯、平台中立：無 `#ifdef` / win32 / cocoa / 真實繪圖 API。
//
// **NFR-02 鐵律**：渲染描述**不含絕對座標、不含數字 z-order**。位置一律以**具名**表達
//   （`surface` 具名 SurfaceId + `slot` 具名佈局槽），指針幾何一律以**比例** [0,1]（長度 /
//   粗細）或**語意角度**（值→角度，是指針本身的語意輸出，非版面座標）表達。故無 x/y/
//   起訖座標像素、無整數層級。
//
// 上游消費（已合併，可讀不可改）：
//   - E1-03（`ds::kernel`，經 alpha_surface.hpp 帶入）：以**具名 `SurfaceId`** 指涉元件所在
//     surface（NFR-02：不用數字 handle）。指針「渲染」到一個具名（可帶 per-pixel alpha 的）
//     surface 上。
//   - E7-03（`ds::format`，section_vars.hpp）：以宣告式「變數段落」文件驅動元件設定——
//     設定值可用 `${var}` 引用段落內宣告的變數，展開由 E7-03 `expand()` 完成後再填入元件。
//
// 錯誤不靜默（NFR-04 精神）：
//   - 值超出範圍 / 長度 / 粗細比例超出 [0,1] → **夾限**（clamp，屬正常行為，非錯誤）。
//   - 無效輸入 → **明確報錯**：非有限值（NaN/Inf）之值、退化範圍（min>=max 或界非有限）、
//     退化角度跨距（sweep 為 0 或起始角/跨距非有限）、非有限長度 / 粗細比例 →
//     擲出 `std::invalid_argument`；設定文件型別不符 → 回傳非 Ok 的 `ConfigStatus`
//     （絕不靜默吞掉）。
#ifndef DS_ELEMENTS_E4_18_RADIAL_POINTER_HPP
#define DS_ELEMENTS_E4_18_RADIAL_POINTER_HPP

#include <string>

#include "alpha_surface.hpp"   // E1-03（上游）：ds::kernel::SurfaceId（具名 surface）
#include "section_vars.hpp"    // E7-03（上游）：ds::format::Value / Document / expand（段落變數）

namespace ds::elements {

// 一個徑向線 / 指針的「渲染描述模型」——純資料，供繪製層消費。
//
// **NFR-02**：本結構刻意**不含任何絕對座標 / 像素尺寸 / 數字 z-order**。
//   - 位置：`surface`（具名 SurfaceId）+ `slot`（具名佈局槽，如 "dial" / "needle"）。
//   - 指針目前角度：`angle_degrees`（值→角度，指針語意輸出）落在
//     [start_angle_degrees, start_angle_degrees + sweep_degrees] 弧上。
//   - 長度 / 粗細：`length_ratio` / `thickness_ratio` ∈ [0,1]（比例，非像素）。
//   - 顏色：具名 / 語意色彩語彙（如 "accent" / "needle"），非 RGB 座標數字。
struct RenderModel {
    // 具名佈局（NFR-02）。
    ds::kernel::SurfaceId surface;  // 具名 surface；空字串 = 尚未綁定 surface。
    std::string slot;               // surface 上的具名佈局槽；空字串 = 預設 / 未指定。

    // 值於範圍內的正規化比例 [0,1]（已夾限）；指針角度即由此映射而來。
    double value_ratio = 0.0;

    // 弧幾何（度）：指針的活動範圍。
    double start_angle_degrees = 0.0;
    double sweep_degrees = 0.0;
    // 指針目前角度（度）= start_angle_degrees + value_ratio*sweep_degrees。
    double angle_degrees = 0.0;

    // 指針幾何比例（皆為 [0,1]，非像素）。
    double length_ratio = 1.0;     // 指針長度相對於量表半徑的比例。
    double thickness_ratio = 0.05;  // 指針粗細比例。

    // 具名 / 語意顏色語彙（非數字座標）。
    std::string color = "accent";
};

// -----------------------------------------------------------------------------
// 設定載入狀態（自 E7-03 宣告式文件填入元件時）。不靜默失敗。
// -----------------------------------------------------------------------------
enum class ConfigStatus {
    Ok,            // 成功套用。
    NotAMap,       // 設定根不是 Map。
    ResolveError,  // E7-03 變數展開失敗（未定義 / 循環 / 未終止引用等）。
    InvalidField,  // 某存在的欄位型別不符或值非法（如 min>=max、sweep=0）。
};

// -----------------------------------------------------------------------------
// RadialPointerElement —— 徑向線 / 指針：值 → 角度，帶長度 / 粗細 / 顏色。
// -----------------------------------------------------------------------------
//
// 持有一個數值範圍與目前值（同 E4-03 RangedElement 的值↔範圍語意，但本單元僅依賴
// E1-03 / E7-03，不依賴 E4-03，故自成一套）：值以**原樣**保存；`value()` / `ratio()`
// 皆回傳**夾限後**的結果，故變更範圍會自動就其重新夾限。另持有弧幾何（起始角 + 角度
// 跨距）與指針幾何（長度 / 粗細比例）+ 顏色語彙 + 具名佈局。無效輸入擲例外（不靜默）。
class RadialPointerElement {
public:
    RadialPointerElement();               // 範圍 [0,1]，弧 135°起、270°跨（常見錶盤）。
    RadialPointerElement(double min, double max);  // 界非有限或 min>=max → std::invalid_argument。

    // --- 值 / 範圍 ---
    // 設定範圍。界非有限或 min>=max → std::invalid_argument（原範圍不變）。
    void set_range(double min, double max);
    double range_min() const noexcept { return min_; }
    double range_max() const noexcept { return max_; }

    // 設定值（原樣保存）。非有限值（NaN/Inf）→ std::invalid_argument（原值不變）。
    // 值超出範圍不報錯——由 value() / ratio() 於取用時夾限。
    void set_value(double v);
    // 原樣（未夾限）值。
    double raw_value() const noexcept { return raw_value_; }
    // 夾限至 [min,max] 後的有效值。
    double value() const noexcept;
    // 值於範圍內的正規化比例 [0,1]（已夾限）。
    double ratio() const noexcept;

    // --- 弧幾何（指針的活動範圍）---
    // 設定起始角 + 角度跨距（度）。sweep 為 0 或界非有限 → std::invalid_argument（原弧不變）。
    void set_angle_span(double start_angle_degrees, double sweep_degrees);
    double start_angle() const noexcept { return start_angle_; }
    double sweep() const noexcept { return sweep_; }
    // 值映射後的指針目前角度（度）= start_angle() + ratio()*sweep()。
    double angle() const noexcept;

    // --- 指針幾何：長度 / 粗細比例 ---
    // 設定指針長度比例。非有限值 → std::invalid_argument；否則夾限至 [0,1]。
    void set_length_ratio(double ratio);
    double length_ratio() const noexcept { return length_ratio_; }
    // 設定指針粗細比例。非有限值 → std::invalid_argument；否則夾限至 [0,1]。
    void set_thickness_ratio(double ratio);
    double thickness_ratio() const noexcept { return thickness_ratio_; }

    // --- 顏色語彙 ---
    void set_color(std::string c) { color_ = std::move(c); }
    const std::string& color() const noexcept { return color_; }

    // --- 具名佈局（NFR-02）---
    void set_surface(ds::kernel::SurfaceId id) { surface_ = std::move(id); }
    const ds::kernel::SurfaceId& surface() const noexcept { return surface_; }
    void set_slot(std::string slot) { slot_ = std::move(slot); }
    const std::string& slot() const noexcept { return slot_; }

    // 產出渲染描述。
    RenderModel render_model() const;

private:
    double min_ = 0.0;
    double max_ = 1.0;
    double raw_value_ = 0.0;

    double start_angle_ = 135.0;
    double sweep_ = 270.0;

    double length_ratio_ = 1.0;
    double thickness_ratio_ = 0.05;

    std::string color_ = "accent";

    ds::kernel::SurfaceId surface_;
    std::string slot_;
};

// -----------------------------------------------------------------------------
// E7-03 宣告式設定驅動（消費上游 E7-03 `expand`）。
// -----------------------------------------------------------------------------
//
// 接受一份**設定根** `ds::format::Value`（Map，可含 `vars:` 變數段落，其餘欄位可用
// `${name}` 引用之），先以 E7-03 `expand()` 展開變數段落，再把解析後的欄位填入元件。
//
// 展開失敗（未定義 / 循環 / 未終止引用）→ 回 `ResolveError`（帶訊息於 err，不靜默）。
// 根非 Map → `NotAMap`。某存在欄位型別不符 / 值非法 → `InvalidField`（帶訊息於 err）。
// 未出現的欄位一律沿用元件現值（缺欄非錯誤）。成功回 `Ok`。
//
// 認得的欄位：min, max, value, start_angle, sweep, length_ratio, thickness_ratio,
//            color, surface, slot
ConfigStatus load_radial_pointer_config(const ds::format::Value& config_root,
                                        RadialPointerElement& out, std::string& err);

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_18_RADIAL_POINTER_HPP
