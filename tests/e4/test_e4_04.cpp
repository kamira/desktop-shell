// E4-04 按鈕三態 — 單元測試（gtest）
//
// 涵蓋：三態轉換與顯示優先序、E5-02 懸停事件切 hover 態（Enter/Leave/Move、透過真實
// HoverTracker 整合、透過 handle_hover_event 直接驅動）、E1-04 命中 + 按下/放開切 pressed
// 態（含拖出範圍取消點擊）、各態視覺（E4-02 ImageElement/render_model）、on_click 回呼、
// 建構子空 id 報錯、無效輸入（非有限座標、無效形狀、無效態）不靜默。
#include "button_element.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

#include "hit_test.hpp"
#include "hover_tracker.hpp"
#include "image_element.hpp"

using ds::elements::ButtonElement;
using ds::elements::ButtonRenderModel;
using ds::elements::ButtonState;
using ds::elements::ButtonStatus;
using ds::elements::ImageElement;
using ds::elements::MemoryImageSource;
using ds::events::HoverEvent;
using ds::events::HoverEventKind;
using ds::events::HoverTracker;
using ds::kernel::LocalPoint;
using ds::kernel::SurfaceLayer;

namespace {

ImageElement MakeVisual(const std::string& reference) {
    ImageElement img;
    MemoryImageSource source(reference, {10, 10});
    img.set_source(source);
    return img;
}

}  // namespace

// --- 建構子 ---

TEST(ButtonElement, ConstructWithNonEmptyIdSucceeds) {
    ButtonElement btn("btn.ok", ds::kernel::make_rect(20.0f, 10.0f));
    EXPECT_EQ(btn.id(), "btn.ok");
    EXPECT_EQ(btn.layer(), SurfaceLayer::Normal);
}

TEST(ButtonElement, ConstructWithEmptyIdThrows) {
    EXPECT_THROW(ButtonElement("", ds::kernel::make_rect(20.0f, 10.0f)),
                 std::invalid_argument);
}

// --- 預設狀態 ---

TEST(ButtonElement, DefaultStateIsNormal) {
    ButtonElement btn("btn.default", ds::kernel::make_rect(20.0f, 10.0f));
    EXPECT_EQ(btn.state(), ButtonState::Normal);
    EXPECT_FALSE(btn.is_hovered());
    EXPECT_FALSE(btn.is_pressed());
}

// --- 各態視覺（E4-02）---

TEST(ButtonElement, SetVisualStoresIndependentCopyPerState) {
    ButtonElement btn("btn.visual", ds::kernel::make_rect(20.0f, 10.0f));

    ImageElement normal_img = MakeVisual("normal.png");
    ImageElement hover_img = MakeVisual("hover.png");
    ImageElement pressed_img = MakeVisual("pressed.png");

    EXPECT_EQ(btn.set_visual(ButtonState::Normal, normal_img), ButtonStatus::Ok);
    EXPECT_EQ(btn.set_visual(ButtonState::Hover, hover_img), ButtonStatus::Ok);
    EXPECT_EQ(btn.set_visual(ButtonState::Pressed, pressed_img), ButtonStatus::Ok);

    ASSERT_NE(btn.visual(ButtonState::Normal), nullptr);
    ASSERT_NE(btn.visual(ButtonState::Hover), nullptr);
    ASSERT_NE(btn.visual(ButtonState::Pressed), nullptr);
    EXPECT_EQ(btn.visual(ButtonState::Normal)->source_reference(), "normal.png");
    EXPECT_EQ(btn.visual(ButtonState::Hover)->source_reference(), "hover.png");
    EXPECT_EQ(btn.visual(ButtonState::Pressed)->source_reference(), "pressed.png");

    // 值複製：事後改變原物件不影響已設定的視覺。
    normal_img.set_opacity(0.2f);
    EXPECT_FLOAT_EQ(btn.visual(ButtonState::Normal)->opacity(), 1.0f);
}

TEST(ButtonElement, SetVisualInvalidStateRejected) {
    ButtonElement btn("btn.invalid_state", ds::kernel::make_rect(20.0f, 10.0f));
    ImageElement img = MakeVisual("x.png");

    const auto bogus = static_cast<ButtonState>(99);
    EXPECT_EQ(btn.set_visual(bogus, img), ButtonStatus::Invalid);
    EXPECT_EQ(btn.visual(bogus), nullptr);
}

