// content/widgets/c2_01/clock_widget.hpp — C2-01 時鐘 widget（artifact/widgets 層 / 相位 1）
//
// 「時鐘 widget」：把當前時間以文字排版顯示在桌面角色皮膚（C1-01 基底）之上的 widget。本單元
// 不是新引擎邏輯，而是把四個已合併的擴充點**組裝**成一個具體 widget + 行為：
//
//   - C1-01（`ds::profiles::SkinProfile`）：**掛載基底** —— 時鐘顯示綁定於一個**已載入**的
//     skin profile 之具名 surface（`base.id()`），不自建 surface / 圖層 / 輸入策略，全數透傳
//     C1-01 已組裝完成的桌面角色基底。
//   - E2-10（`ds::sysinfo::TimeSource` / `CivilTime` / `civil_from_epoch_seconds`）：**時間指標
//     來源** —— 消費**注入式**時間來源（`TimeSource`），相位 1 無真實壁鐘，測試 / 預設一律以
//     `FixedTimeSource` 餵入固定時間點，取樣**決定性**。不重造曆法分解，直接沿用 E2-10 的純算術
//     UTC civil-time 分解。
//   - E4-01（`ds::render::TextLayout` / `FontMetrics`）：**文字排版** —— 把格式化後的時間字串
//     交給注入式 `FontMetrics` 排版為相對佈局的 `LayoutResult`（NFR-02：無絕對座標），目標具名
//     surface 綁定為基底 `base.id()`。
//   - E7-01（`ds::format::Value`）：**宣告式格式定義** —— `configure(Value)` 消費一份宣告式文件
//     的內容根（Map），解讀 12/24 時制、是否顯示秒、對齊、排版寬度等顯示樣式欄位。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` / win32 /
// cocoa）、無絕對座標 / 數字 z-order（NFR-02）。時間一律經注入式 `TimeSource` 取得，**不呼叫
// 任何 wall-clock（`system_clock::now()` 等）**。無效操作（未 configure 即 tick / refresh、宣告式
// 定義結構 / 型別 / 具名值不合法、基底 skin 未載入、缺時間來源）一律明確回傳具名結果，不靜默。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::profiles::SkinProfile&`：掛載基底，須為呼叫端已 `load_skin()` 成功之物件。
//   - `std::shared_ptr<ds::sysinfo::TimeSource>`：注入式時間來源（可為空，代表「無時間來源」）。
//   - `ds::render::FontMetrics&`：文字排版度量來源。
#ifndef DS_CONTENT_WIDGETS_C2_01_CLOCK_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_01_CLOCK_WIDGET_HPP

#include <memory>
#include <string>

#include "document.hpp"       // E7-01（上游，可讀不可改）：Value（宣告式時鐘格式定義）
#include "skin_profile.hpp"   // C1-01（上游，可讀不可改）：SkinProfile（掛載基底）
#include "text_layout.hpp"    // E4-01（上游，可讀不可改）：TextLayout / FontMetrics / LayoutResult
#include "time_date.hpp"      // E2-10（上游，可讀不可改）：TimeSource / CivilTime / civil_from_*

namespace ds::widgets {

// 時鐘 widget 的具名生命週期狀態（NFR-02：具名，非數字）。
enum class ClockState {
    Unconfigured,  // 尚未 configure：無顯示樣式、無取樣、display_text 為空、layout 為空結果。
    Configured,    // 已 configure：顯示樣式已套用；至少完成過一次 tick 後 display_text 才有值。
};

const char* to_string(ClockState s) noexcept;

// 時鐘 widget 操作的具名結果（不靜默；與上游 C1-01 `SkinStatus` 同風格）。
enum class ClockStatus {
    Ok,             // 操作成功。
    Invalid,        // 前置條件不滿足：宣告式定義結構 / 型別 / 具名值不合法、空 id 等。
    Unsupported,    // 掛載基底未載入（無可綁定之具名 surface）、或無注入時間來源。
    NotConfigured,  // 尚未 configure 即 tick() / refresh()。
};

const char* to_string(ClockStatus s) noexcept;

// 12/24 時制（NFR-02：具名，非布林 / 數字旗標的裸用）。
enum class ClockHourFormat {
    H24,  // 24 小時制，"HH:MM[:SS]"。
    H12,  // 12 小時制，"hh:MM[:SS] AM|PM"。
};

const char* to_string(ClockHourFormat f) noexcept;

// ---------------------------------------------------------------------------
// ClockWidget —— 時鐘 widget：組裝 C1-01 + E2-10 + E4-01 + E7-01。
//
// 每個實例代表**一個**掛載於指定基底 surface 上的時鐘顯示。相位 1：時間一律經注入式
// `TimeSource` 取得（無真實壁鐘）；排版一律經注入式 `FontMetrics`（無真實字型引擎）。
// ---------------------------------------------------------------------------
class ClockWidget {
public:
    // 建構一個具名時鐘 widget。
    //   - base：掛載基底（C1-01），不取得所有權；本 widget 的排版 surface 綁定其 `base.id()`。
    //   - time_source：注入式時間來源（E2-10）；可為 nullptr（代表「無時間來源」，tick() 回
    //     Unsupported）。
    //   - font_metrics：文字排版度量（E4-01），不取得所有權，須存活於本物件之外的生命週期內。
    ClockWidget(std::string id, ds::profiles::SkinProfile& base,
                std::shared_ptr<ds::sysinfo::TimeSource> time_source,
                const ds::render::FontMetrics& font_metrics);

