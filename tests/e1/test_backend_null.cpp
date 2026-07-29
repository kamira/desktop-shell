// E1-24 null 後端參考實作 — 契約 / 單元測試（gtest）
//
// 驗證 NullKernelBackend 完整實作 KernelBackend 抽象介面，且所有平台操作為 no-op /
// 記憶體狀態、任何平台可建構執行：
//   - 後端身分（name == "null"）與多型（經 KernelBackend* 基底指標操作）
//   - 生命週期 init / shutdown（冪等、計數、shutdown 釋放 surface）
//   - 能力宣告經 E1-21 能力矩陣查詢（保守：基礎能力可用、可選不可用、未知回 false）
//   - K1 surface kernel：建立 / 重複拒絕 / 空 id 拒絕 / 銷毀 / 顯示隱藏 / 四參數 profile
//   - K2 繪製：begin/end frame no-op 計數、不可重入 begin、未 begin 之 end 拒絕
//   - K3 輸入：set_input_policy 更新、poll_input 永遠回空（no-op）
//   - 未知 surface 一律保守回傳、永不崩潰
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa），介面即跨平台保證。
#include "null_backend.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::HitPolicy;
using ds::kernel::InputEvent;
using ds::kernel::InputPolicy;
using ds::kernel::KernelBackend;
using ds::kernel::NullKernelBackend;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceProfile;

namespace {

// 後端身分：null 後端名稱穩定為 "null"。
TEST(NullKernelBackend, NameIsNull) {
    NullKernelBackend b;
    EXPECT_EQ(b.name(), "null");
}

// 生命週期：init 前未初始化；init 後為真；計數正確；init 冪等。
TEST(NullKernelBackend, InitLifecycle) {
    NullKernelBackend b;
    EXPECT_FALSE(b.is_initialized());
    EXPECT_EQ(b.init_calls(), 0u);

    EXPECT_TRUE(b.init());
    EXPECT_TRUE(b.is_initialized());
    EXPECT_EQ(b.init_calls(), 1u);

    // 冪等：重複 init 仍回 true 且維持已初始化。
    EXPECT_TRUE(b.init());
    EXPECT_TRUE(b.is_initialized());
    EXPECT_EQ(b.init_calls(), 2u);
}

// shutdown：回到未初始化、釋放所有 surface、冪等（未初始化再呼叫不崩潰）。
TEST(NullKernelBackend, ShutdownIsIdempotentAndReleasesSurfaces) {
    NullKernelBackend b;
    b.init();
    EXPECT_TRUE(b.create_surface("surface.a", SurfaceProfile{}));
    EXPECT_EQ(b.surface_count(), 1u);

    b.shutdown();
    EXPECT_FALSE(b.is_initialized());
    EXPECT_EQ(b.surface_count(), 0u);  // surface 已釋放
    EXPECT_EQ(b.shutdown_calls(), 1u);

    // 冪等：未初始化狀態再 shutdown 亦安全。
    b.shutdown();
    EXPECT_FALSE(b.is_initialized());
    EXPECT_EQ(b.shutdown_calls(), 2u);
}

// 能力查詢（經 E1-21）：預設矩陣非空；基礎能力可用；可選能力不可用；未知保守回 false。
TEST(NullKernelBackend, CapabilitiesReportedViaE1_21) {
    NullKernelBackend b;
    const CapabilityMatrix& caps = b.capabilities();
    EXPECT_GT(caps.size(), 0u);

    // 保證存在的基礎能力：可用。
    EXPECT_TRUE(b.has("render.paint"));
    EXPECT_TRUE(b.has("kernel.surface"));

    // 可選能力：null 期保守不可用。
    EXPECT_FALSE(b.has("host.tray_icon"));
    EXPECT_FALSE(b.has("actuator.brightness"));

    // 未知能力：一律 false（NFR-03 保守閘控）。
    EXPECT_FALSE(b.has("no.such.capability"));

    // has() 等價於 capabilities().has()（單一資料來源）。
    EXPECT_EQ(b.has("render.paint"), caps.has("render.paint"));
    EXPECT_EQ(b.has("host.tray_icon"), caps.has("host.tray_icon"));
}

// 能力矩陣可注入：以自訂保守矩陣建構，has() 反映之。
TEST(NullKernelBackend, CustomCapabilityMatrixInjectable) {
    std::vector<CapabilityDecl> decls = {
        {"render.paint", "基礎繪製", /*optional=*/false, /*default_available=*/true},
        {"host.tray_icon", "系統匣", /*optional=*/true, /*default_available=*/false},
    };
    NullKernelBackend b{CapabilityMatrix(decls)};
    EXPECT_EQ(b.capabilities().size(), 2u);
    EXPECT_TRUE(b.has("render.paint"));
    EXPECT_FALSE(b.has("host.tray_icon"));
    EXPECT_FALSE(b.has("kernel.surface"));  // 未宣告 -> 保守 false
}

// K1：建立 surface / 查詢存在 / 計數；空 id 與重複 id 皆拒絕。
TEST(NullKernelBackend, CreateSurfaceAndDuplicateRejection) {
    NullKernelBackend b;
    b.init();

    EXPECT_FALSE(b.has_surface("surface.panel"));
    EXPECT_TRUE(b.create_surface("surface.panel", SurfaceProfile{}));
    EXPECT_TRUE(b.has_surface("surface.panel"));
    EXPECT_EQ(b.surface_count(), 1u);

    // 重複 id：拒絕，計數不變。
    EXPECT_FALSE(b.create_surface("surface.panel", SurfaceProfile{}));
    EXPECT_EQ(b.surface_count(), 1u);

    // 空 id：保守拒絕。
    EXPECT_FALSE(b.create_surface("", SurfaceProfile{}));
    EXPECT_EQ(b.surface_count(), 1u);
}

// K1：四參數 profile 忠實保存並可查回。
TEST(NullKernelBackend, SurfaceProfileStoredFaithfully) {
    NullKernelBackend b;
    SurfaceProfile p;
    p.layer = SurfaceLayer::Overlay;
    p.input = InputPolicy::PassThrough;
    p.hit = HitPolicy::Transparent;
    p.lifecycle = SurfaceLifecycle::Ephemeral;

    EXPECT_TRUE(b.create_surface("surface.overlay", p));
    const SurfaceProfile* got = b.surface_profile("surface.overlay");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->layer, SurfaceLayer::Overlay);
    EXPECT_EQ(got->input, InputPolicy::PassThrough);
    EXPECT_EQ(got->hit, HitPolicy::Transparent);
    EXPECT_EQ(got->lifecycle, SurfaceLifecycle::Ephemeral);

