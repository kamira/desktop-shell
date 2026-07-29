// E9-09 動態配色生成 — 從種子色生成一整套協調配色（平台中立 / 純邏輯 / engine 層）
//
// 語意：本單元把「一個種子顏色」（或來源如桌布主色）動態展開為一套**協調的配色方案**——
// 主色 / 次色 / 強調 / 背景 / 前景 / 各狀態色（成功 / 警告 / 錯誤 / 資訊），供 E9-04 主題
// （ThemeManager / ThemeData）直接使用。核心承諾：
//   1. **色彩理論展開**：依色相環關係（互補 Complementary / 類比 Analogous / 三分 Triadic /
//      單色 Monochromatic）從種子色推導次色與強調色；明度 / 飽和度階用於背景與狀態色。
//   2. **對比度確保可讀性（WCAG）**：前景色相對背景色的對比度保證達 WCAG AA（>= 4.5:1）；
//      不足時自動退回黑 / 白中對比較高者。提供 relative_luminance / contrast_ratio 供查驗。
//   3. **決定性**：純函式——同一種子色 + 同一方案恆得到位元等價的 ColorPalette。
//   4. **無效種子色不靜默**：通道超出 [0,255] 或 hex 字串格式錯誤一律**明確報錯**
//      （回傳帶原因的 PaletteResult / false），絕不安靜吞掉。
//   5. **橋接 E9-04**：`to_theme_data(palette, name)` 把生成的配色轉為 `ds::package::ThemeData`
//      （attributes 為各色角色 → hex 字串的宣告式 `Value` Map），可直接餵入 ThemeManager。
//
// 設計原則（與 E9-01 ~ E9-04 一致，命名空間同為 `ds::package`）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef` / 系統呼叫 / 真實繪圖 API / 平台分支。
//   - **顏色以平台中立表示**：RGB（8-bit 通道）/ HSL（數值）/ hex 字串；不綁任何圖形框架型別。
//   - 本單元不含能力閘控呼叫（純計算），無 `has()` 降級路徑需求（NFR-03 不適用）。
//   - API 面無絕對座標 / 數字 z-order（NFR-02）。
#ifndef DS_ENGINE_E9_09_PALETTE_HPP
#define DS_ENGINE_E9_09_PALETTE_HPP

#include <string>
#include <vector>

#include "theme.hpp"  // E9-04：ds::package::ThemeData（PUBLIC e9_04 傳遞 include）

namespace ds::package {

// -----------------------------------------------------------------------------
// 平台中立色彩表示
// -----------------------------------------------------------------------------

// RGB 顏色，8-bit 通道（每通道有效範圍 [0,255]）。純資料、值語意。
struct RgbColor {
    int r = 0;
    int g = 0;
    int b = 0;

    bool operator==(const RgbColor& o) const noexcept {
        return r == o.r && g == o.g && b == o.b;
    }
    bool operator!=(const RgbColor& o) const noexcept { return !(*this == o); }
};

// HSL 顏色：h 為色相角度 [0,360)，s 飽和度 [0,1]，l 明度 [0,1]。純資料。
struct HslColor {
    double h = 0.0;
    double s = 0.0;
    double l = 0.0;
};

// 配色方案家族（色相環關係）。
enum class ColorScheme {
    Complementary,  // 互補：主色 + 對面（+180°）。
    Analogous,      // 類比：主色 + 相鄰（±30°）。
    Triadic,        // 三分：色相環三等分（+120° / +240°）。
    Monochromatic,  // 單色：同色相，變化明度 / 飽和度。
};

// 一整套生成的協調配色。各角色皆為平台中立 RgbColor。
struct ColorPalette {
    RgbColor primary;     // 主色（即種子色）。
    RgbColor secondary;   // 次色（依方案由色相環關係推導）。
    RgbColor accent;      // 強調色。
    RgbColor background;  // 背景（高明度中性底）。
    RgbColor foreground;  // 前景（對背景保證 WCAG AA 對比）。
    RgbColor success;     // 狀態：成功（綠系，與種子飽和度 / 明度協調）。
    RgbColor warning;     // 狀態：警告（琥珀系）。
    RgbColor error;       // 狀態：錯誤（紅系）。
    RgbColor info;        // 狀態：資訊（藍系）。
    ColorScheme scheme = ColorScheme::Complementary;  // 生成所用方案。
    RgbColor seed;        // 來源種子色（決定性溯源）。

