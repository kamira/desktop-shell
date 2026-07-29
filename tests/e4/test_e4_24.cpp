// E4-24 反鋸齒與內距 — gtest 契約測試
//
// 涵蓋：AA 模式（合法 / 未知列舉值）、渲染品質（合法 / 未知列舉值）、內距設定
// （比例 / 具名權杖）、內距夾限（越界比例）、無效輸入報錯（非有限值 / 未知權杖 /
// 未知 InsetUnit）、render_model()（未套用回 nullptr、成功套用後可查詢、失敗不覆寫）。
#include "render_style.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

using ds::render::AntiAliasMode;
using ds::render::Insets;
using ds::render::InsetUnit;
using ds::render::InsetValue;
using ds::render::is_valid;
using ds::render::RenderConfigStatus;
using ds::render::RenderModel;
using ds::render::RenderQuality;
using ds::render::RenderStyle;
using ds::render::RenderStyleService;
using ds::render::resolve_spacing_token;
using ds::render::SpacingToken;

namespace {
constexpr float kEps = 1e-6f;
}

// --- AntiAliasMode ---
TEST(AntiAliasModeTest, KnownValuesAreValid) {
    EXPECT_TRUE(is_valid(AntiAliasMode::None));
    EXPECT_TRUE(is_valid(AntiAliasMode::Grayscale));
    EXPECT_TRUE(is_valid(AntiAliasMode::Subpixel));
}

TEST(AntiAliasModeTest, OutOfRangeValueIsInvalid) {
    auto bogus = static_cast<AntiAliasMode>(99);
    EXPECT_FALSE(is_valid(bogus));
}

// --- RenderQuality ---
TEST(RenderQualityTest, KnownValuesAreValid) {
    EXPECT_TRUE(is_valid(RenderQuality::Low));
    EXPECT_TRUE(is_valid(RenderQuality::Medium));
    EXPECT_TRUE(is_valid(RenderQuality::High));
}

TEST(RenderQualityTest, OutOfRangeValueIsInvalid) {
    auto bogus = static_cast<RenderQuality>(42);
    EXPECT_FALSE(is_valid(bogus));
}

// --- SpacingToken / resolve_spacing_token ---
TEST(SpacingTokenTest, KnownValuesAreValid) {
    EXPECT_TRUE(is_valid(SpacingToken::None));
    EXPECT_TRUE(is_valid(SpacingToken::XSmall));
    EXPECT_TRUE(is_valid(SpacingToken::Small));
    EXPECT_TRUE(is_valid(SpacingToken::Medium));
    EXPECT_TRUE(is_valid(SpacingToken::Large));
    EXPECT_TRUE(is_valid(SpacingToken::XLarge));
}

TEST(SpacingTokenTest, OutOfRangeValueIsInvalid) {
    auto bogus = static_cast<SpacingToken>(7);
    EXPECT_FALSE(is_valid(bogus));
}

TEST(SpacingTokenTest, NoneResolvesToZero) {
    EXPECT_NEAR(resolve_spacing_token(SpacingToken::None), 0.0f, kEps);
}

TEST(SpacingTokenTest, ResolvedProportionsAreMonotonicIncreasing) {
    float none = resolve_spacing_token(SpacingToken::None);
    float xsmall = resolve_spacing_token(SpacingToken::XSmall);
    float small = resolve_spacing_token(SpacingToken::Small);
    float medium = resolve_spacing_token(SpacingToken::Medium);
    float large = resolve_spacing_token(SpacingToken::Large);
    float xlarge = resolve_spacing_token(SpacingToken::XLarge);
    EXPECT_LT(none, xsmall);
    EXPECT_LT(xsmall, small);
    EXPECT_LT(small, medium);
    EXPECT_LT(medium, large);
    EXPECT_LT(large, xlarge);
}

TEST(SpacingTokenTest, ResolvedProportionsStayWithinUnitRange) {
    EXPECT_GE(resolve_spacing_token(SpacingToken::XLarge), 0.0f);
    EXPECT_LE(resolve_spacing_token(SpacingToken::XLarge), 1.0f);
}

// --- InsetUnit ---
TEST(InsetUnitTest, KnownValuesAreValid) {
    EXPECT_TRUE(is_valid(InsetUnit::Proportion));
    EXPECT_TRUE(is_valid(InsetUnit::Named));
}

TEST(InsetUnitTest, OutOfRangeValueIsInvalid) {
    auto bogus = static_cast<InsetUnit>(5);
    EXPECT_FALSE(is_valid(bogus));
}

// --- InsetValue / Insets 具名工廠 ---
TEST(InsetValueTest, FromProportionSetsUnitAndValue) {
    InsetValue v = InsetValue::from_proportion(0.25f);
    EXPECT_EQ(static_cast<int>(v.unit), static_cast<int>(InsetUnit::Proportion));
    EXPECT_NEAR(v.proportion, 0.25f, kEps);
}

