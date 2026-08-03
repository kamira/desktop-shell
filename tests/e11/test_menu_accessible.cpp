// W1-06 自繪選單無障礙（MSAA）— gtest
//
// 關鍵：**不需要啟動 GUI 應用**。`OwnerDrawnMenu` 在建構時就建好視窗，
// 測試可以直接以 `AccessibleObjectFromWindow` 走真實的 `WM_GETOBJECT` 路徑查詢——
// 也就是螢幕閱讀器走的同一條路，不是繞過它去戳內部狀態。
//
// 這一點很重要：如果只測 `MenuAccessible` 這個類別本身，就驗不到
// 「視窗有沒有真的回應 WM_GETOBJECT」——而那正是無障礙會安靜失效的地方。
#include <gtest/gtest.h>

#include <oleacc.h>
#include <objbase.h>
#include <ole2.h>

#include <iomanip>
#include <string>
#include <vector>

#include "menu_accessible.hpp"
#include "owner_drawn_menu.hpp"

using ds::host::AccessibleRow;
using ds::host::build_accessible_rows;
using ds::host::msaa_role_for;
using ds::host::msaa_state_for;
using ds::host::OwnerDrawnMenu;
using ds::host::TrayMenu;
using ds::host::TrayMenuItem;

namespace {

TrayMenu sample_menu() {
    TrayMenu m;
    m.items().push_back(TrayMenuItem::checkbox("最上層顯示", "widget.toggle_topmost", true));
    m.items().push_back(TrayMenuItem::checkbox("點擊穿透", "widget.toggle_passthrough", false));
    m.items().push_back(TrayMenuItem::separator());
    m.items().push_back(TrayMenuItem::action("結束", "app.quit"));
    return m;
}

VARIANT self_id() {
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = CHILDID_SELF;
    return v;
}

VARIANT child_id(long n) {
    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = n;
    return v;
}

std::string narrow(BSTR b) {
    if (!b) return std::string();
    const int len = ::SysStringLen(b);
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, b, len, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, b, len, &out[0], need, nullptr, nullptr);
    return out;
}

// 走真實的 WM_GETOBJECT 路徑取得 IAccessible——與螢幕閱讀器相同。
// 失敗時把 HRESULT 印出來：無障礙失敗的成因（COM 未初始化 / 訊息沒接 / 介面不符）
// 從外面看完全一樣，不印就只能猜。
IAccessible* acc_from(const OwnerDrawnMenu& menu) {
    IAccessible* acc = nullptr;
    const HRESULT hr = ::AccessibleObjectFromWindow(
        menu.native_window(), static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
        reinterpret_cast<void**>(&acc));
    if (FAILED(hr)) {
        ADD_FAILURE() << "AccessibleObjectFromWindow 失敗 hr=0x" << std::hex << hr;
        return nullptr;
    }
    return acc;
}

// MSAA 的 LresultFromObject / AccessibleObjectFromWindow 都要求行程已初始化 OLE。
// 測試行程與真實 app（host_main）都必須做——漏掉的話無障礙**安靜失效**。
class ComEnvironment : public ::testing::Environment {
public:
    void SetUp() override { ::OleInitialize(nullptr); }
    void TearDown() override { ::OleUninitialize(); }
};

const auto* kComEnv = ::testing::AddGlobalTestEnvironment(new ComEnvironment());

}  // namespace

// --- 純對應（不需要 COM）-----------------------------------------------------

TEST(MenuAccessibleMapping, SeparatorIsSeparatorRoleAndNotFocusable) {
    AccessibleRow sep;
    sep.is_separator = true;
    EXPECT_EQ(msaa_role_for(sep), ROLE_SYSTEM_SEPARATOR);
    // 分隔線標為不可用，螢幕閱讀器據此跳過——否則使用者會聽到一個沒有名字的項目。
    EXPECT_TRUE(msaa_state_for(sep) & STATE_SYSTEM_UNAVAILABLE);
}

TEST(MenuAccessibleMapping, NormalItemIsMenuItemRole) {
    AccessibleRow row;
    EXPECT_EQ(msaa_role_for(row), ROLE_SYSTEM_MENUITEM);
    EXPECT_EQ(msaa_state_for(row), 0) << "預設項目不該帶任何特殊狀態";
}

TEST(MenuAccessibleMapping, CheckedDisabledFocusedMapToStates) {
    AccessibleRow checked;
    checked.checked = true;
    EXPECT_TRUE(msaa_state_for(checked) & STATE_SYSTEM_CHECKED);

    AccessibleRow disabled;
    disabled.enabled = false;
    EXPECT_TRUE(msaa_state_for(disabled) & STATE_SYSTEM_UNAVAILABLE);

    AccessibleRow focused;
    focused.focused = true;
    EXPECT_TRUE(msaa_state_for(focused) & STATE_SYSTEM_FOCUSED);
}

// --- 透過真實 WM_GETOBJECT 查詢 ----------------------------------------------

// 最重要的一條：視窗必須真的回應 WM_GETOBJECT。
// 這是無障礙最容易安靜失效的地方——類別寫得再好，訊息沒接就等於沒有。
TEST(MenuAccessibleWindow, WindowRespondsToGetObject) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.valid());
    ASSERT_TRUE(menu.set_menu(sample_menu()));

    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr) << "視窗沒有回應 WM_GETOBJECT，螢幕閱讀器讀不到任何東西";
    acc->Release();
}

