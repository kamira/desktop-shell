// W1-02 win32 系統匣後端 — 實作
#include "tray_win32.hpp"

namespace ds::host {
namespace {

constexpr wchar_t kTrayWindowClass[] = L"DesktopShellTrayHost";
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

// 選單命令 id 從 1 起（TrackPopupMenu 以 0 表示「使用者沒有選」）。
constexpr UINT kFirstCommandId = 1;

// 預設匣圖示。
//
// 不能直接寫 `LoadIconW(nullptr, IDI_APPLICATION)`：`IDI_APPLICATION` 是
// `MAKEINTRESOURCE(32512)`，而 `MAKEINTRESOURCE` 在未定義 `UNICODE` 時展開成 ANSI 版本，
// 與 `LoadIconW` 的 `LPCWSTR` 對不上。明確用寬字元版的資源序號，
// 避免依賴 `UNICODE` 這個全域巨集是否恰好有定義。
HICON default_tray_icon() {
    constexpr WORD kIdiApplication = 32512;  // IDI_APPLICATION
    return ::LoadIconW(nullptr, MAKEINTRESOURCEW(kIdiApplication));
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], need);
    return out;
}

// 遞迴把 TrayMenuItem 串接進 HMENU，並記錄「命令 id → 索引路徑」。
void append_items(HMENU menu, const std::vector<TrayMenuItem>& items,
                  std::vector<std::size_t>& path_prefix, UINT& next_id,
                  BuiltMenu& built) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        const TrayMenuItem& item = items[i];
        path_prefix.push_back(i);

        if (item.kind() == TrayItemKind::Separator) {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        } else if (item.kind() == TrayItemKind::Submenu) {
            HMENU sub = ::CreatePopupMenu();
            built.submenus.push_back(sub);
            append_items(sub, item.children(), path_prefix, next_id, built);
            UINT flags = MF_STRING | MF_POPUP;
            if (!item.enabled()) flags |= MF_GRAYED;
            ::AppendMenuW(menu, flags, reinterpret_cast<UINT_PTR>(sub),
                          widen(item.label()).c_str());
        } else {
            // Action / Checkbox：可點擊，配一個命令 id 並記下路徑。
            const UINT id = next_id++;
            UINT flags = MF_STRING;
            if (!item.enabled()) flags |= MF_GRAYED;
            if (item.kind() == TrayItemKind::Checkbox && item.checked()) flags |= MF_CHECKED;
            ::AppendMenuW(menu, flags, id, widen(item.label()).c_str());
            built.bindings.push_back(TrayCommandBinding{id, path_prefix});
        }

        path_prefix.pop_back();
    }
}

}  // namespace

BuiltMenu build_native_menu(const TrayMenu& menu) {
    BuiltMenu built;
    built.handle = ::CreatePopupMenu();
    if (!built.handle) return built;
    UINT next_id = kFirstCommandId;
    std::vector<std::size_t> prefix;
    append_items(built.handle, menu.items(), prefix, next_id, built);
    return built;
}

void destroy_built_menu(BuiltMenu& built) {
    // DestroyMenu 會連同以 MF_POPUP 掛上的子選單一起銷毀，故只需毀根選單。
    if (built.handle) ::DestroyMenu(built.handle);
    built.handle = nullptr;
    built.submenus.clear();
    built.bindings.clear();
}

// --- 生命週期 ---------------------------------------------------------------

Win32TrayBackend::Win32TrayBackend() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Win32TrayBackend::wnd_proc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kTrayWindowClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

    // message-only 視窗：不顯示、不進 z-order，純粹收系統匣回呼。
    message_window_ = ::CreateWindowExW(0, kTrayWindowClass, L"", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr,
                                        ::GetModuleHandleW(nullptr), nullptr);
    if (!message_window_) return;
    ::SetWindowLongPtrW(message_window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = message_window_;
    nid_.uID = kTrayIconId;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = kTrayCallbackMessage;
    nid_.hIcon = default_tray_icon();
}