TEST(InsetValueTest, FromTokenSetsUnitAndToken) {
    InsetValue v = InsetValue::from_token(SpacingToken::Large);
    EXPECT_EQ(static_cast<int>(v.unit), static_cast<int>(InsetUnit::Named));
    EXPECT_EQ(static_cast<int>(v.token), static_cast<int>(SpacingToken::Large));
}

TEST(InsetsTest, NoneIsAllZeroProportion) {
    Insets in = Insets::none();
    EXPECT_EQ(static_cast<int>(in.top.unit), static_cast<int>(InsetUnit::Proportion));
    EXPECT_NEAR(in.top.proportion, 0.0f, kEps);
    EXPECT_NEAR(in.right.proportion, 0.0f, kEps);
    EXPECT_NEAR(in.bottom.proportion, 0.0f, kEps);
    EXPECT_NEAR(in.left.proportion, 0.0f, kEps);
}

TEST(InsetsTest, UniformAppliesSameValueToAllSides) {
    Insets in = Insets::uniform(InsetValue::from_proportion(0.1f));
    EXPECT_NEAR(in.top.proportion, 0.1f, kEps);
    EXPECT_NEAR(in.right.proportion, 0.1f, kEps);
    EXPECT_NEAR(in.bottom.proportion, 0.1f, kEps);
    EXPECT_NEAR(in.left.proportion, 0.1f, kEps);
}

TEST(InsetsTest, SymmetricSplitsHorizontalAndVertical) {
    Insets in = Insets::symmetric(InsetValue::from_proportion(0.2f),
                                   InsetValue::from_proportion(0.3f));
    EXPECT_NEAR(in.left.proportion, 0.2f, kEps);
    EXPECT_NEAR(in.right.proportion, 0.2f, kEps);
    EXPECT_NEAR(in.top.proportion, 0.3f, kEps);
    EXPECT_NEAR(in.bottom.proportion, 0.3f, kEps);
}

// --- RenderStyleService::apply / render_model —— 基本成功路徑 ---
TEST(RenderStyleServiceTest, NoModelBeforeAnyApply) {
    RenderStyleService svc;
    EXPECT_FALSE(svc.has_model());
    EXPECT_EQ(svc.render_model(), nullptr);
}

TEST(RenderStyleServiceTest, ApplyDefaultStyleSucceeds) {
    RenderStyleService svc;
    RenderStyle style;  // 預設：Grayscale / Medium / 零內距
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Ok));
    ASSERT_TRUE(svc.has_model());
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(static_cast<int>(model->aa), static_cast<int>(AntiAliasMode::Grayscale));
    EXPECT_EQ(static_cast<int>(model->quality), static_cast<int>(RenderQuality::Medium));
    EXPECT_NEAR(model->inset_top, 0.0f, kEps);
    EXPECT_NEAR(model->inset_right, 0.0f, kEps);
    EXPECT_NEAR(model->inset_bottom, 0.0f, kEps);
    EXPECT_NEAR(model->inset_left, 0.0f, kEps);
}

TEST(RenderStyleServiceTest, ApplyWithProportionInsetsResolvesDirectly) {
    RenderStyleService svc;
    RenderStyle style;
    style.aa = AntiAliasMode::Subpixel;
    style.quality = RenderQuality::High;
    style.insets = Insets::uniform(InsetValue::from_proportion(0.05f));
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Ok));
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(static_cast<int>(model->aa), static_cast<int>(AntiAliasMode::Subpixel));
    EXPECT_EQ(static_cast<int>(model->quality), static_cast<int>(RenderQuality::High));
    EXPECT_NEAR(model->inset_top, 0.05f, kEps);
    EXPECT_NEAR(model->inset_right, 0.05f, kEps);
    EXPECT_NEAR(model->inset_bottom, 0.05f, kEps);
    EXPECT_NEAR(model->inset_left, 0.05f, kEps);
}

TEST(RenderStyleServiceTest, ApplyWithNamedInsetsResolvesViaTable) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets = Insets::symmetric(InsetValue::from_token(SpacingToken::Small),
                                      InsetValue::from_token(SpacingToken::Large));
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Ok));
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_NEAR(model->inset_left, resolve_spacing_token(SpacingToken::Small), kEps);
    EXPECT_NEAR(model->inset_right, resolve_spacing_token(SpacingToken::Small), kEps);
    EXPECT_NEAR(model->inset_top, resolve_spacing_token(SpacingToken::Large), kEps);
    EXPECT_NEAR(model->inset_bottom, resolve_spacing_token(SpacingToken::Large), kEps);
}

