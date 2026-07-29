// E9-08 圖形安裝器 — 圖形化安裝流程的資料模型 / 狀態機（engine 層 / package 子系統）
//
// 語意：把 E9-07 的安裝生命週期（Received → Resolved → Parsed → Validated → Registered）
// 包裝成適合 UI 呈現的**多步驟安裝精靈模型**。這**不是**真實 GUI——相位 1 只做平台中立、
// 純記憶體的狀態機：步驟列（歡迎 / 授權 / 選項 / 進度 / 完成）、每步呈現狀態、可前進 / 後退 /
// 取消、進度百分比與訊息，並把使用者選項（安裝路徑 / 元件勾選）餵給 E9-07 安裝。
//
// 與 E9-07 的關係（genuine link）：
//   - `InstallWizard` 建構時注入一個 E9-07 `DropInstaller`。`begin_install(source, options)` 於
//     「進度」步驟**實際委派** `DropInstaller::handle_drop`——把使用者選項組成 `DropEvent`（選項
//     摘要放入 `DropEvent.detail`，E9-07 原樣攜帶）並跑完 E9-07 安裝生命週期。
//   - 進度回報是**真連結**：begin_install 期間於 `DropInstaller` 掛上 E9-07 `InstallLifecycleListener`，
//     將每個 `InstallStage` 映射為精靈的進度百分比與訊息（實際使用 E9-07 的逐階段回報路徑）。
//
// 設計原則（與 E9-07 / E9-01 一致）：
//   - **命名空間 `ds::package`**（與上游一致）。
//   - **平台中立、純邏輯**：無 `#ifdef` / `win32` / `cocoa`、無真實 GUI、無真實檔案 I/O。
//   - **錯誤 / 取消明確回報、不得靜默**：非法步驟轉移、授權未接受、取消、安裝失敗一律以明確
//     回傳值（`StepTransition` / `InstallResult` / `outcome()`）表達，絕不靜默吞掉。
//   - 相位 2 換真實 GUI 時：本狀態機一行不動，只需由真實 UI 事件驅動既有 API。
#ifndef DS_ENGINE_E9_08_INSTALL_WIZARD_HPP
#define DS_ENGINE_E9_08_INSTALL_WIZARD_HPP

#include <functional>
#include <string>
#include <vector>

#include "drop_install.hpp"  // E9-07（PUBLIC 傳遞：DropInstaller / DropEvent / InstallSource / InstallResult / InstallStage 等）

namespace ds::package {

// 安裝精靈的步驟。固定序列，對應 UI 的多步驟頁面。
enum class WizardStep {
    Welcome,   // 歡迎
    License,   // 授權（須接受才能前進）
    Options,   // 選項（安裝路徑 / 元件勾選）
    Progress,  // 進度（執行安裝，委派 E9-07）
    Complete,  // 完成
};

// 單一步驟的呈現狀態（供 UI 畫步驟列）。
enum class StepState {
    Upcoming,  // 尚未到達
    Active,    // 目前所在
    Done,      // 已完成並離開
};

// 精靈整體結果。
enum class WizardOutcome {
    InProgress,  // 進行中（尚未安裝完成 / 失敗 / 取消）
    Installed,   // 安裝成功完成（位於 Complete 步驟）
    Failed,      // 安裝失敗（停在 Progress 步驟；可後退至 Options 重試或取消）
    Cancelled,   // 使用者取消（終態）
};

// 使用者於「選項」步驟提供的安裝選項。平台中立字串，相位 1 不做任何真實檔案 I/O。
struct InstallOptions {
    std::string install_path;                       // 安裝路徑（僅作為選項資料 / 回報摘要）。
    std::vector<std::string> selected_components;    // 勾選的元件邏輯名稱清單。
};

// 步驟轉移結果。ok=false 時 message 給出明確原因（不靜默）。step 為轉移後（或維持）的步驟。
struct StepTransition {
    bool ok = false;
    WizardStep step = WizardStep::Welcome;
    std::string message;
    explicit operator bool() const noexcept { return ok; }
};

// 進度回報快照（供 UI 更新進度條 / 訊息）。
struct WizardProgress {
    WizardStep step = WizardStep::Progress;
    int percent = 0;            // 0..100。
    std::string message;        // 人類可讀階段訊息（成功階段或失敗原因）。
    bool failed = false;        // true = 此回報對應一個安裝失敗階段。
};

// 進度觀察者回呼；於 begin_install 期間每個生命週期階段轉換時被呼叫。
using WizardProgressListener = std::function<void(const WizardProgress&)>;

// 圖形安裝器精靈：把 E9-07 安裝生命週期包裝為多步驟精靈狀態機。
//
// 建構時注入一個 E9-07 `DropInstaller`（不持有所有權，生命週期須長於本物件）。
class InstallWizard {
public:
    explicit InstallWizard(DropInstaller& installer);

