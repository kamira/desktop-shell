// H1-04 鎖定位置開關 + UI 狀態持久化 — gtest
//
// 兩塊：
//   1. UI 狀態的序列化 / 還原（E7-01 格式，全有或全無）
//   2. 鎖定開關真的讓視窗拖不動——斷言去問 W1-03 的 `is_draggable`，不問旗標
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "tray_win32.hpp"
#include "ui_state.hpp"
#include "widget_controls.hpp"

using ds::command::CommandBus;
using ds::host::build_widget_tray_menu;
using ds::host::default_ui_state_path;
using ds::host::parse_ui_state;
using ds::host::register_widget_controls;
using ds::host::serialize_ui_state;
using ds::host::SystemTray;
using ds::host::TrayClickStatus;
using ds::host::WidgetControlState;
using ds::host::WidgetUiState;
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

SurfaceProfile panel() {
    SurfaceProfile p;
    p.layer = SurfaceLayer::Topmost;
    p.input = InputPolicy::Accepting;
    p.hit = HitPolicy::Solid;
    p.lifecycle = SurfaceLifecycle::Persistent;
    return p;
}

// 選單索引：[0] 最上層、[1] 穿透、[2] 鎖定、[3] 分隔線、[4] 結束
const std::vector<std::size_t> kLock{2};

struct Rig {
    Win32KernelBackend backend{CapabilityMatrix::defaults()};
    bool backend_initialized_ = backend.init();
    WidgetControlState state;
    CommandBus bus;
    std::unique_ptr<SystemTray> tray;
    ds::kernel::SurfaceId id = kSurface;

    Rig() {
        EXPECT_TRUE(backend.create_surface(id, panel()));
        EXPECT_TRUE(backend.set_draggable(id, true));  // host 啟動時的預設：可拖
        register_widget_controls(bus, backend, id, state);
        tray = std::make_unique<SystemTray>(std::make_unique<Win32TrayBackend>(), &bus);
        tray->set_menu(build_widget_tray_menu(state));
    }
};

}  // namespace

// --- UI 狀態序列化 -----------------------------------------------------------

TEST(UiState, RoundTripsThroughDeclarativeFormat) {
    for (const WidgetUiState in : {WidgetUiState{true, false, false},
                                   WidgetUiState{false, true, true},
                                   WidgetUiState{true, true, false}}) {
        const std::string text = serialize_ui_state(in);
        WidgetUiState out;
        ASSERT_TRUE(parse_ui_state(text, out)) << text;
        EXPECT_EQ(out.topmost, in.topmost);
        EXPECT_EQ(out.passthrough, in.passthrough);
        EXPECT_EQ(out.locked, in.locked);
    }
}

// 輸出必須是 E7-01 合法文字（首行 format_version），否則下次讀不回來。
TEST(UiState, SerializedTextCarriesFormatVersion) {
    const std::string text = serialize_ui_state(WidgetUiState{});
    EXPECT_EQ(text.rfind("format_version:", 0), 0u) << text;
}

// 壞掉的檔案：不得留半套狀態（與 E1-08 load_positions 同語意）。
TEST(UiState, CorruptTextLeavesOutputUntouched) {
    WidgetUiState out{false, true, true};  // 哨兵值
    const WidgetUiState before = out;

    for (const char* bad : {"", "這不是合法格式 {{{", "format_version: 1.0\n",
                            "format_version: 1.0\nwidget.controls:\n  topmost: true\n"}) {
        EXPECT_FALSE(parse_ui_state(bad, out)) << bad;
        EXPECT_EQ(out.topmost, before.topmost) << "回 false 時不得改寫 out";
        EXPECT_EQ(out.passthrough, before.passthrough);
        EXPECT_EQ(out.locked, before.locked);
    }
}

// 欄位型別錯（不是布林）也要整批放棄。
TEST(UiState, WrongFieldTypeIsRejected) {
    const char* text =
        "format_version: 1.0\n"
        "widget.controls:\n"
        "  topmost: \"yes\"\n"
        "  passthrough: false\n"
        "  locked: false\n";
    WidgetUiState out;
    EXPECT_FALSE(parse_ui_state(text, out));
}

TEST(UiState, DefaultPathIsUnderLocalAppData) {
    const std::string p = default_ui_state_path();
    ASSERT_FALSE(p.empty()) << "測試環境應有 LOCALAPPDATA";
    EXPECT_NE(p.find("desktop-shell"), std::string::npos);
    EXPECT_NE(p.find("ui-state.conf"), std::string::npos);
}

// --- 鎖定開關 ---------------------------------------------------------------

TEST(WidgetControls, MenuHasLockItemWithCorrectInitialCheck) {
    WidgetControlState s;
    s.locked = true;
    auto menu = build_widget_tray_menu(s);
    ASSERT_EQ(menu.size(), 5u);
    EXPECT_EQ(menu.items()[2].command_id(), ds::host::kCmdToggleLock);
    EXPECT_TRUE(menu.items()[2].checked()) << "鎖定中，選單一打開就該顯示已勾選";
}

// 核心：點「鎖定位置」真的讓後端變成不可拖，再點一次真的可拖。
// 斷言問的是 W1-03 的 is_draggable（後端真實狀態），不是 state.locked 旗標。
TEST(WidgetControls, LockMenuItemActuallyDisablesDragging) {
    Rig rig;
    ASSERT_TRUE(rig.backend.is_draggable(rig.id));

    EXPECT_EQ(rig.tray->click(kLock).status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(rig.state.locked);
    EXPECT_FALSE(rig.backend.is_draggable(rig.id)) << "選單說鎖了，後端卻還可拖";

    EXPECT_EQ(rig.tray->click(kLock).status, TrayClickStatus::Dispatched);
    EXPECT_FALSE(rig.state.locked);
    EXPECT_TRUE(rig.backend.is_draggable(rig.id));
}

// 鎖定不得干擾其他兩個開關——三個開關必須各自獨立。
TEST(WidgetControls, LockIsIndependentOfOtherToggles) {
    Rig rig;
    HWND hwnd = rig.backend.hwnd_for(rig.id);
    ASSERT_NE(hwnd, nullptr);
    const auto has_ex = [&](LONG_PTR bit) {
        return (::GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & bit) != 0;
    };
    ASSERT_TRUE(has_ex(WS_EX_TOPMOST));
    ASSERT_FALSE(has_ex(WS_EX_TRANSPARENT));

    rig.tray->click(kLock);
    EXPECT_FALSE(rig.backend.is_draggable(rig.id));
    EXPECT_TRUE(has_ex(WS_EX_TOPMOST)) << "鎖定不得動到最上層";
    EXPECT_FALSE(has_ex(WS_EX_TRANSPARENT)) << "鎖定不得動到穿透";
}

// click 後選單模型的勾選狀態要跟著翻面（下次打開才顯示正確）。
TEST(WidgetControls, LockCheckStatePersistsInMenuModel) {
    Rig rig;
    ASSERT_FALSE(rig.tray->menu().items()[2].checked());
    rig.tray->click(kLock);
    EXPECT_TRUE(rig.tray->menu().items()[2].checked());
}