TEST(MenuAccessibleWindow, ReportsMenuPopupRoleAndChildCount) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    VARIANT role;
    ::VariantInit(&role);
    ASSERT_EQ(acc->get_accRole(self_id(), &role), S_OK);
    EXPECT_EQ(role.lVal, ROLE_SYSTEM_MENUPOPUP);

    long count = 0;
    ASSERT_EQ(acc->get_accChildCount(&count), S_OK);
    EXPECT_EQ(count, 4) << "四個選單項應對應四個無障礙子項";
    acc->Release();
}

// 螢幕閱讀器朗讀的就是這些名稱。
TEST(MenuAccessibleWindow, ExposesItemLabelsAsNames) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    BSTR name = nullptr;
    ASSERT_EQ(acc->get_accName(child_id(1), &name), S_OK);
    EXPECT_EQ(narrow(name), "最上層顯示");
    ::SysFreeString(name);

    name = nullptr;
    ASSERT_EQ(acc->get_accName(child_id(4), &name), S_OK);
    EXPECT_EQ(narrow(name), "結束");
    ::SysFreeString(name);
    acc->Release();
}

// 分隔線不得有名稱——否則螢幕閱讀器會唸出無意義內容。
TEST(MenuAccessibleWindow, SeparatorHasNoName) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    BSTR name = nullptr;
    ASSERT_EQ(acc->get_accName(child_id(3), &name), S_OK);  // 第 3 項是分隔線
    EXPECT_TRUE(narrow(name).empty());
    ::SysFreeString(name);

    VARIANT role;
    ::VariantInit(&role);
    ASSERT_EQ(acc->get_accRole(child_id(3), &role), S_OK);
    EXPECT_EQ(role.lVal, ROLE_SYSTEM_SEPARATOR);
    acc->Release();
}

// 勾選狀態要傳達出去——否則使用者不知道「最上層顯示」是開還是關。
TEST(MenuAccessibleWindow, CheckedStateIsExposed) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    VARIANT s1;
    ::VariantInit(&s1);
    ASSERT_EQ(acc->get_accState(child_id(1), &s1), S_OK);
    EXPECT_TRUE(s1.lVal & STATE_SYSTEM_CHECKED) << "第 1 項是已勾選的 checkbox";

    VARIANT s2;
    ::VariantInit(&s2);
    ASSERT_EQ(acc->get_accState(child_id(2), &s2), S_OK);
    EXPECT_FALSE(s2.lVal & STATE_SYSTEM_CHECKED) << "第 2 項未勾選";
    acc->Release();
}

// 越界 childId 要結構化拒絕，不得崩潰或回傳垃圾。
TEST(MenuAccessibleWindow, InvalidChildIdIsRejected) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    BSTR name = nullptr;
    EXPECT_EQ(acc->get_accName(child_id(99), &name), E_INVALIDARG);
    EXPECT_EQ(name, nullptr);

    // 注意：`CHILDID_SELF` 就是 0，所以 childId 0 指的是**視窗本身**，不是無效值。
    // 用 0 當「無效」案例是錯的（初版如此寫，測試因此紅）。真正的無效是超出範圍。
    VARIANT role;
    ::VariantInit(&role);
    EXPECT_EQ(acc->get_accRole(child_id(0), &role), S_OK);
    EXPECT_EQ(role.lVal, ROLE_SYSTEM_MENUPOPUP) << "childId 0 = CHILDID_SELF";

    VARIANT bad;
    ::VariantInit(&bad);
    EXPECT_EQ(acc->get_accRole(child_id(-5), &bad), E_INVALIDARG);
    EXPECT_EQ(acc->get_accRole(child_id(999), &bad), E_INVALIDARG);
    acc->Release();
}

// 尚未巡覽時不得回報焦點——否則螢幕閱讀器會宣告一個使用者沒有選的項目。
TEST(MenuAccessibleWindow, NoFocusBeforeNavigation) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    VARIANT focus;
    ::VariantInit(&focus);
    EXPECT_EQ(acc->get_accFocus(&focus), S_FALSE);
    acc->Release();
}

// 可致動項要有預設動作名稱；分隔線不得有。
TEST(MenuAccessibleWindow, DefaultActionOnlyForActionableRows) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);

    BSTR action = nullptr;
    EXPECT_EQ(acc->get_accDefaultAction(child_id(4), &action), S_OK);
    EXPECT_FALSE(narrow(action).empty());
    ::SysFreeString(action);

    action = nullptr;
    EXPECT_EQ(acc->get_accDefaultAction(child_id(3), &action), S_FALSE) << "分隔線無動作";
    acc->Release();
}

// 空選單不得崩潰，且子項數為 0。
TEST(MenuAccessibleWindow, EmptyMenuReportsNoChildren) {
    OwnerDrawnMenu menu;
    TrayMenu empty;
    menu.set_menu(empty);
    IAccessible* acc = acc_from(menu);
    ASSERT_NE(acc, nullptr);
    long count = -1;
    ASSERT_EQ(acc->get_accChildCount(&count), S_OK);
    EXPECT_EQ(count, 0);
    acc->Release();
}

// --- 快照建構 ---------------------------------------------------------------

// 螢幕座標要以面板原點為基準累加——accLocation / accHitTest 依賴它。
TEST(AccessibleRows, ScreenRectsStackFromPanelOrigin) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    const auto m = menu.render_model();
    ASSERT_FALSE(m.panels.empty());

    const auto rows = build_accessible_rows(menu.model(), m.panels.front(), POINT{100, 200});
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].screen_rect.left, 100);
    EXPECT_EQ(rows[0].screen_rect.top, 200);
    EXPECT_GT(rows[1].screen_rect.top, rows[0].screen_rect.top) << "第二列應在第一列下方";
    EXPECT_EQ(rows[1].screen_rect.left, 100);
}
