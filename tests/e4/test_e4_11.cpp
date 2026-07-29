// E4-11 逐字顯示 — gtest 測試
//
// 涵蓋：advance 逐字增加、速度控制（含小數速度累計）、完成偵測、reset、以 E4-01 排版、
// 經 E4-09 動畫驅動源推進、空文字、render_model 內容、無效輸入（速度 / UTF-8）不靜默。
#include "typewriter_element.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

#include "animation_driver.hpp"
#include "heartbeat_source.hpp"
#include "text_layout.hpp"

using ds::elements::TypewriterElement;
using ds::events::HeartbeatSource;
using ds::render::AnimationDriver;
using ds::render::FixedFontMetrics;
using ds::render::LayoutConstraints;
using ds::render::LayoutResult;

namespace {

// 等寬度量：advance=10、行高=20、ascent=16（承 E4-01 測試慣例）。
FixedFontMetrics mono() { return FixedFontMetrics(10.0, 20.0, 16.0); }

}  // namespace

// --- advance 逐字增加 ---
TEST(TypewriterElement, AdvanceRevealsCharsIncrementally) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("hello");
    tw.set_speed(1.0);  // 1 字元 / tick

    EXPECT_EQ(tw.visible_count(), 0u);
    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 1u);
    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 2u);
    tw.advance(3);
    EXPECT_EQ(tw.visible_count(), 5u);
}

// --- 速度控制：整數倍速 ---
TEST(TypewriterElement, SpeedControlsCharsPerTick) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("abcdefgh");
    tw.set_speed(3.0);  // 每 tick 3 字元

    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 3u);
    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 6u);
}

// --- 速度控制：小數速度（< 1 字元/tick）累計進度不卡住 ---
TEST(TypewriterElement, FractionalSpeedAccumulatesWithoutStalling) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("ab");
    tw.set_speed(0.5);  // 每 2 tick 才顯示 1 字元

    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 0u);  // 累計 0.5，未達 1
    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 1u);  // 累計 1.0
    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 1u);  // 累計 1.5
    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 2u);  // 累計 2.0 → 全顯示
}

// --- 完成偵測 ---
TEST(TypewriterElement, IsCompleteDetection) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("hi");
    tw.set_speed(1.0);

    EXPECT_FALSE(tw.is_complete());
    tw.advance(1);
    EXPECT_FALSE(tw.is_complete());
    tw.advance(1);
    EXPECT_TRUE(tw.is_complete());
    EXPECT_EQ(tw.visible_count(), tw.total_count());

    // 完成後再 advance：安全 no-op，不超過總字數。
    tw.advance(100);
    EXPECT_TRUE(tw.is_complete());
    EXPECT_EQ(tw.visible_count(), 2u);
}

// --- reset：進度歸零，文字 / 速度不變 ---
TEST(TypewriterElement, ResetRestartsProgress) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("world");
    tw.set_speed(2.0);
    tw.advance(2);
    EXPECT_EQ(tw.visible_count(), 4u);

    tw.reset();
    EXPECT_EQ(tw.visible_count(), 0u);
    EXPECT_FALSE(tw.is_complete());
    EXPECT_EQ(tw.speed(), 2.0);  // 速度保留

    tw.advance(1);
    EXPECT_EQ(tw.visible_count(), 2u);  // 速度延續生效
}

// --- set_text 重設進度 ---
TEST(TypewriterElement, SetTextResetsProgress) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("abc");
    tw.set_speed(1.0);
    tw.advance(3);
    EXPECT_TRUE(tw.is_complete());

    tw.set_text("de");  // 換新文字：重新從頭
    EXPECT_EQ(tw.visible_count(), 0u);
    EXPECT_EQ(tw.total_count(), 2u);
    EXPECT_FALSE(tw.is_complete());
}

// --- 以 E4-01 排版：render_model 反映目前已顯示前綴 ---
TEST(TypewriterElement, RenderModelUsesE4_01Layout) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("abcde");
    tw.set_speed(1.0);

    tw.advance(3);  // 顯示 "abc"
    LayoutResult r = tw.render_model();
    ASSERT_EQ(r.glyphs.size(), 3u);
    EXPECT_EQ(r.glyphs[0].codepoint, U'a');
    EXPECT_EQ(r.glyphs[1].codepoint, U'b');
    EXPECT_EQ(r.glyphs[2].codepoint, U'c');
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_NEAR(r.lines[0].width, 30.0, 1e-9);  // 3 字 × advance 10

    tw.advance(2);  // 顯示全部 "abcde"
    LayoutResult full = tw.render_model();
    EXPECT_EQ(full.glyphs.size(), 5u);
}

