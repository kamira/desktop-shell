// 確保 glibc 於嚴格模式下仍宣告 getloadavg（Mac 無此需求，定義無害）。須在任何標頭之前。
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

// examples/cpu_gpu_validator/main.cpp — CPU/GPU Usage Widget 驗證器
//
// 「最終交付」：**不改任何 src/**，純從已合併的擴充點**組裝**一個 CPU/GPU 使用率 widget，
// 餵入指標值、驅動其 refresh()/render()，並把 widget 產出的 render_model 畫成可檢視的 ASCII
// 量表「螢幕」，跑一個 live 更新迴圈——證明擴充點真的能被外部組裝出可運作的桌面 widget。
//
// 組裝的擴充點（全部只讀其公開介面，不碰實作）：
//   C2-02 SystemStatusWidget（系統狀態 widget） · E2-01 MetricRegistry / InMemoryMetric（統一
//   指標介面） · E4-03 BarGauge（量表 render_model，取 fill_ratio） · E4-01 TextLayout · E7-01
//   Value（宣告式設定） · C1-01 SkinProfile（桌面基底）。
//
// 指標來源（驗證器層自行取得，非 src/）：CPU 讀**真實**主機負載（**跨平台**：Windows 用
// GetSystemTimes，Mac/Linux 用 getloadavg），GPU 與 RAM 以動態 sweep 模擬——重點在證明 widget
// 對「任意指標」皆能組裝呈現，不寫死任何具體感測器。此為 example（非治理 src/ 單元），故容許
// `#ifdef _WIN32` 平台分支；176 個核心單元維持平台中立、無平台分支。

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>   // GetSystemTimes（Windows CPU 取樣，MSVC）
#else
#include <cstdlib>     // getloadavg（POSIX，Mac / Linux）
#include <unistd.h>    // sysconf(_SC_NPROCESSORS_ONLN)（可攜取核心數）
#endif

#include "document.hpp"              // E7-01：ds::format::Value（map / list / string / number）
#include "metric.hpp"                 // E2-01
#include "system_status_widget.hpp"  // C2-02（透遞 C1-01 / E4-01 / E4-03 / E7-* / null backend）

using ds::format::Value;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::metrics::InMemoryMetric;
using ds::metrics::InMemoryMetricInstance;
using ds::metrics::MetricRange;
using ds::metrics::MetricRegistry;
using ds::render::FixedFontMetrics;
using ds::widgets::SystemStatusStatus;
using ds::widgets::SystemStatusWidget;

