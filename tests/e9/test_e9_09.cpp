// E9-09 動態配色生成 — 契約測試（gtest）
//
// 驗證：各配色方案生成（互補 / 類比 / 三分 / 單色，色相環關係正確）、色彩轉換
// （RGB↔HSL 往返、灰階、hex 進出）、WCAG 對比度計算、明度階、無效種子色明確報錯
// （RGB 通道超界 / hex 格式錯誤，不靜默）、to_theme_data 橋接 E9-04（可餵入 ThemeManager
// 並於切換時通知）、決定性（同種子同方案 → 位元等價）。平台中立：不含任何平台分支。
#include "palette.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "theme.hpp"  // E9-04：ThemeManager / ThemeData（驗橋接）

using ds::format::Value;
using ds::package::ColorPalette;
using ds::package::ColorScheme;
using ds::package::contrast_ratio;
using ds::package::from_hex;
using ds::package::HslColor;
using ds::package::is_valid_color;
using ds::package::lightness_scale;
using ds::package::PaletteGenerator;
using ds::package::PaletteResult;
using ds::package::relative_luminance;
using ds::package::RgbColor;
using ds::package::rgb_to_hsl;
using ds::package::hsl_to_rgb;
using ds::package::ThemeData;
using ds::package::ThemeManager;
using ds::package::to_hex;

namespace {

// 兩色通道差是否都在容差內（往返轉換的浮點誤差）。
bool near_color(const RgbColor& a, const RgbColor& b, int tol) {
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol &&
           std::abs(a.b - b.b) <= tol;
}

}  // namespace

// -----------------------------------------------------------------------------
// 色彩轉換：RGB ↔ HSL
// -----------------------------------------------------------------------------

TEST(E9_09_Convert, RgbToHslKnownColors) {
    // 純紅 → h=0, s=1, l=0.5。
    const HslColor red = rgb_to_hsl(RgbColor{255, 0, 0});
    EXPECT_NEAR(red.h, 0.0, 1e-6);
    EXPECT_NEAR(red.s, 1.0, 1e-6);
    EXPECT_NEAR(red.l, 0.5, 1e-6);

    // 純綠 → h=120。
    EXPECT_NEAR(rgb_to_hsl(RgbColor{0, 255, 0}).h, 120.0, 1e-6);
    // 純藍 → h=240。
    EXPECT_NEAR(rgb_to_hsl(RgbColor{0, 0, 255}).h, 240.0, 1e-6);
}

TEST(E9_09_Convert, GrayHasZeroSaturation) {
    const HslColor gray = rgb_to_hsl(RgbColor{128, 128, 128});
    EXPECT_NEAR(gray.s, 0.0, 1e-6);
    EXPECT_NEAR(gray.l, 128.0 / 255.0, 1e-6);
    // 灰階往返仍是灰。
    const RgbColor back = hsl_to_rgb(gray);
    EXPECT_TRUE(near_color(back, RgbColor{128, 128, 128}, 1));
}

TEST(E9_09_Convert, RgbHslRoundTrip) {
    const RgbColor samples[] = {
        {51, 102, 255}, {200, 30, 90}, {10, 200, 120}, {255, 200, 0}, {0, 0, 0},
        {255, 255, 255}, {123, 45, 67}};
    for (const auto& c : samples) {
        const RgbColor back = hsl_to_rgb(rgb_to_hsl(c));
        EXPECT_TRUE(near_color(c, back, 2))
            << "round-trip drift for (" << c.r << "," << c.g << "," << c.b << ")";
    }
}

