// W1-06 自繪選單的無障礙支援（MSAA / IAccessible）
//
// **這是在補 W1-05 造成的功能退步。** 原生 `HMENU` 免費提供螢幕閱讀器支援；
// 換成自繪之後那個支援就沒了，`CHG-20260803-15` 已明載為真實退步。本單元補回來。
//
// 做法：選單視窗回應 `WM_GETOBJECT`，交出一個手寫的 `IAccessible`，
// 把 E11-02 的選單模型翻譯成 MSAA 的物件樹：
//
//   視窗本身      → ROLE_SYSTEM_MENUPOPUP
//   每個選單項     → ROLE_SYSTEM_MENUITEM（分隔線為 ROLE_SYSTEM_SEPARATOR）
//   名稱          → 選單項標籤
//   狀態          → STATE_SYSTEM_CHECKED / _UNAVAILABLE / _FOCUSED
//
// 巡覽游標移動時另發 `NotifyWinEvent(EVENT_OBJECT_FOCUS)`，
// 否則螢幕閱讀器只有在使用者主動查詢時才知道選到哪一項——那等於沒有朗讀。
//
// 為什麼選 MSAA 而非 UI Automation：MSAA 的 `IAccessible` 是單一介面、可手寫、
// 且 UIA 會透過內建的 MSAA-to-UIA 橋接自動看得到它。以本單元的規模，
// 直接實作 UIA 供應器（多個介面 + 模式）不會換到更好的結果。
#ifndef DS_HOST_MENU_MENU_ACCESSIBLE_HPP
#define DS_HOST_MENU_MENU_ACCESSIBLE_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleacc.h>

#include <cstddef>
#include <string>
#include <vector>

#include "menu_renderer.hpp"  // E11-02（上游）：MenuModel / MenuRenderModel

namespace ds::host {

// 一列選單項的無障礙快照。刻意是**純資料**：取自 E11-02 的渲染描述後就與它脫鉤，
// 因為 IAccessible 可能在任何時間被輔助工具查詢，不該回頭碰可變的 renderer 狀態。
struct AccessibleRow {
    std::wstring name;
    bool is_separator = false;
    bool enabled = true;
    bool checked = false;
    bool focused = false;
    bool has_submenu = false;
    RECT screen_rect = {};  // 螢幕座標，供 accLocation / accHitTest
};

// 由 E11-02 的模型與渲染描述建出無障礙列快照。
// `panel_origin` 為面板左上角的螢幕座標（渲染描述是相對佈局，NFR-02）。
std::vector<AccessibleRow> build_accessible_rows(const MenuModel& model,
                                                 const MenuPanelRender& panel,
                                                 POINT panel_origin);

// 依索引路徑取節點；路徑無效回 nullptr。
// （定義在 owner_drawn_menu.cpp；此處宣告以避免本檔反過來 include 它而形成循環。）
const MenuNode* node_at(const MenuModel& model, const std::vector<std::size_t>& path);

// MSAA 角色 / 狀態的對應（抽出來才能單獨測，不必先有 COM 物件）。
LONG msaa_role_for(const AccessibleRow& row);
LONG msaa_state_for(const AccessibleRow& row);

// 選單視窗的 IAccessible 實作。以引用計數自持；由 `WM_GETOBJECT` 交給輔助工具。
class MenuAccessible final : public IAccessible {
public:
    explicit MenuAccessible(HWND window);

    // 更新目前列快照（選單內容或巡覽游標變動時呼叫）。
    void set_rows(std::vector<AccessibleRow> rows);
    // 目前有幾列（供測試與診斷）。
    std::size_t row_count() const noexcept { return rows_.size(); }

    // --- IUnknown ---
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // --- IDispatch（IAccessible 繼承自它；輔助工具不使用，回 E_NOTIMPL）---
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                                     EXCEPINFO*, UINT*) override;

    // --- IAccessible（只實作螢幕閱讀器實際會用到的）---
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch**) override;
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override;
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch**) override;
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) override;
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT, BSTR*) override;
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT, BSTR*) override;
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) override;
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* state) override;
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR*) override;
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR*, VARIANT, long*) override;
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT, BSTR*) override;
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* child) override;
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT*) override;
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* action) override;
    HRESULT STDMETHODCALLTYPE accSelect(long, VARIANT) override;
    HRESULT STDMETHODCALLTYPE accLocation(long* l, long* t, long* w, long* h,
                                          VARIANT child) override;
    HRESULT STDMETHODCALLTYPE accNavigate(long, VARIANT, VARIANT*) override;
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* child) override;
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT) override;
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override;
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override;

private:
    ~MenuAccessible() = default;  // 只能經 Release 銷毀
    // child VARIANT → 列索引。CHILDID_SELF 回 -1；無效回 -2。
    int row_index(const VARIANT& child) const;

    LONG ref_ = 1;
    HWND window_ = nullptr;
    std::vector<AccessibleRow> rows_;
};

}  // namespace ds::host

#endif  // DS_HOST_MENU_MENU_ACCESSIBLE_HPP
