// E11-01 系統匣圖示與右鍵選單 — 契約測試（gtest）
//
// 驗證相位 1（Mac / null 期）語意，全程不含任何平台分支：
//   - 設定圖示 / 提示文字（同步推送 null 後端、狀態一致）
//   - 建立選單（一般項 / 分隔線 / 子選單 / 勾選項）
//   - 選單項點擊經 E6-01 命令匯流排分派（Action / 巢狀子選單項）
//   - 勾選項點擊：先切換勾選、再帶 checked 參數分派；反覆切換
//   - 邊界：無效路徑 / 不可點擊（分隔線・子選單・停用）/ 無命令 / 未接匯流排 → 不崩潰
//   - null 後端狀態一致（記錄圖示 / 提示 / 選單 / 可見狀態 / 呼叫次數）
//   - 更新選單（sync_menu / set_menu 重新推送後端）
//   - has() == false（相位 1 無真實匣）
#include "tray.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "command_bus.hpp"

using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::host::make_default_tray;
using ds::host::NullTrayBackend;
using ds::host::SystemTray;
using ds::host::TrayBackend;
using ds::host::TrayClickStatus;
using ds::host::TrayItemKind;
using ds::host::TrayMenu;
using ds::host::TrayMenuItem;

namespace {

// 便捷：以已知的 NullTrayBackend 指標建構 SystemTray（測試需觀察後端記錄）。
SystemTray make_tray_with_null(const CommandBus* bus, NullTrayBackend** out_backend) {
    auto backend = std::make_unique<NullTrayBackend>();
    *out_backend = backend.get();
    return SystemTray(std::move(backend), bus);
}

// --- 相位 1 能力語意 -------------------------------------------------------

// null 後端不對應真實匣：has() == false（能力閘控入口，NFR-03 精神）。
TEST(SystemTrayPhase1, HasIsFalseForNullBackend) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    EXPECT_FALSE(tray.has());
    EXPECT_FALSE(be->has());
}

// 工廠回傳可用（非空）的 null 後端匣。
TEST(MakeDefaultTray, ReturnsUsableNullBackedTray) {
    CommandBus bus;
    std::unique_ptr<SystemTray> tray = make_default_tray(&bus);
    ASSERT_NE(tray, nullptr);
    EXPECT_FALSE(tray->has());  // 相位 1 無真實匣
    EXPECT_FALSE(tray->visible());
}

// --- 圖示 / 提示 -----------------------------------------------------------

// 設定圖示 / 提示：控制器狀態更新，且同步推送 null 後端（狀態一致）。
TEST(SystemTrayIcon, SetIconAndTooltipSyncToBackend) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);

    tray.set_icon("icon.default");
    tray.set_tooltip("Desktop Shell");

    EXPECT_EQ(tray.icon(), "icon.default");
    EXPECT_EQ(tray.tooltip(), "Desktop Shell");
    EXPECT_EQ(be->icon(), "icon.default");
    EXPECT_EQ(be->tooltip(), "Desktop Shell");
    EXPECT_EQ(be->set_icon_calls(), 1u);
    EXPECT_EQ(be->set_tooltip_calls(), 1u);
}

// 更新圖示 / 提示：後端反映最後一次的值，且累計呼叫次數。
TEST(SystemTrayIcon, UpdateIconReflectsLatestValue) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);

    tray.set_icon("icon.a");
    tray.set_icon("icon.b");
    EXPECT_EQ(tray.icon(), "icon.b");
    EXPECT_EQ(be->icon(), "icon.b");
    EXPECT_EQ(be->set_icon_calls(), 2u);
}

// 顯示 / 隱藏：可見狀態同步後端。
TEST(SystemTrayVisibility, ShowHideSyncToBackend) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);

    EXPECT_FALSE(tray.visible());
    tray.show();
    EXPECT_TRUE(tray.visible());
    EXPECT_TRUE(be->visible());
    EXPECT_EQ(be->show_calls(), 1u);
    tray.hide();
    EXPECT_FALSE(tray.visible());
    EXPECT_FALSE(be->visible());
    EXPECT_EQ(be->hide_calls(), 1u);
}

