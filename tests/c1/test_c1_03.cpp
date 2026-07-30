// tests/c1/test_c1_03.cpp — C1-03 氣球 profile（角色對話氣球 / 氣泡通知）（gtest）
//
// 涵蓋：組裝（建構預設，Hidden 狀態下各查詢的安全預設值）、show_balloon（成功顯示、依附角色 /
// 逐字內容 / 存活計時三方一次到位）、E4-11 逐字推進（`advance` 依速度逐步顯示、完成後不再前進）、
// E1-14 計時消失（注入 tick 推進跨過 ttl 即自動消失、`remaining()` 倒數）、E1-11
// 定位/跟隨（`resolve()` 依角色矩形計算絕對佈局、角色矩形改變後重新解析即「跟隨」）、
// dismiss（手動提早結束、重複 dismiss 不靜默拒絕）、多氣球（各自獨立計時 / 內容，互不干擾）、
// 空文字（拒絕顯示）、以及各類無效輸入（空 id、角色未載入、ttl 為 0、已顯示中重複顯示、自附、
// 無效 anchor spec）。
//
// PortraitProfile（C1-02）刻意刪除複製建構子且自訂解構子（故亦無隱式移動建構子），因此本檔
// 一律**就地建構**（同 tests/c1/test_c1_02.cpp 慣例），不寫回傳 PortraitProfile 值的輔助函式
// （NRVO 非標準保證，該類型又無複製 / 移動路徑可退，會編譯失敗）。
//
// 本檔同時 #include "balloon_profile.hpp"（不直接引入 E1-14 標頭，見其說明）與
// "portrait_profile.hpp"（C1-02，需要真實 PortraitProfile 以呼叫 load_portrait 建立測試用
// 角色）——兩者可安全共存於同一翻譯單元，因為 balloon_profile.hpp 已把 E1-14 的實際串接
// pimpl 隔離，本檔並未引入 E1-02 input_strategy.hpp（詳見 balloon_profile.hpp / character_bridge.hpp
// 頂部說明的既有上游 ds::kernel::HitResult 命名碰撞，與 content/profiles/c1_06 已記錄之相同
// 手法）。
#include "balloon_profile.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "portrait_profile.hpp"  // C1-02（上游，可讀不可改）：PortraitProfile（測試用角色）

using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::Offset;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;

using ds::profiles::BalloonProfile;
using ds::profiles::BalloonState;
using ds::profiles::BalloonStatus;
using ds::profiles::PortraitProfile;
using ds::profiles::PortraitStatus;

using ds::render::FixedFontMetrics;

namespace {
constexpr float kEps = 1e-4f;
}  // namespace

// -----------------------------------------------------------------------------
// 組裝 / 建構預設
// -----------------------------------------------------------------------------

TEST(BalloonProfile, ConstructedHiddenWithSafeDefaults) {
    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);

    EXPECT_EQ(balloon.state(), BalloonState::Hidden);
    EXPECT_FALSE(balloon.is_visible());
    EXPECT_EQ(balloon.id(), "balloon.miku");
    EXPECT_TRUE(balloon.anchor_parent().empty());
    EXPECT_EQ(balloon.visible_count(), 0u);
    EXPECT_EQ(balloon.total_count(), 0u);
    EXPECT_FALSE(balloon.is_text_complete());
    EXPECT_TRUE(balloon.render_model().lines.empty());
    EXPECT_TRUE(balloon.render_model().glyphs.empty());
    EXPECT_FALSE(balloon.remaining().has_value());

    ResolvedPlacement out;
    EXPECT_EQ(balloon.resolve(ResolvedPlacement{0, 0, 100, 50}, Size{30, 10}, out),
              AnchorStatus::Invalid);  // 未依附任何角色
}

// -----------------------------------------------------------------------------
// show_balloon — 依附角色 + 逐字內容 + 存活計時，三方一次到位
// -----------------------------------------------------------------------------

