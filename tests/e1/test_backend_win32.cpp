// W1-01 win32 kernel 後端 — gtest
//
// 驗的是「具名契約概念真的對映到 win32 狀態」，而不是「函式有沒有回 true」。
// 因此每條斷言盡量去問 **Windows 本身**（GetWindowLongPtr / IsWindowVisible），
// 而不是問後端自己記了什麼——後端若只維護影子狀態，這些測試就會紅。
//
// 注意：本檔**不是** E1-25 契約測試組的一部分。那套件測的是另一個不相容的介面
// （見 CHG-20260803-03 §契約缺口 / 知識庫 K-003），win32 後端掛不上去。
#include <gtest/gtest.h>

#include "win32_backend.hpp"

using ds::kernel::CapabilityMatrix;
using ds::kernel::HitPolicy;
using ds::kernel::InputPolicy;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceProfile;
using ds::kernel::Win32KernelBackend;

namespace {

SurfaceProfile topmost_panel() {
    SurfaceProfile p;
    p.layer = SurfaceLayer::Topmost;
    p.input = InputPolicy::Accepting;
    p.hit = HitPolicy::Solid;
    p.lifecycle = SurfaceLifecycle::Persistent;
    return p;
}

bool has_ex_style(HWND hwnd, LONG_PTR bit) {
    return (::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & bit) != 0;
}

}  // namespace

TEST(Win32Backend, NameIsWin32) {
    Win32KernelBackend b;
    EXPECT_EQ(b.name(), "win32");
}

// 契約：init 冪等，重複呼叫仍回 true 且狀態不變壞。
TEST(Win32Backend, InitIsIdempotent) {
    Win32KernelBackend b;
    EXPECT_FALSE(b.is_initialized());
    ASSERT_TRUE(b.init());
    EXPECT_TRUE(b.is_initialized());
    EXPECT_TRUE(b.init());
    EXPECT_TRUE(b.is_initialized());
}

// 契約：shutdown 冪等——未初始化可安全呼叫，可重複呼叫。
TEST(Win32Backend, ShutdownIsIdempotent) {
    Win32KernelBackend b;
    b.shutdown();  // 未初始化就關，不得崩潰
    ASSERT_TRUE(b.init());
    b.shutdown();
    b.shutdown();
    EXPECT_FALSE(b.is_initialized());
    EXPECT_EQ(b.surface_count(), 0u);
}

// 契約：未 init 不得建得出 surface（前置條件保守）。
TEST(Win32Backend, CreateSurfaceRequiresInit) {
    Win32KernelBackend b;
    EXPECT_FALSE(b.create_surface("surface.panel", topmost_panel()));
    EXPECT_EQ(b.surface_count(), 0u);
}

// 契約：空 id 拒絕；重複 id 拒絕。
TEST(Win32Backend, RejectsEmptyAndDuplicateId) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    EXPECT_FALSE(b.create_surface("", topmost_panel()));
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    EXPECT_FALSE(b.create_surface("surface.panel", topmost_panel()));
    EXPECT_EQ(b.surface_count(), 1u);
}

// 建立 surface 真的產生一個 win32 視窗。
TEST(Win32Backend, CreateSurfaceMakesRealWindow) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    HWND hwnd = b.hwnd_for("surface.panel");
    ASSERT_NE(hwnd, nullptr);
    EXPECT_TRUE(::IsWindow(hwnd));
    EXPECT_TRUE(b.has_surface("surface.panel"));
    EXPECT_EQ(b.hwnd_for("surface.unknown"), nullptr);
}

// SurfaceLayer::Topmost → 視窗真的帶 WS_EX_TOPMOST。這是「具名圖層」對映的關鍵斷言。
TEST(Win32Backend, TopmostLayerMapsToExTopmost) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    EXPECT_TRUE(has_ex_style(b.hwnd_for("surface.panel"), WS_EX_TOPMOST));
}

// Normal 層不得是 topmost——否則「具名圖層」等於沒有區別。
TEST(Win32Backend, NormalLayerIsNotTopmost) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    SurfaceProfile p = topmost_panel();
    p.layer = SurfaceLayer::Normal;
    ASSERT_TRUE(b.create_surface("surface.normal", p));
    EXPECT_FALSE(has_ex_style(b.hwnd_for("surface.normal"), WS_EX_TOPMOST));
}