    bool operator==(const ColorPalette& o) const noexcept {
        return primary == o.primary && secondary == o.secondary && accent == o.accent &&
               background == o.background && foreground == o.foreground &&
               success == o.success && warning == o.warning && error == o.error &&
               info == o.info && scheme == o.scheme && seed == o.seed;
    }
    bool operator!=(const ColorPalette& o) const noexcept { return !(*this == o); }
};

// 生成結果：成功則持有 palette，失敗則帶不靜默的原因。二者互斥。
struct PaletteResult {
    bool ok = false;
    std::string message;   // 失敗時的人類可讀原因；成功時為空。
    ColorPalette palette;  // ok 為 true 時有效。

    explicit operator bool() const noexcept { return ok; }

    static PaletteResult success(ColorPalette p) { return {true, {}, std::move(p)}; }
    static PaletteResult failure(std::string why) { return {false, std::move(why), {}}; }
};

// -----------------------------------------------------------------------------
// 色彩工具（平台中立、純函式）
// -----------------------------------------------------------------------------

// 種子色是否有效：每通道皆在 [0,255]。
bool is_valid_color(const RgbColor& c) noexcept;

// RGB → HSL。無效通道會先夾到 [0,255] 再轉（不 throw，供內部安全使用）。
HslColor rgb_to_hsl(const RgbColor& c) noexcept;

// HSL → RGB。h 會被正規化到 [0,360)，s / l 夾到 [0,1]；通道四捨五入到最近整數 [0,255]。
RgbColor hsl_to_rgb(const HslColor& c) noexcept;

// RGB → 小寫 hex 字串（形如 "#3366ff"）。通道會夾到 [0,255]。
std::string to_hex(const RgbColor& c);

// hex 字串 → RGB。接受 "#rrggbb" / "rrggbb" / "#rgb" / "rgb"（大小寫皆可）。
//   - 格式錯誤（長度不符 / 非 16 進位字元）→ 回 false，out 不動（不靜默）。
bool from_hex(const std::string& hex, RgbColor& out);

// WCAG 相對亮度（relative luminance），值域 [0,1]。用於對比度計算。
double relative_luminance(const RgbColor& c) noexcept;

// WCAG 對比度比值：(L_lighter + 0.05) / (L_darker + 0.05)，值域 [1,21]。
double contrast_ratio(const RgbColor& a, const RgbColor& b) noexcept;

// 明度階：以 base 為中點，生成由暗到亮共 steps 階的色帶（同色相 / 飽和度，明度線性分佈）。
//   - steps <= 0 → 空清單；steps == 1 → 僅 base。
std::vector<RgbColor> lightness_scale(const RgbColor& base, int steps);

// -----------------------------------------------------------------------------
// 配色生成器
// -----------------------------------------------------------------------------

// 由種子色動態生成協調配色。純邏輯、決定性、無狀態。
class PaletteGenerator {
public:
    // 由種子色 + 方案生成一整套配色。
    //   - seed 無效（通道超出 [0,255]）→ failure(帶原因)，不生成。
    //   - 同一 (seed, scheme) 恆得到位元等價結果（決定性）。
    //   - 前景 / 背景對比保證 >= WCAG AA（4.5:1）。
    PaletteResult generate(const RgbColor& seed, ColorScheme scheme) const;

    // 便捷：由 hex 種子字串生成。hex 格式錯誤 → failure(帶原因)。
    PaletteResult generate_from_hex(const std::string& seed_hex, ColorScheme scheme) const;

    // 橋接 E9-04：把配色轉為 ThemeData（供 ThemeManager::register_theme / set_theme）。
    //   - name 為主題註冊名（呼叫端負責非空——空名於 E9-04 註冊時被拒）。
    //   - attributes 為各色角色（primary / secondary / accent / background / foreground /
    //     success / warning / error / info）→ hex 字串的宣告式 `Value` Map。
    //   - components 為空組合（配色不含可互換元件；相依端可另行綁定）。
    ThemeData to_theme_data(const ColorPalette& palette, std::string name) const;
};

}  // namespace ds::package

#endif  // DS_ENGINE_E9_09_PALETTE_HPP