    // 未知 surface 的 profile 查詢回 nullptr（保守）。
    EXPECT_EQ(b.surface_profile("surface.ghost"), nullptr);
}

// K1：顯示 / 隱藏 surface 更新可見狀態；未知 id 回 false。
TEST(NullKernelBackend, ShowHideSurface) {
    NullKernelBackend b;
    b.create_surface("surface.a", SurfaceProfile{});
    EXPECT_FALSE(b.is_visible("surface.a"));  // 建立時預設不可見

    EXPECT_TRUE(b.show_surface("surface.a"));
    EXPECT_TRUE(b.is_visible("surface.a"));

    EXPECT_TRUE(b.hide_surface("surface.a"));
    EXPECT_FALSE(b.is_visible("surface.a"));

    // 未知 id：保守回 false、不崩潰。
    EXPECT_FALSE(b.show_surface("surface.ghost"));
    EXPECT_FALSE(b.hide_surface("surface.ghost"));
    EXPECT_FALSE(b.is_visible("surface.ghost"));
}

// K1：銷毀 surface；未知 id 回 false。
TEST(NullKernelBackend, DestroySurface) {
    NullKernelBackend b;
    b.create_surface("surface.a", SurfaceProfile{});
    b.create_surface("surface.b", SurfaceProfile{});
    EXPECT_EQ(b.surface_count(), 2u);

    EXPECT_TRUE(b.destroy_surface("surface.a"));
    EXPECT_FALSE(b.has_surface("surface.a"));
    EXPECT_TRUE(b.has_surface("surface.b"));
    EXPECT_EQ(b.surface_count(), 1u);

    // 未知 / 已銷毀 id：回 false。
    EXPECT_FALSE(b.destroy_surface("surface.a"));
    EXPECT_FALSE(b.destroy_surface("surface.ghost"));
}

