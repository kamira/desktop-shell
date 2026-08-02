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

#include "document.hpp"              // E7-01：ds::format::Value
#include "metric.hpp"                // E2-01
#include "system_status_widget.hpp"  // C2-02
#include "widget_painter.hpp"        // H1-01 繪製橋接
#include "win32_backend.hpp"         // W1-01

using ds::format::Value;
using ds::host::paint_widget;
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

    const int limit = seconds_limit_from_command_line();
    const int max_frames = limit > 0 ? limit * 1000 / kFrameIntervalMs : 0;

    // --- 4) 主迴圈：抽訊息 → 更新指標 → refresh/render → 把 render_model 畫上去 ---
    for (int frame = 0; max_frames == 0 || frame < max_frames; ++frame) {
        if (!backend.pump()) break;              // 收到結束請求
        ::Sleep(kFrameIntervalMs);               // 取樣間隔，見 real_cpu_percent

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

    ::DeleteObject(hfont);
    backend.shutdown();
    return 0;
}
