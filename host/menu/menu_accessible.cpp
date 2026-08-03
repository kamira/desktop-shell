// W1-06 自繪選單的無障礙支援（MSAA / IAccessible）— 實作
#include "menu_accessible.hpp"

#include <new>

namespace ds::host {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], need);
    return out;
}

BSTR to_bstr(const std::wstring& s) {
    return ::SysAllocStringLen(s.c_str(), static_cast<UINT>(s.size()));
}

}  // namespace

std::vector<AccessibleRow> build_accessible_rows(const MenuModel& model,
                                                 const MenuPanelRender& panel,
                                                 POINT panel_origin) {
    std::vector<AccessibleRow> rows;
    rows.reserve(panel.rows.size());
    for (const MenuRowRender& r : panel.rows) {
        AccessibleRow a;
        a.is_separator = (r.kind == MenuNodeKind::Separator);
        a.enabled = r.enabled;
        a.checked = r.checked;
        a.focused = r.selected;
        a.has_submenu = r.has_submenu;

        const MenuNode* node = node_at(model, r.path);
        // 分隔線沒有可朗讀的名稱；給空字串而非標籤，避免螢幕閱讀器唸出無意義內容。
        a.name = a.is_separator ? std::wstring() : widen(node ? node->label() : std::string());

        a.screen_rect.left = panel_origin.x;
        a.screen_rect.top = panel_origin.y + static_cast<LONG>(r.y);
        a.screen_rect.right = panel_origin.x + static_cast<LONG>(panel.size.width);
        a.screen_rect.bottom = a.screen_rect.top + static_cast<LONG>(r.row_height);
        rows.push_back(std::move(a));
    }
    return rows;
}

LONG msaa_role_for(const AccessibleRow& row) {
    return row.is_separator ? ROLE_SYSTEM_SEPARATOR : ROLE_SYSTEM_MENUITEM;
}

LONG msaa_state_for(const AccessibleRow& row) {
    LONG state = 0;
    if (!row.enabled) state |= STATE_SYSTEM_UNAVAILABLE;
    if (row.checked) state |= STATE_SYSTEM_CHECKED;
    if (row.focused) state |= STATE_SYSTEM_FOCUSED | STATE_SYSTEM_SELECTED;
    // 分隔線不可聚焦——螢幕閱讀器據此跳過它。
    if (row.is_separator) state |= STATE_SYSTEM_UNAVAILABLE;
    return state;
}

// --- MenuAccessible ----------------------------------------------------------

MenuAccessible::MenuAccessible(HWND window) : window_(window) {}

void MenuAccessible::set_rows(std::vector<AccessibleRow> rows) { rows_ = std::move(rows); }

int MenuAccessible::row_index(const VARIANT& child) const {
    if (child.vt != VT_I4) return -2;
    if (child.lVal == CHILDID_SELF) return -1;
    const long idx = child.lVal - 1;  // MSAA 子項編號自 1 起
    if (idx < 0 || static_cast<std::size_t>(idx) >= rows_.size()) return -2;
    return static_cast<int>(idx);
}

HRESULT STDMETHODCALLTYPE MenuAccessible::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
        *ppv = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE MenuAccessible::AddRef() {
    return static_cast<ULONG>(::InterlockedIncrement(&ref_));
}

ULONG STDMETHODCALLTYPE MenuAccessible::Release() {
    const LONG n = ::InterlockedDecrement(&ref_);
    if (n == 0) delete this;
    return static_cast<ULONG>(n);
}

