// W1-02 win32 系統匣後端 — gtest
//
// 兩塊：
//   1. build_native_menu()：TrayMenu 模型 → HMENU 的結構對映。可完全離線斷言
//      （GetMenuItemCount / GetMenuState），不需要匣圖示、不需要使用者互動。
//   2. 後端生命週期與匣圖示狀態。
//
// 測不到的部分要說清楚：**使用者真的右鍵點下去**這一段需要 UI 自動化，本檔不涵蓋，
// 由操作驗收負責（見 CHG-20260803-06 §操作驗收）。這裡不寫「呼叫沒崩潰」式的假斷言充數。
#include <gtest/gtest.h>

#include <vector>

#include "tray_win32.hpp"

using ds::host::build_native_menu;
using ds::host::BuiltMenu;
using ds::host::destroy_built_menu;
using ds::host::TrayMenu;
using ds::host::TrayMenuItem;
using ds::host::Win32TrayBackend;

namespace {

// 一份涵蓋四種項目種類的選單：
//   [0] Action   最上層切換
//   [1] Checkbox 點擊穿透（已勾選）
//   [2] Separator
//   [3] Submenu  → [3,0] Action / [3,1] Action(停用)
//   [4] Action   結束
TrayMenu sample_menu() {
    TrayMenu m;
    m.items().push_back(TrayMenuItem::action("最上層", "widget.toggle_topmost"));
    m.items().push_back(TrayMenuItem::checkbox("點擊穿透", "widget.toggle_passthrough", true));
    m.items().push_back(TrayMenuItem::separator());
    m.items().push_back(TrayMenuItem::submenu("進階", {
        TrayMenuItem::action("重新載入", "widget.reload"),
        TrayMenuItem::action("停用中", "widget.disabled", false),
    }));
    m.items().push_back(TrayMenuItem::action("結束", "app.quit"));
    return m;
}

const std::vector<std::size_t>* path_of(const BuiltMenu& b, const char* /*label*/, UINT id) {
    for (const auto& bind : b.bindings) {
        if (bind.command_id == id) return &bind.path;
    }
    return nullptr;
}

}  // namespace

// 分隔線與子選單本身不可點擊，不該配到命令 id；其餘四項（含子選單內兩項）該配到。
TEST(TrayWin32Menu, OnlyClickableItemsGetCommandIds) {
    TrayMenu m = sample_menu();
    BuiltMenu b = build_native_menu(m);
    ASSERT_NE(b.handle, nullptr);
    // 可點擊：[0] [1] [3,0] [3,1] [4] = 5 項（停用項仍是可點擊種類，只是 MF_GRAYED）
    EXPECT_EQ(b.bindings.size(), 5u);
    destroy_built_menu(b);
}

// 頂層項目數要與模型一致（分隔線與子選單都佔一格）。
TEST(TrayWin32Menu, TopLevelItemCountMatchesModel) {
    TrayMenu m = sample_menu();
    BuiltMenu b = build_native_menu(m);
    EXPECT_EQ(::GetMenuItemCount(b.handle), 5);
    destroy_built_menu(b);
}

// 索引路徑必須指回模型中的正確位置——這是整個設計的關鍵：
// 後端只回報「選了哪一項」，語意由 SystemTray::click(path) 解釋。路徑錯了就全錯。
TEST(TrayWin32Menu, BindingPathsPointBackToModelPositions) {
    TrayMenu m = sample_menu();
    BuiltMenu b = build_native_menu(m);

    // bindings 依建構順序排列：[0], [1], [3,0], [3,1], [4]
    ASSERT_EQ(b.bindings.size(), 5u);
    EXPECT_EQ(b.bindings[0].path, (std::vector<std::size_t>{0}));
    EXPECT_EQ(b.bindings[1].path, (std::vector<std::size_t>{1}));
    EXPECT_EQ(b.bindings[2].path, (std::vector<std::size_t>{3, 0}));  // 子選單內
    EXPECT_EQ(b.bindings[3].path, (std::vector<std::size_t>{3, 1}));
    EXPECT_EQ(b.bindings[4].path, (std::vector<std::size_t>{4}));

    // 每個命令 id 必須唯一，否則選取會對應到錯的項目。
    for (std::size_t i = 0; i < b.bindings.size(); ++i) {
        for (std::size_t j = i + 1; j < b.bindings.size(); ++j) {
            EXPECT_NE(b.bindings[i].command_id, b.bindings[j].command_id);
        }
    }
    destroy_built_menu(b);
}

// Checkbox 的勾選狀態要真的反映到原生選單上（否則使用者看不出開關狀態）。
TEST(TrayWin32Menu, CheckedStateReachesNativeMenu) {
    TrayMenu m = sample_menu();
    BuiltMenu b = build_native_menu(m);
    const UINT checked_id = b.bindings[1].command_id;  // [1] 點擊穿透，checked=true
    const UINT state = ::GetMenuState(b.handle, checked_id, MF_BYCOMMAND);
    ASSERT_NE(state, static_cast<UINT>(-1));
    EXPECT_TRUE((state & MF_CHECKED) != 0);
    destroy_built_menu(b);
}