Win32TrayBackend::~Win32TrayBackend() {
    hide();
    destroy_built_menu(built_);
    if (message_window_) {
        ::SetWindowLongPtrW(message_window_, GWLP_USERDATA, 0);
        ::DestroyWindow(message_window_);
        message_window_ = nullptr;
    }
}

// --- TrayBackend 契約 --------------------------------------------------------

void Win32TrayBackend::set_icon(const std::string& icon) {
    icon_name_ = icon;
    // 具名圖示 → 實際圖示資源的對照尚未建立（需要圖示資源與 E9 套件格式）。
    // 目前一律用系統預設圖示，但**如實保存具名值**——接上資源後這裡是唯一要改的地方。
    nid_.hIcon = default_tray_icon();
    if (icon_added_) refresh_icon_data(NIM_MODIFY);
}

void Win32TrayBackend::set_tooltip(const std::string& tooltip) {
    tooltip_ = tooltip;
    const std::wstring w = widen(tooltip_);
    ::wcsncpy_s(nid_.szTip, w.c_str(), _TRUNCATE);
    if (icon_added_) refresh_icon_data(NIM_MODIFY);
}

void Win32TrayBackend::set_menu(const TrayMenu& menu) {
    // 保存模型快照（勾選狀態由 SystemTray 更新後重推），並重建原生選單以反映新狀態。
    menu_ = menu;
    destroy_built_menu(built_);
    built_ = build_native_menu(menu_);
}

void Win32TrayBackend::show() {
    if (!message_window_ || icon_added_) return;
    if (::Shell_NotifyIconW(NIM_ADD, &nid_)) icon_added_ = true;
}

void Win32TrayBackend::hide() {
    if (!icon_added_) return;
    ::Shell_NotifyIconW(NIM_DELETE, &nid_);
    icon_added_ = false;
}

void Win32TrayBackend::refresh_icon_data(DWORD action) {
    ::Shell_NotifyIconW(action, &nid_);
}

// --- 訊息處理 ---------------------------------------------------------------

LRESULT CALLBACK Win32TrayBackend::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kTrayCallbackMessage) {
        auto* self = reinterpret_cast<Win32TrayBackend*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) self->on_tray_callback(wp, lp);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void Win32TrayBackend::on_tray_callback(WPARAM, LPARAM lp) {
    const UINT event = LOWORD(lp);
    if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU || event == WM_LBUTTONUP) {
        popup_menu_at_cursor();
    }
}

void Win32TrayBackend::popup_menu_at_cursor() {
    if (!built_.handle || !message_window_) return;

    POINT pt = {};
    ::GetCursorPos(&pt);

    // 系統匣選單的標準作法：先把自己設為前景視窗，否則選單不會在點外面時關閉。
    ::SetForegroundWindow(message_window_);

    // TPM_RETURNCMD：同步回傳被選中的命令 id（0 = 使用者沒選）。
    // 這樣不必處理 WM_COMMAND，選取結果就地拿到。
    const UINT chosen = static_cast<UINT>(::TrackPopupMenu(
        built_.handle, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, 0, message_window_, nullptr));

    // 標準收尾，讓選單確實收掉。
    ::PostMessageW(message_window_, WM_NULL, 0, 0);
    if (chosen == 0) return;

    for (const auto& b : built_.bindings) {
        if (b.command_id == chosen) {
            // 只回報「使用者選了哪一項」，不自行解釋語意——
            // Checkbox 切換、命令分派、邊界情形全由 E11-01 SystemTray::click(path) 負責。
            pending_selection_ = b.path;
            has_pending_ = true;
            return;
        }
    }
}

bool Win32TrayBackend::poll_selection(std::vector<std::size_t>& out_path) {
    if (!has_pending_) return false;
    out_path = pending_selection_;
    has_pending_ = false;
    pending_selection_.clear();
    return true;
}

}  // namespace ds::host