// --- 建立選單 --------------------------------------------------------------

// 建立含各型別（一般項 / 分隔線 / 勾選項 / 子選單）的選單並推送後端。
TEST(TrayMenuBuild, BuildsAllItemKindsAndSyncsToBackend) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);

    TrayMenu menu;
    menu.add_action("Open", "app.open")
        .add_separator()
        .add_checkbox("Mute", "audio.mute", /*checked=*/false)
        .add_submenu("More",
                     {TrayMenuItem::action("About", "app.about"),
                      TrayMenuItem::action("Quit", "app.quit")});
    tray.set_menu(std::move(menu));

    const TrayMenu& m = tray.menu();
    ASSERT_EQ(m.size(), 4u);
    EXPECT_EQ(m.items()[0].kind(), TrayItemKind::Action);
    EXPECT_EQ(m.items()[0].label(), "Open");
    EXPECT_EQ(m.items()[1].kind(), TrayItemKind::Separator);
    EXPECT_FALSE(m.items()[1].is_clickable());
    EXPECT_EQ(m.items()[2].kind(), TrayItemKind::Checkbox);
    EXPECT_EQ(m.items()[3].kind(), TrayItemKind::Submenu);
    ASSERT_EQ(m.items()[3].children().size(), 2u);
    EXPECT_EQ(m.items()[3].children()[1].command_id(), "app.quit");

    // set_menu 推送後端一次，後端記錄同一選單快照。
    EXPECT_EQ(be->set_menu_calls(), 1u);
    EXPECT_EQ(be->menu().size(), 4u);
}

// 子選單本身不可點擊（容器角色）。
TEST(TrayMenuBuild, SubmenuIsNotClickable) {
    TrayMenuItem sub = TrayMenuItem::submenu("More", {TrayMenuItem::action("X", "cmd.x")});
    EXPECT_FALSE(sub.is_clickable());
    EXPECT_EQ(sub.kind(), TrayItemKind::Submenu);
}

// --- 點擊 → E6-01 分派 -----------------------------------------------------

