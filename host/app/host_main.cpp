// H1-01 host shell app — 把擴充點組裝成一個真的浮在 Windows 桌面上的 widget
//
// 這是「平台是不是真的可用」的最終證明：**不改 src/ 任何一行**，只消費已合併單元的公開介面，
// 組出一個常駐桌面、最上層、不搶焦點的 CPU/GPU/RAM widget。
//
// 組裝的擴充點（全部只讀公開介面）：
//   W1-01 Win32KernelBackend（真實視窗 / 具名圖層 / 輸入策略）· C2-02 SystemStatusWidget
//   · E2-01 MetricRegistry / InMemoryMetric（統一指標介面）· E4-03 BarGauge（fill_ratio）
//   · E4-01 TextLayout · E7-01 Value（宣告式設定）· H1-01 GDI 繪製橋接
//
// 與 examples/cpu_gpu_validator 的差別：那支把 render_model 畫成主控台 ASCII，
// 這支把同一份 render_model 畫成**桌面上的真實視窗像素**。widget 與指標的組裝路徑完全相同——
// 這正是重點：換掉的只有最外層的呈現，擴充點一個都沒改。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdlib>  // _wtoi
#include <cwchar>   // wcsstr
#include <memory>
#include <string>

#include "command_bus.hpp"           // E6-01：CommandBus / CommandArgs / CommandResult
#include "document.hpp"              // E7-01：ds::format::Value
#include "draggable_surface.hpp"     // E1-08：拖曳狀態機 + 位置記憶
#include "metric.hpp"                // E2-01
#include "owner_drawn_menu.hpp"      // W1-05：自繪選單呈現
#include "position_store.hpp"        // H1-03：拖曳裝配 + 位置持久化
#include "system_status_widget.hpp"  // C2-02
#include "tray.hpp"                  // E11-01：SystemTray / TrayMenu / TrayMenuItem
#include "tray_win32.hpp"            // W1-02：Win32TrayBackend
#include "ui_state.hpp"              // H1-04：UI 開關狀態持久化
#include "widget_controls.hpp"       // H1-02：選單與命令的共用裝配
#include "widget_painter.hpp"        // H1-01 繪製橋接
#include "win32_backend.hpp"         // W1-01

using ds::command::CommandBus;
using ds::format::Value;
using ds::host::build_widget_tray_menu;
using ds::host::default_positions_path;
using ds::host::default_ui_state_path;
using ds::host::kDefaultSnapThreshold;
using ds::host::OwnerDrawnMenu;
using ds::host::snap_to_work_area_edges;
using ds::host::WorkArea;
using ds::host::parse_ui_state;
using ds::host::PositionPersistence;
using ds::host::read_text_file;
using ds::host::serialize_ui_state;
using ds::host::WidgetUiState;
using ds::host::write_text_file;
using ds::kernel::DraggableSurface;
using ds::host::paint_widget;
using ds::host::register_widget_controls;
using ds::host::SystemTray;
using ds::host::WidgetControlState;
using ds::host::Win32TrayBackend;
using ds::kernel::CapabilityMatrix;
using ds::kernel::HitPolicy;
using ds::kernel::InputPolicy;
using ds::kernel::LayerStack;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceProfile;
using ds::kernel::Win32KernelBackend;
using ds::metrics::InMemoryMetric;
using ds::metrics::InMemoryMetricInstance;
using ds::metrics::MetricRange;
using ds::metrics::MetricRegistry;
using ds::render::FixedFontMetrics;
using ds::widgets::SystemStatusStatus;
using ds::widgets::SystemStatusWidget;