// K2：begin/end frame no-op 計數；不可重入 begin；未 begin 之 end 拒絕。
TEST(NullKernelBackend, PaintFrameLifecycle) {
    NullKernelBackend b;
    b.create_surface("surface.a", SurfaceProfile{});
    EXPECT_EQ(b.completed_frames("surface.a"), 0u);
    EXPECT_FALSE(b.in_frame("surface.a"));

    // 一輪完整 frame。
    EXPECT_TRUE(b.begin_frame("surface.a"));
    EXPECT_TRUE(b.in_frame("surface.a"));
    // 重入 begin：拒絕（已在 frame 中）。
    EXPECT_FALSE(b.begin_frame("surface.a"));
    EXPECT_TRUE(b.end_frame("surface.a"));
    EXPECT_FALSE(b.in_frame("surface.a"));
    EXPECT_EQ(b.completed_frames("surface.a"), 1u);

    // 未 begin 之 end：拒絕，計數不變。
    EXPECT_FALSE(b.end_frame("surface.a"));
    EXPECT_EQ(b.completed_frames("surface.a"), 1u);

    // 第二輪：計數累加。
    EXPECT_TRUE(b.begin_frame("surface.a"));
    EXPECT_TRUE(b.end_frame("surface.a"));
    EXPECT_EQ(b.completed_frames("surface.a"), 2u);

    // 未知 surface：begin/end 皆拒絕。
    EXPECT_FALSE(b.begin_frame("surface.ghost"));
    EXPECT_FALSE(b.end_frame("surface.ghost"));
}

// K3：set_input_policy 更新 profile；未知 id 回 false。
TEST(NullKernelBackend, SetInputPolicy) {
    NullKernelBackend b;
    SurfaceProfile p;
    p.input = InputPolicy::Accepting;
    b.create_surface("surface.modal", p);

    EXPECT_TRUE(b.set_input_policy("surface.modal", InputPolicy::Modal));
    const SurfaceProfile* got = b.surface_profile("surface.modal");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->input, InputPolicy::Modal);

    // 未知 id：回 false。
    EXPECT_FALSE(b.set_input_policy("surface.ghost", InputPolicy::Modal));
}

// K3：poll_input 永遠回空（null 後端無真實輸入來源），且計數正確。
TEST(NullKernelBackend, PollInputAlwaysEmpty) {
    NullKernelBackend b;
    b.init();
    std::vector<InputEvent> ev1 = b.poll_input();
    EXPECT_TRUE(ev1.empty());
    std::vector<InputEvent> ev2 = b.poll_input();
    EXPECT_TRUE(ev2.empty());
    EXPECT_EQ(b.poll_input_calls(), 2u);
}

// 多型：經 KernelBackend* 基底指標操作整組介面 —— 契約以介面表達，與具體後端解耦。
TEST(NullKernelBackend, UsableThroughBasePointer) {
    NullKernelBackend impl;
    KernelBackend* be = &impl;

    EXPECT_EQ(be->name(), "null");
    EXPECT_TRUE(be->init());
    EXPECT_TRUE(be->is_initialized());
    EXPECT_TRUE(be->has("render.paint"));
    EXPECT_FALSE(be->has("host.tray_icon"));

    EXPECT_TRUE(be->create_surface("surface.x", SurfaceProfile{}));
    EXPECT_TRUE(be->has_surface("surface.x"));
    EXPECT_TRUE(be->show_surface("surface.x"));
    EXPECT_TRUE(be->is_visible("surface.x"));
    EXPECT_TRUE(be->begin_frame("surface.x"));
    EXPECT_TRUE(be->end_frame("surface.x"));
    EXPECT_TRUE(be->set_input_policy("surface.x", InputPolicy::Modal));
    EXPECT_TRUE(be->poll_input().empty());
    EXPECT_EQ(be->surface_count(), 1u);

    be->shutdown();
    EXPECT_FALSE(be->is_initialized());
    EXPECT_EQ(be->surface_count(), 0u);
}

// 預設 profile 值合理（不指定即得中性的一般視窗參數）。
TEST(NullKernelBackend, DefaultProfileIsSaneNeutral) {
    NullKernelBackend b;
    b.create_surface("surface.default", SurfaceProfile{});
    const SurfaceProfile* p = b.surface_profile("surface.default");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->layer, SurfaceLayer::Normal);
    EXPECT_EQ(p->input, InputPolicy::Accepting);
    EXPECT_EQ(p->hit, HitPolicy::Solid);
    EXPECT_EQ(p->lifecycle, SurfaceLifecycle::Persistent);
}

}  // namespace