    InstallWizard(const InstallWizard&) = delete;
    InstallWizard& operator=(const InstallWizard&) = delete;

    // ---- 步驟查詢 ----------------------------------------------------------

    // 完整步驟序列（固定：Welcome→License→Options→Progress→Complete）。
    std::vector<WizardStep> steps() const;

    // 目前所在步驟。
    WizardStep current_step() const noexcept { return current_; }

    // 指定步驟相對目前進度的呈現狀態（供 UI 畫步驟列）。
    StepState step_state(WizardStep step) const;

    // 精靈整體結果。
    WizardOutcome outcome() const noexcept { return outcome_; }

    // ---- 授權步驟 ----------------------------------------------------------

    // 使用者接受 / 取消接受授權（授權步驟的前進閘門）。
    void set_license_accepted(bool accepted) noexcept { license_accepted_ = accepted; }
    void accept_license() noexcept { license_accepted_ = true; }
    bool license_accepted() const noexcept { return license_accepted_; }

    // ---- 選項步驟 ----------------------------------------------------------

    // 暫存使用者選項（begin_install 未帶選項時採用之）。
    void set_options(InstallOptions options) { options_ = std::move(options); }
    const InstallOptions& options() const noexcept { return options_; }

    // ---- 導覽 --------------------------------------------------------------

    // 前進一步。Welcome→License→Options 為單純導覽（License 須先接受授權，否則明確報錯）。
    // 於 Options 步驟不由 next() 進入安裝——須改呼叫 begin_install（會明確回報指引）。
    // 於終態 / 進度中呼叫 → 明確報錯（無效步驟轉移）。
    StepTransition next();

    // 後退一步。Options→License→Welcome；安裝失敗後可自 Progress 退回 Options 重試（重置為進行中）。
    // 於首步 / 終態呼叫 → 明確報錯。
    StepTransition back();

    // 取消整個精靈（任何非終態步驟皆可）。設為 Cancelled 終態；其後導覽 / 安裝一律明確報錯。
    StepTransition cancel();

    // ---- 安裝（委派 E9-07）-------------------------------------------------

    // 進度觀察者（可選）；傳空以清除。
    void set_progress_listener(WizardProgressListener listener);

    // 於「選項」步驟執行安裝：以 source + options 組成 E9-07 `DropEvent` 並委派
    // `DropInstaller::handle_drop`，期間逐階段回報進度。成功 → 前進至 Complete（outcome=Installed）；
    // 失敗 → 停於 Progress（outcome=Failed，progress 反映失敗階段）。回傳 E9-07 的 `InstallResult`。
    // 非於 Options 步驟 / 已取消 / 已完成時呼叫 → 回傳明確失敗的 InstallResult（不靜默、不委派）。
    InstallResult begin_install(const InstallSource& source, const InstallOptions& options);

    // 便捷多載：採用先前 set_options 暫存的選項。
    InstallResult begin_install(const InstallSource& source);

    // 最近一次 begin_install 的結果（尚未安裝過時為預設 Failed/Received、message 空）。
    const InstallResult& last_result() const noexcept { return last_result_; }

    // ---- 進度查詢 ----------------------------------------------------------

    int progress_percent() const noexcept { return progress_percent_; }
    const std::string& progress_message() const noexcept { return progress_message_; }

private:
    // 步驟在固定序列中的索引（0..4）。
    static int index_of(WizardStep step) noexcept;

    // 把 E9-07 InstallStage 映射為進度百分比。
    static int percent_for_stage(InstallStage stage) noexcept;

    // 更新進度快照並通知觀察者（不靜默）。
    void emit_progress(WizardStep step, int percent, const std::string& message, bool failed);

    // 由選項組出 E9-07 DropEvent 的 detail 摘要（平台中立字串）。
    static std::string detail_from_options(const InstallOptions& options);

    DropInstaller& installer_;
    WizardStep current_ = WizardStep::Welcome;
    WizardOutcome outcome_ = WizardOutcome::InProgress;
    bool license_accepted_ = false;
    InstallOptions options_;
    WizardProgressListener progress_listener_;
    int progress_percent_ = 0;
    std::string progress_message_;
    InstallResult last_result_;
};

}  // namespace ds::package

#endif  // DS_ENGINE_E9_08_INSTALL_WIZARD_HPP