TEST(E9_09_Convert, HexRoundTrip) {
    EXPECT_EQ(to_hex(RgbColor{51, 102, 255}), "#3366ff");
    EXPECT_EQ(to_hex(RgbColor{0, 0, 0}), "#000000");
    EXPECT_EQ(to_hex(RgbColor{255, 255, 255}), "#ffffff");

    RgbColor c;
    ASSERT_TRUE(from_hex("#3366ff", c));
    EXPECT_EQ(c, (RgbColor{51, 102, 255}));

    // 無 '#' 前綴亦可。
    ASSERT_TRUE(from_hex("3366FF", c));
    EXPECT_EQ(c, (RgbColor{51, 102, 255}));

    // 3 位簡寫展開。
    ASSERT_TRUE(from_hex("#f0a", c));
    EXPECT_EQ(c, (RgbColor{255, 0, 170}));
}

TEST(E9_09_Convert, HexInvalidReportsNotSilent) {
    RgbColor c{9, 9, 9};
    EXPECT_FALSE(from_hex("#12", c));      // 長度不符。
    EXPECT_FALSE(from_hex("#12345g", c));  // 非 16 進位字元。
    EXPECT_FALSE(from_hex("xyz", c));      // 3 位但非法字元。
    EXPECT_EQ(c, (RgbColor{9, 9, 9}));     // 失敗時 out 不動。
}

// -----------------------------------------------------------------------------
// WCAG 對比度 / 亮度
// -----------------------------------------------------------------------------

TEST(E9_09_Contrast, BlackWhiteMaxRatio) {
    // 黑白對比 = 21:1（WCAG 上限）。
    EXPECT_NEAR(contrast_ratio(RgbColor{0, 0, 0}, RgbColor{255, 255, 255}), 21.0, 0.01);
    // 對稱：交換順序結果相同。
    EXPECT_NEAR(contrast_ratio(RgbColor{255, 255, 255}, RgbColor{0, 0, 0}), 21.0, 0.01);
    // 同色對比 = 1:1。
    EXPECT_NEAR(contrast_ratio(RgbColor{80, 80, 80}, RgbColor{80, 80, 80}), 1.0, 1e-9);
}

TEST(E9_09_Contrast, LuminanceOrdering) {
    EXPECT_LT(relative_luminance(RgbColor{0, 0, 0}),
              relative_luminance(RgbColor{128, 128, 128}));
    EXPECT_LT(relative_luminance(RgbColor{128, 128, 128}),
              relative_luminance(RgbColor{255, 255, 255}));
    EXPECT_NEAR(relative_luminance(RgbColor{255, 255, 255}), 1.0, 1e-9);
    EXPECT_NEAR(relative_luminance(RgbColor{0, 0, 0}), 0.0, 1e-9);
}

// -----------------------------------------------------------------------------
// 明度階
// -----------------------------------------------------------------------------

TEST(E9_09_Scale, LightnessScaleMonotonic) {
    const auto scale = lightness_scale(RgbColor{51, 102, 255}, 5);
    ASSERT_EQ(scale.size(), 5u);
    // 由暗到亮：相對亮度嚴格遞增。
    for (std::size_t i = 1; i < scale.size(); ++i) {
        EXPECT_LT(relative_luminance(scale[i - 1]), relative_luminance(scale[i]));
    }
}

TEST(E9_09_Scale, LightnessScaleEdgeCounts) {
    EXPECT_TRUE(lightness_scale(RgbColor{10, 20, 30}, 0).empty());
    EXPECT_TRUE(lightness_scale(RgbColor{10, 20, 30}, -3).empty());
    const auto one = lightness_scale(RgbColor{10, 20, 30}, 1);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], (RgbColor{10, 20, 30}));
}

// -----------------------------------------------------------------------------
// 各配色方案生成 + 色相環關係
// -----------------------------------------------------------------------------

TEST(E9_09_Generate, PrimaryEqualsSeedAndReadable) {
    const PaletteGenerator gen;
    const RgbColor seed{51, 102, 255};
    for (ColorScheme s : {ColorScheme::Complementary, ColorScheme::Analogous,
                          ColorScheme::Triadic, ColorScheme::Monochromatic}) {
        const PaletteResult r = gen.generate(seed, s);
        ASSERT_TRUE(r.ok) << r.message;
        EXPECT_EQ(r.palette.primary, seed);
        EXPECT_EQ(r.palette.seed, seed);
        EXPECT_EQ(r.palette.scheme, s);
        // 前景 / 背景保證 WCAG AA。
        EXPECT_GE(contrast_ratio(r.palette.foreground, r.palette.background), 4.5)
            << "scheme " << static_cast<int>(s);
    }
}

