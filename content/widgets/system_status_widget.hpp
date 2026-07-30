// content/widgets/c2_02/system_status_widget.hpp — C2-02 系統狀態 widget（artifact 層 / 相位 1：組裝型 widget）
//
// 語意：桌面「系統狀態」widget —— 可選一組指標（CPU / GPU / RAM / 儲存 / 網路 / 電池 / IO…，
// 任意數量、任意種類）經 **E2-01** 統一指標介面讀取，以 **E4-03** 量表 / 長條 / 進度視覺元素
// 呈現、以 **E4-01** 文字排版顯示標籤與格式化後的值，經 **E7-03**（`vars:` 段落 → 具名常數
// 作用域）與 **E7-05**（公式運算，含每指標可選的 `transform` 值轉換公式，如位元組→GB）宣告式
// 決定顯示哪些指標與如何格式化，並以 **E2-02** 除頻排程對各指標登記其取樣分級需求。掛
// **C1-01**（`SkinProfile`）為桌面基底（具名圖層歸屬 / 輸入策略 / 透明外形 / 自由拖曳皆透傳，
// 本單元不重造）。
//
// **本 widget 是最終 CPU/GPU Usage 驗證器的核心前身**：擴充點組裝正確、指標可選可組是本單元
// 的驗收重點——消費者只依賴 E2-01 的抽象介面（`MetricRegistry::get` / `Metric` / `MetricInstance`），
// 不寫死任何具體感測器；新增一種指標只需在宣告式設定的 `metrics:` 清單多加一筆，widget 本身
// 一行不用改。
//
// **NFR-03 能力閘控 / 優雅降級**：`refresh()` 對每個已設定指標各自查詢 E2-01 的
// `MetricRegistry::get()`（相位 1 為注入式來源，可為 null / stub provider，亦可完全未註冊
// 任何指標）——指標不存在、無實例、目前值 `valid==false`、值非有限（NaN / Inf）、或設定的
// `transform` 公式求值失敗，一律將**該筆**條目標記為 `available=false`（略過 / 降級呈現），
// **絕不擲例外、絕不中止其餘指標的刷新**。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實感測器讀取（`MetricRegistry` 為注入式
// 來源，相位 1 僅記憶體內 / stub 提供者）、無真實字型光柵化（`FontMetrics` 為注入式）、無平台
// 分支（無 `#ifdef` / win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02，全數透傳上游具名模型：
// E4-03 `RenderModel` 的 `surface` + `slot`、E4-01 `LayoutResult` 的相對偏移）。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&` / `ds::kernel::LayerStack&`：透傳給內部的 C1-01 `SkinProfile`
//     基底（同 C1-01 約定）。
//   - `const ds::render::FontMetrics&`：E4-01 排版用字型度量，須存活於本物件之外。
//   - `configure()` / `refresh()` / `sync_demands()` 皆以參數注入方式取得宣告式設定、指標來源
//     （E2-01 `MetricRegistry`）、排程器（E2-02 `SamplingScheduler`），本物件不持有其生命週期。
//
// 命名空間 `ds::widgets`。
#ifndef DS_CONTENT_WIDGETS_C2_02_SYSTEM_STATUS_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_02_SYSTEM_STATUS_WIDGET_HPP

#include <string>
#include <vector>

#include "bar_gauge.hpp"      // E4-03（上游，可讀不可改）：BarElement / GaugeElement / ProgressElement /
                               //   RenderModel / Range（長條 / 量表 / 進度渲染描述）
#include "formula.hpp"        // E7-05（上游，可讀不可改）：Evaluator / EvalResult / is_formula / formula_body
#include "metric.hpp"         // E2-01（上游，可讀不可改）：MetricRegistry / Metric / MetricInstance / MetricId
#include "sampling.hpp"       // E2-02（上游，可讀不可改）：SamplingScheduler / SamplingTier / DemandId
#include "section_vars.hpp"   // E7-03（上游，可讀不可改）：build_scope（也透傳 E7-02 VariableScope）
#include "skin_profile.hpp"   // C1-01（上游，可讀不可改）：SkinProfile（桌面基底）
#include "text_layout.hpp"    // E4-01（上游，可讀不可改）：TextLayout / FontMetrics / LayoutResult