// Action 點擊：經匯流排分派至已註冊處理器，處理器被呼叫、回傳成功。
TEST(TrayClickDispatch, ActionClickDispatchesViaCommandBus) {
    CommandBus bus;
    int opened = 0;
    ASSERT_TRUE(bus.register_command("app.open", [&](const CommandArgs&) {
        ++opened;
        return CommandResult::make_ok();
    }));

    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_action("Open", "app.open");
    tray.set_menu(std::move(menu));

    auto r = tray.click({0});
    EXPECT_EQ(r.status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(opened, 1);
}

// 巢狀子選單項點擊：以索引路徑 {submenu, child} 定位並分派。
TEST(TrayClickDispatch, NestedSubmenuItemDispatches) {
    CommandBus bus;
    int quit = 0;
    ASSERT_TRUE(bus.register_command("app.quit", [&](const CommandArgs&) {
        ++quit;
        return CommandResult::make_ok();
    }));

    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_submenu("More",
                     {TrayMenuItem::action("About", "app.about"),
                      TrayMenuItem::action("Quit", "app.quit")});
    tray.set_menu(std::move(menu));

    auto r = tray.click({0, 1});  // More → Quit
    EXPECT_EQ(r.status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(quit, 1);
}

// 未知命令：匯流排回 NotFound（分派成功但處理器不存在），不崩潰。
TEST(TrayClickDispatch, UnknownCommandReturnsNotFoundNotCrash) {
    CommandBus bus;  // 未註冊任何命令
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_action("Ghost", "does.not.exist");
    tray.set_menu(std::move(menu));

    auto r = tray.click({0});
    EXPECT_EQ(r.status, TrayClickStatus::Dispatched);
    EXPECT_FALSE(r.ok());  // 分派了，但命令不存在
    EXPECT_EQ(r.command_result.status, ds::command::CommandStatus::NotFound);
}

// --- 勾選切換 --------------------------------------------------------------

// Checkbox 點擊：先切換勾選（false→true），再帶 checked=true 參數分派；後端反映新狀態。
TEST(TrayCheckbox, ClickTogglesThenDispatchesWithCheckedArg) {
    CommandBus bus;
    bool received_checked = false;
    bool handler_called = false;
    ASSERT_TRUE(bus.register_command("audio.mute", [&](const CommandArgs& a) {
        handler_called = true;
        auto c = a.get_bool("checked");
        received_checked = c.has_value() && *c;
        return CommandResult::make_ok();
    }));

    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_checkbox("Mute", "audio.mute", /*checked=*/false);
    tray.set_menu(std::move(menu));
    ASSERT_EQ(be->set_menu_calls(), 1u);  // 初次 set_menu

    auto r = tray.click({0});
    EXPECT_EQ(r.status, TrayClickStatus::Dispatched);
    EXPECT_TRUE(r.checked);                       // 切換後為 true
    EXPECT_TRUE(handler_called);
    EXPECT_TRUE(received_checked);                // 命令收到 checked=true
    EXPECT_TRUE(tray.menu().items()[0].checked());  // 模型已更新
    EXPECT_EQ(be->set_menu_calls(), 2u);          // 切換後再推送後端一次
    EXPECT_TRUE(be->menu().items()[0].checked());  // 後端反映新勾選狀態
}

// 反覆點擊勾選項：狀態在 true / false 間交替，每次帶當前狀態分派。
TEST(TrayCheckbox, RepeatedClicksAlternateState) {
    CommandBus bus;
    std::vector<bool> seen;
    ASSERT_TRUE(bus.register_command("audio.mute", [&](const CommandArgs& a) {
        auto c = a.get_bool("checked");
        seen.push_back(c.has_value() && *c);
        return CommandResult::make_ok();
    }));

    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_checkbox("Mute", "audio.mute", /*checked=*/false);
    tray.set_menu(std::move(menu));

    tray.click({0});  // → true
    tray.click({0});  // → false
    tray.click({0});  // → true
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_TRUE(seen[0]);
    EXPECT_FALSE(seen[1]);
    EXPECT_TRUE(seen[2]);
    EXPECT_TRUE(tray.menu().items()[0].checked());
}

// 勾選項無命令 id：仍切換勾選、但回 NoCommand（切換生效、不分派）。
TEST(TrayCheckbox, CheckboxWithoutCommandTogglesButReportsNoCommand) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_checkbox("Flag", /*command_id=*/"", /*checked=*/false);
    tray.set_menu(std::move(menu));

    auto r = tray.click({0});
    EXPECT_EQ(r.status, TrayClickStatus::NoCommand);
    EXPECT_TRUE(r.checked);                          // 切換仍生效
    EXPECT_TRUE(tray.menu().items()[0].checked());
}

// --- 點擊邊界（不崩潰）-----------------------------------------------------

// 無效索引路徑 → InvalidPath。
TEST(TrayClickEdge, InvalidPathReturnsInvalidPath) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_action("Open", "app.open");
    tray.set_menu(std::move(menu));

    EXPECT_EQ(tray.click({5}).status, TrayClickStatus::InvalidPath);      // 越界
    EXPECT_EQ(tray.click({}).status, TrayClickStatus::InvalidPath);       // 空路徑
    EXPECT_EQ(tray.click({0, 0}).status, TrayClickStatus::InvalidPath);   // 穿越非子選單
}

// 點擊分隔線 → NotClickable（不分派）。
TEST(TrayClickEdge, ClickSeparatorNotClickable) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_separator();
    tray.set_menu(std::move(menu));
    EXPECT_EQ(tray.click({0}).status, TrayClickStatus::NotClickable);
}

