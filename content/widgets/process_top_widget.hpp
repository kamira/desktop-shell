// content/widgets/process_top_widget.hpp — C2-11 行程 top-N widget（artifact 層 / 相位 1：組裝型 widget）
//
// 語意：桌面「行程 top-N」widget —— 把 **E2-19**（行程監看）掛上的多實例指標（每個行程一個
// `MetricInstance`）經 **E2-01** 統一指標介面讀出，依宣告式設定取前 **N** 名，每名一列，以
// **E4-03** 長條 / 量表 / 進度視覺元素 + 格式化文字呈現；**N** 與**排序維度**（CPU / 記憶體）
// 由 **E7-01** 宣告式格式（含 `format_version` 版本欄位）決定，顯示範圍與單位換算可寫
// **E7-05** 公式。
//
// 與 C2-02（系統狀態 widget）同形，差別在**指標是多實例的**：C2-02 一個指標畫一列（取第 0 個
// 實例），本單元一個指標畫 N 列（每個行程實例一列）。
//
// ---------------------------------------------------------------------------
// 排序維度的語意（CHG-20260829-05 Q-A 停點裁定，本單元的核心約束）
// ---------------------------------------------------------------------------
// **切換排序維度 = 換掉註冊的指標。** E2-19 的 `ProcessMonitorProvider` 把排序維度做成
// **建構子參數**（`sort_key` / `top_n` 皆無 setter），且其 `metric_id()` 依維度回傳
// `proc.cpu` 或 `proc.memory`，單位亦隨之由 `%` 變成 `MB`。因此：
//   - 本 widget **不持有、不建立、不重建** provider。設定裡的維度只做一件事——
//     **決定 `refresh()` 去 `MetricRegistry` 要哪一個指標 id**（見 `metric_id()`）。
//   - 維度改了就是去要另一個 id。舊 id 的指標即使還留在 registry 裡也**一律不讀**：讀了會讓
//     單位從 `%` 悄悄變成 `MB`，是「看起來完全正常的假資料」（知識庫 K-002 的陷阱）。
//   - 要讓新維度真的有資料，**provider 必須由 host 重建並重新註冊**——那是 `H1-11`（設定視窗
//     裝配）的職責，**不是本單元的**。在 host 重建之前，`refresh()` 誠實回報
//     `ProcessMetricState::Missing`（具名狀態，不是一個落在合法值域裡的 0）。
//   - **歷史不保留**：本 widget 自身不存任何時序、也不讀 `MetricInstance::history()`；
//     `configure()` 與每次 `refresh()` 都把列從頭重建。
//
// ---------------------------------------------------------------------------
// 「沒有顯示範圍就不畫長條」（知識庫 K-002 的直接應用）
// ---------------------------------------------------------------------------
// `proc.cpu` 的 range 是 `bounded[0,100]`——有上界，長條 / 量表的填充比例有意義。
// `proc.memory` 的 range 是 `at_least(0)`——**無上界**。若憑空塞一個上界進去，「用了 8 GB」
// 與「用了 100 MB」會畫成一樣滿：值域檢查全數通過、畫面看起來完全正常，但那是假資料。
// 故：解不出顯示範圍（既無宣告式 `min`/`max`、指標自身 range 又非有界）時，該列
// `has_range == false`、**不掛 E4-03 元素**（element 維持預設 / `surface` 為空 = 未綁定），
// 只出格式化文字。要畫記憶體維度的長條，就得在宣告式設定裡明寫 `min`/`max`。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實行程 API（行程資料一律經注入的
// E2-01 `MetricRegistry` 流入，相位 1 為記憶體內 / null 來源）、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02：E4-03 `RenderModel` 的具名
// `surface` + `slot`、比例 / 角度表達）。
//
// 依賴注入約定：`refresh()` 以參數注入指標來源（E2-01 `MetricRegistry`），本物件不持有其
// 生命週期、不持有任何 provider。
//
// 命名空間 `ds::widgets`（型別名刻意與 C2-02 區隔，兩者標頭可在同一翻譯單元共存）。
#ifndef DS_CONTENT_WIDGETS_C2_11_PROCESS_TOP_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_11_PROCESS_TOP_WIDGET_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "bar_gauge.hpp"        // E4-03（上游，可讀不可改）：BarElement / GaugeElement /
                                 //   ProgressElement / RenderModel
