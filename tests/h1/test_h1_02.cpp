// H1-02 托盤裝配 + widget 控制命令 — gtest（整合測試）
//
// 走完整條鏈：**托盤選單項 → E11-01 SystemTray::click(path) → E6-01 命令 → W1-01 後端 →
// 真實視窗的 ex-style 改變**。斷言一律去問 Windows（GetWindowLongPtr），不問旗標。
//
// 唯一沒被涵蓋的是「滑鼠實際點在托盤圖示上」——那需要 UI 自動化，由操作驗收負責。
// 換句話說：如果這裡全綠而實機無效，問題只可能出在那一個環節，範圍很窄。
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "tray_win32.hpp"
#include "widget_controls.hpp"

using ds::command::CommandBus;
using ds::host::build_widget_tray_menu;
using ds::host::register_widget_controls;
using ds::host::SystemTray;
using ds::host::TrayClickStatus;
using ds::host::WidgetControlState;
using ds::host::Win32TrayBackend;
using ds::kernel::CapabilityMatrix;
using ds::kernel::HitPolicy;
using ds::kernel::InputPolicy;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceProfile;
using ds::kernel::Win32KernelBackend;

namespace {

constexpr const char* kSurface = "surface.test_widget";

SurfaceProfile topmost_panel() {
    SurfaceProfile p;
    p.layer = SurfaceLayer::Topmost;
    p.input = InputPolicy::Accepting;
    p.hit = HitPolicy::Solid;
    p.lifecycle = SurfaceLifecycle::Persistent;
    return p;
}

bool has_ex(HWND h, LONG_PTR bit) { return (::GetWindowLongPtrW(h, GWL_EXSTYLE) & bit) != 0; }

// 一組接好線的完整環境：真實 surface + 命令匯流排 + 托盤。
struct Rig {
    Win32KernelBackend backend;
    WidgetControlState state;
    CommandBus bus;
    std::unique_ptr<SystemTray> tray;
    HWND hwnd = nullptr;

    Rig() : backend(CapabilityMatrix::defaults()) {
        EXPECT_TRUE(backend.init());
        EXPECT_TRUE(backend.create_surface(kSurface, topmost_panel()));
        hwnd = backend.hwnd_for(kSurface);
        register_widget_controls(bus, backend, surface_id(), state);
        tray = std::make_unique<SystemTray>(std::make_unique<Win32TrayBackend>(), &bus);
        tray->set_menu(build_widget_tray_menu(state));
    }
    // 命令處理器持有 surface_id 的參照，必須是穩定的物件。
    const ds::kernel::SurfaceId& surface_id() const { return id_; }

private:
    ds::kernel::SurfaceId id_ = kSurface;
};

// 選單索引：[0] 最上層、[1] 點擊穿透、[2] 鎖定位置、[3] 分隔線、[4] 結束
// （[2] 由 CHG-20260803-12 新增，其後索引順移；`MenuShapeMatchesIndicesUsedByHost`
//   會在這組常數與實際選單脫節時紅燈。）
const std::vector<std::size_t> kTopmost{0};
const std::vector<std::size_t> kPassthrough{1};
const std::vector<std::size_t> kLock{2};
const std::vector<std::size_t> kSeparator{3};
const std::vector<std::size_t> kQuit{4};

}  // namespace

TEST(WidgetControls, MenuShapeMatchesIndicesUsedByHost) {
    WidgetControlState s;
    auto menu = build_widget_tray_menu(s);
    ASSERT_EQ(menu.size(), 5u);
    EXPECT_TRUE(menu.items()[3].kind() == ds::host::TrayItemKind::Separator);
    EXPECT_EQ(menu.items()[0].command_id(), ds::host::kCmdToggleTopmost);
    EXPECT_EQ(menu.items()[1].command_id(), ds::host::kCmdTogglePassthrough);
    EXPECT_EQ(menu.items()[2].command_id(), ds::host::kCmdToggleLock);
    EXPECT_EQ(menu.items()[4].command_id(), ds::host::kCmdQuit);
}

// 初始勾選狀態必須反映實際狀態，否則選單一打開就在說謊。
TEST(WidgetControls, MenuCheckStateReflectsState) {
    WidgetControlState s;
    s.topmost = false;
    s.passthrough = true;
    auto menu = build_widget_tray_menu(s);
    EXPECT_FALSE(menu.items()[0].checked());
    EXPECT_TRUE(menu.items()[1].checked());
}