// 點擊子選單容器 → NotClickable。
TEST(TrayClickEdge, ClickSubmenuContainerNotClickable) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_submenu("More", {TrayMenuItem::action("X", "cmd.x")});
    tray.set_menu(std::move(menu));
    EXPECT_EQ(tray.click({0}).status, TrayClickStatus::NotClickable);
}

// 點擊停用項 → NotClickable（不分派）。
TEST(TrayClickEdge, ClickDisabledItemNotClickable) {
    CommandBus bus;
    int called = 0;
    ASSERT_TRUE(bus.register_command("app.open", [&](const CommandArgs&) {
        ++called;
        return CommandResult::make_ok();
    }));
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_action("Open", "app.open", /*enabled=*/false);
    tray.set_menu(std::move(menu));

    EXPECT_EQ(tray.click({0}).status, TrayClickStatus::NotClickable);
    EXPECT_EQ(called, 0);  // 停用項不分派
}

// 未注入匯流排（bus == nullptr）→ NoBus，不崩潰。
TEST(TrayClickEdge, NoBusInjectedReturnsNoBus) {
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(/*bus=*/nullptr, &be);
    TrayMenu menu;
    menu.add_action("Open", "app.open");
    tray.set_menu(std::move(menu));
    EXPECT_EQ(tray.click({0}).status, TrayClickStatus::NoBus);
}

// --- 更新選單 --------------------------------------------------------------

// 就地修改選單後 sync_menu()：後端反映新選單。
TEST(TrayMenuUpdate, SyncMenuPushesInPlaceEdits) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);
    TrayMenu menu;
    menu.add_action("Open", "app.open");
    tray.set_menu(std::move(menu));
    EXPECT_EQ(be->set_menu_calls(), 1u);

    tray.menu().add_action("Close", "app.close");  // 就地追加
    tray.sync_menu();
    EXPECT_EQ(be->set_menu_calls(), 2u);
    EXPECT_EQ(be->menu().size(), 2u);
    EXPECT_EQ(be->menu().items()[1].command_id(), "app.close");
}

// set_menu 整體替換：後端反映替換後的選單，可見 / 圖示狀態不受影響。
TEST(TrayMenuUpdate, SetMenuReplacesWholeMenu) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);

    TrayMenu first;
    first.add_action("A", "cmd.a").add_action("B", "cmd.b");
    tray.set_menu(std::move(first));
    EXPECT_EQ(tray.menu().size(), 2u);

    TrayMenu second;
    second.add_action("C", "cmd.c");
    tray.set_menu(std::move(second));
    EXPECT_EQ(tray.menu().size(), 1u);
    EXPECT_EQ(be->menu().size(), 1u);
    EXPECT_EQ(be->menu().items()[0].command_id(), "cmd.c");
    EXPECT_EQ(be->set_menu_calls(), 2u);
}

// --- null 後端狀態一致 -----------------------------------------------------

// 綜合：一連串操作後，null 後端記錄的圖示 / 提示 / 選單 / 可見狀態與控制器一致。
TEST(NullBackendConsistency, RecordsAllStateFaithfully) {
    CommandBus bus;
    NullTrayBackend* be = nullptr;
    SystemTray tray = make_tray_with_null(&bus, &be);

    tray.set_icon("i1");
    tray.set_tooltip("t1");
    TrayMenu menu;
    menu.add_action("Open", "app.open").add_separator().add_checkbox("Mute", "audio.mute");
    tray.set_menu(std::move(menu));
    tray.show();

    EXPECT_EQ(be->icon(), tray.icon());
    EXPECT_EQ(be->tooltip(), tray.tooltip());
    EXPECT_EQ(be->menu().size(), tray.menu().size());
    EXPECT_EQ(be->visible(), tray.visible());
    EXPECT_TRUE(be->visible());
    EXPECT_EQ(be->set_icon_calls(), 1u);
    EXPECT_EQ(be->set_tooltip_calls(), 1u);
    EXPECT_EQ(be->set_menu_calls(), 1u);
    EXPECT_EQ(be->show_calls(), 1u);
    EXPECT_EQ(be->hide_calls(), 0u);
}

}  // namespace
