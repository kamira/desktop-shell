// E9-09 動態配色生成 — 實作（平台中立 / 純邏輯）。介面與語意見 palette.hpp。
#include "palette.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace ds::package {
namespace {

// --- 小工具：夾值 / 正規化 ---------------------------------------------------

int clamp_channel(int v) noexcept { return v < 0 ? 0 : (v > 255 ? 255 : v); }

double clamp01(double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// 色相正規化到 [0,360)。
double wrap_hue(double h) noexcept {
    h = std::fmod(h, 360.0);
    if (h < 0.0) h += 360.0;
    return h;
}

// 四捨五入到最近整數通道並夾範圍。
int round_channel(double v) noexcept {
    return clamp_channel(static_cast<int>(std::lround(v)));
}

// 單一 hex 字元 → 值（0..15）；非法回 -1。
int hex_digit(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower >= 'a' && lower <= 'f') return 10 + (lower - 'a');
    return -1;
}

// WCAG 單通道線性化。
double linearize(int channel) noexcept {
    const double cs = clamp_channel(channel) / 255.0;
    return cs <= 0.03928 ? cs / 12.92 : std::pow((cs + 0.055) / 1.055, 2.4);
}

// 以指定色相 / 飽和度 / 明度組出 HSL 並轉 RGB（集中一處，利決定性）。
RgbColor make_hsl(double h, double s, double l) noexcept {
    return hsl_to_rgb(HslColor{wrap_hue(h), clamp01(s), clamp01(l)});
}

// 保證前景對背景達 WCAG AA（4.5:1）：不足則退回黑 / 白中對比較高者。
RgbColor ensure_readable(const RgbColor& fg, const RgbColor& bg) noexcept {
    if (contrast_ratio(fg, bg) >= 4.5) return fg;
    const RgbColor black{0, 0, 0};
    const RgbColor white{255, 255, 255};
    return contrast_ratio(black, bg) >= contrast_ratio(white, bg) ? black : white;
}

// 各狀態色的基準色相（度）：綠 / 琥珀 / 紅 / 藍。
constexpr double kHueSuccess = 140.0;
constexpr double kHueWarning = 40.0;
constexpr double kHueError = 5.0;
constexpr double kHueInfo = 210.0;

}  // namespace

// -----------------------------------------------------------------------------
// 色彩工具
// -----------------------------------------------------------------------------

bool is_valid_color(const RgbColor& c) noexcept {
    return c.r >= 0 && c.r <= 255 && c.g >= 0 && c.g <= 255 && c.b >= 0 && c.b <= 255;
}

HslColor rgb_to_hsl(const RgbColor& c) noexcept {
    const double r = clamp_channel(c.r) / 255.0;
    const double g = clamp_channel(c.g) / 255.0;
    const double b = clamp_channel(c.b) / 255.0;

    const double max = std::max({r, g, b});
    const double min = std::min({r, g, b});
    const double delta = max - min;

    HslColor out;
    out.l = (max + min) / 2.0;

    if (delta <= 0.0) {
        out.h = 0.0;  // 無彩（灰）→ 色相未定義，取 0。
        out.s = 0.0;
        return out;
    }

    out.s = out.l > 0.5 ? delta / (2.0 - max - min) : delta / (max + min);

    double h;
    if (max == r) {
        h = (g - b) / delta + (g < b ? 6.0 : 0.0);
    } else if (max == g) {
        h = (b - r) / delta + 2.0;
    } else {
        h = (r - g) / delta + 4.0;
    }
    out.h = wrap_hue(h * 60.0);
    return out;
}

RgbColor hsl_to_rgb(const HslColor& c) noexcept {
    const double h = wrap_hue(c.h) / 360.0;
    const double s = clamp01(c.s);
    const double l = clamp01(c.l);

    if (s <= 0.0) {
        const int v = round_channel(l * 255.0);
        return RgbColor{v, v, v};  // 無飽和 = 灰階。
    }

    const auto hue_to_rgb = [](double p, double q, double t) noexcept {
        if (t < 0.0) t += 1.0;
        if (t > 1.0) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 1.0 / 2.0) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };

    const double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    const double p = 2.0 * l - q;
    const double r = hue_to_rgb(p, q, h + 1.0 / 3.0);
    const double g = hue_to_rgb(p, q, h);
    const double b = hue_to_rgb(p, q, h - 1.0 / 3.0);
    return RgbColor{round_channel(r * 255.0), round_channel(g * 255.0),
                    round_channel(b * 255.0)};
}

std::string to_hex(const RgbColor& c) {
    static const char* kDigits = "0123456789abcdef";
    const int chans[3] = {clamp_channel(c.r), clamp_channel(c.g), clamp_channel(c.b)};
    std::string out = "#";
    out.reserve(7);
    for (int v : chans) {
        out.push_back(kDigits[(v >> 4) & 0xF]);
        out.push_back(kDigits[v & 0xF]);
    }
    return out;
}

bool from_hex(const std::string& hex, RgbColor& out) {
    std::size_t start = 0;
    if (!hex.empty() && hex[0] == '#') start = 1;
    const std::size_t len = hex.size() - start;

    // 只接受 6 位（rrggbb）或 3 位（rgb，逐位展開）。
    if (len != 6 && len != 3) return false;

    int digits[6];
    for (std::size_t i = 0; i < len; ++i) {
        const int d = hex_digit(hex[start + i]);
        if (d < 0) return false;
        digits[i] = d;
    }

    RgbColor parsed;
    if (len == 6) {
        parsed.r = digits[0] * 16 + digits[1];
        parsed.g = digits[2] * 16 + digits[3];
        parsed.b = digits[4] * 16 + digits[5];
    } else {  // 3 位：每位展開為兩位（如 f → ff）。
        parsed.r = digits[0] * 16 + digits[0];
        parsed.g = digits[1] * 16 + digits[1];
        parsed.b = digits[2] * 16 + digits[2];
    }
    out = parsed;
    return true;
}