// 核心：點「最上層」真的把視窗的 WS_EX_TOPMOST 拿掉，再點一次真的加回來。
TEST(WidgetControls, TopmostMenuItemActuallyTogglesWindowStyle) {
    Rig rig;
    ASSERT_NE(rig.hwnd, nullptr);
    ASSERT_TRUE(has_ex(rig.hwnd, WS_EX_TOPMOST));

    auto r1 = rig.tray->click(kTopmost);
    EXPECT_EQ(r1.status, TrayClickStatus::Dispatched);
    EXPECT_FALSE(rig.state.topmost);
    EXPECT_FALSE(has_ex(rig.hwnd, WS_EX_TOPMOST)) << "選單說切掉了，視窗卻還是最上層";

    auto r2 = rig.tray->click(kTopmost);
    EXPECT_EQ(r2.status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(rig.state.topmost);
    EXPECT_TRUE(has_ex(rig.hwnd, WS_EX_TOPMOST));
}

// 核心：點「點擊穿透」真的加上 / 拿掉 WS_EX_TRANSPARENT。
TEST(WidgetControls, PassthroughMenuItemActuallyTogglesWindowStyle) {
    Rig rig;
    ASSERT_FALSE(has_ex(rig.hwnd, WS_EX_TRANSPARENT));

    EXPECT_EQ(rig.tray->click(kPassthrough).status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(rig.state.passthrough);
    EXPECT_TRUE(has_ex(rig.hwnd, WS_EX_TRANSPARENT));

    EXPECT_EQ(rig.tray->click(kPassthrough).status, TrayClickStatus::Dispatched);
    EXPECT_FALSE(rig.state.passthrough);
    EXPECT_FALSE(has_ex(rig.hwnd, WS_EX_TRANSPARENT));
}

// 兩個開關必須互不干擾——否則切最上層會順手改掉穿透，使用者無從預期。
TEST(WidgetControls, TogglesAreIndependent) {
    Rig rig;
    rig.tray->click(kPassthrough);
    ASSERT_TRUE(has_ex(rig.hwnd, WS_EX_TRANSPARENT));

    rig.tray->click(kTopmost);
    EXPECT_FALSE(has_ex(rig.hwnd, WS_EX_TOPMOST));
    EXPECT_TRUE(has_ex(rig.hwnd, WS_EX_TRANSPARENT)) << "切最上層時不該動到穿透";
}

TEST(WidgetControls, QuitMenuItemSetsQuitFlag) {
    Rig rig;
    EXPECT_FALSE(rig.state.quit);
    EXPECT_EQ(rig.tray->click(kQuit).status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(rig.state.quit);
}

// 點分隔線不得分派任何命令，也不得改變任何狀態。
TEST(WidgetControls, ClickingSeparatorDoesNothing) {
    Rig rig;
    const bool before_topmost = has_ex(rig.hwnd, WS_EX_TOPMOST);
    const bool before_transparent = has_ex(rig.hwnd, WS_EX_TRANSPARENT);

    EXPECT_EQ(rig.tray->click(kSeparator).status, TrayClickStatus::NotClickable);
    EXPECT_FALSE(rig.state.quit);
    EXPECT_EQ(has_ex(rig.hwnd, WS_EX_TOPMOST), before_topmost);
    EXPECT_EQ(has_ex(rig.hwnd, WS_EX_TRANSPARENT), before_transparent);
}

// 越界路徑要結構化回報，不得崩潰。
TEST(WidgetControls, InvalidPathIsReportedNotCrashed) {
    Rig rig;
    EXPECT_EQ(rig.tray->click({99}).status, TrayClickStatus::InvalidPath);
    EXPECT_EQ(rig.tray->click({0, 5}).status, TrayClickStatus::InvalidPath);
    EXPECT_EQ(rig.tray->click({}).status, TrayClickStatus::InvalidPath);
}

// 勾選狀態要在 click 後真的翻面（選單下次打開才會顯示正確狀態）。
TEST(WidgetControls, CheckStatePersistsInMenuModelAfterClick) {
    Rig rig;
    ASSERT_TRUE(rig.tray->menu().items()[0].checked());
    rig.tray->click(kTopmost);
    EXPECT_FALSE(rig.tray->menu().items()[0].checked())
        << "視窗已非最上層，選單卻還打著勾";
}

// 真實托盤後端的 has() 必須為 true——否則能力閘控會以為沒有系統匣。
TEST(WidgetControls, TrayReportsRealSystemTray) {
    Rig rig;
    EXPECT_TRUE(rig.tray->has());
}