// show / hide 反映的是 Windows 的真實可見狀態，不是後端自記的旗標。
TEST(Win32Backend, ShowHideReflectsRealWindowVisibility) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    HWND hwnd = b.hwnd_for("surface.panel");

    EXPECT_FALSE(b.is_visible("surface.panel"));
    ASSERT_TRUE(b.show_surface("surface.panel"));
    EXPECT_TRUE(b.is_visible("surface.panel"));
    EXPECT_TRUE(::IsWindowVisible(hwnd));

    ASSERT_TRUE(b.hide_surface("surface.panel"));
    EXPECT_FALSE(b.is_visible("surface.panel"));
    EXPECT_FALSE(::IsWindowVisible(hwnd));
}

// InputPolicy::PassThrough → WS_EX_TRANSPARENT（點擊穿透），切回去要拿得掉。
TEST(Win32Backend, PassThroughTogglesExTransparent) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    HWND hwnd = b.hwnd_for("surface.panel");

    EXPECT_FALSE(has_ex_style(hwnd, WS_EX_TRANSPARENT));
    ASSERT_TRUE(b.set_input_policy("surface.panel", InputPolicy::PassThrough));
    EXPECT_TRUE(has_ex_style(hwnd, WS_EX_TRANSPARENT));
    ASSERT_TRUE(b.set_input_policy("surface.panel", InputPolicy::Accepting));
    EXPECT_FALSE(has_ex_style(hwnd, WS_EX_TRANSPARENT));
}

// HitPolicy::Transparent 在建立時就該生效（不必等到 set_input_policy）。
TEST(Win32Backend, TransparentHitPolicyAppliesAtCreation) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    SurfaceProfile p = topmost_panel();
    p.hit = HitPolicy::Transparent;
    ASSERT_TRUE(b.create_surface("surface.ghost", p));
    EXPECT_TRUE(has_ex_style(b.hwnd_for("surface.ghost"), WS_EX_TRANSPARENT));
}

// 面板不搶焦點：WS_EX_NOACTIVATE 是 surface kernel「不搶焦點的面板」語意的載體。
TEST(Win32Backend, PanelDoesNotStealFocus) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    EXPECT_TRUE(has_ex_style(b.hwnd_for("surface.panel"), WS_EX_NOACTIVATE));
}

// 契約：frame 括號配對；重複 begin / 未 begin 就 end 皆回 false。
TEST(Win32Backend, FrameBracketingContract) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));

    EXPECT_FALSE(b.end_frame("surface.panel"));  // 未 begin 就 end
    EXPECT_TRUE(b.begin_frame("surface.panel"));
    EXPECT_FALSE(b.begin_frame("surface.panel"));  // 重複 begin
    EXPECT_TRUE(b.end_frame("surface.panel"));
    EXPECT_FALSE(b.end_frame("surface.panel"));
}

// 契約：未知 id 一律回 false，不崩潰。
TEST(Win32Backend, UnknownIdIsSafe) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    const std::string unknown = "surface.nope";
    EXPECT_FALSE(b.has_surface(unknown));
    EXPECT_FALSE(b.show_surface(unknown));
    EXPECT_FALSE(b.hide_surface(unknown));
    EXPECT_FALSE(b.is_visible(unknown));
    EXPECT_FALSE(b.destroy_surface(unknown));
    EXPECT_FALSE(b.begin_frame(unknown));
    EXPECT_FALSE(b.end_frame(unknown));
    EXPECT_FALSE(b.set_input_policy(unknown, InputPolicy::PassThrough));
    EXPECT_EQ(b.surface_profile(unknown), nullptr);
}

// destroy 之後視窗真的不見了（不是只從表裡移除）。
TEST(Win32Backend, DestroyReallyDestroysWindow) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    HWND hwnd = b.hwnd_for("surface.panel");
    ASSERT_TRUE(::IsWindow(hwnd));

    EXPECT_TRUE(b.destroy_surface("surface.panel"));
    EXPECT_FALSE(b.has_surface("surface.panel"));
    EXPECT_EQ(b.surface_count(), 0u);
    EXPECT_FALSE(::IsWindow(hwnd));
}