TEST(ButtonElement, UnsetVisualDefaultsToNoSource) {
    ButtonElement btn("btn.unset", ds::kernel::make_rect(20.0f, 10.0f));
    ASSERT_NE(btn.visual(ButtonState::Hover), nullptr);
    EXPECT_FALSE(btn.visual(ButtonState::Hover)->has_source());
}

// --- 按下 / 放開（E1-04 命中）---

TEST(ButtonElement, PressInsideEntersPressedState) {
    ButtonElement btn("btn.press", ds::kernel::make_rect(20.0f, 10.0f));

    EXPECT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    EXPECT_TRUE(btn.is_pressed());
    EXPECT_EQ(btn.state(), ButtonState::Pressed);
}

TEST(ButtonElement, PressOutsideDoesNotEnterPressed) {
    ButtonElement btn("btn.press_outside", ds::kernel::make_rect(20.0f, 10.0f));

    EXPECT_EQ(btn.press(LocalPoint{50.0f, 50.0f}), ButtonStatus::Ok);
    EXPECT_FALSE(btn.is_pressed());
    EXPECT_EQ(btn.state(), ButtonState::Normal);
}

TEST(ButtonElement, ReleaseInsideAfterPressFiresOnClickAndEndsPressed) {
    ButtonElement btn("btn.click", ds::kernel::make_rect(20.0f, 10.0f));
    int clicks = 0;
    btn.set_on_click([&clicks]() { ++clicks; });

    ASSERT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    ASSERT_TRUE(btn.is_pressed());

    EXPECT_EQ(btn.release(LocalPoint{6.0f, 6.0f}), ButtonStatus::Ok);
    EXPECT_FALSE(btn.is_pressed());
    EXPECT_EQ(clicks, 1);
}

TEST(ButtonElement, ReleaseOutsideAfterPressCancelsClick) {
    ButtonElement btn("btn.cancel", ds::kernel::make_rect(20.0f, 10.0f));
    int clicks = 0;
    btn.set_on_click([&clicks]() { ++clicks; });

    ASSERT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    ASSERT_TRUE(btn.is_pressed());

    // 拖出按鈕範圍外再放開：正常「取消點擊」語意，非錯誤，不觸發 on_click。
    EXPECT_EQ(btn.release(LocalPoint{500.0f, 500.0f}), ButtonStatus::Ok);
    EXPECT_FALSE(btn.is_pressed());
    EXPECT_EQ(clicks, 0);
}