#include "document.hpp"         // E7-01（上游，可讀不可改）：Value / parse / Document / FormatVersion
#include "formula.hpp"          // E7-05（上游，可讀不可改）：is_formula / Evaluator / EvalResult
#include "process_monitor.hpp"  // E2-19（上游，可讀不可改）：ProcessSortKey /
                                 //   ProcessMonitorProvider 的指標身分常數。本標頭亦經此取得
                                 //   E2-01 的 MetricRegistry / Metric / MetricInstance（E2-19
                                 //   PUBLIC 相依 e2_01，include 目錄已透傳）。

namespace ds::widgets {

// 單一行程列於本 widget 的呈現種類（NFR-02：具名，非數字）。`Text` = 只出文字，不掛 E4-03
// 視覺元素。
enum class ProcessRowElementKind {
    Bar,
    Gauge,
    Progress,
    Text,
};

const char* to_string(ProcessRowElementKind k) noexcept;

// widget 操作的具名結果（與上游各單元同精神：明確、不靜默）。
enum class ProcessTopStatus {
    Ok,            // 操作成功。
    Invalid,       // 前置條件不滿足：尚未 configure()、設定結構 / 具名值不合法、`top_n` 不在
                    // 允許集合內、`min`/`max` 未成對或退化、`transform` 非合法公式語法等。
    FormulaError,  // `min`/`max` 的**靜態**公式求值失敗（語法錯 / 未定義變數 / 除零 / 型別誤用）。
                    // 靜態 = 不得引用 `value`（求值作用域不含它，引用即為未定義變數）。
    FormatError,   // 僅 configure_text()：E7-01 解析失敗或 `format_version` 不相容（帶行號於
                    // last_error()）。
};

const char* to_string(ProcessTopStatus s) noexcept;

// 最近一次 refresh() 對指標來源的**具名**查核結果。
//
// 刻意不以「空列表」或「0」表達失敗（知識庫 K-002：落在合法值域內的失敗代表值會偽裝成正常
// 資料流過所有下游檢查）——每種情形都有自己的名字。
enum class ProcessMetricState {
    Ok,             // 指標存在、單位與維度相符、至少一個行程實例。
    NotConfigured,  // 尚未 configure()（未查詢）。
    Missing,        // registry 沒有本維度的指標 id。**維度剛切換、host 尚未重建 provider 時的
                     // 正常狀態**（Q-A：provider 由 host 重建）。
    UnitMismatch,   // id 在，但 Metric::unit() 不是本維度的期望單位（`%` / `MB`）→ 那不是
                     // E2-19 掛的那個指標，不讀、不猜、不畫。
    NoInstances,    // 指標在、單位相符，但沒有任何行程實例（provider 無讀值）。
};

const char* to_string(ProcessMetricState s) noexcept;

// configure() 的解讀結果（本 widget 目前的宣告式設定）。
struct ProcessTopSpec {
    // 排序維度 —— 直接沿用 E2-19 的列舉，不另造一套（它同時決定要讀哪個指標 id 與期望單位）。
    ds::sysinfo::ProcessSortKey sort_key = ds::sysinfo::ProcessSortKey::Cpu;

    // 取前幾名（0 = 全部）。僅允許 allowed_top_n() 的值。
    std::size_t top_n = 0;

    ProcessRowElementKind kind = ProcessRowElementKind::Bar;

    bool has_range_override = false;  // 是否以宣告式 min/max 覆寫顯示範圍。
    double range_min = 0.0;           // has_range_override 時有效。
    double range_max = 1.0;

    std::string transform;  // 選填 E7-05 公式（如 "= value / 1024"）；空 = 不轉換。求值時變數
                             // `value` 綁定為該列的原始指標值。
};

// 單一行程列於最近一次 refresh() + render() 後的呈現結果。
struct ProcessRow {
    std::string instance_id;  // E2-01 實例識別碼（E2-19 以 pid 字串為之）。
    std::string label;        // 實例標籤（E2-19 以行程名為之）。

    // NFR-03：該實例值 `valid==false`、數值非有限（NaN/Inf）、或 `transform` 求值失敗 → false
    // （降級：不含 element / display_text 為 "—"），且**不中止其餘列**。
    bool available = false;

    double raw_value = 0.0;      // 指標原始值（排序維度值；unavailable 時為 0）。
    double display_value = 0.0;  // 經 transform（若有）轉換後的值（unavailable 時為 0）。
    std::string unit;            // E2-01 Metric::unit()（unavailable 時為空）。

    // 顯示範圍（宣告式覆寫 > Metric 有界 range()）。解不出來時 has_range == false，該列**不掛**
    // E4-03 元素（見檔首「沒有顯示範圍就不畫長條」）。
    bool has_range = false;
    double range_min = 0.0;
    double range_max = 1.0;

