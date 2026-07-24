// E1-17 DPI 感知與混合 DPI — 契約測試（gtest）
//
// 驗證：單螢幕縮放、混合 DPI 各螢幕獨立不同 scale、未知螢幕保守回預設、
// 相對縮放比值、非法縮放正規化、重複 id 覆蓋、預設拓撲（null 期）語意。
// 相位 1：只驗介面 + null（宣告式預設）行為，不含任何平台分支或絕對座標。
#include "dpi_awareness.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::kernel::DpiInfo;
using ds::kernel::kDefaultScaleFactor;
using ds::kernel::ScreenDpi;

namespace {

// 預設拓撲（null 期）：單一具名主螢幕、縮放為中性預設、非混合 DPI。
TEST(DpiInfo, DefaultsAreSingleNeutralScreen) {
    const DpiInfo info = DpiInfo::defaults();
    EXPECT_EQ(info.size(), 1u);
    EXPECT_TRUE(info.is_known("screen.primary"));
    EXPECT_DOUBLE_EQ(info.scale_of("screen.primary"), kDefaultScaleFactor);
    EXPECT_FALSE(info.is_mixed_dpi());
}

// 單螢幕自訂縮放：scale_of 反映宣告值。
TEST(DpiInfo, SingleScreenScaleFactor) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.retina", "2x 螢幕", 2.0},
    });
    EXPECT_TRUE(info.is_known("screen.retina"));
    EXPECT_DOUBLE_EQ(info.scale_of("screen.retina"), 2.0);
    EXPECT_FALSE(info.is_mixed_dpi());  // 只有一個螢幕
}

// 混合 DPI：各螢幕獨立、不同 scale，查詢彼此不受影響。
TEST(DpiInfo, MixedDpiScreensAreIndependent) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.laptop", "內建 1x", 1.0},
        {"screen.hidpi", "外接 1.5x", 1.5},
        {"screen.retina", "外接 2x", 2.0},
    });
    EXPECT_EQ(info.size(), 3u);
    EXPECT_TRUE(info.is_mixed_dpi());
    EXPECT_DOUBLE_EQ(info.scale_of("screen.laptop"), 1.0);
    EXPECT_DOUBLE_EQ(info.scale_of("screen.hidpi"), 1.5);
    EXPECT_DOUBLE_EQ(info.scale_of("screen.retina"), 2.0);
}

// 全部螢幕同一縮放：非混合 DPI。
TEST(DpiInfo, UniformScreensAreNotMixed) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.a", "1x", 1.0},
        {"screen.b", "1x", 1.0},
    });
    EXPECT_FALSE(info.is_mixed_dpi());
}

// 未知螢幕（核心保守語意）：回中性預設、is_known 為 false、find 回 nullptr。
TEST(DpiInfo, UnknownScreenIsConservativeDefault) {
    const DpiInfo info = DpiInfo::defaults();
    EXPECT_FALSE(info.is_known("screen.does-not-exist"));
    EXPECT_DOUBLE_EQ(info.scale_of("screen.does-not-exist"), kDefaultScaleFactor);
    EXPECT_EQ(info.find("screen.does-not-exist"), nullptr);
}

// 相對縮放：from -> to 的比值 = scale_of(to) / scale_of(from)。
TEST(DpiInfo, RelativeScaleIsRatio) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.laptop", "1x", 1.0},
        {"screen.retina", "2x", 2.0},
    });
    EXPECT_DOUBLE_EQ(info.relative_scale("screen.laptop", "screen.retina"), 2.0);
    EXPECT_DOUBLE_EQ(info.relative_scale("screen.retina", "screen.laptop"), 0.5);
    EXPECT_DOUBLE_EQ(info.relative_scale("screen.laptop", "screen.laptop"), 1.0);
}

// 相對縮放對未知端保守：以中性預設計算，永不除以零。
TEST(DpiInfo, RelativeScaleWithUnknownEnds) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.retina", "2x", 2.0},
    });
    // 未知 from（=1.0）到已知 to（=2.0）：比值 2.0。
    EXPECT_DOUBLE_EQ(info.relative_scale("screen.unknown", "screen.retina"), 2.0);
    // 已知 from（=2.0）到未知 to（=1.0）：比值 0.5。
    EXPECT_DOUBLE_EQ(info.relative_scale("screen.retina", "screen.unknown"), 0.5);
    // 兩端皆未知：皆為預設，比值 1.0。
    EXPECT_DOUBLE_EQ(info.relative_scale("screen.x", "screen.y"), 1.0);
}

// 非法縮放（<= 0）正規化為中性預設（保守：拒絕不合法縮放）。
TEST(DpiInfo, NonPositiveScaleIsNormalized) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.zero", "0 縮放（非法）", 0.0},
        {"screen.neg", "負縮放（非法）", -3.0},
    });
    EXPECT_DOUBLE_EQ(info.scale_of("screen.zero"), kDefaultScaleFactor);
    EXPECT_DOUBLE_EQ(info.scale_of("screen.neg"), kDefaultScaleFactor);
    EXPECT_FALSE(info.is_mixed_dpi());  // 兩者皆被正規化為 1.0
}

// find() 對已知螢幕回傳指向正確欄位的指標。
TEST(DpiInfo, FindReturnsScreenFields) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.hidpi", "外接 1.5x", 1.5},
    });
    const ScreenDpi* s = info.find("screen.hidpi");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->id, "screen.hidpi");
    EXPECT_DOUBLE_EQ(s->scale_factor, 1.5);
    EXPECT_FALSE(s->description.empty());
}

// 空拓撲：任何查詢皆保守回預設 / false / nullptr。
TEST(DpiInfo, EmptyTopologyQueriesAreSafe) {
    DpiInfo info(std::vector<ScreenDpi>{});
    EXPECT_EQ(info.size(), 0u);
    EXPECT_FALSE(info.is_known("anything"));
    EXPECT_DOUBLE_EQ(info.scale_of("anything"), kDefaultScaleFactor);
    EXPECT_FALSE(info.is_mixed_dpi());
    EXPECT_EQ(info.find("anything"), nullptr);
}

// 重複 id：後定義者為準（後端可覆寫先前宣告的縮放）。
TEST(DpiInfo, DuplicateIdLastWins) {
    DpiInfo info(std::vector<ScreenDpi>{
        {"screen.p", "first 1x", 1.0},
        {"screen.p", "second 2x", 2.0},
    });
    const ScreenDpi* s = info.find("screen.p");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->description, "second 2x");
    EXPECT_DOUBLE_EQ(info.scale_of("screen.p"), 2.0);
}

}  // namespace