namespace ds::widgets {

// 單一指標於本 widget 的呈現種類（NFR-02：具名，非數字）。Text 表示只顯示格式化文字，不掛
// E4-03 視覺元素（`render()` 產出的該筆 `element` 維持預設 / 未綁定）。
enum class MetricElementKind {
    Bar,
    Gauge,
    Progress,
    Text,
};

const char* to_string(MetricElementKind k) noexcept;

// widget 操作的具名結果（與上游各單元同精神：明確、不靜默）。
enum class SystemStatusStatus {
    Ok,             // 操作成功。
    Invalid,        // 前置條件不滿足：尚未 configure()、宣告式設定結構 / 具名值不合法、
                     // min/max 未成對提供或退化（min>=max）、transform 非合法公式語法等。
    ResolveError,    // E7-03 `vars:` 段落非 Map（build_scope 失敗）；帶肇因於 configure() 內部處理。
    FormulaError,    // configure() 對某指標的 min/max **靜態**公式（不含 `value`）求值失敗
                     // （語法錯誤 / 未定義變數 / 除零 / 型別誤用）。
};

const char* to_string(SystemStatusStatus s) noexcept;

// 一筆已設定的指標選擇（configure() 的解讀結果之一）。
struct MetricEntrySpec {
    ds::metrics::MetricId metric_id;               // E2-01 指標識別碼（如 "cpu.usage"）。必填、非空。
    std::string label;                              // 顯示標籤覆寫；空 = 自動採 Metric::name()。
    MetricElementKind kind = MetricElementKind::Bar;

    bool has_range_override = false;                 // 是否以宣告式 min/max 覆寫顯示範圍。
    double range_min = 0.0;                          // has_range_override 時有效。
    double range_max = 1.0;

    std::string transform;                           // 選填 E7-05 公式（如 "= value / 1073741824"）；
                                                       // 空 = 不轉換，直接用指標原始數值。求值時
                                                       // 變數 `value` 綁定為該指標當次原始值，且可
                                                       // 引用 `vars:` 段落宣告的具名常數。

    ds::metrics::SamplingTier tier = ds::metrics::SamplingTier::Normal;  // E2-02 取樣分級。
};

// 一筆指標於最近一次 refresh() + render() 後的呈現結果。
struct MetricRenderEntry {
    ds::metrics::MetricId metric_id;
    std::string label;                 // 實際生效標籤（覆寫優先，否則 Metric::name()；不可用時為空）。

    // NFR-03：指標不存在 / 無實例 / 值無效 / 值非有限 / transform 公式求值失敗 → false（降級：
    // 不含有效 element / display_text，呼叫端可據此略過或以佔位呈現，不因此中止其餘指標）。
    bool available = false;

    double raw_value = 0.0;            // 指標原始值（unavailable 時為 0）。
    double display_value = 0.0;        // 經 transform（若有）轉換後的值（unavailable 時為 0）。
    std::string unit;                  // E2-01 Metric::unit()（unavailable 時為空）。

    // render() 用的實際顯示範圍（Bar/Gauge 適用；available 時已解出：宣告式覆寫 > Metric 有界
    // range() > 保守預設 [0,100]）。unavailable 時維持預設 [0,1]（不使用）。
    double range_min = 0.0;
    double range_max = 1.0;