double relative_luminance(const RgbColor& c) noexcept {
    return 0.2126 * linearize(c.r) + 0.7152 * linearize(c.g) + 0.0722 * linearize(c.b);
}

double contrast_ratio(const RgbColor& a, const RgbColor& b) noexcept {
    const double la = relative_luminance(a);
    const double lb = relative_luminance(b);
    const double lighter = std::max(la, lb);
    const double darker = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

std::vector<RgbColor> lightness_scale(const RgbColor& base, int steps) {
    std::vector<RgbColor> out;
    if (steps <= 0) return out;
    const HslColor hsl = rgb_to_hsl(base);
    if (steps == 1) {
        out.push_back(base);
        return out;
    }
    // 由暗（l=0.10）到亮（l=0.95）線性分佈；同色相 / 飽和度。
    const double lo = 0.10;
    const double hi = 0.95;
    out.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps - 1);
        out.push_back(make_hsl(hsl.h, hsl.s, lo + (hi - lo) * t));
    }
    return out;
}

// -----------------------------------------------------------------------------
// 配色生成器
// -----------------------------------------------------------------------------

PaletteResult PaletteGenerator::generate(const RgbColor& seed, ColorScheme scheme) const {
    if (!is_valid_color(seed)) {
        return PaletteResult::failure(
            "invalid seed color: channels must be within [0,255]");
    }

    const HslColor base = rgb_to_hsl(seed);

    ColorPalette p;
    p.scheme = scheme;
    p.seed = seed;
    p.primary = seed;

    // 次色 / 強調：依方案由色相環關係推導（明度 / 飽和度與主色協調）。
    switch (scheme) {
        case ColorScheme::Complementary:
            p.secondary = make_hsl(base.h + 180.0, base.s, base.l);
            p.accent = make_hsl(base.h + 180.0, clamp01(base.s * 1.15),
                                clamp01(base.l * 0.85));
            break;
        case ColorScheme::Analogous:
            p.secondary = make_hsl(base.h + 30.0, base.s, base.l);
            p.accent = make_hsl(base.h - 30.0, base.s, base.l);
            break;
        case ColorScheme::Triadic:
            p.secondary = make_hsl(base.h + 120.0, base.s, base.l);
            p.accent = make_hsl(base.h + 240.0, base.s, base.l);
            break;
        case ColorScheme::Monochromatic:
            p.secondary = make_hsl(base.h, base.s, clamp01(base.l * 0.70));
            p.accent = make_hsl(base.h, clamp01(base.s * 1.20), clamp01(base.l * 1.25));
            break;
    }

    // 背景：帶種子色相的極高明度中性底（微飽和，避免死白）。
    p.background = make_hsl(base.h, std::min(base.s, 0.06), 0.97);
    // 前景：同色相的極低明度深色，再保證 WCAG AA 對比。
    const RgbColor fg_candidate = make_hsl(base.h, std::min(base.s, 0.25), 0.12);
    p.foreground = ensure_readable(fg_candidate, p.background);

    // 狀態色：固定語意色相，飽和度 / 明度與種子協調（取中間帶以保鮮明可讀）。
    const double state_s = clamp01(std::max(base.s, 0.55));
    const double state_l = 0.45;
    p.success = make_hsl(kHueSuccess, state_s, state_l);
    p.warning = make_hsl(kHueWarning, state_s, clamp01(state_l + 0.05));
    p.error = make_hsl(kHueError, state_s, state_l);
    p.info = make_hsl(kHueInfo, state_s, state_l);

    return PaletteResult::success(p);
}

PaletteResult PaletteGenerator::generate_from_hex(const std::string& seed_hex,
                                                  ColorScheme scheme) const {
    RgbColor seed;
    if (!from_hex(seed_hex, seed)) {
        return PaletteResult::failure("invalid seed hex string: " + seed_hex);
    }
    return generate(seed, scheme);
}

ThemeData PaletteGenerator::to_theme_data(const ColorPalette& palette,
                                          std::string name) const {
    using ds::format::Value;
    std::vector<Value::Member> members;
    members.reserve(9);
    members.emplace_back("primary", Value::string(to_hex(palette.primary)));
    members.emplace_back("secondary", Value::string(to_hex(palette.secondary)));
    members.emplace_back("accent", Value::string(to_hex(palette.accent)));
    members.emplace_back("background", Value::string(to_hex(palette.background)));
    members.emplace_back("foreground", Value::string(to_hex(palette.foreground)));
    members.emplace_back("success", Value::string(to_hex(palette.success)));
    members.emplace_back("warning", Value::string(to_hex(palette.warning)));
    members.emplace_back("error", Value::string(to_hex(palette.error)));
    members.emplace_back("info", Value::string(to_hex(palette.info)));

    ThemeData data;
    data.name = std::move(name);
    data.attributes = Value::map(std::move(members));
    // components 保持空組合（配色不含可互換元件）。
    return data;
}

}  // namespace ds::package