TEST(ButtonElement, ReleaseWithoutPriorPressIsNoOpAndDoesNotClick) {
    ButtonElement btn("btn.no_press", ds::kernel::make_rect(20.0f, 10.0f));
    int clicks = 0;
    btn.set_on_click([&clicks]() { ++clicks; });

    EXPECT_EQ(btn.release(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    EXPECT_FALSE(btn.is_pressed());
    EXPECT_EQ(clicks, 0);
}

TEST(ButtonElement, OnClickNotRequiredForReleaseToSucceed) {
    ButtonElement btn("btn.no_callback", ds::kernel::make_rect(20.0f, 10.0f));
    ASSERT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    EXPECT_EQ(btn.release(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);  // 未設 on_click 不炸
}

TEST(ButtonElement, PressedStateTakesPriorityOverHoverForDisplay) {
    ButtonElement btn("btn.priority", ds::kernel::make_rect(20.0f, 10.0f));

    HoverEvent enter{HoverEventKind::Enter, "btn.priority", LocalPoint{5.0f, 5.0f}};
    btn.handle_hover_event(enter);
    EXPECT_EQ(btn.state(), ButtonState::Hover);

    ASSERT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    // 仍懸停，但顯示優先為 Pressed。
    EXPECT_TRUE(btn.is_hovered());
    EXPECT_EQ(btn.state(), ButtonState::Pressed);

    ASSERT_EQ(btn.release(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    // 放開後仍懸停 → 回到 Hover（懸停旗標未因按下而遺失）。
    EXPECT_EQ(btn.state(), ButtonState::Hover);
}

// --- 無效輸入不靜默 ---

TEST(ButtonElement, PressWithNonFiniteCoordinateIsInvalid) {
    ButtonElement btn("btn.nan_press", ds::kernel::make_rect(20.0f, 10.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();

    EXPECT_EQ(btn.press(LocalPoint{nan, 0.0f}), ButtonStatus::Invalid);
    EXPECT_FALSE(btn.is_pressed());  // 不改變狀態
}

TEST(ButtonElement, ReleaseWithNonFiniteCoordinateIsInvalid) {
    ButtonElement btn("btn.nan_release", ds::kernel::make_rect(20.0f, 10.0f));
    ASSERT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);

    const float inf = std::numeric_limits<float>::infinity();
    int clicks = 0;
    btn.set_on_click([&clicks]() { ++clicks; });

    EXPECT_EQ(btn.release(LocalPoint{inf, 0.0f}), ButtonStatus::Invalid);
    EXPECT_TRUE(btn.is_pressed());  // 無效輸入：不改變既有按下狀態
    EXPECT_EQ(clicks, 0);
}

TEST(ButtonElement, PressWithInvalidShapeIsInvalid) {
    // 負寬高的 rect 對 E1-04 HitTester::is_valid 而言無效。
    ds::kernel::Shape bad_shape = ds::kernel::make_rect(-1.0f, 10.0f);
    ButtonElement btn("btn.bad_shape", bad_shape);

    EXPECT_EQ(btn.press(LocalPoint{1.0f, 1.0f}), ButtonStatus::Invalid);
    EXPECT_FALSE(btn.is_pressed());
}

// --- 懸停整合（E5-02）：直接事件驅動 ---

TEST(ButtonElement, HandleHoverEventEnterAndLeaveTogglesHoverState) {
    ButtonElement btn("btn.hover", ds::kernel::make_rect(20.0f, 10.0f));

    HoverEvent enter{HoverEventKind::Enter, "btn.hover", LocalPoint{1.0f, 1.0f}};
    btn.handle_hover_event(enter);
    EXPECT_TRUE(btn.is_hovered());
    EXPECT_EQ(btn.state(), ButtonState::Hover);

    HoverEvent move{HoverEventKind::Move, "btn.hover", LocalPoint{2.0f, 2.0f}};
    btn.handle_hover_event(move);
    EXPECT_TRUE(btn.is_hovered());  // Move 不重發 Enter、不改變狀態

    HoverEvent leave{HoverEventKind::Leave, "btn.hover", LocalPoint{99.0f, 99.0f}};
    btn.handle_hover_event(leave);
    EXPECT_FALSE(btn.is_hovered());
    EXPECT_EQ(btn.state(), ButtonState::Normal);
}

TEST(ButtonElement, HandleHoverEventForOtherSurfaceIgnored) {
    ButtonElement btn("btn.mine", ds::kernel::make_rect(20.0f, 10.0f));

    HoverEvent enter_other{HoverEventKind::Enter, "btn.other", LocalPoint{1.0f, 1.0f}};
    btn.handle_hover_event(enter_other);
    EXPECT_FALSE(btn.is_hovered());  // 非本按鈕的事件：忽略
}

// --- 懸停整合（E5-02）：透過真實 HoverTracker ---

TEST(ButtonElement, AttachToHoverTrackerReceivesEnterLeaveFromRealTracker) {
    ButtonElement btn("btn.tracked", ds::kernel::make_rect(20.0f, 10.0f));
    HoverTracker tracker;

    const auto sub = btn.attach(tracker);
    EXPECT_NE(sub, 0u);

    ASSERT_TRUE(tracker.inject_move(LocalPoint{5.0f, 5.0f}));  // 移入按鈕範圍
    EXPECT_TRUE(btn.is_hovered());
    EXPECT_EQ(btn.state(), ButtonState::Hover);

    ASSERT_TRUE(tracker.inject_move(LocalPoint{500.0f, 500.0f}));  // 移出
    EXPECT_FALSE(btn.is_hovered());
    EXPECT_EQ(btn.state(), ButtonState::Normal);
}

TEST(ButtonElement, DetachStopsFurtherHoverUpdatesAndRemovesSurface) {
    ButtonElement btn("btn.detach", ds::kernel::make_rect(20.0f, 10.0f));
    HoverTracker tracker;

    const auto sub = btn.attach(tracker);
    ASSERT_TRUE(tracker.inject_move(LocalPoint{5.0f, 5.0f}));
    ASSERT_TRUE(btn.is_hovered());

    EXPECT_TRUE(btn.detach(tracker, sub));
    EXPECT_EQ(tracker.surface_count(), 0u);
    EXPECT_EQ(tracker.listener_count(), 0u);

    // detach 後 tracker 已無登記此 surface；再次移入按鈕原範圍不應命中（surface 已移除），
    // 故按鈕的 hover_ 旗標維持 detach 當下的值不變。
    tracker.inject_move(LocalPoint{6.0f, 6.0f});
    EXPECT_TRUE(btn.is_hovered());  // 未再收到事件，旗標保持不變（非重新判定）
}

TEST(ButtonElement, PressAndReleaseIntegratedWithTrackerHoverAfterClick) {
    // 整合情境：先以真實 HoverTracker 進入 hover，再以本單元 press/release 完成一次點擊，
    // 驗證懸停與按下兩條路徑各自運作、放開後正確回到 Hover。
    ButtonElement btn("btn.integrated", ds::kernel::make_rect(20.0f, 10.0f));
    HoverTracker tracker;
    btn.attach(tracker);

    int clicks = 0;
    btn.set_on_click([&clicks]() { ++clicks; });

    ASSERT_TRUE(tracker.inject_move(LocalPoint{5.0f, 5.0f}));
    ASSERT_EQ(btn.state(), ButtonState::Hover);

    ASSERT_EQ(btn.press(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    EXPECT_EQ(btn.state(), ButtonState::Pressed);

    ASSERT_EQ(btn.release(LocalPoint{5.0f, 5.0f}), ButtonStatus::Ok);
    EXPECT_EQ(clicks, 1);
    EXPECT_EQ(btn.state(), ButtonState::Hover);
}

// --- 渲染描述（render_model）---

TEST(ButtonElement, RenderModelReflectsCurrentStateAndMatchingVisual) {
    ButtonElement btn("btn.render", ds::kernel::make_rect(20.0f, 10.0f));
    btn.set_visual(ButtonState::Normal, MakeVisual("normal.png"));
    btn.set_visual(ButtonState::Hover, MakeVisual("hover.png"));
    btn.set_visual(ButtonState::Pressed, MakeVisual("pressed.png"));

    ButtonRenderModel normal_model = btn.render_model();
    EXPECT_EQ(normal_model.state, ButtonState::Normal);
    EXPECT_EQ(normal_model.visual.source_reference, "normal.png");

    HoverEvent enter{HoverEventKind::Enter, "btn.render", LocalPoint{1.0f, 1.0f}};
    btn.handle_hover_event(enter);
    ButtonRenderModel hover_model = btn.render_model();
    EXPECT_EQ(hover_model.state, ButtonState::Hover);
    EXPECT_EQ(hover_model.visual.source_reference, "hover.png");

    ASSERT_EQ(btn.press(LocalPoint{1.0f, 1.0f}), ButtonStatus::Ok);
    ButtonRenderModel pressed_model = btn.render_model();
    EXPECT_EQ(pressed_model.state, ButtonState::Pressed);
    EXPECT_EQ(pressed_model.visual.source_reference, "pressed.png");
}

TEST(ButtonElement, RenderModelWithNoVisualSetHasEmptyImageDescription) {
    ButtonElement btn("btn.empty_render", ds::kernel::make_rect(20.0f, 10.0f));

    ButtonRenderModel model = btn.render_model();
    EXPECT_EQ(model.state, ButtonState::Normal);
    EXPECT_FALSE(model.visual.has_source);
    EXPECT_EQ(model.visual.source_reference, "");
}

// --- NFR-02：具名 id / 具名圖層，無座標欄位混入 API ---

TEST(ButtonElement, UsesNamedIdAndNamedLayerNotNumericZOrder) {
    ButtonElement btn("btn.nfr02", ds::kernel::make_rect(20.0f, 10.0f),
                      SurfaceLayer::Overlay);
    EXPECT_EQ(btn.id(), "btn.nfr02");
    EXPECT_EQ(btn.layer(), SurfaceLayer::Overlay);
}