TEST(E9_09_Generate, ComplementaryHueOpposite) {
    const PaletteGenerator gen;
    const RgbColor seed{51, 102, 255};  // 藍，h≈225。
    const PaletteResult r = gen.generate(seed, ColorScheme::Complementary);
    ASSERT_TRUE(r.ok);
    const double hp = rgb_to_hsl(r.palette.primary).h;
    const double hs = rgb_to_hsl(r.palette.secondary).h;
    // 次色色相 ≈ 主色 + 180（環狀差取最小角）。
    double diff = std::fmod(std::abs(hp - hs), 360.0);
    if (diff > 180.0) diff = 360.0 - diff;
    EXPECT_NEAR(diff, 180.0, 1.0);
}

TEST(E9_09_Generate, TriadicHues120Apart) {
    const PaletteGenerator gen;
    const RgbColor seed{200, 30, 30};
    const PaletteResult r = gen.generate(seed, ColorScheme::Triadic);
    ASSERT_TRUE(r.ok);
    const double hp = rgb_to_hsl(r.palette.primary).h;
    const double hs = rgb_to_hsl(r.palette.secondary).h;
    const double ha = rgb_to_hsl(r.palette.accent).h;
    EXPECT_NEAR(std::fmod(hs - hp + 360.0, 360.0), 120.0, 1.0);
    EXPECT_NEAR(std::fmod(ha - hp + 360.0, 360.0), 240.0, 1.0);
}

TEST(E9_09_Generate, AnalogousHuesAdjacent) {
    const PaletteGenerator gen;
    const RgbColor seed{51, 102, 255};
    const PaletteResult r = gen.generate(seed, ColorScheme::Analogous);
    ASSERT_TRUE(r.ok);
    const double hp = rgb_to_hsl(r.palette.primary).h;
    const double hs = rgb_to_hsl(r.palette.secondary).h;
    const double ha = rgb_to_hsl(r.palette.accent).h;
    EXPECT_NEAR(std::fmod(hs - hp + 360.0, 360.0), 30.0, 1.0);
    EXPECT_NEAR(std::fmod(ha - hp + 360.0, 360.0), 330.0, 1.0);  // -30°。
}

TEST(E9_09_Generate, MonochromaticSameHue) {
    const PaletteGenerator gen;
    const RgbColor seed{51, 102, 255};
    const PaletteResult r = gen.generate(seed, ColorScheme::Monochromatic);
    ASSERT_TRUE(r.ok);
    const double hp = rgb_to_hsl(r.palette.primary).h;
    const double hs = rgb_to_hsl(r.palette.secondary).h;
    // 同色相（僅明度 / 飽和度變化）。
    EXPECT_NEAR(hp, hs, 1.5);
}

TEST(E9_09_Generate, StatusColorsPresentAndDistinct) {
    const PaletteGenerator gen;
    const PaletteResult r = gen.generate(RgbColor{51, 102, 255}, ColorScheme::Triadic);
    ASSERT_TRUE(r.ok);
    // 狀態色語意色相：success≈綠、warning≈琥珀、error≈紅、info≈藍。
    EXPECT_NEAR(rgb_to_hsl(r.palette.success).h, 140.0, 3.0);
    EXPECT_NEAR(rgb_to_hsl(r.palette.warning).h, 40.0, 3.0);
    EXPECT_NEAR(rgb_to_hsl(r.palette.info).h, 210.0, 3.0);
    // error 與 success 不同色。
    EXPECT_NE(r.palette.error, r.palette.success);
}