    std::string display_text;           // 格式化文字，如 "62.3%" / "512.0 MB"（unavailable 時為 "—"）。
    ds::elements::RenderModel element;  // E4-03 渲染描述；Text 種類 / 無範圍 / unavailable 時維持
                                         //   預設建構（`surface` 為空 = 未綁定）。
};

// 本 widget 目前的完整渲染描述。純資料，供後續相位的繪製層消費。
struct ProcessTopRenderModel {
    // 本次 refresh() 實際查詢的指標 id（= 設定維度的證據：proc.cpu ↔ proc.memory）。
    ds::metrics::MetricId metric_id;
    std::string unit;  // 本次讀到的單位（state != Ok 時為空）。

    ProcessMetricState state = ProcessMetricState::NotConfigured;

    // 依排序維度值由高到低、取前 top_n 列（0 = 全部）。unavailable 列排在最後。
    std::vector<ProcessRow> rows;
};

// ---------------------------------------------------------------------------
// ProcessTopWidget —— 行程 top-N widget：組裝 E2-19 + E4-03 + E7-01 + E7-05。
//
// 行為順序：`configure()`（或 `configure_text()`，走 E7-01 文字宣告 + 版本欄位）
//   → `refresh()`（注入 E2-01 指標來源讀值、排序、取前 N）
//   → `render()`（組裝 E4-03 元素 + 顯示文字）。
// ---------------------------------------------------------------------------
class ProcessTopWidget {
public:
    // 「全部行程」（不截斷）—— 沿用 E2-19 的常數，不另定一個可能走鐘的 0。
    static constexpr std::size_t kAllProcesses = ds::sysinfo::ProcessMonitorProvider::kAllProcesses;

    // `top_n` 的允許集合：{0, 3, 5, 10}（0 = 全部）。驗收明定 N 為 3 / 5 / 10 / 0=全部，故
    // 其餘值一律 Invalid（不靜默接受一個規格未定義的 N）。**公開**是為了讓設定介面
    // （H1-11）直接據此產生選項，而不是在另一處重打一份會走鐘的清單。
    static const std::vector<std::size_t>& allowed_top_n();
    static bool is_allowed_top_n(std::size_t n) noexcept;

    // 某排序維度對應的指標身分（= E2-19 `ProcessMonitorProvider::metric_id()` 的對照）。
    static ds::metrics::MetricId metric_id_for(ds::sysinfo::ProcessSortKey key);
    // 某排序維度的期望單位（Cpu → "%"；Memory → "MB"）。
    static const char* unit_for(ds::sysinfo::ProcessSortKey key) noexcept;
    // 宣告式 `sort_by` 的具名值 → 維度（"cpu" / "memory"）。未知值回 false。
    static bool parse_sort_key(const std::string& s, ds::sysinfo::ProcessSortKey& out);

    // 建構一個具名 widget。`id` 同時衍生各列元素的具名目標 surface（"<id>.rows"）。
    explicit ProcessTopWidget(std::string id);

    const std::string& id() const noexcept { return id_; }

    // --- 設定（E7-01 宣告式；E7-05 公式）---
    //
    // `definition` 須為 Map。認得的欄位（**不認得的鍵一律 Invalid**，不靜默忽略拼錯的鍵）：
    //   sort_by:   **必填**。字串 "cpu" / "memory" —— 排序維度。它決定 `metric_id()`
    //              （proc.cpu ↔ proc.memory）與期望單位（% ↔ MB）。無預設：維度是設定的一部分，
    //              漏寫就該紅，不該靜默採用某一邊。
    //   top_n:     **必填**。非負整數值（型別為 Number、值須為整數），且須為 allowed_top_n()
    //              之一（0 / 3 / 5 / 10；0 = 全部）。
    //   kind:      選填。字串 "bar" / "gauge" / "progress" / "text"（預設 "bar"）。
    //   min/max:   選填，須**成對**提供。數字**或** E7-05 公式字串（如 "= 1024 * 8"）。公式為
    //              **靜態**（求值於 configure()，作用域不含 `value`）。僅一者出現、或求出
    //              min>=max / 非有限 → Invalid；公式求值失敗 → FormulaError。
    //   transform: 選填。E7-05 公式字串（前導 `=` 或 `${ ... }`），如 "= value / 1024"。求值時
    //              `value` 綁定為該列的原始指標值；**必須**符合 `is_formula()`（否則 Invalid），
    //              實際求值延後至 refresh()。
    //
    // 失敗為**全有或全無**：不套用、不改既有設定。成功 → Ok，且先前 refresh() / render() 結果
    // 清空（歷史不保留，需重新 refresh() 才有資料）。
    ProcessTopStatus configure(const ds::format::Value& definition);

