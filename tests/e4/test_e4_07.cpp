// E4-07 幀序列動畫 — gtest 測試
//
// 涵蓋：advance 逐幀推進、fps 控制（含小數 fps 不卡住）、循環 / 單次播放、play/pause、
// 目前幀查詢、經 E4-09 AnimationDriver 心跳驅動、以 E4-02 顯示目前幀（render_model）、
// 空幀序列 / 無效 fps 報錯不靜默、reset。
#include "frame_animation_element.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <vector>

#include "animation_driver.hpp"
#include "heartbeat_source.hpp"
#include "image_element.hpp"

using ds::elements::FrameAnimationElement;
using ds::elements::FrameAnimationStatus;
using ds::elements::ImageDimensions;
using ds::elements::ImageRenderModel;
using ds::elements::ImageSource;
using ds::elements::ImageStatus;
using ds::elements::MemoryImageSource;
using ds::elements::NullImageSource;
using ds::elements::ScaleMode;
using ds::events::HeartbeatSource;
using ds::render::AnimationDriver;

namespace {

// 三幀的尋常幀序列：res://f0, res://f1, res://f2，皆 16x16。
std::vector<MemoryImageSource> three_frames() {
    return {
        MemoryImageSource("res://f0", ImageDimensions{16, 16}),
        MemoryImageSource("res://f1", ImageDimensions{16, 16}),
        MemoryImageSource("res://f2", ImageDimensions{16, 16}),
    };
}

std::vector<std::reference_wrapper<const ImageSource>> as_refs(
    const std::vector<MemoryImageSource>& frames) {
    std::vector<std::reference_wrapper<const ImageSource>> refs;
    refs.reserve(frames.size());
    for (const auto& f : frames) {
        refs.emplace_back(f);
    }
    return refs;
}

}  // namespace

// --- set_frames 成功套用 + 從頭播放 ---
TEST(FrameAnimationElement, SetFramesSucceedsAndStartsAtFrameZero) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    EXPECT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    EXPECT_EQ(anim.frame_count(), 3u);
    EXPECT_EQ(anim.current_frame(), 0u);

    ImageRenderModel model = anim.render_model();
    EXPECT_TRUE(model.has_source);
    EXPECT_EQ(model.source_reference, "res://f0");
    EXPECT_EQ(model.source_dimensions.width, 16);
    EXPECT_EQ(model.source_dimensions.height, 16);
}

// --- advance 逐幀推進（fps=1：每 tick 前進 1 幀） ---
TEST(FrameAnimationElement, AdvanceRevealsFramesIncrementally) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(false);
    anim.play();

    EXPECT_EQ(anim.current_frame(), 0u);
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 2u);
}

// --- fps 控制：整數倍速 ---
TEST(FrameAnimationElement, FpsControlsFramesPerTick) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = {
        MemoryImageSource("res://a", ImageDimensions{8, 8}),
        MemoryImageSource("res://b", ImageDimensions{8, 8}),
        MemoryImageSource("res://c", ImageDimensions{8, 8}),
        MemoryImageSource("res://d", ImageDimensions{8, 8}),
    };
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(2.0), FrameAnimationStatus::Ok);  // 每 tick 前進 2 幀
    anim.set_loop(false);
    anim.play();

    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 2u);
}

// --- fps 控制：小數 fps（< 1 幀/tick）累計進度不卡住 ---
TEST(FrameAnimationElement, FractionalFpsAccumulatesWithoutStalling) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(0.5), FrameAnimationStatus::Ok);  // 每 2 tick 才前進 1 幀
    anim.set_loop(false);
    anim.play();

    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 0u);  // 累計 0.5，未達 1
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);  // 累計 1.0
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);  // 累計 1.5
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 2u);  // 累計 2.0
}