// --- render_model 遵循排版約束（NFR-02：相對佈局，無絕對座標） ---
TEST(TypewriterElement, RenderModelRespectsConstraintsAndRelativeLayout) {
    FixedFontMetrics fm = mono();
    LayoutConstraints c;
    c.align = ds::render::TextAlign::Right;
    c.max_width = 100.0;
    TypewriterElement tw(fm, c);
    tw.set_text("ab");
    tw.set_speed(1.0);
    tw.advance(2);

    LayoutResult r = tw.render_model();
    ASSERT_EQ(r.lines.size(), 1u);
    // Right 對齊：行起點相對偏移 > 0（相對於盒左緣，非螢幕絕對座標）。
    EXPECT_GT(r.lines[0].x, 0.0);
    EXPECT_TRUE(r.surface.empty());  // 未綁定 surface：具名空字串（非數字 z-order）
}

// --- 經 E4-09 動畫驅動源推進 ---
TEST(TypewriterElement, DrivenByE4_09AnimationDriver) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("hello");
    tw.set_speed(1.0);

    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);
    tw.attach(drv);

    EXPECT_EQ(tw.visible_count(), 0u);
    hb.advance(3);  // 3 次脈衝，各 dt=1
    EXPECT_EQ(tw.visible_count(), 3u);
    hb.advance(10);  // 超過剩餘字數：夾在總字數
    EXPECT_TRUE(tw.is_complete());
    EXPECT_EQ(tw.visible_count(), 5u);
}

// --- 空文字 ---
TEST(TypewriterElement, EmptyTextIsImmediatelyComplete) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    // 未呼叫 set_text：預設空文字。
    EXPECT_EQ(tw.total_count(), 0u);
    EXPECT_EQ(tw.visible_count(), 0u);
    EXPECT_TRUE(tw.is_complete());

    LayoutResult r = tw.render_model();
    EXPECT_TRUE(r.glyphs.empty());
    EXPECT_TRUE(r.lines.empty());

    tw.advance(5);  // 空文字上 advance：安全 no-op
    EXPECT_EQ(tw.visible_count(), 0u);
}

TEST(TypewriterElement, SetTextEmptyStringIsImmediatelyComplete) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("abc");
    tw.advance(1);
    EXPECT_FALSE(tw.is_complete());

    tw.set_text("");  // 重設為空字串
    EXPECT_EQ(tw.total_count(), 0u);
    EXPECT_TRUE(tw.is_complete());
}

// --- 無效速度不靜默 ---
TEST(TypewriterElement, InvalidSpeedThrows) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    EXPECT_THROW(tw.set_speed(0.0), std::invalid_argument);
    EXPECT_THROW(tw.set_speed(-1.0), std::invalid_argument);
    EXPECT_THROW(tw.set_speed(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    EXPECT_THROW(tw.set_speed(std::numeric_limits<double>::infinity()), std::invalid_argument);
}

// --- 無效 UTF-8 文字不靜默 ---
TEST(TypewriterElement, InvalidUtf8TextThrows) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    const std::string bad = "\xFF\xFE";  // 非法 UTF-8 序列
    EXPECT_THROW(tw.set_text(bad), std::invalid_argument);
}

// --- 多字節 UTF-8：以碼位（非位元組）計數逐字顯示 ---
TEST(TypewriterElement, MultiByteUtf8CountedByCodepoint) {
    FixedFontMetrics fm = mono();
    TypewriterElement tw(fm);
    tw.set_text("a\xC3\xA9z");  // 'a' + 'é'(U+00E9, 2 bytes) + 'z' = 3 個碼位
    EXPECT_EQ(tw.total_count(), 3u);
    tw.set_speed(1.0);

    tw.advance(1);
    LayoutResult r1 = tw.render_model();
    ASSERT_EQ(r1.glyphs.size(), 1u);
    EXPECT_EQ(r1.glyphs[0].codepoint, U'a');

    tw.advance(1);
    LayoutResult r2 = tw.render_model();
    ASSERT_EQ(r2.glyphs.size(), 2u);
    EXPECT_EQ(r2.glyphs[1].codepoint, static_cast<ds::render::CodePoint>(0x00E9));

    tw.advance(1);
    EXPECT_TRUE(tw.is_complete());
    LayoutResult r3 = tw.render_model();
    ASSERT_EQ(r3.glyphs.size(), 3u);
    EXPECT_EQ(r3.glyphs[2].codepoint, U'z');
}