// -----------------------------------------------------------------------------
// 無效種子色明確報錯（不靜默）
// -----------------------------------------------------------------------------

TEST(E9_09_Invalid, OutOfRangeSeedFails) {
    const PaletteGenerator gen;
    EXPECT_FALSE(is_valid_color(RgbColor{300, 0, 0}));
    EXPECT_FALSE(is_valid_color(RgbColor{0, -1, 0}));

    const PaletteResult r = gen.generate(RgbColor{256, 0, 0}, ColorScheme::Complementary);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.message.empty());
}

TEST(E9_09_Invalid, HexSeedFailsCleanly) {
    const PaletteGenerator gen;
    const PaletteResult r = gen.generate_from_hex("not-a-color", ColorScheme::Triadic);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.message.empty());

    const PaletteResult ok = gen.generate_from_hex("#3366ff", ColorScheme::Triadic);
    ASSERT_TRUE(ok.ok) << ok.message;
    EXPECT_EQ(ok.palette.primary, (RgbColor{51, 102, 255}));
}

// -----------------------------------------------------------------------------
// 決定性：同種子同方案 → 位元等價
// -----------------------------------------------------------------------------

TEST(E9_09_Deterministic, SameInputSameOutput) {
    const PaletteGenerator gen;
    const RgbColor seed{88, 144, 210};
    const PaletteResult a = gen.generate(seed, ColorScheme::Analogous);
    const PaletteResult b = gen.generate(seed, ColorScheme::Analogous);
    ASSERT_TRUE(a.ok && b.ok);
    EXPECT_EQ(a.palette, b.palette);

    // 不同方案 → 不同結果（至少次色不同）。
    const PaletteResult c = gen.generate(seed, ColorScheme::Triadic);
    ASSERT_TRUE(c.ok);
    EXPECT_NE(a.palette.secondary, c.palette.secondary);
}

// -----------------------------------------------------------------------------
// to_theme_data 橋接 E9-04
// -----------------------------------------------------------------------------

TEST(E9_09_Bridge, ToThemeDataShape) {
    const PaletteGenerator gen;
    const PaletteResult r = gen.generate(RgbColor{51, 102, 255}, ColorScheme::Complementary);
    ASSERT_TRUE(r.ok);

    const ThemeData td = gen.to_theme_data(r.palette, "ocean");
    EXPECT_EQ(td.name, "ocean");
    ASSERT_TRUE(td.attributes.is_map());

    // 九個色角色皆存在且為 hex 字串。
    for (const char* key : {"primary", "secondary", "accent", "background", "foreground",
                            "success", "warning", "error", "info"}) {
        const Value* v = td.attributes.find(key);
        ASSERT_NE(v, nullptr) << "missing key " << key;
        ASSERT_TRUE(v->is_string());
        EXPECT_EQ(v->as_string().size(), 7u);      // "#rrggbb"
        EXPECT_EQ(v->as_string()[0], '#');
    }
    // primary 的 hex 對應種子色。
    EXPECT_EQ(td.attributes.at("primary").as_string(), to_hex(r.palette.primary));
}

TEST(E9_09_Bridge, FeedsThemeManagerAndNotifies) {
    const PaletteGenerator gen;
    const PaletteResult r = gen.generate(RgbColor{200, 30, 90}, ColorScheme::Triadic);
    ASSERT_TRUE(r.ok);

    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("sunset", gen.to_theme_data(r.palette, "sunset")).ok);

    std::string applied;
    mgr.on_theme_change([&applied](const ThemeData& d) { applied = d.name; });

    ASSERT_TRUE(mgr.switch_to("sunset").ok);
    EXPECT_EQ(mgr.current_name(), "sunset");
    EXPECT_EQ(applied, "sunset");
    // 生成的配色屬性經 ThemeManager 原樣保留。
    EXPECT_TRUE(mgr.current().attributes.contains("accent"));
}