// --- 循環播放：抵達最後一幀後繞回第 0 幀，恆不完成 ---
TEST(FrameAnimationElement, LoopWrapsAroundToFirstFrame) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(true);  // 預設即為 true，顯式設定以求明確
    anim.play();

    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 2u);
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 0u);  // 繞回第 0 幀
    EXPECT_FALSE(anim.is_finished());
    anim.advance(4);
    EXPECT_EQ(anim.current_frame(), 1u);  // 3 幀為模：(0 + 4) % 3 = 1
    EXPECT_FALSE(anim.is_finished());
}

// --- 單次播放：抵達最後一幀即停留並標記完成 ---
TEST(FrameAnimationElement, SinglePlayStopsAtLastFrameAndFinishes) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(false);
    anim.play();

    EXPECT_FALSE(anim.is_finished());
    anim.advance(1);
    EXPECT_FALSE(anim.is_finished());
    anim.advance(1);
    EXPECT_TRUE(anim.is_finished());
    EXPECT_EQ(anim.current_frame(), 2u);

    // 完成後再 advance：安全 no-op，不超出最後一幀。
    anim.advance(100);
    EXPECT_TRUE(anim.is_finished());
    EXPECT_EQ(anim.current_frame(), 2u);
}

// --- play / pause：暫停中 advance 為安全 no-op ---
TEST(FrameAnimationElement, PauseStopsAdvancing) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(false);

    EXPECT_FALSE(anim.is_playing());  // 預設暫停，需顯式 play()
    anim.advance(5);
    EXPECT_EQ(anim.current_frame(), 0u);  // 未 play：不推進

    anim.play();
    EXPECT_TRUE(anim.is_playing());
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);

    anim.pause();
    EXPECT_FALSE(anim.is_playing());
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);  // 暫停中：不推進

    anim.play();
    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 2u);  // 繼續播放：延續進度
}

// --- reset：進度歸零回到第 0 幀，fps / loop / 播放狀態不變 ---
TEST(FrameAnimationElement, ResetRestartsProgress) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(false);
    anim.play();
    anim.advance(2);
    EXPECT_TRUE(anim.is_finished());

    anim.reset();
    EXPECT_EQ(anim.current_frame(), 0u);
    EXPECT_FALSE(anim.is_finished());
    EXPECT_EQ(anim.fps(), 1.0);   // fps 保留
    EXPECT_TRUE(anim.is_playing());  // 播放狀態保留

    anim.advance(1);
    EXPECT_EQ(anim.current_frame(), 1u);  // fps 延續生效
}

// --- set_frames 重設進度與播完旗標 ---
TEST(FrameAnimationElement, SetFramesResetsProgress) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(false);
    anim.play();
    anim.advance(2);
    EXPECT_TRUE(anim.is_finished());

    std::vector<MemoryImageSource> more = {
        MemoryImageSource("res://x0", ImageDimensions{4, 4}),
        MemoryImageSource("res://x1", ImageDimensions{4, 4}),
    };
    ASSERT_EQ(anim.set_frames(as_refs(more)), FrameAnimationStatus::Ok);
    EXPECT_EQ(anim.frame_count(), 2u);
    EXPECT_EQ(anim.current_frame(), 0u);
    EXPECT_FALSE(anim.is_finished());
}

// --- 以 E4-02 顯示目前幀：render_model 反映目前幀內容與透傳設定 ---
TEST(FrameAnimationElement, RenderModelUsesE4_02ImageElement) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(false);
    anim.play();

    ASSERT_EQ(anim.set_scale_mode(ScaleMode::Tile), ImageStatus::Ok);
    ASSERT_EQ(anim.set_opacity(0.5f), ImageStatus::Ok);
    ASSERT_EQ(anim.set_target(ds::kernel::SurfaceId("layer0")), ImageStatus::Ok);

    anim.advance(1);  // 前進到第 1 幀
    ImageRenderModel model = anim.render_model();
    EXPECT_TRUE(model.has_source);
    EXPECT_EQ(model.source_reference, "res://f1");
    EXPECT_EQ(model.scale_mode, ScaleMode::Tile);
    EXPECT_FLOAT_EQ(model.alpha.opacity, 0.5f);
    EXPECT_EQ(model.target, ds::kernel::SurfaceId("layer0"));
    // NFR-02：裁切維持 E4-02 的正規化 [0,1] 全圖預設，未新增絕對座標 / 數字 z-order。
    EXPECT_EQ(model.crop.x, 0.0);
    EXPECT_EQ(model.crop.width, 1.0);
}

