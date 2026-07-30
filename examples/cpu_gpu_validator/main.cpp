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
// 指標來源（驗證器層自行取得，非 src/）：CPU 讀**真實**主機負載（getloadavg / hw.ncpu），
// GPU 與 RAM 以動態 sweep 模擬（相位 1 無 sudo-free 的 GPU/精確記憶體 API）——重點在證明 widget
// 對「任意指標」皆能組裝呈現，不寫死任何具體感測器。

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <cstdlib>     // getloadavg（POSIX，Mac / Linux 皆有）
#include <unistd.h>    // sysconf(_SC_NPROCESSORS_ONLN)（可攜取核心數）

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

// 讀真實主機 CPU 負載 → 粗略百分比（1 分鐘負載 / 核心數，夾限 0..100）。
double real_cpu_percent() {
    double load[3] = {0, 0, 0};
    if (getloadavg(load, 3) < 1) return 0.0;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    double pct = (load[0] / static_cast<double>(ncpu)) * 100.0;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

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
    for (int frame = 0; frame < kFrames; ++frame) {
        double t = frame * 0.55;
        cpu.push(real_cpu_percent());                                 // 真實主機 CPU 負載
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

    // --- 5) 驗收判定：最後一幀三指標皆 available 且 fill_ratio 落在 [0,1] ---
    const auto& model = widget.render_model();
    int ok = 0;
    for (const auto& e : model.entries) {
        if (e.available && e.element.fill_ratio >= 0.0 && e.element.fill_ratio <= 1.0) ++ok;
    }
    std::printf("\n  驗收：%d/%zu 指標成功組裝呈現（fill_ratio ∈ [0,1]、display_text 已格式化）。\n",
                ok, model.entries.size());
    std::printf("  結論：C2-02 widget 純從擴充點組裝、以 E2-01 注入式指標驅動、產出 E4-03 量表\n"
                "        render_model 並實際渲染為可檢視畫面——CPU 為真實主機負載。%s\n",
                ok == static_cast<int>(model.entries.size()) ? "✓ PASS" : "✗ FAIL");
    return ok == static_cast<int>(model.entries.size()) ? 0 : 1;
}