    std::string display_text;          // 格式化後文字，如 "62.3%" / "512.0 MB"（unavailable 時為 "—"）。
    ds::elements::RenderModel element;  // E4-03 渲染描述（Bar/Gauge/Progress；Text 種類或 unavailable
                                         //   時維持預設建構、`surface` 為空字串 = 未綁定）。
    ds::render::LayoutResult text;      // E4-01 排版結果（"標籤: 值" 文字的相對佈局）。
};

// 本 widget 目前的完整渲染描述（合成各筆指標的呈現）。純資料，供後續相位的繪製層消費。
struct SystemStatusRenderModel {
    std::vector<MetricRenderEntry> entries;  // 依 configure() 設定順序。
};

// ---------------------------------------------------------------------------
// SystemStatusWidget —— 系統狀態 widget：組裝 C1-01 + E2-01 + E2-02 + E4-01 + E4-03 + E7-03 + E7-05。
//
// 每個實例代表**一個**具名系統狀態 widget（如 "widget.sysstat.main"）。內部持有一個 C1-01
// `SkinProfile` 作為桌面基底、一個 E4-01 `TextLayout`（綁定字型度量與文字 surface）、一份由
// E7-03 `vars:` 段落建成的 E7-02 `VariableScope`（供 E7-05 公式求值引用具名常數）。行為順序：
// `configure()`（選指標清單 + 呈現方式，E7-03/E7-05 宣告式）→ 選擇性 `sync_demands()`（E2-02
// 登記取樣需求）→ `refresh()`（注入 E2-01 指標來源讀值，NFR-03 優雅降級）→ `render()`（組裝
// E4-03 元素 + E4-01 文字渲染描述）。
// ---------------------------------------------------------------------------
class SystemStatusWidget {
public:
    // 建構一個具名系統狀態 widget。
    //   id             widget 的具名識別碼；同時作為內部 C1-01 基底的 SurfaceId，並衍生文字的
    //                  具名目標 surface（"<id>.text"）與各指標元素的具名目標 surface（"<id>.metrics"）。
    //   backend/layers 透傳給內部 C1-01 SkinProfile（見其建構子約定），不取得所有權。
    //   metrics        E4-01 排版用字型度量（不取得所有權，須存活於本物件之外）。
    SystemStatusWidget(std::string id, ds::kernel::KernelBackend& backend,
                        ds::kernel::LayerStack& layers, const ds::render::FontMetrics& metrics);

    // --- C1-01 基底存取（圖層 / 輸入 / 透明外形 / 拖曳皆透傳，本單元不重造）---
    ds::profiles::SkinProfile& base() noexcept { return base_; }
    const ds::profiles::SkinProfile& base() const noexcept { return base_; }

    const std::string& id() const noexcept { return id_; }

    // --- 設定（選指標清單 + 呈現方式；E7-03 vars 段落 + E7-05 公式）---
    //
    // `definition` 須為 Map，解讀欄位：
    //   vars:     選填。Map（E7-03 變數段落）：宣告具名常數（如 `gb: 1073741824`），供本設定
    //             其餘欄位的 min/max **靜態**公式，以及每指標 transform 公式（連同動態 `value`）
    //             引用。非 Map → ResolveError（E7-03 build_scope 失敗，不套用、不改既有設定）。
    //   metrics:  **必填**。List（可為空清單 = 「空選擇」，合法，代表本 widget 目前不顯示任何
    //             指標）。每筆須為 Map，欄位：
    //     id:        字串，E2-01 指標識別碼。必填、非空。
    //     label:     字串，顯示標籤覆寫。選填。
    //     kind:      字串，"bar" / "gauge" / "progress" / "text"（預設 "bar"）。
    //     min/max:   數字**或** E7-05 公式字串（如 "= gb / 1024"，可引用 vars 常數，**不含**
    //                `value`——min/max 為靜態、非依當次指標值變動）。須成對提供（僅一者出現、
    //                或求出 min>=max）→ Invalid；公式語法/ 求值失敗 → FormulaError。缺此二欄
    //                → 顯示時退回 Metric::range()（若有界）或保守預設 [0,100]。
    //     transform: 字串，E7-05 公式（前導 `=` 或 `${ ... }`），如 "= value / gb"。求值時變數
    //                `value` 綁定為該指標當次原始值。**必須**符合 `is_formula()` 語法（否則
    //                Invalid）；實際求值延後至 refresh()（因 `value` 僅於當次讀值後可知）。選填，
    //                空 = 不轉換。
    //     tier:      字串，"high" / "normal" / "low" / "on-demand"（預設 "normal"）；E2-02
    //                取樣分級，供 sync_demands() 登記。
    //   `metrics` 缺失、非 List、任一項目非 Map / 型別 / 具名值不合法 → Invalid（不套用、不改
    //   既有設定，全有或全無）。
    //
    // 成功 → Ok：visible_metrics() 反映新選擇；先前 refresh() / render() 結果清空（需重新
    // refresh() 才有資料）。
    SystemStatusStatus configure(const ds::format::Value& definition);

    bool is_configured() const noexcept { return configured_; }

    // 目前已設定、依序顯示的指標清單（可能為空，見 configure() 的「空選擇」語意）。
    const std::vector<MetricEntrySpec>& visible_metrics() const noexcept { return entries_; }

