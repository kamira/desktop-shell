// tests/c2/test_c2_06.cpp — C2-06 動畫 / 序列圖 widget（gtest）
//
// 涵蓋：configure（幀清單 / fps / 循環套用、選填欄位省略沿用預設、重新設定取代並重置進度）、
// play/pause、advance 逐幀推進、loop 循環繞回 / 單次播放完成、current_frame 查詢、E4-07 整合
// （render_model 反映目前幀 + 目標 surface 綁定 C1-01 基底 id）、空幀降級（未設定時安全查詢 /
// 播放）、以及各類無效設定（非 map / frames 缺失或非 List 或空清單 / 幀項結構或具名值不合法 /
// fps 非正或非有限或非數字 / loop 非 bool，一律不部分套用）。
#include "animation_widget.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::elements::ImageRenderModel;
using ds::format::Value;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::profiles::SkinProfile;
using ds::widgets::AnimationWidget;
using ds::widgets::AnimationWidgetStatus;

namespace {

Value make_frame(const std::string& ref, double width, double height) {
    return Value::map({
        {"ref", Value::string(ref)},
        {"width", Value::number(width, /*integral=*/true)},
        {"height", Value::number(height, /*integral=*/true)},
    });
}

// 三幀定義：frame0/frame1/frame2，皆 16x16；fps=2、不循環（單次播放，便於測試 finished）。
Value three_frame_definition_no_loop() {
    return Value::map({
        {"frames", Value::list({
                       make_frame("res://frame0", 16, 16),
                       make_frame("res://frame1", 16, 16),
                       make_frame("res://frame2", 16, 16),
                   })},
        {"fps", Value::number(2.0)},
        {"loop", Value::boolean(false)},
    });
}

// 供每個測試各自建構獨立的 skin + widget（skin 不需 load_skin：widget 只取用其具名 id）。
struct Fixture {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.anim", backend, layers};
    AnimationWidget widget{skin};
};

}  // namespace

// -----------------------------------------------------------------------------
// 空幀降級 —— 未 configure 時的安全狀態
// -----------------------------------------------------------------------------

TEST(AnimationWidget, EmptyFramesDegradeSafely) {
    Fixture f;

    EXPECT_EQ(f.widget.frame_count(), 0u);
    EXPECT_EQ(f.widget.current_frame(), 0u);
    EXPECT_TRUE(f.widget.is_finished());  // 空幀序列恆完成（無可播放內容）
    EXPECT_FALSE(f.widget.is_playing());

    // 安全 no-op：不崩潰、不改變狀態。
    f.widget.play();
    f.widget.advance(5);
    f.widget.pause();
    f.widget.reset();
    EXPECT_EQ(f.widget.current_frame(), 0u);

    ImageRenderModel model = f.widget.render_model();
    EXPECT_FALSE(model.has_source);  // 明確降級，不靜默假裝有資料
    // 目標 surface 仍綁定所掛載 C1-01 基底的具名 id（於建構時即綁定，不倚賴 configure）。
    EXPECT_EQ(model.target, f.skin.id());
}

// -----------------------------------------------------------------------------
// configure —— 幀清單 / fps / 循環套用
// -----------------------------------------------------------------------------

TEST(AnimationWidget, ConfigureAppliesFramesFpsAndLoop) {
    Fixture f;

    EXPECT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    EXPECT_EQ(f.widget.frame_count(), 3u);
    EXPECT_DOUBLE_EQ(f.widget.fps(), 2.0);
    EXPECT_FALSE(f.widget.loop());
    EXPECT_EQ(f.widget.current_frame(), 0u);

    ImageRenderModel model = f.widget.render_model();
    EXPECT_TRUE(model.has_source);
    EXPECT_EQ(model.source_reference, "res://frame0");
    EXPECT_EQ(model.source_dimensions.width, 16);
    EXPECT_EQ(model.source_dimensions.height, 16);
    EXPECT_EQ(model.target, f.skin.id());  // E4-07 整合：目標 surface 綁定 C1-01 基底 id
}

TEST(AnimationWidget, ConfigureOmittedFpsAndLoopKeepDefaults) {
    Fixture f;

    Value def = Value::map({
        {"frames", Value::list({make_frame("res://only", 8, 8)})},
    });
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Ok);
    EXPECT_DOUBLE_EQ(f.widget.fps(), 1.0);  // E4-07 元件預設
    EXPECT_TRUE(f.widget.loop());           // E4-07 元件預設（循環）
}

TEST(AnimationWidget, ReconfigureReplacesFramesAndRestartsAtFrameZero) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    f.widget.play();
    f.widget.advance(1);  // fps=2 → 前進 2 幀
    ASSERT_EQ(f.widget.current_frame(), 2u);

    Value replacement = Value::map({
        {"frames", Value::list({make_frame("res://new0", 4, 4), make_frame("res://new1", 4, 4)})},
    });
    EXPECT_EQ(f.widget.configure(replacement), AnimationWidgetStatus::Ok);
    EXPECT_EQ(f.widget.frame_count(), 2u);
    EXPECT_EQ(f.widget.current_frame(), 0u);  // 重新設定即從頭播放
    EXPECT_EQ(f.widget.render_model().source_reference, "res://new0");
}

// -----------------------------------------------------------------------------
// play / pause / advance —— 播放控制與逐幀推進
// -----------------------------------------------------------------------------

