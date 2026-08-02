// W1-01 win32 kernel 後端 — 實作
#include "win32_backend.hpp"

#include <algorithm>

namespace ds::kernel {
namespace {

constexpr wchar_t kWindowClass[] = L"DesktopShellSurface";

// surface 的預設幾何。**刻意寫死在平台實作內部**：契約不得出現絕對座標（NFR-02），
// 但 CreateWindowEx 非要不可。之後由 E7 宣告式設定驅動時，也是經 host 呼叫平台專屬
// 擴充來調整，契約仍然不會長出 set_position(x, y)。
constexpr int kDefaultWidth = 320;
constexpr int kDefaultHeight = 132;
constexpr int kDefaultMargin = 24;

// 把具名輸入事件型別從 win32 訊息轉出來；非輸入訊息回 false。
bool input_type_for(UINT msg, InputEventType& out) {
    switch (msg) {
        case WM_MOUSEMOVE:   out = InputEventType::PointerMove; return true;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: out = InputEventType::PointerDown; return true;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:   out = InputEventType::PointerUp;   return true;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:  out = InputEventType::Key;         return true;
        default: return false;
    }
}

}  // namespace

Win32KernelBackend::Win32KernelBackend(CapabilityMatrix caps) : caps_(std::move(caps)) {}

Win32KernelBackend::~Win32KernelBackend() { shutdown(); }

// --- 生命週期 ---------------------------------------------------------------

bool Win32KernelBackend::init() {
    if (initialized_) return true;  // 冪等（契約要求）
    if (!ensure_window_class()) return false;
    initialized_ = true;
    quit_requested_ = false;
    return true;
}

void Win32KernelBackend::shutdown() {
    // 冪等：未初始化亦可安全呼叫、可重複呼叫（契約要求）。
    for (auto& kv : surfaces_) {
        if (kv.second.hwnd) {
            ::SetWindowLongPtrW(kv.second.hwnd, GWLP_USERDATA, 0);
            ::DestroyWindow(kv.second.hwnd);
        }
    }
    surfaces_.clear();
    pending_.clear();
    initialized_ = false;
}

bool Win32KernelBackend::ensure_window_class() {
    if (class_registered_) return true;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Win32KernelBackend::wnd_proc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&wc)) {
        // 已註冊（同行程重複建後端）不算失敗。
        if (::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    class_registered_ = true;
    return true;
}

// --- 訊息回呼 ---------------------------------------------------------------

LRESULT CALLBACK Win32KernelBackend::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Win32KernelBackend*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self) {
        InputEventType type{};
        if (input_type_for(msg, type)) {
            InputEvent ev;
            ev.type = type;
            ev.target = self->id_for_hwnd(hwnd);  // 事件標回**具名**目標，不用數字 handle
            self->pending_.push_back(ev);
        } else if (msg == WM_CLOSE) {
            self->quit_requested_ = true;
            return 0;  // 不讓 DefWindowProc 直接銷毀；由 host 決定何時收攤
        }
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// --- K1 surface -------------------------------------------------------------

bool Win32KernelBackend::create_surface(const SurfaceId& id, const SurfaceProfile& profile) {
    if (!initialized_ || id.empty() || find(id)) return false;  // 保守（契約要求）

    // 預設幾何：工作區右上角。座標只存在於此處，不經 API 傳入（NFR-02）。
    RECT work = {};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.right - kDefaultWidth - kDefaultMargin;
    const int y = work.top + kDefaultMargin + static_cast<int>(surfaces_.size()) *
                                                  (kDefaultHeight + kDefaultMargin);

    // WS_EX_NOACTIVATE：面板不搶焦點——這正是 surface kernel「不搶焦點的面板」語意。
    // WS_EX_TOOLWINDOW：不出現在工作列 / Alt-Tab。
    // WS_EX_LAYERED：之後要做透明 / 點擊穿透都需要它。
    const DWORD ex_style = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    HWND hwnd = ::CreateWindowExW(ex_style, kWindowClass, L"", WS_POPUP,
                                  x, y, kDefaultWidth, kDefaultHeight,
                                  nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;

    ::SetLayeredWindowAttributes(hwnd, 0, 235, LWA_ALPHA);  // 略透明，桌面浮層的視覺慣例
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    SurfaceRecord rec;
    rec.hwnd = hwnd;
    rec.profile = profile;
    surfaces_.emplace_back(id, rec);

    apply_layer(hwnd, profile.layer);
    apply_input_policy(hwnd, profile.input, profile.hit);
    return true;
}

bool Win32KernelBackend::destroy_surface(const SurfaceId& id) {
    for (auto it = surfaces_.begin(); it != surfaces_.end(); ++it) {
        if (it->first != id) continue;
        if (it->second.hwnd) {
            ::SetWindowLongPtrW(it->second.hwnd, GWLP_USERDATA, 0);
            ::DestroyWindow(it->second.hwnd);
        }
        surfaces_.erase(it);
        return true;
    }
    return false;  // 未知 id 回 false，不崩潰（契約要求）
}

bool Win32KernelBackend::has_surface(const SurfaceId& id) const { return find(id) != nullptr; }

bool Win32KernelBackend::show_surface(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (!rec || !rec->hwnd) return false;
    // SW_SHOWNOACTIVATE：顯示但不奪取焦點（與 WS_EX_NOACTIVATE 一致）。
    ::ShowWindow(rec->hwnd, SW_SHOWNOACTIVATE);
    apply_layer(rec->hwnd, rec->profile.layer);  // 顯示後重申圖層，避免被其他視窗擠下去
    return true;
}

bool Win32KernelBackend::hide_surface(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (!rec || !rec->hwnd) return false;
    ::ShowWindow(rec->hwnd, SW_HIDE);
    return true;
}

bool Win32KernelBackend::is_visible(const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    // 以 win32 為準，不維護影子狀態——這樣「後端說可見」等於「桌面上真的看得到」。
    return rec && rec->hwnd && ::IsWindowVisible(rec->hwnd) != FALSE;
}

const SurfaceProfile* Win32KernelBackend::surface_profile(const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    return rec ? &rec->profile : nullptr;
}

// --- 圖層 / 輸入策略對映 ------------------------------------------------------

void Win32KernelBackend::apply_layer(HWND hwnd, SurfaceLayer layer) const {
    // 具名圖層 → win32 z-order。**沒有任何數字層級**（NFR-02）。
    HWND after = HWND_NOTOPMOST;
    switch (layer) {
        case SurfaceLayer::Wallpaper:
        case SurfaceLayer::BelowNormal: after = HWND_BOTTOM;    break;
        case SurfaceLayer::Normal:      after = HWND_NOTOPMOST; break;
        case SurfaceLayer::Overlay:
        case SurfaceLayer::Topmost:     after = HWND_TOPMOST;   break;
    }
    ::SetWindowPos(hwnd, after, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Win32KernelBackend::apply_input_policy(HWND hwnd, InputPolicy input, HitPolicy hit) const {
    LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool pass_through =
        (input == InputPolicy::PassThrough) || (hit == HitPolicy::Transparent);
    // WS_EX_TRANSPARENT = 點擊穿透：命中測試直接落到其後的視窗。
    if (pass_through) {
        ex |= WS_EX_TRANSPARENT;
    } else {
        ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    }
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
}

// --- K2 繪製 ----------------------------------------------------------------

bool Win32KernelBackend::begin_frame(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (!rec || rec->in_frame) return false;  // 未知 id 或已在 frame 中 → false（契約要求）
    rec->in_frame = true;
    return true;
}

bool Win32KernelBackend::end_frame(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (!rec || !rec->in_frame) return false;  // 未曾 begin → false（契約要求）
    rec->in_frame = false;
    ++rec->completed_frames;
    return true;
}

// --- K3 輸入 ----------------------------------------------------------------

bool Win32KernelBackend::set_input_policy(const SurfaceId& id, InputPolicy policy) {
    SurfaceRecord* rec = find(id);
    if (!rec || !rec->hwnd) return false;
    rec->profile.input = policy;
    apply_input_policy(rec->hwnd, policy, rec->profile.hit);
    return true;
}

bool Win32KernelBackend::pump() {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            quit_requested_ = true;
            break;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);  // wnd_proc 於此把輸入事件塞進 pending_
    }
    return !quit_requested_;
}

std::vector<InputEvent> Win32KernelBackend::poll_input() {
    pump();
    std::vector<InputEvent> out;
    out.swap(pending_);
    return out;
}

// --- 具名查找 ----------------------------------------------------------------

HWND Win32KernelBackend::hwnd_for(const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    return rec ? rec->hwnd : nullptr;
}

bool Win32KernelBackend::set_surface_layer(const SurfaceId& id, SurfaceLayer layer) {
    SurfaceRecord* rec = find(id);
    if (!rec || !rec->hwnd) return false;
    rec->profile.layer = layer;  // profile 要跟著更新，否則 surface_profile() 會說謊
    apply_layer(rec->hwnd, layer);
    return true;
}

Win32KernelBackend::SurfaceRecord* Win32KernelBackend::find(const SurfaceId& id) {
    for (auto& kv : surfaces_) {
        if (kv.first == id) return &kv.second;
    }
    return nullptr;
}

const Win32KernelBackend::SurfaceRecord* Win32KernelBackend::find(const SurfaceId& id) const {
    for (const auto& kv : surfaces_) {
        if (kv.first == id) return &kv.second;
    }
    return nullptr;
}

SurfaceId Win32KernelBackend::id_for_hwnd(HWND hwnd) const {
    for (const auto& kv : surfaces_) {
        if (kv.second.hwnd == hwnd) return kv.first;
    }
    return SurfaceId{};
}

}  // namespace ds::kernel