TEST(BalloonProfile, ShowBalloonAttachesContentAndTimer) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);

    EXPECT_EQ(balloon.show_balloon(character, "Hi", 5), BalloonStatus::Ok);
    EXPECT_EQ(balloon.state(), BalloonState::Showing);
    EXPECT_TRUE(balloon.is_visible());
    EXPECT_EQ(balloon.anchor_parent(), "portrait.miku");
    EXPECT_EQ(balloon.total_count(), 2u);  // "Hi" = 2 個碼位
    ASSERT_TRUE(balloon.remaining().has_value());
    EXPECT_EQ(*balloon.remaining(), 5u);
    EXPECT_EQ(balloon.render_model().surface, "balloon.miku");  // NFR-02：具名目標 surface
}

// -----------------------------------------------------------------------------
// E4-11 逐字推進
// -----------------------------------------------------------------------------

TEST(BalloonProfile, AdvanceRevealsTextProgressively) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hello", 100), BalloonStatus::Ok);

    EXPECT_EQ(balloon.visible_count(), 0u);
    EXPECT_FALSE(balloon.is_text_complete());

    balloon.advance(1);
    EXPECT_EQ(balloon.visible_count(), 1u);
    balloon.advance(1);
    EXPECT_EQ(balloon.visible_count(), 2u);

    balloon.advance(3);  // 推進到第 5 個字元：完整顯示
    EXPECT_EQ(balloon.visible_count(), 5u);
    EXPECT_TRUE(balloon.is_text_complete());

    balloon.advance(10);  // 完成後再推進：安全 no-op，不越界
    EXPECT_EQ(balloon.visible_count(), 5u);
    EXPECT_TRUE(balloon.is_visible());  // 尚未逾時，仍顯示中
}

TEST(BalloonProfile, AdvanceWhileHiddenIsNoOp) {
    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    balloon.advance(5);  // 未顯示中：no-op，不崩潰
    EXPECT_EQ(balloon.state(), BalloonState::Hidden);
    EXPECT_EQ(balloon.visible_count(), 0u);
}

// -----------------------------------------------------------------------------
// E1-14 計時消失（注入 tick）
// -----------------------------------------------------------------------------

TEST(BalloonProfile, AutoDismissesWhenTtlElapses) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 3), BalloonStatus::Ok);

    balloon.advance(2);
    EXPECT_TRUE(balloon.is_visible());  // 尚未跨過 ttl
    ASSERT_TRUE(balloon.remaining().has_value());
    EXPECT_EQ(*balloon.remaining(), 1u);

    balloon.advance(1);  // 累計推進 3 tick，跨過 ttl → 自動消失
    EXPECT_FALSE(balloon.is_visible());
    EXPECT_EQ(balloon.state(), BalloonState::Hidden);
    EXPECT_TRUE(balloon.anchor_parent().empty());  // 依附已解除
    EXPECT_FALSE(balloon.remaining().has_value());

    // 消失後可重新顯示（狀態機正確回到可再次 show_balloon 的起點）。
    EXPECT_EQ(balloon.show_balloon(character, "Again", 5), BalloonStatus::Ok);
}

TEST(BalloonProfile, AdvancePastTtlInSingleCallStillDismisses) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 3), BalloonStatus::Ok);

    balloon.advance(50);  // 單次大幅推進，跨過 ttl
    EXPECT_FALSE(balloon.is_visible());
}

// -----------------------------------------------------------------------------
// E1-11 定位 / 跟隨
// -----------------------------------------------------------------------------

TEST(BalloonProfile, ResolvePlacesAboveCharacterByDefault) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 10), BalloonStatus::Ok);

    const ResolvedPlacement character_placement{0.0f, 0.0f, 100.0f, 50.0f};
    const Size balloon_size{30.0f, 10.0f};
    ResolvedPlacement out;
    ASSERT_EQ(balloon.resolve(character_placement, balloon_size, out), AnchorStatus::Ok);

    // TopCenter：局部 x = 0.5*(100-30) = 35，局部 y = 0*(50-10) + (-0.15)*50 = -7.5；
    // 平移 (+0, +0)。
    EXPECT_NEAR(out.x, 35.0f, kEps);
    EXPECT_NEAR(out.y, -7.5f, kEps);
    EXPECT_NEAR(out.width, 30.0f, kEps);
    EXPECT_NEAR(out.height, 10.0f, kEps);
}