namespace {

// 讀真實主機 CPU 負載 → 百分比（夾限 0..100）。跨平台：Windows 用 GetSystemTimes，
// 其餘（Mac / Linux）用 POSIX getloadavg。這是驗證器層自行取得指標的示範，非 src/ 平台碼。
//
// **回傳負值 = 這次取樣無效**（尚無基準點、或兩次取樣之間時鐘未前進）。
// 不可以拿 0.0 當「無效」的代表值——0.0 是合法的 CPU%，混用會讓「量表死掉」看起來像「CPU 很閒」，
// 呼叫端與驗收條件都分辨不出來（CHG-20260803-02 踩過）。
#if defined(_WIN32)
// Windows：GetSystemTimes 回傳的是**累計**時間，必須兩次取樣之間真的經過時間才有意義
// （系統時鐘約 15.6ms 跳一次）。kernel 時間已含 idle。
double real_cpu_percent() {
    static bool primed = false;
    static ULONGLONG prev_idle = 0, prev_kernel = 0, prev_user = 0;
    FILETIME idle_ft, kernel_ft, user_ft;
    if (!GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) return -1.0;
    auto to_u64 = [](const FILETIME& f) {
        return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    ULONGLONG idle = to_u64(idle_ft), kernel = to_u64(kernel_ft), user = to_u64(user_ft);

    // 首次呼叫只建立基準點。**不可以拿 prev_* 的初值 0 去差分**——那算出來的是
    // 「開機至今的平均 CPU」而非瞬時值，看起來像個合理數字，其實完全不是要量的東西。
    if (!primed) {
        prev_idle = idle; prev_kernel = kernel; prev_user = user;
        primed = true;
        return -1.0;
    }

    ULONGLONG d_idle = idle - prev_idle, d_kernel = kernel - prev_kernel, d_user = user - prev_user;
    prev_idle = idle; prev_kernel = kernel; prev_user = user;
    ULONGLONG total = d_kernel + d_user;  // kernel 已含 idle → total 為總 CPU 時間
    if (total == 0) return -1.0;          // 時鐘未前進 → 無效取樣（不是 0%）
    double busy = static_cast<double>(total - d_idle) / static_cast<double>(total) * 100.0;
    if (busy < 0) busy = 0;
    if (busy > 100) busy = 100;
    return busy;
}
#else
// POSIX（Mac / Linux）：1 分鐘負載平均 / 核心數。無狀態——不需基準點、不依賴取樣間隔。
double real_cpu_percent() {
    double load[3] = {0, 0, 0};
    if (getloadavg(load, 3) < 1) return -1.0;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    double pct = (load[0] / static_cast<double>(ncpu)) * 100.0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}
#endif

// 把一個 [0,1] 的 fill_ratio 畫成寬 w 的 ASCII 長條。
std::string bar(double ratio, int w) {
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    int filled = static_cast<int>(ratio * w + 0.5);
    std::string s;
    s.reserve(w * 3);
    for (int i = 0; i < w; ++i) s += (i < filled ? "█" : "░");  // █ / ░
    return s;
}

// 註冊一個 [0,100] 單位 % 的記憶體指標，回傳其唯一實例參照供每幀推值。
InMemoryMetricInstance& add_pct_metric(MetricRegistry& reg, const std::string& id,
                                       const std::string& name) {
    auto m = std::make_shared<InMemoryMetric>(id, name, "%",
                                              MetricRange{0.0, 100.0});
    InMemoryMetricInstance& inst = m->add_instance(id + ".0", name, 64);
    reg.register_metric(m);
    return inst;
}

}  // namespace

int main() {
    // --- 1) 組裝 widget（C1-01 基底 + E4-01 字型度量，皆注入式，不改 src/）---
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics font{6.0, 14.0};
    SystemStatusWidget widget{"widget.cpugpu.main", backend, layers, font};

    // --- 2) 宣告式設定：選 CPU / GPU / RAM 三個指標（E7-01 Value；kind 各異驗證多型呈現）---
    Value def = Value::map({
        {"metrics",
         Value::list({
             Value::map({{"id", Value::string("cpu.usage")},
                         {"label", Value::string("CPU")},
                         {"kind", Value::string("gauge")},
                         {"min", Value::number(0)},
                         {"max", Value::number(100)}}),
             Value::map({{"id", Value::string("gpu.usage")},
                         {"label", Value::string("GPU")},
                         {"kind", Value::string("bar")},
                         {"min", Value::number(0)},
                         {"max", Value::number(100)}}),
             Value::map({{"id", Value::string("mem.used")},
                         {"label", Value::string("RAM")},
                         {"kind", Value::string("progress")},
                         {"min", Value::number(0)},
                         {"max", Value::number(100)}}),
         })},
    });
    if (widget.configure(def) != SystemStatusStatus::Ok) {
        std::fprintf(stderr, "configure 失敗\n");
        return 1;
    }

    // --- 3) 指標來源：E2-01 registry + 三個記憶體指標 ---
    MetricRegistry registry;
    InMemoryMetricInstance& cpu = add_pct_metric(registry, "cpu.usage", "CPU");
    InMemoryMetricInstance& gpu = add_pct_metric(registry, "gpu.usage", "GPU");
    InMemoryMetricInstance& ram = add_pct_metric(registry, "mem.used", "RAM");

    // --- 4) live 迴圈：每幀更新指標 → widget.refresh()/render() → 把 render_model 畫成螢幕 ---
    const int kFrames = 12;
    const int kBarW = 24;
    // 取樣間隔：Windows 的 GetSystemTimes 是差分式的，兩次取樣之間必須真的經過時間，
    // 否則系統時鐘（約 15.6ms 一跳）根本沒前進，差分恆為 0。250ms 遠大於一個時鐘刻度。
    // `std::this_thread::sleep_for` 是標準 C++，不引入平台分支；Mac/Linux 的 getloadavg
    // 不依賴間隔，行為不受影響（只是迴圈變成真的「live 更新」而非瞬間跑完）。
    const auto kSampleInterval = std::chrono::milliseconds(250);

    // 先取樣一次把基準點填好再進迴圈，否則第一幀必定是無效取樣。回傳值必為負，丟棄。
    (void)real_cpu_percent();

    int invalid_cpu_samples = 0;
    for (int frame = 0; frame < kFrames; ++frame) {
        std::this_thread::sleep_for(kSampleInterval);

        double t = frame * 0.55;
        double cpu_pct = real_cpu_percent();  // 真實主機 CPU 負載；負值 = 取樣無效
        if (cpu_pct < 0.0) {
            ++invalid_cpu_samples;
            cpu_pct = 0.0;  // 仍推 0 讓畫面畫得出來，但已計入無效樣本，驗收會擋
        }
        cpu.push(cpu_pct);
        gpu.push(45.0 + 35.0 * std::sin(t) + 8.0 * std::sin(t * 3));  // 模擬 GPU sweep
        ram.push(55.0 + 20.0 * std::sin(t * 0.4 + 1.0));              // 模擬 RAM

        // 驅動 widget：一律走「注入指標 → 刷新 → 呈現」的擴充點路徑。
        widget.refresh(registry);
        widget.render();
        const auto& model = widget.render_model();

        // 畫「螢幕」：widget 自己算出的 render_model（fill_ratio + display_text）即畫面來源。
        std::printf("\n  ┌─ 系統狀態 Widget  [widget.cpugpu.main]  frame %2d/%d ─┐\n",
                    frame + 1, kFrames);
        for (const auto& e : model.entries) {
            const char* lbl = e.label.empty() ? "?" : e.label.c_str();
            if (!e.available) {
                std::printf("  │ %-4s  [%s]  %-8s │\n", lbl,
                            bar(0, kBarW).c_str(), "—（降級）");
                continue;
            }
            std::printf("  │ %-4s  [%s]  %-8s │\n", lbl,
                        bar(e.element.fill_ratio, kBarW).c_str(),
                        e.display_text.c_str());
        }
        std::printf("  └%s┘\n", std::string(40 + kBarW - 16, '-').c_str());
        std::fflush(stdout);
    }

    // --- 5) 驗收判定 ---
    // 條件 ①（組裝面）：最後一幀三指標皆 available 且 fill_ratio 落在 [0,1]。
    const auto& model = widget.render_model();
    int ok = 0;
    for (const auto& e : model.entries) {
        if (e.available && e.element.fill_ratio >= 0.0 && e.element.fill_ratio <= 1.0) ++ok;
    }
    const bool assembled_ok = ok == static_cast<int>(model.entries.size());

    // 條件 ②（資料面）：CPU 每一幀都必須取到**有效**樣本。
    // 為什麼需要這條：條件 ① 只看 fill_ratio 是否落在 [0,1]，而 0.0 完美滿足它——
    // 一旦 CPU 取樣壞掉、量表整輪停在 0.0%，舊版驗收照樣印 ✓ PASS。這是會放行假綠燈的驗收條件，
    // 比讀不到值本身更危險。這條專門擋它（CHG-20260803-02）。
    const bool sampling_ok = invalid_cpu_samples == 0;

    std::printf("\n  驗收①組裝：%d/%zu 指標成功組裝呈現（fill_ratio ∈ [0,1]、display_text 已格式化）。\n",
                ok, model.entries.size());
    std::printf("  驗收②取樣：CPU 有效樣本 %d/%d 幀%s\n",
                kFrames - invalid_cpu_samples, kFrames,
                sampling_ok ? "。" : "——**量表停在 0.0% 不是 CPU 很閒，是取樣失效**。");
    std::printf("  結論：C2-02 widget 純從擴充點組裝、以 E2-01 注入式指標驅動、產出 E4-03 量表\n"
                "        render_model 並實際渲染為可檢視畫面——CPU 為真實主機負載。%s\n",
                (assembled_ok && sampling_ok) ? "✓ PASS" : "✗ FAIL");
    return (assembled_ok && sampling_ok) ? 0 : 1;
}