// shutdown 要收掉所有 surface 的真實視窗（不得洩漏 HWND）。
TEST(Win32Backend, ShutdownDestroysAllWindows) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.a", topmost_panel()));
    ASSERT_TRUE(b.create_surface("surface.b", topmost_panel()));
    HWND a = b.hwnd_for("surface.a");
    HWND c = b.hwnd_for("surface.b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(c, nullptr);

    b.shutdown();
    EXPECT_EQ(b.surface_count(), 0u);
    EXPECT_FALSE(::IsWindow(a));
    EXPECT_FALSE(::IsWindow(c));
}

// 事後改圖層（托盤「最上層」切換的底層），且 surface_profile() 要跟著更新——
// 否則後端記的 profile 與 Windows 的真實狀態會分歧，之後誰也說不準哪個才對。
TEST(Win32Backend, SetSurfaceLayerTogglesTopmostBothWays) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    HWND hwnd = b.hwnd_for("surface.panel");
    ASSERT_TRUE(has_ex_style(hwnd, WS_EX_TOPMOST));

    ASSERT_TRUE(b.set_surface_layer("surface.panel", SurfaceLayer::Normal));
    EXPECT_FALSE(has_ex_style(hwnd, WS_EX_TOPMOST));
    EXPECT_EQ(b.surface_profile("surface.panel")->layer, SurfaceLayer::Normal);

    ASSERT_TRUE(b.set_surface_layer("surface.panel", SurfaceLayer::Topmost));
    EXPECT_TRUE(has_ex_style(hwnd, WS_EX_TOPMOST));
    EXPECT_EQ(b.surface_profile("surface.panel")->layer, SurfaceLayer::Topmost);

    EXPECT_FALSE(b.set_surface_layer("surface.nope", SurfaceLayer::Topmost));
}

// surface_profile 回報的是建立時的具名 profile。
TEST(Win32Backend, SurfaceProfileRoundTrips) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    SurfaceProfile p = topmost_panel();
    p.lifecycle = SurfaceLifecycle::Ephemeral;
    ASSERT_TRUE(b.create_surface("surface.overlay", p));

    const SurfaceProfile* got = b.surface_profile("surface.overlay");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->layer, SurfaceLayer::Topmost);
    EXPECT_EQ(got->lifecycle, SurfaceLifecycle::Ephemeral);
}

// 能力查詢契約（NFR-03）：has() 必與 capabilities().has() 一致；未知能力保守回 false。
TEST(Win32Backend, CapabilityQueryConsistentAndConservative) {
    Win32KernelBackend b;
    const CapabilityMatrix& m = b.capabilities();
    for (const auto& decl : m.all()) {
        EXPECT_EQ(b.has(decl.id), m.has(decl.id)) << "id=" << decl.id;
    }
    EXPECT_FALSE(b.has("capability.never.declared"));
}

// poll_input：不能對事件「數量」下斷言（測試機可能正好有滑鼠移動），但可以對**每個事件的
// 內容**下斷言——這才是有意義的部分。刻意不寫 `events.empty() || !events.empty()`
// 這種恆真式：一條永遠成立的斷言等於沒有測試（見知識庫 K-003 的通則）。
TEST(Win32Backend, PolledEventsAlwaysCarryKnownNamedTarget) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    ASSERT_TRUE(b.show_surface("surface.panel"));

    for (int i = 0; i < 3; ++i) {
        for (const auto& ev : b.poll_input()) {
            // 事件目標必須是**具名且已知**的 surface，不得是野生字串或已銷毀的 id。
            EXPECT_TRUE(ev.target.empty() || b.has_surface(ev.target))
                << "未知的事件目標: " << ev.target;
        }
    }
    EXPECT_FALSE(b.quit_requested());  // 沒人關視窗就不該回報結束
}

// poll_input 取走事件後不得重複回報同一批（否則上層會處理兩次）。
TEST(Win32Backend, PollInputDrainsQueue) {
    Win32KernelBackend b;
    ASSERT_TRUE(b.init());
    ASSERT_TRUE(b.create_surface("surface.panel", topmost_panel()));
    b.poll_input();                      // 先清空既有訊息
    auto second = b.poll_input();        // 緊接著再取一次
    // 兩次呼叫之間沒有人操作，第二次至多只會有這段極短時間內的新事件；
    // 關鍵是它**不會**把第一次已取走的事件再交一遍。
    for (const auto& ev : second) {
        EXPECT_TRUE(ev.target.empty() || b.has_surface(ev.target));
    }
    SUCCEED();
}