TEST(BalloonProfile, ResolveFollowsCharacterWhenPlacementChanges) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 10), BalloonStatus::Ok);

    const Size balloon_size{30.0f, 10.0f};
    ResolvedPlacement out;

    ASSERT_EQ(balloon.resolve(ResolvedPlacement{0.0f, 0.0f, 100.0f, 50.0f}, balloon_size, out),
              AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 35.0f, kEps);
    EXPECT_NEAR(out.y, -7.5f, kEps);

    // 角色「移動」到新位置（同尺寸）：不快取——同一附著在新矩形下重新解析即得新位置。
    ASSERT_EQ(balloon.resolve(ResolvedPlacement{20.0f, 40.0f, 100.0f, 50.0f}, balloon_size, out),
              AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 55.0f, kEps);  // 35 + 20
    EXPECT_NEAR(out.y, 32.5f, kEps);  // -7.5 + 40
}

TEST(BalloonProfile, CustomAnchorSpecOverridesDefault) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    const AnchorSpec spec{Anchor::BottomRight, Offset::none()};
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 10, spec), BalloonStatus::Ok);

    ResolvedPlacement out;
    ASSERT_EQ(balloon.resolve(ResolvedPlacement{0.0f, 0.0f, 40.0f, 40.0f}, Size{10.0f, 10.0f}, out),
              AnchorStatus::Ok);
    // BottomRight 貼齊 (40-10, 40-10) = (30, 30)。
    EXPECT_NEAR(out.x, 30.0f, kEps);
    EXPECT_NEAR(out.y, 30.0f, kEps);
}

// -----------------------------------------------------------------------------
// dismiss
// -----------------------------------------------------------------------------

TEST(BalloonProfile, DismissEndsDisplayEarly) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 100), BalloonStatus::Ok);

    EXPECT_TRUE(balloon.dismiss());
    EXPECT_EQ(balloon.state(), BalloonState::Hidden);
    EXPECT_FALSE(balloon.is_visible());
    EXPECT_TRUE(balloon.anchor_parent().empty());
    EXPECT_FALSE(balloon.remaining().has_value());

    EXPECT_FALSE(balloon.dismiss());  // 重複 dismiss：no-op，不靜默
}

TEST(BalloonProfile, DismissWhenHiddenReturnsFalse) {
    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    EXPECT_FALSE(balloon.dismiss());
}

TEST(BalloonProfile, DismissedThenReshowResetsProgress) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 100), BalloonStatus::Ok);
    balloon.advance(1);
    EXPECT_EQ(balloon.visible_count(), 1u);
    ASSERT_TRUE(balloon.dismiss());

    ASSERT_EQ(balloon.show_balloon(character, "Yo", 100), BalloonStatus::Ok);
    EXPECT_EQ(balloon.visible_count(), 0u);  // 新一輪顯示，進度歸零重新開始
    EXPECT_EQ(balloon.total_count(), 2u);
}

// -----------------------------------------------------------------------------
// 多氣球 — 各自獨立計時 / 內容，互不干擾
// -----------------------------------------------------------------------------