namespace {

constexpr int kFrameIntervalMs = 250;  // 見下方 real_cpu_percent 的取樣間隔說明

// 讀真實主機 CPU 使用率。回傳**負值代表取樣無效**——不可以拿 0.0 當失敗值，
// 0.0 是合法的 CPU%，混用會讓「量表死掉」看起來像「CPU 很閒」（知識庫 K-002 踩過）。
//
// GetSystemTimes 是差分式 API：兩次取樣之間必須真的經過時間（系統時鐘約 15.6ms 一跳），
// 因此主迴圈的每幀間隔（kFrameIntervalMs）不能省。
double real_cpu_percent() {
    static bool primed = false;
    static ULONGLONG prev_idle = 0, prev_kernel = 0, prev_user = 0;
    FILETIME idle_ft, kernel_ft, user_ft;
    if (!::GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) return -1.0;
    auto to_u64 = [](const FILETIME& f) {
        return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    const ULONGLONG idle = to_u64(idle_ft), kernel = to_u64(kernel_ft), user = to_u64(user_ft);

    if (!primed) {  // 首次呼叫只建立基準點；拿 prev=0 差分算出的是「開機至今平均」而非瞬時值
        prev_idle = idle; prev_kernel = kernel; prev_user = user;
        primed = true;
        return -1.0;
    }
    const ULONGLONG d_idle = idle - prev_idle;
    const ULONGLONG total = (kernel - prev_kernel) + (user - prev_user);
    prev_idle = idle; prev_kernel = kernel; prev_user = user;
    if (total == 0) return -1.0;  // 時鐘未前進 → 無效取樣（不是 0%）
    double busy = static_cast<double>(total - d_idle) / static_cast<double>(total) * 100.0;
    if (busy < 0) busy = 0;
    if (busy > 100) busy = 100;
    return busy;
}

// 真實實體記憶體使用率。
double real_ram_percent() {
    MEMORYSTATUSEX ms = {};
    ms.dwLength = sizeof(ms);
    if (!::GlobalMemoryStatusEx(&ms)) return -1.0;
    return static_cast<double>(ms.dwMemoryLoad);
}

InMemoryMetricInstance& add_pct_metric(MetricRegistry& reg, const std::string& id,
                                       const std::string& name) {
    auto m = std::make_shared<InMemoryMetric>(id, name, "%", MetricRange{0.0, 100.0});
    InMemoryMetricInstance& inst = m->add_instance(id + ".0", name, 64);
    reg.register_metric(m);
    return inst;
}

// 宣告式設定（E7-01）：選三個指標與各自的呈現方式。
Value widget_definition() {
    return Value::map({
        {"metrics", Value::list({
             Value::map({{"id", Value::string("cpu.usage")}, {"label", Value::string("CPU")},
                         {"kind", Value::string("gauge")},
                         {"min", Value::number(0)}, {"max", Value::number(100)}}),
             Value::map({{"id", Value::string("gpu.usage")}, {"label", Value::string("GPU")},
                         {"kind", Value::string("bar")},
                         {"min", Value::number(0)}, {"max", Value::number(100)}}),
             Value::map({{"id", Value::string("mem.used")}, {"label", Value::string("RAM")},
                         {"kind", Value::string("progress")},
                         {"min", Value::number(0)}, {"max", Value::number(100)}}),
         })},
    });
}

// `--seconds N`：跑 N 秒後自動結束，供腳本化驗收 / 截圖使用。0 = 常駐不自動結束。
//
// 刻意不用 CommandLineToArgvW：那在 shellapi.h，會為了解析一個數字多拉一個 shell32 相依。
int seconds_limit_from_command_line() {
    const wchar_t* p = ::wcsstr(::GetCommandLineW(), L"--seconds");
    if (!p) return 0;
    p += 9;  // 略過 "--seconds"
    while (*p == L' ' || *p == L'=') ++p;
    const int seconds = ::_wtoi(p);
    return seconds > 0 ? seconds : 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // --- 1) 真實平台後端 + 一個最上層、不搶焦點的常駐 surface ---
    Win32KernelBackend backend{CapabilityMatrix::defaults()};
    if (!backend.init()) return 1;

    SurfaceProfile profile;
    profile.layer = SurfaceLayer::Topmost;        // → HWND_TOPMOST
    profile.input = InputPolicy::Accepting;       // 收得到輸入但不獨占焦點
    profile.hit = HitPolicy::Solid;
    profile.lifecycle = SurfaceLifecycle::Persistent;

    const std::string surface_id = "surface.cpu_widget";
    if (!backend.create_surface(surface_id, profile)) return 1;
    if (!backend.show_surface(surface_id)) return 1;
    HWND hwnd = backend.hwnd_for(surface_id);
    if (!hwnd) return 1;

    // --- 1.5) 拖曳與位置記憶（W1-03 + H1-03）---
    // 可拖曳性稍後由 H1-04 的「鎖定位置」狀態決定（見下方 UI 狀態還原）——
    // 後端預設不可拖是平台層的保守設定，「這個 widget 能不能被搬動」是 host 層的產品決策。
    DraggableSurface draggable{backend};
    PositionPersistence positions{backend, draggable, default_positions_path()};
    // 還原上次的位置。沒有記錄（第一次執行）或檔案壞掉時什麼都不做，
    // widget 就留在後端選的預設幾何——不是錯誤，不需回報。
    positions.restore(surface_id);

    // --- 2) 組裝 widget（同一組擴充點，不改 src/）---
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics font{6.0, 14.0};
    SystemStatusWidget widget{"widget.cpugpu.main", backend, layers, font};
    if (widget.configure(widget_definition()) != SystemStatusStatus::Ok) return 1;

    MetricRegistry registry;
    InMemoryMetricInstance& cpu = add_pct_metric(registry, "cpu.usage", "CPU");
    InMemoryMetricInstance& gpu = add_pct_metric(registry, "gpu.usage", "GPU");
    InMemoryMetricInstance& ram = add_pct_metric(registry, "mem.used", "RAM");

    // --- 3) GDI 資源（雙緩衝，避免每幀閃爍）---
    RECT bounds = {};
    ::GetClientRect(hwnd, &bounds);
    HFONT hfont = ::CreateFontW(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    // --- 3.5) 系統匣：選單每一項都只是「發一個 E6-01 命令」---
    //
    // 這裡沒有任何選單邏輯：勾選切換、分隔線/停用項的處理、命令分派全部由 E11-01
    // `SystemTray::click(path)` 負責；W1-02 後端只回報「使用者選了哪一項」的索引路徑。
    // host 要做的只有兩件事：把選單組出來、把命令處理器接上後端。
    // 選單與命令處理器來自 H1-02 的共用裝配（`host/tray/`），不在此就地寫一份——
    // 同一份邏輯有兩份的話，被測到的那份與實際跑的那份就會慢慢分岔。
    WidgetControlState controls;

    // 還原上次的 UI 開關狀態。沒有檔案（第一次執行）或檔案壞掉時沿用預設值——
    // 不是錯誤，不需回報。**一個每次啟動都自己解鎖的「鎖定」等於沒有鎖。**
    const std::string ui_path = default_ui_state_path();
    WidgetUiState ui;
    if (parse_ui_state(read_text_file(ui_path), ui)) {
        controls.topmost = ui.topmost;
        controls.passthrough = ui.passthrough;
        controls.locked = ui.locked;
    }
    // 把還原後的狀態真的套到後端上——否則選單顯示的與實際行為會不一致。
    backend.set_surface_layer(surface_id,
                              controls.topmost ? SurfaceLayer::Topmost : SurfaceLayer::Normal);
    backend.set_input_policy(surface_id, controls.passthrough ? InputPolicy::PassThrough
                                                              : InputPolicy::Accepting);
    backend.set_draggable(surface_id, !controls.locked);

    // 把目前開關狀態寫回檔案。任何一次切換後都呼叫——與位置持久化同理，
    // 這支程式沒有「正常關閉」以外的收尾機會。
    const auto save_ui_state = [&] {
        if (ui_path.empty()) return;
        write_text_file(ui_path, serialize_ui_state(
            WidgetUiState{controls.topmost, controls.passthrough, controls.locked}));
    };

    CommandBus bus;
    register_widget_controls(bus, backend, surface_id, controls);

    auto tray_backend = std::make_unique<Win32TrayBackend>();
    Win32TrayBackend* tray_raw = tray_backend.get();  // 供 poll_selection（win32 專屬擴充）
    SystemTray tray{std::move(tray_backend), &bus};

    tray.set_menu(build_widget_tray_menu(controls));
    tray.set_icon("icon.desktop_shell");
    tray.set_tooltip("desktop-shell — 系統狀態");

    // W1-05：改用 E11-02 的自繪選單呈現。
    // 取捨很明確——自繪拿到一致的外觀（與 widget、托盤圖示同一套配色），
    // 但**失去原生選單免費提供的無障礙支援**（螢幕閱讀器讀不到）。
    // 見 CHG-20260803-15 的已知限制；若要退回原生選單，把這一行拿掉即可。
    tray_raw->set_owner_drawn(true);
    OwnerDrawnMenu owner_menu;
    owner_menu.set_menu(tray.menu());

    tray.show();

    const int limit = seconds_limit_from_command_line();
    const int max_frames = limit > 0 ? limit * 1000 / kFrameIntervalMs : 0;

    // 等待下一幀期間**持續抽訊息**。
    //
    // 直接 `Sleep(kFrameIntervalMs)` 會讓視窗在那 250ms 內完全不回應：系統的拖曳移動迴圈
    // 是在 `DispatchMessage` 裡跑的，抽訊息太稀疏會讓使用者的拖曳「按不下去」或一頓一頓。
    // CHG-20260803-12 的操作驗收就是這樣間歇性失敗的——合成拖曳的整個動作落在兩次抽取之間。
    //
    // 取樣間隔必須是 250ms（`GetSystemTimes` 是差分式，見 K-002），但**抽訊息不需要跟著慢**。
    // 兩者本來就是不同的節奏，之前只是恰好用同一個 Sleep 混在一起。
    constexpr int kPumpStepMs = 15;
    const auto pump_while_waiting = [&](int total_ms) {
        for (int waited = 0; waited < total_ms; waited += kPumpStepMs) {
            if (!backend.pump()) return false;
            ::Sleep(kPumpStepMs);
        }
        return true;
    };

    // --- 4) 主迴圈：抽訊息 → 更新指標 → refresh/render → 把 render_model 畫上去 ---
    for (int frame = 0; max_frames == 0 || frame < max_frames; ++frame) {
        if (!backend.pump()) break;              // 收到結束請求
        if (controls.quit) break;                // 托盤選單的「結束」

        // 使用者拖完 widget → 邊緣吸附 → 記住新位置並立刻寫回檔案。
        //
        // **順序不能反**：先吸附再記憶，記下來的才是吸附後的位置；
        // 反過來的話下次還原會回到吸附前那個差一點點的座標，使用者會看到 widget
        // 每次開機都從邊上「彈開」一點。
        //
        // 立刻寫而不是等關閉時再寫：這支程式沒有「正常關閉」以外的收尾機會
        // （工作管理員結束、當機、登出都不會走到結尾），拖完就存才真的存得住。
        ds::kernel::SurfaceId moved;
        while (backend.poll_drag_finished(moved)) {
            int wx = 0, wy = 0, ww = 0, wh = 0, ax = 0, ay = 0, aw = 0, ah = 0;
            if (backend.surface_origin(moved, wx, wy) &&
                backend.surface_size(moved, ww, wh) &&
                backend.work_area(ax, ay, aw, ah)) {
                const auto snapped = snap_to_work_area_edges(
                    wx, wy, ww, wh, WorkArea{ax, ay, aw, ah}, kDefaultSnapThreshold);
                if (snapped.x != wx || snapped.y != wy) {
                    backend.set_surface_origin(moved, snapped.x, snapped.y);
                }
            }
            if (positions.remember_current(moved)) positions.flush();
        }

        // 使用者右鍵托盤圖示 → 用自繪選單呈現（W1-05），選到的路徑仍走同一條分派路徑。
        POINT menu_at = {};
        if (tray_raw->poll_menu_request(menu_at)) {
            owner_menu.set_menu(tray.menu());  // 帶入目前的勾選狀態
            std::vector<std::size_t> picked;
            if (owner_menu.popup_at(menu_at, picked)) {
                tray.click(picked);
                tray.sync_menu();
                save_ui_state();
                if (controls.quit) break;
            }
        }

        // 使用者在托盤選單選了東西 → 交給 E11-01 解釋語意並分派命令。
        // host 完全不知道那一項是什麼意思，只負責轉交——語意留在選單模型裡。
        // （原生選單路徑；自繪模式下不會有選取事件，見上方。）
        std::vector<std::size_t> chosen;
        if (tray_raw->poll_selection(chosen)) {
            tray.click(chosen);
            tray.sync_menu();  // Checkbox 勾選狀態已由 click() 更新，重推後端讓選單反映
            save_ui_state();   // 切換當下就存，不等關閉
            if (controls.quit) break;
        }

        // 取樣間隔（見 real_cpu_percent），期間持續抽訊息以保持拖曳流暢。
        if (!pump_while_waiting(kFrameIntervalMs)) break;

        const double cpu_pct = real_cpu_percent();
        const double ram_pct = real_ram_percent();
        cpu.push(cpu_pct < 0.0 ? 0.0 : cpu_pct);
        ram.push(ram_pct < 0.0 ? 0.0 : ram_pct);
        // GPU 目前無跨廠商的免驅動查詢途徑，以動態 sweep 佔位——
        // 重點是證明 widget 對「任意指標」皆能呈現，不寫死任何具體感測器。
        gpu.push(45.0 + 35.0 * std::sin(frame * 0.13) + 8.0 * std::sin(frame * 0.39));

        widget.refresh(registry);
        widget.render();

        if (!backend.begin_frame(surface_id)) continue;
        ::GetClientRect(hwnd, &bounds);
        HDC screen = ::GetDC(hwnd);
        if (screen) {
            HDC mem = ::CreateCompatibleDC(screen);
            HBITMAP bmp = ::CreateCompatibleBitmap(screen, bounds.right, bounds.bottom);
            HGDIOBJ old_bmp = ::SelectObject(mem, bmp);
            HGDIOBJ old_font = ::SelectObject(mem, hfont);

            paint_widget(mem, bounds, widget.render_model(), L"desktop-shell — 系統狀態");

            ::BitBlt(screen, 0, 0, bounds.right, bounds.bottom, mem, 0, 0, SRCCOPY);

            ::SelectObject(mem, old_font);
            ::SelectObject(mem, old_bmp);
            ::DeleteObject(bmp);
            ::DeleteDC(mem);
            ::ReleaseDC(hwnd, screen);
        }
        backend.end_frame(surface_id);
    }

    positions.flush();  // 收尾再存一次（拖曳當下已存過，這裡只是保險）
    tray.hide();  // 移除匣圖示，否則行程結束後會留一個死圖示直到滑鼠掃過
    ::DeleteObject(hfont);
    backend.shutdown();
    return 0;
}