TEST(AnimationWidget, PlayPauseTogglesIsPlaying) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);

    EXPECT_FALSE(f.widget.is_playing());  // 預設暫停
    f.widget.play();
    EXPECT_TRUE(f.widget.is_playing());
    f.widget.pause();
    EXPECT_FALSE(f.widget.is_playing());
}

TEST(AnimationWidget, AdvanceNoOpWhenPaused) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    // 未呼叫 play()：暫停中。
    f.widget.advance(10);
    EXPECT_EQ(f.widget.current_frame(), 0u);
}

TEST(AnimationWidget, AdvanceProgressesFramesAccordingToFps) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    f.widget.play();

    EXPECT_EQ(f.widget.current_frame(), 0u);
    f.widget.advance(1);  // fps=2 → 前進 2 幀
    EXPECT_EQ(f.widget.current_frame(), 2u);
}

// -----------------------------------------------------------------------------
// loop 循環 / 單次播放完成
// -----------------------------------------------------------------------------

TEST(AnimationWidget, LoopWrapsAroundFrames) {
    Fixture f;
    Value def = Value::map({
        {"frames", Value::list({make_frame("res://a", 8, 8), make_frame("res://b", 8, 8),
                                make_frame("res://c", 8, 8)})},
        {"fps", Value::number(1.0)},
        {"loop", Value::boolean(true)},
    });
    ASSERT_EQ(f.widget.configure(def), AnimationWidgetStatus::Ok);
    f.widget.play();

    f.widget.advance(1);
    f.widget.advance(1);
    f.widget.advance(1);           // 抵達幀數 → 循環模式繞回第 0 幀
    EXPECT_EQ(f.widget.current_frame(), 0u);
    EXPECT_FALSE(f.widget.is_finished());  // 循環模式恆不完成
}

TEST(AnimationWidget, SingleShotFinishesAtLastFrame) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    f.widget.play();

    f.widget.advance(1);  // fps=2 → 幀 2（最後一幀，index 2）
    EXPECT_EQ(f.widget.current_frame(), 2u);
    EXPECT_TRUE(f.widget.is_finished());

    f.widget.advance(1);  // 播完後安全 no-op，不再前進
    EXPECT_EQ(f.widget.current_frame(), 2u);
}

TEST(AnimationWidget, ResetReturnsToFrameZeroPreservingSettings) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    f.widget.play();
    f.widget.advance(1);
    ASSERT_EQ(f.widget.current_frame(), 2u);

    f.widget.reset();
    EXPECT_EQ(f.widget.current_frame(), 0u);
    EXPECT_DOUBLE_EQ(f.widget.fps(), 2.0);  // fps / loop / 播放狀態不變
    EXPECT_FALSE(f.widget.loop());
    EXPECT_TRUE(f.widget.is_playing());
}

// -----------------------------------------------------------------------------
// 無效設定 —— 一律不部分套用
// -----------------------------------------------------------------------------

TEST(AnimationWidget, ConfigureRejectsNonMapDefinition) {
    Fixture f;
    EXPECT_EQ(f.widget.configure(Value::string("nope")), AnimationWidgetStatus::Invalid);
    EXPECT_EQ(f.widget.frame_count(), 0u);
}

TEST(AnimationWidget, ConfigureRejectsMissingFrames) {
    Fixture f;
    Value def = Value::map({{"fps", Value::number(1.0)}});
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsFramesNotList) {
    Fixture f;
    Value def = Value::map({{"frames", Value::string("res://frame0")}});
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsEmptyFramesList) {
    Fixture f;
    Value def = Value::map({{"frames", Value::list({})}});
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsInvalidFrameItemStructure) {
    Fixture f;
    // 幀項缺 width。
    Value def = Value::map({
        {"frames", Value::list({Value::map({{"ref", Value::string("res://x")}})})},
    });
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsFrameWithEmptyRef) {
    Fixture f;
    Value def = Value::map({{"frames", Value::list({make_frame("", 8, 8)})}});
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsFrameWithNonPositiveDimensions) {
    Fixture f;
    Value def = Value::map({{"frames", Value::list({make_frame("res://x", 0, 8)})}});
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsInvalidFps) {
    Fixture f;
    Value def = Value::map({
        {"frames", Value::list({make_frame("res://x", 8, 8)})},
        {"fps", Value::number(-1.0)},
    });
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, ConfigureRejectsInvalidLoopType) {
    Fixture f;
    Value def = Value::map({
        {"frames", Value::list({make_frame("res://x", 8, 8)})},
        {"loop", Value::string("yes")},  // 非 bool
    });
    EXPECT_EQ(f.widget.configure(def), AnimationWidgetStatus::Invalid);
}

TEST(AnimationWidget, InvalidConfigureDoesNotPartiallyApplyOverExistingConfig) {
    Fixture f;
    ASSERT_EQ(f.widget.configure(three_frame_definition_no_loop()), AnimationWidgetStatus::Ok);
    ASSERT_EQ(f.widget.frame_count(), 3u);

    Value bad = Value::map({
        {"frames", Value::list({make_frame("res://bad", -1, 8)})},
        {"fps", Value::number(99.0)},
        {"loop", Value::boolean(true)},
    });
    EXPECT_EQ(f.widget.configure(bad), AnimationWidgetStatus::Invalid);

    // 既有設定完全不受影響。
    EXPECT_EQ(f.widget.frame_count(), 3u);
    EXPECT_DOUBLE_EQ(f.widget.fps(), 2.0);
    EXPECT_FALSE(f.widget.loop());
    EXPECT_EQ(f.widget.render_model().source_reference, "res://frame0");
}