TEST(RenderStyleServiceTest, ApplyWithMixedNamedAndProportionInsets) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets.top = InsetValue::from_token(SpacingToken::XSmall);
    style.insets.right = InsetValue::from_proportion(0.3f);
    style.insets.bottom = InsetValue::from_token(SpacingToken::XLarge);
    style.insets.left = InsetValue::from_proportion(0.0f);
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Ok));
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_NEAR(model->inset_top, resolve_spacing_token(SpacingToken::XSmall), kEps);
    EXPECT_NEAR(model->inset_right, 0.3f, kEps);
    EXPECT_NEAR(model->inset_bottom, resolve_spacing_token(SpacingToken::XLarge), kEps);
    EXPECT_NEAR(model->inset_left, 0.0f, kEps);
}

// --- 內距夾限（有限但越界 → clamp，非錯誤）---
TEST(RenderStyleServiceTest, ProportionAboveOneIsClampedNotRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets = Insets::uniform(InsetValue::from_proportion(1.75f));
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Ok));
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_NEAR(model->inset_top, 1.0f, kEps);
    EXPECT_NEAR(model->inset_right, 1.0f, kEps);
    EXPECT_NEAR(model->inset_bottom, 1.0f, kEps);
    EXPECT_NEAR(model->inset_left, 1.0f, kEps);
}

TEST(RenderStyleServiceTest, NegativeProportionIsClampedToZero) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets = Insets::uniform(InsetValue::from_proportion(-0.5f));
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Ok));
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_NEAR(model->inset_top, 0.0f, kEps);
}

// --- 無效輸入報錯（不靜默）---
TEST(RenderStyleServiceTest, UnknownAntiAliasModeIsRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.aa = static_cast<AntiAliasMode>(123);
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));
    EXPECT_FALSE(svc.has_model());
    EXPECT_EQ(svc.render_model(), nullptr);
}

TEST(RenderStyleServiceTest, UnknownRenderQualityIsRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.quality = static_cast<RenderQuality>(123);
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));
    EXPECT_FALSE(svc.has_model());
}

TEST(RenderStyleServiceTest, NanProportionInsetIsRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets.top = InsetValue::from_proportion(std::numeric_limits<float>::quiet_NaN());
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));
    EXPECT_FALSE(svc.has_model());
}

TEST(RenderStyleServiceTest, InfiniteProportionInsetIsRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets.left = InsetValue::from_proportion(std::numeric_limits<float>::infinity());
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));
    EXPECT_FALSE(svc.has_model());
}

TEST(RenderStyleServiceTest, UnknownSpacingTokenIsRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets.bottom = InsetValue::from_token(static_cast<SpacingToken>(88));
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));
    EXPECT_FALSE(svc.has_model());
}

TEST(RenderStyleServiceTest, UnknownInsetUnitIsRejected) {
    RenderStyleService svc;
    RenderStyle style;
    style.insets.right.unit = static_cast<InsetUnit>(9);
    RenderConfigStatus status = svc.apply(style);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));
    EXPECT_FALSE(svc.has_model());
}

// --- 失敗的 apply 不覆寫既有成功設定 ---
TEST(RenderStyleServiceTest, FailedApplyDoesNotOverwritePreviousModel) {
    RenderStyleService svc;
    RenderStyle good;
    good.aa = AntiAliasMode::Subpixel;
    good.insets = Insets::uniform(InsetValue::from_proportion(0.2f));
    ASSERT_EQ(static_cast<int>(svc.apply(good)), static_cast<int>(RenderConfigStatus::Ok));

    RenderStyle bad;
    bad.aa = static_cast<AntiAliasMode>(200);
    RenderConfigStatus status = svc.apply(bad);
    EXPECT_EQ(static_cast<int>(status), static_cast<int>(RenderConfigStatus::Invalid));

    // 仍保留前一次成功套用的設定，未被無效輸入破壞。
    ASSERT_TRUE(svc.has_model());
    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(static_cast<int>(model->aa), static_cast<int>(AntiAliasMode::Subpixel));
    EXPECT_NEAR(model->inset_top, 0.2f, kEps);
}

// --- 重新 apply 成功後覆寫舊設定 ---
TEST(RenderStyleServiceTest, SecondSuccessfulApplyOverwritesModel) {
    RenderStyleService svc;
    RenderStyle first;
    first.aa = AntiAliasMode::None;
    ASSERT_EQ(static_cast<int>(svc.apply(first)), static_cast<int>(RenderConfigStatus::Ok));

    RenderStyle second;
    second.aa = AntiAliasMode::Subpixel;
    second.quality = RenderQuality::High;
    ASSERT_EQ(static_cast<int>(svc.apply(second)), static_cast<int>(RenderConfigStatus::Ok));

    const RenderModel* model = svc.render_model();
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(static_cast<int>(model->aa), static_cast<int>(AntiAliasMode::Subpixel));
    EXPECT_EQ(static_cast<int>(model->quality), static_cast<int>(RenderQuality::High));
}