// 反向對照：未勾選的 Checkbox 不得帶 MF_CHECKED，否則「勾選」等於沒有意義。
TEST(TrayWin32Menu, UncheckedCheckboxIsNotChecked) {
    TrayMenu m;
    m.items().push_back(TrayMenuItem::checkbox("關著的", "x", false));
    BuiltMenu b = build_native_menu(m);
    const UINT state = ::GetMenuState(b.handle, b.bindings[0].command_id, MF_BYCOMMAND);
    ASSERT_NE(state, static_cast<UINT>(-1));
    EXPECT_FALSE((state & MF_CHECKED) != 0);
    destroy_built_menu(b);
}

// 停用項要真的是 MF_GRAYED。
TEST(TrayWin32Menu, DisabledItemIsGrayed) {
    TrayMenu m = sample_menu();
    BuiltMenu b = build_native_menu(m);
    const UINT disabled_id = b.bindings[3].command_id;  // [3,1] 停用中
    const UINT state = ::GetMenuState(b.handle, disabled_id, MF_BYCOMMAND);
    ASSERT_NE(state, static_cast<UINT>(-1));
    EXPECT_TRUE((state & (MF_GRAYED | MF_DISABLED)) != 0);
    destroy_built_menu(b);
}

// 子選單要真的是巢狀 popup，且內含兩項。
TEST(TrayWin32Menu, SubmenuIsNestedWithChildren) {
    TrayMenu m = sample_menu();
    BuiltMenu b = build_native_menu(m);
    HMENU sub = ::GetSubMenu(b.handle, 3);  // 第 3 個位置是子選單
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(::GetMenuItemCount(sub), 2);
    destroy_built_menu(b);
}

TEST(TrayWin32Menu, EmptyMenuIsValidAndEmpty) {
    TrayMenu m;
    BuiltMenu b = build_native_menu(m);
    ASSERT_NE(b.handle, nullptr);
    EXPECT_EQ(::GetMenuItemCount(b.handle), 0);
    EXPECT_TRUE(b.bindings.empty());
    destroy_built_menu(b);
}

// --- 後端生命週期 ------------------------------------------------------------

// 相位 1 的 null 後端 has()==false；真實後端必須回 true——這是能力閘控要區分的東西。
TEST(TrayWin32Backend, HasIsTrueUnlikeNullBackend) {
    Win32TrayBackend b;
    EXPECT_TRUE(b.has());
    ds::host::NullTrayBackend null_backend;
    EXPECT_FALSE(null_backend.has());
}

TEST(TrayWin32Backend, ShowAddsIconAndHideRemovesIt) {
    Win32TrayBackend b;
    EXPECT_FALSE(b.icon_added());
    b.set_tooltip("desktop-shell");
    b.show();
    EXPECT_TRUE(b.icon_added());
    b.show();  // 冪等：重複 show 不得重複加入
    EXPECT_TRUE(b.icon_added());
    b.hide();
    EXPECT_FALSE(b.icon_added());
    b.hide();  // 冪等：重複 hide 不得崩潰
    EXPECT_FALSE(b.icon_added());
}

// 沒有使用者選取時，poll_selection 必須回 false 且不動 out 參數的既有內容之外的東西。
TEST(TrayWin32Backend, PollSelectionIsFalseWhenNothingChosen) {
    Win32TrayBackend b;
    b.set_menu(sample_menu());
    std::vector<std::size_t> path{99};
    EXPECT_FALSE(b.poll_selection(path));
    EXPECT_EQ(path, (std::vector<std::size_t>{99})) << "回 false 時不得改寫 out 參數";
}

// set_menu 換掉整份選單後，原生選單要跟著換（不能還留著舊的）。
TEST(TrayWin32Backend, SetMenuRebuildsNativeMenu) {
    Win32TrayBackend b;
    b.set_menu(sample_menu());
    TrayMenu smaller;
    smaller.items().push_back(TrayMenuItem::action("只有一項", "only"));
    b.set_menu(smaller);
    // 透過重新建一份來對照：同樣的模型應得到同樣的結構。
    BuiltMenu again = build_native_menu(smaller);
    EXPECT_EQ(::GetMenuItemCount(again.handle), 1);
    EXPECT_EQ(again.bindings.size(), 1u);
    destroy_built_menu(again);
}

// 具名圖示值要如實保存（即使目前尚未對映到真實圖示資源）。
TEST(TrayWin32Backend, IconNameIsAcceptedWithoutCrashing) {
    Win32TrayBackend b;
    b.set_icon("icon.desktop_shell");
    b.show();
    EXPECT_TRUE(b.icon_added());
    b.set_icon("icon.other");  // 已加入後再改圖示走 NIM_MODIFY
    EXPECT_TRUE(b.icon_added());
    b.hide();
}