HRESULT STDMETHODCALLTYPE MenuAccessible::GetTypeInfoCount(UINT* n) {
    if (n) *n = 0;
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE MenuAccessible::GetTypeInfo(UINT, LCID, ITypeInfo**) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE MenuAccessible::GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE MenuAccessible::Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*,
                                                 VARIANT*, EXCEPINFO*, UINT*) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accParent(IDispatch** parent) {
    if (!parent) return E_POINTER;
    *parent = nullptr;
    // 父物件交給系統的視窗預設實作（桌面 / 擁有者視窗）。
    return ::AccessibleObjectFromWindow(window_, OBJID_WINDOW, IID_IDispatch,
                                        reinterpret_cast<void**>(parent));
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accChildCount(long* count) {
    if (!count) return E_POINTER;
    *count = static_cast<long>(rows_.size());
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accChild(VARIANT, IDispatch** child) {
    if (!child) return E_POINTER;
    *child = nullptr;
    // 每一列都是「簡單子項」（由本物件以 childId 代理），沒有獨立的 IDispatch。
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accName(VARIANT child, BSTR* name) {
    if (!name) return E_POINTER;
    *name = nullptr;
    const int i = row_index(child);
    if (i == -2) return E_INVALIDARG;
    if (i == -1) {
        *name = to_bstr(L"desktop-shell 選單");
        return S_OK;
    }
    *name = to_bstr(rows_[static_cast<std::size_t>(i)].name);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accValue(VARIANT, BSTR* v) {
    if (v) *v = nullptr;
    return S_FALSE;  // 選單項沒有「值」
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accDescription(VARIANT, BSTR* d) {
    if (d) *d = nullptr;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accRole(VARIANT child, VARIANT* role) {
    if (!role) return E_POINTER;
    ::VariantInit(role);
    const int i = row_index(child);
    if (i == -2) return E_INVALIDARG;
    role->vt = VT_I4;
    role->lVal = (i == -1) ? ROLE_SYSTEM_MENUPOPUP
                           : msaa_role_for(rows_[static_cast<std::size_t>(i)]);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accState(VARIANT child, VARIANT* state) {
    if (!state) return E_POINTER;
    ::VariantInit(state);
    const int i = row_index(child);
    if (i == -2) return E_INVALIDARG;
    state->vt = VT_I4;
    state->lVal = (i == -1) ? STATE_SYSTEM_FOCUSABLE
                            : msaa_state_for(rows_[static_cast<std::size_t>(i)]);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accHelp(VARIANT, BSTR* h) {
    if (h) *h = nullptr;
    return S_FALSE;
}
HRESULT STDMETHODCALLTYPE MenuAccessible::get_accHelpTopic(BSTR*, VARIANT, long*) {
    return E_NOTIMPL;
}
HRESULT STDMETHODCALLTYPE MenuAccessible::get_accKeyboardShortcut(VARIANT, BSTR* s) {
    if (s) *s = nullptr;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accFocus(VARIANT* child) {
    if (!child) return E_POINTER;
    ::VariantInit(child);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].focused) {
            child->vt = VT_I4;
            child->lVal = static_cast<long>(i) + 1;
            return S_OK;
        }
    }
    return S_FALSE;  // 尚無巡覽游標
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accSelection(VARIANT* sel) {
    if (sel) ::VariantInit(sel);
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::get_accDefaultAction(VARIANT child, BSTR* action) {
    if (!action) return E_POINTER;
    *action = nullptr;
    const int i = row_index(child);
    if (i == -2) return E_INVALIDARG;
    if (i == -1) return S_FALSE;
    const AccessibleRow& r = rows_[static_cast<std::size_t>(i)];
    if (r.is_separator || !r.enabled) return S_FALSE;
    *action = to_bstr(r.has_submenu ? L"開啟" : L"執行");
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::accSelect(long, VARIANT) { return E_NOTIMPL; }

HRESULT STDMETHODCALLTYPE MenuAccessible::accLocation(long* l, long* t, long* w, long* h,
                                                      VARIANT child) {
    if (!l || !t || !w || !h) return E_POINTER;
    const int i = row_index(child);
    if (i == -2) return E_INVALIDARG;
    RECT r = {};
    if (i == -1) {
        ::GetWindowRect(window_, &r);
    } else {
        r = rows_[static_cast<std::size_t>(i)].screen_rect;
    }
    *l = r.left;
    *t = r.top;
    *w = r.right - r.left;
    *h = r.bottom - r.top;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::accNavigate(long, VARIANT, VARIANT* end) {
    if (end) ::VariantInit(end);
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::accHitTest(long x, long y, VARIANT* child) {
    if (!child) return E_POINTER;
    ::VariantInit(child);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const RECT& r = rows_[i].screen_rect;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            child->vt = VT_I4;
            child->lVal = static_cast<long>(i) + 1;
            return S_OK;
        }
    }
    child->vt = VT_I4;
    child->lVal = CHILDID_SELF;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MenuAccessible::accDoDefaultAction(VARIANT) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE MenuAccessible::put_accName(VARIANT, BSTR) { return E_NOTIMPL; }
HRESULT STDMETHODCALLTYPE MenuAccessible::put_accValue(VARIANT, BSTR) { return E_NOTIMPL; }

}  // namespace ds::host
