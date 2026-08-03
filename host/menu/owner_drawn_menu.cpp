// W1-05 win32 自繪選單呈現 — 實作
#include "owner_drawn_menu.hpp"

#include <algorithm>
#include <string>

#include "document.hpp"  // E7-01：Value

namespace ds::host {
namespace {

constexpr wchar_t kMenuWindowClass[] = L"DesktopShellOwnerDrawnMenu";
constexpr int kPaddingX = 12;
constexpr int kCheckColumn = 20;
constexpr int kMinWidth = 150;

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                           static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], need);
    return out;
}

const char* kind_name(TrayItemKind k) {
    switch (k) {
        case TrayItemKind::Separator: return "separator";
        case TrayItemKind::Checkbox:  return "checkbox";
        case TrayItemKind::Submenu:   return "submenu";
        default:                      return "action";
    }
}

// 遞迴轉換一層。`prefix` 用來為無命令 id 的項目（分隔線等）合成穩定唯一 id——
// E11-02 要求 id 唯一，而 E11-01 允許空命令 id。
void convert_items(const std::vector<TrayMenuItem>& src, const std::string& prefix,
                   std::vector<ds::format::Item>& out) {
    using ds::format::Value;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const TrayMenuItem& it = src[i];
        std::string id = it.command_id();
        if (id.empty()) id = prefix + "item." + std::to_string(i);

        // `checked` **只有 checkbox 項可用**（E11-02 檔頭明載）；對其他種類帶上會被
        // build_menu 判為錯誤而整份選單建不起來。初版對每一項都塞，結果 set_menu 回 false。
        std::vector<Value::Member> members = {
            {menu_item_keys::kKind, Value::string(kind_name(it.kind()))},
            {menu_item_keys::kEnabled, Value::boolean(it.enabled())},
        };
        if (it.kind() == TrayItemKind::Checkbox) {
            members.push_back({menu_item_keys::kChecked, Value::boolean(it.checked())});
        }
        Value payload = Value::map(std::move(members));

        ds::format::Item item{id, it.label(), std::move(payload)};
        if (!it.children().empty()) {
            // Item::children() 只有 const 版；以 add_child 逐一加入。
            std::vector<ds::format::Item> kids;
            convert_items(it.children(), id + ".", kids);
            for (auto& kid : kids) item.add_child(std::move(kid));
        }
        out.push_back(std::move(item));
    }
}

void fill_rect(HDC hdc, const RECT& r, COLORREF c) {
    HBRUSH b = ::CreateSolidBrush(c);
    ::FillRect(hdc, &r, b);
    ::DeleteObject(b);
}

}  // namespace

std::vector<ds::format::Item> tray_menu_to_items(const TrayMenu& menu) {
    std::vector<ds::format::Item> out;
    convert_items(menu.items(), "menu.", out);
    return out;
}

const MenuNode* node_at(const MenuModel& model, const std::vector<std::size_t>& path) {
    if (path.empty()) return nullptr;
    const std::vector<MenuNode>* level = &model.items();
    const MenuNode* node = nullptr;
    for (const std::size_t idx : path) {
        if (level == nullptr || idx >= level->size()) return nullptr;
        node = &(*level)[idx];
        level = &node->children();
    }
    return node;
}

MenuTheme default_menu_theme() {
    MenuTheme t;
    t.background = RGB(30, 33, 40);
    t.text = RGB(228, 232, 240);
    t.text_disabled = RGB(112, 118, 130);
    t.highlight = RGB(58, 110, 165);
    t.highlight_text = RGB(255, 255, 255);
    t.separator = RGB(62, 68, 80);
    t.check_mark = RGB(120, 190, 255);
    return t;
}

POINT place_menu_popup(POINT cursor, int panel_width, int panel_height,
                       const RECT& work_area) {
    POINT p = cursor;
    // 邊緣翻轉：放不下就往反方向長。原生選單免費提供這個行為，自繪必須自己做，
    // 否則選單會有一半落在畫面外——而且使用者只會覺得「選單壞了」。
    if (p.x + panel_width > work_area.right) p.x = cursor.x - panel_width;
    if (p.y + panel_height > work_area.bottom) p.y = cursor.y - panel_height;
    // 翻轉後仍越界（面板比工作區還大）則夾到工作區內，寧可貼邊也不要跑出畫面。
    p.x = std::max(static_cast<LONG>(work_area.left), std::min(p.x, work_area.right - panel_width));
    p.y = std::max(static_cast<LONG>(work_area.top), std::min(p.y, work_area.bottom - panel_height));
    return p;
}

