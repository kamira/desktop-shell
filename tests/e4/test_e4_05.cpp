// E4-05 過場動畫與緩動 — 契約測試（gtest）
//
// 驗證：
//   - 邊界精確：每條曲線 f(0)=0、f(1)=1。
//   - 越界安全：t<0 夾為 0、t>1 夾為 1。
//   - 單調不減：每條曲線在 [0,1] 上不下降。
//   - 各曲線特性：linear 為恆等、In 慢入（中點低於等速）、Out 快入（中點高於等速）、
//     InOut 對稱、Sine 中點精確 0.5。
//   - Tween 插值：起訖精確、線性中點、easing 影響、advance 累加與單調、
//     duration<=0 瞬間完成、progress / done 語意。
// 純數學、無平台分支。
#include "easing.hpp"
#include "tween.hpp"

#include <gtest/gtest.h>

using ds::render::Easing;
using ds::render::EasingType;
using ds::render::Tween;

namespace {

// 所有曲線種類，供逐條共通性質檢驗。
constexpr EasingType kAllTypes[] = {
    EasingType::Linear,     EasingType::InQuad,   EasingType::OutQuad,
    EasingType::InOutQuad,  EasingType::InCubic,  EasingType::OutCubic,
    EasingType::InOutCubic, EasingType::InSine,   EasingType::OutSine,
    EasingType::InOutSine,
};

// 邊界精確：每條曲線 f(0)=0、f(1)=1。
TEST(Easing, EndpointsExact) {
    for (EasingType t : kAllTypes) {
        EXPECT_DOUBLE_EQ(Easing::apply(t, 0.0), 0.0);
        EXPECT_DOUBLE_EQ(Easing::apply(t, 1.0), 1.0);
    }
}

// 越界輸入被夾到 [0,1]：t<0 等同 0、t>1 等同 1。
TEST(Easing, OutOfRangeClamped) {
    for (EasingType t : kAllTypes) {
        EXPECT_DOUBLE_EQ(Easing::apply(t, -5.0), Easing::apply(t, 0.0));
        EXPECT_DOUBLE_EQ(Easing::apply(t, 7.0), Easing::apply(t, 1.0));
    }
    EXPECT_DOUBLE_EQ(Easing::clamp01(-0.1), 0.0);
    EXPECT_DOUBLE_EQ(Easing::clamp01(1.1), 1.0);
    EXPECT_DOUBLE_EQ(Easing::clamp01(0.42), 0.42);
}

// 單調不減：每條曲線在 [0,1] 取樣皆不下降，且輸出恆在 [0,1]。
TEST(Easing, MonotonicNonDecreasing) {
    for (EasingType t : kAllTypes) {
        double prev = Easing::apply(t, 0.0);
        for (int i = 1; i <= 100; ++i) {
            const double v = Easing::apply(t, i / 100.0);
            EXPECT_GE(v, prev - 1e-12) << "type index sample " << i;
            EXPECT_GE(v, -1e-12);
            EXPECT_LE(v, 1.0 + 1e-12);
            prev = v;
        }
    }
}

// linear 為恆等映射。
TEST(Easing, LinearIsIdentity) {
    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        EXPECT_DOUBLE_EQ(Easing::linear(t), t);
        EXPECT_DOUBLE_EQ(Easing::apply(EasingType::Linear, t), t);
    }
}

// In 曲線慢入：中點 eased 值低於等速 0.5。Out 曲線快入：中點高於 0.5。
TEST(Easing, InSlowOutFastAtMidpoint) {
    EXPECT_LT(Easing::in_quad(0.5), 0.5);
    EXPECT_LT(Easing::in_cubic(0.5), 0.5);
    EXPECT_LT(Easing::in_sine(0.5), 0.5);

    EXPECT_GT(Easing::out_quad(0.5), 0.5);
    EXPECT_GT(Easing::out_cubic(0.5), 0.5);
    EXPECT_GT(Easing::out_sine(0.5), 0.5);

    // 具體值：in_quad(0.5)=0.25、out_quad(0.5)=0.75。
    EXPECT_DOUBLE_EQ(Easing::in_quad(0.5), 0.25);
    EXPECT_DOUBLE_EQ(Easing::out_quad(0.5), 0.75);
    EXPECT_DOUBLE_EQ(Easing::in_cubic(0.5), 0.125);
}

// InOut 曲線中點精確為 0.5，且對稱：f(t)+f(1-t)=1。
TEST(Easing, InOutSymmetricAtMidpoint) {
    EXPECT_NEAR(Easing::in_out_quad(0.5), 0.5, 1e-12);
    EXPECT_NEAR(Easing::in_out_cubic(0.5), 0.5, 1e-12);
    EXPECT_NEAR(Easing::in_out_sine(0.5), 0.5, 1e-12);

    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        EXPECT_NEAR(Easing::in_out_quad(t) + Easing::in_out_quad(1.0 - t),
                    1.0, 1e-12);
        EXPECT_NEAR(Easing::in_out_cubic(t) + Easing::in_out_cubic(1.0 - t),
                    1.0, 1e-12);
        EXPECT_NEAR(Easing::in_out_sine(t) + Easing::in_out_sine(1.0 - t),
                    1.0, 1e-12);
    }
}

// In / Out 互補對偶：out(t) = 1 - in(1-t)。
TEST(Easing, InOutDuality) {
    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        EXPECT_NEAR(Easing::out_quad(t), 1.0 - Easing::in_quad(1.0 - t), 1e-12);
        EXPECT_NEAR(Easing::out_cubic(t), 1.0 - Easing::in_cubic(1.0 - t), 1e-12);
        EXPECT_NEAR(Easing::out_sine(t), 1.0 - Easing::in_sine(1.0 - t), 1e-12);
    }
}