    // --- 宣告式設定（E7-01）---

    // 從宣告式時鐘定義（E7-01 `Value`，須為 Map；通常為 `Document::root`）套用顯示樣式。
    // 解讀的欄位（皆為選填，缺者用預設；未知鍵忽略）：
    //   format:  "24h"（預設）| "12h"
    //   seconds: bool（預設 true）—— 是否顯示秒。
    //   align:   "left"（預設）| "center" | "right" —— E4-01 TextAlign。
    //   width:   非負有限數字（預設 0 = 無界寬度）—— E4-01 LayoutConstraints.max_width。
    // 流程（全有或全無）：
    //   - id 為空 → Invalid。
    //   - 定義非 Map、任一已知欄位型別 / 具名值不合法 → Invalid，且**不改任何狀態**（不靜默）。
    //   - 掛載基底未載入（`!base.is_loaded()`）→ Unsupported，不提交任何設定。
    //   - 全數成功 → 提交顯示樣式、state() 轉為 Configured，並立即嘗試取樣一次（等同內部呼叫
    //     `tick()`；若無時間來源則 display_text() 暫為空、layout 亦為空結果，待呼叫端另行 tick()）。
    //     回傳值恆為 Ok（時間來源不可用不影響 configure 本身成功）。
    ClockStatus configure(const ds::format::Value& definition);

    // --- 時間更新（E2-10 注入式時間來源）---

    // 重新取樣注入的時間來源，更新目前時間、格式化 display_text()，並重新排版（等同接著呼叫
    // refresh()）。未 configure → NotConfigured。無注入時間來源（nullptr）→ Unsupported，
    // 不改變既有 display_text() / layout_result()。成功 → Ok。
    ClockStatus tick();

    // --- 重新排版（E4-01）---

    // 以目前的 display_text() 與目前顯示樣式（align / width）重新跑一次 E4-01 排版，更新
    // layout_result()。**不重新取樣時間**（純排版重算，供樣式相關外部狀態變動後使用）。
    // 未 configure → NotConfigured。成功 → Ok。
    ClockStatus refresh();

    // --- 查詢 ---

    // 目前格式化後的時間顯示文字（尚未 tick 過 → 空字串）。
    const std::string& display_text() const noexcept { return display_text_; }

    // 目前的排版結果（E4-01；尚未 refresh 過 → 預設空結果）。
    const ds::render::LayoutResult& layout_result() const noexcept { return layout_result_; }

    ClockState state() const noexcept { return state_; }
    bool is_configured() const noexcept { return state_ == ClockState::Configured; }
    const std::string& id() const noexcept { return id_; }

    // 目前 / 期望的顯示樣式（configure 前為建構預設）。
    ClockHourFormat hour_format() const noexcept { return hour_format_; }
    bool show_seconds() const noexcept { return show_seconds_; }
    ds::render::TextAlign align() const noexcept { return align_; }
    double max_width() const noexcept { return max_width_; }

    // 是否已至少成功取樣過一次時間（tick 或 configure 內建的初次取樣）。
    bool has_sampled() const noexcept { return has_sampled_; }

private:
    // 把目前時間格式化為顯示文字（依 hour_format_ / show_seconds_）。
    std::string format_display(const ds::sysinfo::CivilTime& c) const;

    // 以目前 display_text_ 與顯示樣式重新排版（refresh 的實作核心；tick 內部亦呼叫）。
    void relayout();

    std::string id_;
    ds::profiles::SkinProfile& base_;
    std::shared_ptr<ds::sysinfo::TimeSource> time_source_;
    const ds::render::FontMetrics& font_metrics_;

    ClockState state_ = ClockState::Unconfigured;

    // 顯示樣式（configure 依宣告式定義覆寫）。
    ClockHourFormat hour_format_ = ClockHourFormat::H24;
    bool show_seconds_ = true;
    ds::render::TextAlign align_ = ds::render::TextAlign::Left;
    double max_width_ = 0.0;  // 0 = 無界（見 E4-01 LayoutConstraints.max_width 語意）

    bool has_sampled_ = false;
    ds::sysinfo::CivilTime last_civil_{};
    std::string display_text_;
    ds::render::LayoutResult layout_result_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_01_CLOCK_WIDGET_HPP