    // --- E2-02 取樣需求登記（除頻）---
    //
    // 為每個已設定指標，依其 tier 對給定 scheduler 登記一筆需求（除頻合併由 E2-02 負責）。
    // 冪等：若先前已對某個 scheduler 登記過需求，會先撤銷該批舊需求（`release_demands`）再對
    // 新給定的 scheduler 重新登記——重複呼叫、或切換 scheduler 皆安全。未 configure() 或
    // visible_metrics() 為空 → no-op（僅釋放舊需求，不登記新的）。
    void sync_demands(ds::metrics::SamplingScheduler& scheduler);

    // 撤銷本 widget 先前經 sync_demands() 登記的所有需求（若有）。無先前登記 → no-op。
    void release_demands();

    // --- 刷新（經注入 E2-01 指標來源讀值；NFR-03 優雅降級）---
    //
    // 未 configure() → Invalid（不讀取、既有結果不變）。否則對 visible_metrics() 逐筆向
    // `source` 查詢：
    //   - `source.get(id)` 為 nullptr、或指標 `instance_count()==0` → 該筆 available=false。
    //   - 否則取其第 0 個實例的 value()；`valid==false` 或數值非有限（NaN/Inf）→ available=false。
    //   - 否則 raw_value/unit 填入；若設定了 transform，以（`value`=raw_value，父作用域為
    //     configure() 建立的 vars 作用域）求值：求值失敗或結果非數值 → available=false；
    //     否則 display_value = 求值結果，否則 display_value = raw_value。
    // 個別指標降級**不**中止其餘指標的刷新，整體回傳恆為 Ok（除非 Invalid 前置條件）。
    // 結果經 render() 消費以組裝渲染描述。
    SystemStatusStatus refresh(const ds::metrics::MetricRegistry& source);

    bool has_refreshed() const noexcept { return has_refreshed_; }

    // --- 呈現（組裝 E4-03 元素 + E4-01 文字渲染描述）---
    //
    // 尚未 refresh() 過 → Invalid（render_model() 維持前一份，初始為空）。否則對最近一次
    // refresh() 的每筆結果組裝：
    //   - available 指標：依 kind 建立 Bar/Gauge/Progress 元素（Text 種類不建立元素），套用
    //     range_min/max（或退回 Metric 範圍 / 保守預設）、display_value；以 E4-01 排版
    //     "<標籤>: <格式化值><單位>" 文字。
    //   - unavailable 指標：element 維持預設（未綁定 surface）、display_text 為 "—"，仍以
    //     E4-01 排版佔位文字（讓呼叫端可統一走文字繪製路徑呈現「未知」，符合 NFR-03「降級
    //     顯示」而非整筆消失）。
    // 成功 → Ok，render_model() 反映新結果。
    SystemStatusStatus render();

    const SystemStatusRenderModel& render_model() const noexcept { return model_; }

private:
    struct DemandTicket {
        ds::metrics::MetricId metric_id;
        ds::metrics::DemandId demand_id;
    };

    SystemStatusStatus parse_entry(const ds::format::Value& item,
                                    const ds::format::VariableScope& scope, MetricEntrySpec& out,
                                    std::string& err);

    std::string id_;
    ds::profiles::SkinProfile base_;  // C1-01：桌面基底（圖層 / 輸入 / 透明外形 / 拖曳）。

    const ds::render::FontMetrics& metrics_;
    ds::render::TextLayout layout_;  // E4-01：自持，綁定 metrics_ 與 "<id>.text" 目標 surface。

    bool configured_ = false;
    std::vector<MetricEntrySpec> entries_;
    ds::format::VariableScope vars_;  // E7-03 build_scope() 建成；供 E7-05 公式求值的具名常數。

    bool has_refreshed_ = false;
    std::vector<MetricRenderEntry> last_refresh_;  // refresh() 產出，render() 消費（依 entries_ 順序）。

    ds::metrics::SamplingScheduler* demand_scheduler_ = nullptr;  // 非擁有；sync_demands() 綁定對象。
    std::vector<DemandTicket> demand_tickets_;

    SystemStatusRenderModel model_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_02_SYSTEM_STATUS_WIDGET_HPP