// ---- Tween ----

// 起訖點精確：at(0)=from、at(duration)=to。
TEST(Tween, EndpointsExact) {
    Tween tw(10.0, 20.0, 2.0, EasingType::Linear);
    EXPECT_DOUBLE_EQ(tw.at(0.0), 10.0);
    EXPECT_DOUBLE_EQ(tw.at(2.0), 20.0);
    // 越界時間夾住。
    EXPECT_DOUBLE_EQ(tw.at(-1.0), 10.0);
    EXPECT_DOUBLE_EQ(tw.at(99.0), 20.0);
}

// 線性 tween 中點為算術平均。
TEST(Tween, LinearMidpoint) {
    Tween tw(0.0, 100.0, 4.0, EasingType::Linear);
    EXPECT_DOUBLE_EQ(tw.at(1.0), 25.0);
    EXPECT_DOUBLE_EQ(tw.at(2.0), 50.0);
    EXPECT_DOUBLE_EQ(tw.at(3.0), 75.0);
}

// easing 曲線影響插值：in_quad 中點 = from + (to-from)*0.25。
TEST(Tween, EasingAffectsValue) {
    Tween tw(0.0, 100.0, 2.0, EasingType::InQuad);
    EXPECT_DOUBLE_EQ(tw.at(1.0), 25.0);  // in_quad(0.5)=0.25
    EXPECT_DOUBLE_EQ(tw.at(0.0), 0.0);
    EXPECT_DOUBLE_EQ(tw.at(2.0), 100.0);
}

// 反向 tween（from > to）也正確。
TEST(Tween, ReverseDirection) {
    Tween tw(50.0, 10.0, 4.0, EasingType::Linear);
    EXPECT_DOUBLE_EQ(tw.at(0.0), 50.0);
    EXPECT_DOUBLE_EQ(tw.at(2.0), 30.0);
    EXPECT_DOUBLE_EQ(tw.at(4.0), 10.0);
}

// advance 累加經過時間，回傳當前值；達終點後 done。
TEST(Tween, AdvanceAccumulates) {
    Tween tw(0.0, 10.0, 1.0, EasingType::Linear);
    EXPECT_FALSE(tw.done());
    EXPECT_DOUBLE_EQ(tw.advance(0.25), 2.5);
    EXPECT_DOUBLE_EQ(tw.elapsed(), 0.25);
    EXPECT_DOUBLE_EQ(tw.advance(0.25), 5.0);
    EXPECT_FALSE(tw.done());
    EXPECT_DOUBLE_EQ(tw.advance(0.5), 10.0);
    EXPECT_TRUE(tw.done());
    // 推進過頭仍夾在終點。
    EXPECT_DOUBLE_EQ(tw.advance(5.0), 10.0);
    EXPECT_TRUE(tw.done());
}

// advance 的經過時間單調不減：負 dt 視為 0。
TEST(Tween, AdvanceNegativeIgnored) {
    Tween tw(0.0, 10.0, 1.0, EasingType::Linear);
    tw.advance(0.5);
    const double before = tw.elapsed();
    EXPECT_DOUBLE_EQ(tw.advance(-0.3), 5.0);
    EXPECT_DOUBLE_EQ(tw.elapsed(), before);  // 未回捲
}

// progress 為未經 easing 的線性比例。
TEST(Tween, ProgressIsLinearRatio) {
    Tween tw(0.0, 100.0, 4.0, EasingType::InQuad);
    tw.advance(1.0);
    EXPECT_DOUBLE_EQ(tw.progress(), 0.25);   // 線性比例
    EXPECT_DOUBLE_EQ(tw.value(), 6.25);      // in_quad(0.25)=0.0625 → 6.25
}

// reset 後可重播。
TEST(Tween, ResetReplays) {
    Tween tw(0.0, 10.0, 1.0, EasingType::Linear);
    tw.advance(1.0);
    EXPECT_TRUE(tw.done());
    tw.reset();
    EXPECT_DOUBLE_EQ(tw.elapsed(), 0.0);
    EXPECT_FALSE(tw.done());
    EXPECT_DOUBLE_EQ(tw.value(), 0.0);
}

// duration<=0：瞬間完成，任何時間皆回 to、progress=1、done 恆真。
TEST(Tween, ZeroDurationInstant) {
    Tween tw(0.0, 42.0, 0.0, EasingType::Linear);
    EXPECT_DOUBLE_EQ(tw.at(0.0), 42.0);
    EXPECT_DOUBLE_EQ(tw.at(-1.0), 42.0);
    EXPECT_DOUBLE_EQ(tw.progress(), 1.0);
    EXPECT_TRUE(tw.done());

    Tween neg(5.0, 9.0, -3.0, EasingType::Linear);
    EXPECT_DOUBLE_EQ(neg.value(), 9.0);
    EXPECT_TRUE(neg.done());
}

// 存取器如實回傳建構參數。
TEST(Tween, Accessors) {
    Tween tw(1.5, 3.5, 2.0, EasingType::OutCubic);
    EXPECT_DOUBLE_EQ(tw.from(), 1.5);
    EXPECT_DOUBLE_EQ(tw.to(), 3.5);
    EXPECT_DOUBLE_EQ(tw.duration(), 2.0);
    EXPECT_EQ(tw.easing(), EasingType::OutCubic);
}

}  // namespace