void paint_menu_panel(HDC hdc, const RECT& bounds, const MenuPanelRender& panel,
                      const MenuModel& model, const MenuTheme& theme) {
    fill_rect(hdc, bounds, theme.background);
    ::SetBkMode(hdc, TRANSPARENT);

    for (const MenuRowRender& row : panel.rows) {
        RECT r = {bounds.left, bounds.top + static_cast<LONG>(row.y),
                  bounds.right, bounds.top + static_cast<LONG>(row.y + row.row_height)};

        if (row.kind == MenuNodeKind::Separator) {
            const LONG mid = (r.top + r.bottom) / 2;
            RECT line = {r.left + kPaddingX, mid, r.right - kPaddingX, mid + 1};
            fill_rect(hdc, line, theme.separator);
            continue;
        }

        COLORREF text_color = row.enabled ? theme.text : theme.text_disabled;
        if (row.selected && row.enabled) {
            fill_rect(hdc, r, theme.highlight);
            text_color = theme.highlight_text;
        }

        // 勾選標記畫在左側固定欄，讓有勾與沒勾的項目文字對齊。
        if (row.checked) {
            ::SetTextColor(hdc, row.selected ? theme.highlight_text : theme.check_mark);
            RECT c = {r.left + kPaddingX, r.top, r.left + kPaddingX + kCheckColumn, r.bottom};
            ::DrawTextW(hdc, L"✓", 1, &c, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        const MenuNode* node = node_at(model, row.path);
        const std::wstring label = widen(node ? node->label() : std::string());
        ::SetTextColor(hdc, text_color);
        RECT t = {r.left + kPaddingX + kCheckColumn, r.top, r.right - kPaddingX, r.bottom};
        ::DrawTextW(hdc, label.c_str(), static_cast<int>(label.size()), &t,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        // 子選單以右側箭頭示意（本輪不展開子面板，見檔頭限制）。
        if (row.has_submenu) {
            RECT a = {r.right - kPaddingX - 12, r.top, r.right - kPaddingX, r.bottom};
            ::DrawTextW(hdc, L"›", 1, &a, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
    }
}

// --- OwnerDrawnMenu ----------------------------------------------------------

OwnerDrawnMenu::OwnerDrawnMenu() : renderer_(metrics_, "surface.menu.root") {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &OwnerDrawnMenu::wnd_proc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kMenuWindowClass;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

    window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMenuWindowClass, L"",
                                WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                                ::GetModuleHandleW(nullptr), nullptr);
    if (!window_) return;
    ::SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    font_ = ::CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH, L"Segoe UI");
}

OwnerDrawnMenu::~OwnerDrawnMenu() {
    if (font_) ::DeleteObject(font_);
    if (window_) {
        ::SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        ::DestroyWindow(window_);
    }
}

bool OwnerDrawnMenu::set_menu(const TrayMenu& menu) {
    return renderer_.set_menu(tray_menu_to_items(menu)).ok();
}

bool OwnerDrawnMenu::row_at(int y, std::vector<std::size_t>& out_path) const {
    const MenuRenderModel m = renderer_.render_model();
    if (m.panels.empty()) return false;
    for (const MenuRowRender& row : m.panels.front().rows) {
        if (y >= static_cast<int>(row.y) && y < static_cast<int>(row.y + row.row_height)) {
            out_path = row.path;
            return true;
        }
    }
    return false;
}

void OwnerDrawnMenu::paint() {
    if (!window_) return;
    const MenuRenderModel m = renderer_.render_model();
    if (m.panels.empty()) return;

    RECT rc = {};
    ::GetClientRect(window_, &rc);
    HDC screen = ::GetDC(window_);
    if (!screen) return;
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, rc.right, rc.bottom);
    HGDIOBJ old_bmp = ::SelectObject(mem, bmp);
    HGDIOBJ old_font = ::SelectObject(mem, font_);

    paint_menu_panel(mem, rc, m.panels.front(), renderer_.model(), default_menu_theme());

    ::BitBlt(screen, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    ::SelectObject(mem, old_font);
    ::SelectObject(mem, old_bmp);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(window_, screen);
}

LRESULT CALLBACK OwnerDrawnMenu::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<OwnerDrawnMenu*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            ::BeginPaint(hwnd, &ps);
            ::EndPaint(hwnd, &ps);
            self->paint();
            return 0;
        }
        case WM_MOUSEMOVE: {
            std::vector<std::size_t> path;
            if (self->row_at(GET_Y_LPARAM(lp), path)) {
                // hover 失敗（分隔線 / 停用項）不改游標——由 E11-02 決定，這裡不自行判斷。
                if (self->renderer_.hover(path).ok()) ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            std::vector<std::size_t> path;
            if (self->row_at(GET_Y_LPARAM(lp), path)) {
                const MenuNavResult r = self->renderer_.select(path);
                if (r.status == MenuNavStatus::Selected) {
                    self->chosen_ = r.path;
                    self->accepted_ = true;
                    self->done_ = true;
                }
                // 其餘（分隔線 / 停用 / 子選單）維持選單開啟，不做任何事。
            }
            return 0;
        }
        case WM_KEYDOWN: {
            switch (wp) {
                case VK_ESCAPE: self->done_ = true; return 0;
                case VK_DOWN:   self->renderer_.move_next(); ::InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_UP:     self->renderer_.move_prev(); ::InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_RETURN: {
                    const MenuNavResult r = self->renderer_.activate();
                    if (r.status == MenuNavStatus::Selected) {
                        self->chosen_ = r.path;
                        self->accepted_ = true;
                        self->done_ = true;
                    }
                    return 0;
                }
                default: break;
            }
            return 0;
        }
        case WM_KILLFOCUS:
            self->done_ = true;  // 點到選單外面 → 取消
            return 0;
        default: break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

bool OwnerDrawnMenu::popup_at(POINT cursor, std::vector<std::size_t>& out_path) {
    if (!window_) return false;
    const MenuRenderModel m = renderer_.render_model();
    if (m.panels.empty()) return false;

    const int w = std::max(kMinWidth, static_cast<int>(m.panels.front().size.width) + 2 * kPaddingX + kCheckColumn);
    const int h = static_cast<int>(m.panels.front().size.height);

    RECT work = {};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const POINT at = place_menu_popup(cursor, w, h, work);

    done_ = false;
    accepted_ = false;
    chosen_.clear();

    ::SetWindowPos(window_, HWND_TOPMOST, at.x, at.y, w, h, SWP_SHOWWINDOW);
    ::SetForegroundWindow(window_);
    ::SetFocus(window_);
    ::InvalidateRect(window_, nullptr, TRUE);

    // 小型 modal 迴圈：跑到使用者選了東西或取消為止。
    MSG msg;
    while (!done_ && ::GetMessageW(&msg, nullptr, 0, 0)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    ::ShowWindow(window_, SW_HIDE);

    if (!accepted_) return false;
    out_path = chosen_;
    return true;
}

}  // namespace ds::host