TEST(BalloonProfile, MultipleBalloonsAreIndependent) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile miku("portrait.miku", backend, layers);
    ASSERT_EQ(miku.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    PortraitProfile rin("portrait.rin", backend, layers);
    ASSERT_EQ(rin.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon_a("balloon.a", metrics);
    BalloonProfile balloon_b("balloon.b", metrics);

    ASSERT_EQ(balloon_a.show_balloon(miku, "Short", 2), BalloonStatus::Ok);
    ASSERT_EQ(balloon_b.show_balloon(rin, "Long message", 100), BalloonStatus::Ok);

    // 各自的 anchor_parent 互不干擾。
    EXPECT_EQ(balloon_a.anchor_parent(), "portrait.miku");
    EXPECT_EQ(balloon_b.anchor_parent(), "portrait.rin");

    // 共同推進：balloon_a 的 ttl 較短，先自動消失；balloon_b 仍顯示中。
    balloon_a.advance(2);
    balloon_b.advance(2);
    EXPECT_FALSE(balloon_a.is_visible());
    EXPECT_TRUE(balloon_b.is_visible());
    EXPECT_EQ(balloon_b.visible_count(), 2u);  // 不受 balloon_a 消失影響，逐字進度照常累積

    // 手動 dismiss balloon_b 不影響（早已消失的）balloon_a。
    EXPECT_TRUE(balloon_b.dismiss());
    EXPECT_FALSE(balloon_a.is_visible());
    EXPECT_FALSE(balloon_b.is_visible());
}

// -----------------------------------------------------------------------------
// 空文字
// -----------------------------------------------------------------------------

TEST(BalloonProfile, EmptyTextRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    EXPECT_EQ(balloon.show_balloon(character, "", 5), BalloonStatus::Invalid);
    EXPECT_EQ(balloon.state(), BalloonState::Hidden);
    EXPECT_FALSE(balloon.is_visible());
}

// -----------------------------------------------------------------------------
// 無效輸入 — 結構化拒絕，不靜默
// -----------------------------------------------------------------------------

TEST(BalloonProfile, EmptyIdRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("", metrics);
    EXPECT_EQ(balloon.show_balloon(character, "Hi", 5), BalloonStatus::Invalid);
}

TEST(BalloonProfile, UnloadedCharacterRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);  // 未 load_portrait

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    EXPECT_EQ(balloon.show_balloon(character, "Hi", 5), BalloonStatus::Invalid);
    EXPECT_FALSE(balloon.is_visible());
}

TEST(BalloonProfile, ZeroTtlRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    EXPECT_EQ(balloon.show_balloon(character, "Hi", 0), BalloonStatus::Invalid);
}

TEST(BalloonProfile, AlreadyShowingRejectsSecondShow) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    ASSERT_EQ(balloon.show_balloon(character, "Hi", 5), BalloonStatus::Ok);
    EXPECT_EQ(balloon.show_balloon(character, "Again", 5), BalloonStatus::AlreadyShowing);
    EXPECT_EQ(balloon.total_count(), 2u);  // 未被覆寫（仍是 "Hi"）
}

TEST(BalloonProfile, SelfAttachRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    // 角色與氣球同名：E1-11 自附偵測應拒絕。
    PortraitProfile character("shared.id", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("shared.id", metrics);
    EXPECT_EQ(balloon.show_balloon(character, "Hi", 5), BalloonStatus::Invalid);
    EXPECT_FALSE(balloon.is_visible());
}

TEST(BalloonProfile, InvalidAnchorSpecRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile character("portrait.miku", backend, layers);
    ASSERT_EQ(character.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    FixedFontMetrics metrics(1.0, 10.0);
    BalloonProfile balloon("balloon.miku", metrics);
    const AnchorSpec bad_offset{Anchor::Center,
                                Offset{std::numeric_limits<float>::infinity(), 0.0f}};
    EXPECT_EQ(balloon.show_balloon(character, "Hi", 5, bad_offset), BalloonStatus::Invalid);
    EXPECT_FALSE(balloon.is_visible());
}

// -----------------------------------------------------------------------------
// 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(BalloonProfile, StatusAndStateToString) {
    EXPECT_STREQ(ds::profiles::to_string(BalloonState::Hidden), "Hidden");
    EXPECT_STREQ(ds::profiles::to_string(BalloonState::Showing), "Showing");
    EXPECT_STREQ(ds::profiles::to_string(BalloonStatus::Ok), "Ok");
    EXPECT_STREQ(ds::profiles::to_string(BalloonStatus::Invalid), "Invalid");
    EXPECT_STREQ(ds::profiles::to_string(BalloonStatus::AlreadyShowing), "AlreadyShowing");
}