// --- 經 E4-09 動畫驅動源推進 ---
TEST(FrameAnimationElement, DrivenByE4_09AnimationDriver) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    ASSERT_EQ(anim.set_fps(1.0), FrameAnimationStatus::Ok);
    anim.set_loop(true);
    anim.play();

    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);
    anim.attach(drv);

    EXPECT_EQ(anim.current_frame(), 0u);
    hb.advance(2);  // 2 次脈衝，各 dt=1
    EXPECT_EQ(anim.current_frame(), 2u);
    hb.advance(1);  // 繞回第 0 幀（循環）
    EXPECT_EQ(anim.current_frame(), 0u);
}

// --- 空幀序列：報錯不靜默，既有狀態不變 ---
TEST(FrameAnimationElement, EmptyFramesReportsInvalid) {
    FrameAnimationElement anim;
    std::vector<std::reference_wrapper<const ImageSource>> empty;
    EXPECT_EQ(anim.set_frames(empty), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.frame_count(), 0u);
    EXPECT_TRUE(anim.is_finished());  // 空幀序列視為已完成（無可播放內容）

    // 先成功設一次，再嘗試以空序列覆蓋：不套用，維持既有幀序列。
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);
    EXPECT_EQ(anim.set_frames(empty), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.frame_count(), 3u);  // 未被清空
}

// --- 任一幀來源無效：整批不套用 ---
TEST(FrameAnimationElement, InvalidFrameSourceReportsInvalidAndDoesNotPartiallyApply) {
    FrameAnimationElement anim;
    NullImageSource invalid_src;
    MemoryImageSource good("res://ok", ImageDimensions{10, 10});
    std::vector<std::reference_wrapper<const ImageSource>> mixed{good, invalid_src};

    EXPECT_EQ(anim.set_frames(mixed), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.frame_count(), 0u);  // 全部不套用，非只丟棄壞的一幀
}

// --- 非正尺寸的幀來源：整批不套用 ---
TEST(FrameAnimationElement, NonPositiveDimensionFrameReportsInvalid) {
    FrameAnimationElement anim;
    MemoryImageSource zero_w("res://z", ImageDimensions{0, 10});
    std::vector<std::reference_wrapper<const ImageSource>> frames{zero_w};

    EXPECT_EQ(anim.set_frames(frames), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.frame_count(), 0u);
}

// --- 無效 fps 報錯不靜默 ---
TEST(FrameAnimationElement, InvalidFpsReportsInvalid) {
    FrameAnimationElement anim;
    std::vector<MemoryImageSource> frames = three_frames();
    ASSERT_EQ(anim.set_frames(as_refs(frames)), FrameAnimationStatus::Ok);

    EXPECT_EQ(anim.set_fps(0.0), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.set_fps(-1.0), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.set_fps(std::numeric_limits<double>::quiet_NaN()), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.set_fps(std::numeric_limits<double>::infinity()), FrameAnimationStatus::Invalid);
    EXPECT_EQ(anim.fps(), 1.0);  // 未套用非法值：維持預設 fps
}

// --- 未設幀序列即操作：安全狀態（不崩潰、視為已完成） ---
TEST(FrameAnimationElement, NoFramesSetIsSafeAndFinished) {
    FrameAnimationElement anim;
    EXPECT_EQ(anim.frame_count(), 0u);
    EXPECT_EQ(anim.current_frame(), 0u);
    EXPECT_TRUE(anim.is_finished());

    anim.play();
    anim.advance(5);  // 空幀序列：安全 no-op
    EXPECT_EQ(anim.current_frame(), 0u);

    ImageRenderModel model = anim.render_model();
    EXPECT_FALSE(model.has_source);
    EXPECT_TRUE(model.source_reference.empty());
}