    // 同上，但吃 **E7-01 文字宣告**（真正消費 E7-01 的「宣告式格式核心 + 版本欄位」）：
    // 以 `ds::format::parse()` 解析，版本相容性由 E7-01 驗證（根層必含 `format_version`），
    // 再以其 `Document::root`（已去除 format_version 的純內容）走 configure()。
    // 解析 / 版本失敗 → FormatError（行號與訊息見 last_error()，不靜默）。
    ProcessTopStatus configure_text(const std::string& text);

    bool is_configured() const noexcept { return configured_; }
    const ProcessTopSpec& spec() const noexcept { return spec_; }

    // 最近一次失敗的人類可讀原因（含 E7-01 行號 / E7-05 欄位置）；成功時為空。
    // 也在 refresh() 查核到非 Ok 的 ProcessMetricState 時填入原因（不靜默吞錯）。
    const std::string& last_error() const noexcept { return last_error_; }

    // 目前設定的維度所對應的指標 id / 期望單位 —— **即 refresh() 會去 registry 要的那一個**。
    // 未 configure() 時以預設維度（Cpu）回答。
    ds::metrics::MetricId metric_id() const { return metric_id_for(spec_.sort_key); }
    const char* expected_unit() const noexcept { return unit_for(spec_.sort_key); }

    // --- 刷新（經注入 E2-01 指標來源讀值；NFR-03 優雅降級）---
    //
    // 未 configure() → Invalid（不讀取；metric_state() 為 NotConfigured）。否則：
    //   1. 以 `metric_id()`（**由設定維度決定**）向 `source` 查詢；不存在 → state=Missing。
    //   2. `Metric::unit()` 不等於 expected_unit() → state=UnitMismatch（不是 E2-19 掛的那個指標）。
    //   3. 無實例 → state=NoInstances。
    //   4. 否則走訪**全部**實例各成一列，取值 / 單位 / 套用 transform、解出顯示範圍，依原始值
    //      由高到低排序（同值以 instance_id 的 pid 數值遞增為次序，決定性），unavailable 列
    //      置後，再截斷為前 top_n（0 = 全部）。state=Ok。
    //
    // 1–3 的情形**列清空**（誠實的「沒有資料」，而非沿用上一輪的舊值）。個別列降級不中止其餘
    // 列。回傳值表示「刷新流程是否跑完」：除未 configure() 外恆為 Ok；**找到什麼**由
    // metric_state() / render_model().state 具名回報。
    ProcessTopStatus refresh(const ds::metrics::MetricRegistry& source);

    bool has_refreshed() const noexcept { return has_refreshed_; }
    ProcessMetricState metric_state() const noexcept { return state_; }

    // --- 呈現（組裝 E4-03 元素 + 顯示文字）---
    //
    // 尚未 refresh() 過 → Invalid（render_model() 維持前一份，初始為空）。否則對最近一次
    // refresh() 的每列組裝：
    //   - available 且 has_range：依 kind 建立 Bar / Gauge / Progress 元素（Progress 以「值於
    //     顯示範圍內的比例」換算百分比，故對任何維度皆有意義），綁具名 surface "<id>.rows" +
    //     slot = instance_id；Text 種類不建立元素。
    //   - available 但無範圍：不建立元素（見檔首 K-002 說明），仍出格式化文字。
    //   - unavailable：element 維持預設、display_text 為 "—"（降級顯示，不整列消失）。
    ProcessTopStatus render();

    const ProcessTopRenderModel& render_model() const noexcept { return model_; }

    // 最近一次 refresh() 後的列數（已截斷為 top_n）。
    std::size_t row_count() const noexcept { return rows_.size(); }

private:
    // 解析設定 Map 到暫存 spec（不改既有狀態）。
    ProcessTopStatus parse_spec(const ds::format::Value& definition, ProcessTopSpec& out);

    // 解出本次 refresh() 的顯示範圍：宣告式覆寫 > Metric 有界 range() > 無（has_range=false）。
    bool resolve_range(const ds::metrics::MetricRange& metric_range, double& lo, double& hi) const;

    // 清空列與渲染描述（維度切換 / 找不到指標時用：歷史不保留）。
    void clear_rows();

    std::string id_;

    bool configured_ = false;
    ProcessTopSpec spec_;
    std::string last_error_;

    bool has_refreshed_ = false;
    ProcessMetricState state_ = ProcessMetricState::NotConfigured;
    std::vector<ProcessRow> rows_;  // refresh() 產出，render() 消費。

    ProcessTopRenderModel model_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_11_PROCESS_TOP_WIDGET_HPP
