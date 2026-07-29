// E9-08 圖形安裝器 — 實作（平台中立、純邏輯，無 GUI、無真實檔案 I/O、無平台分支）。
#include "install_wizard.hpp"

#include <string>
#include <utility>

namespace ds::package {

namespace {

// 固定步驟序列。索引即順序。
constexpr WizardStep kSteps[] = {
    WizardStep::Welcome,  WizardStep::License,  WizardStep::Options,
    WizardStep::Progress, WizardStep::Complete,
};
constexpr int kStepCount = 5;

}  // namespace

InstallWizard::InstallWizard(DropInstaller& installer) : installer_(installer) {}

int InstallWizard::index_of(WizardStep step) noexcept {
    for (int i = 0; i < kStepCount; ++i) {
        if (kSteps[i] == step) {
            return i;
        }
    }
    return 0;  // 不可達（列舉封閉）；防禦性回傳首步索引。
}

std::vector<WizardStep> InstallWizard::steps() const {
    return std::vector<WizardStep>(kSteps, kSteps + kStepCount);
}

StepState InstallWizard::step_state(WizardStep step) const {
    const int target = index_of(step);
    const int cur = index_of(current_);
    if (target < cur) {
        return StepState::Done;
    }
    if (target == cur) {
        return StepState::Active;
    }
    return StepState::Upcoming;
}

// ---- 導覽 -------------------------------------------------------------------

StepTransition InstallWizard::next() {
    if (outcome_ == WizardOutcome::Cancelled) {
        return StepTransition{false, current_, "精靈已取消：無法前進"};
    }
    if (outcome_ == WizardOutcome::Installed) {
        return StepTransition{false, current_, "安裝已完成：無法再前進"};
    }
    switch (current_) {
        case WizardStep::Welcome:
            current_ = WizardStep::License;
            return StepTransition{true, current_, {}};
        case WizardStep::License:
            if (!license_accepted_) {
                return StepTransition{false, current_, "尚未接受授權條款：無法前進"};
            }
            current_ = WizardStep::Options;
            return StepTransition{true, current_, {}};
        case WizardStep::Options:
            // 進入安裝須經 begin_install（攜帶來源與選項），非單純導覽。
            return StepTransition{false, current_,
                                  "無效步驟轉移：請以 begin_install() 啟動安裝，而非 next()"};
        case WizardStep::Progress:
            // 進度→完成由 begin_install 於成功時自動完成；失敗停於此、不得以 next() 前進。
            return StepTransition{false, current_,
                                  "無效步驟轉移：安裝進度由 begin_install 驅動"};
        case WizardStep::Complete:
            return StepTransition{false, current_, "已在最後一步：無法前進"};
    }
    return StepTransition{false, current_, "無效步驟"};
}

StepTransition InstallWizard::back() {
    if (outcome_ == WizardOutcome::Cancelled) {
        return StepTransition{false, current_, "精靈已取消：無法後退"};
    }
    if (outcome_ == WizardOutcome::Installed) {
        return StepTransition{false, current_, "安裝已完成：無法後退"};
    }
    switch (current_) {
        case WizardStep::Welcome:
            return StepTransition{false, current_, "已在第一步：無法後退"};
        case WizardStep::License:
            current_ = WizardStep::Welcome;
            return StepTransition{true, current_, {}};
        case WizardStep::Options:
            current_ = WizardStep::License;
            return StepTransition{true, current_, {}};
        case WizardStep::Progress:
            // 安裝失敗後可退回選項重試；重置為進行中並清除失敗進度。
            current_ = WizardStep::Options;
            outcome_ = WizardOutcome::InProgress;
            progress_percent_ = 0;
            progress_message_.clear();
            return StepTransition{true, current_, {}};
        case WizardStep::Complete:
            return StepTransition{false, current_, "已完成：無法後退"};
    }
    return StepTransition{false, current_, "無效步驟"};
}

StepTransition InstallWizard::cancel() {
    if (outcome_ == WizardOutcome::Cancelled) {
        return StepTransition{false, current_, "精靈已取消"};
    }
    if (outcome_ == WizardOutcome::Installed) {
        return StepTransition{false, current_, "安裝已完成：無可取消"};
    }
    outcome_ = WizardOutcome::Cancelled;
    return StepTransition{true, current_, "已取消安裝精靈"};
}

// ---- 安裝（委派 E9-07）------------------------------------------------------

void InstallWizard::set_progress_listener(WizardProgressListener listener) {
    progress_listener_ = std::move(listener);
}

int InstallWizard::percent_for_stage(InstallStage stage) noexcept {
    switch (stage) {
        case InstallStage::Received:
            return 10;
        case InstallStage::Resolved:
            return 30;
        case InstallStage::Parsed:
            return 55;
        case InstallStage::Validated:
            return 80;
        case InstallStage::Registered:
            return 100;
    }
    return 0;
}

void InstallWizard::emit_progress(WizardStep step, int percent, const std::string& message,
                                  bool failed) {
    progress_percent_ = percent;
    progress_message_ = message;
    if (progress_listener_) {
        progress_listener_(WizardProgress{step, percent, message, failed});
    }
}

std::string InstallWizard::detail_from_options(const InstallOptions& options) {
    // 平台中立字串摘要；E9-07 DropEvent.detail 原樣攜帶（人類可讀補充說明）。
    std::string detail = "install_path=" + options.install_path;
    detail += "; components=";
    for (std::size_t i = 0; i < options.selected_components.size(); ++i) {
        if (i != 0) {
            detail += ",";
        }
        detail += options.selected_components[i];
    }
    return detail;
}

InstallResult InstallWizard::begin_install(const InstallSource& source,
                                           const InstallOptions& options) {
    // 步驟 / 狀態閘門：僅可於 Options 步驟、進行中時啟動安裝；否則明確報錯、不委派、不靜默。
    if (outcome_ == WizardOutcome::Cancelled) {
        InstallResult r;
        r.status = InstallStatus::Failed;
        r.stage = InstallStage::Received;
        r.message = "精靈已取消：不執行安裝";
        last_result_ = r;
        return r;
    }
    if (outcome_ == WizardOutcome::Installed) {
        InstallResult r;
        r.status = InstallStatus::Failed;
        r.stage = InstallStage::Received;
        r.message = "安裝已完成：不重複執行";
        last_result_ = r;
        return r;
    }
    if (current_ != WizardStep::Options) {
        InstallResult r;
        r.status = InstallStatus::Failed;
        r.stage = InstallStage::Received;
        r.message = "無效步驟轉移：begin_install 僅可於 Options 步驟呼叫";
        last_result_ = r;
        return r;
    }

    options_ = options;

    // 進入「進度」步驟。
    current_ = WizardStep::Progress;
    outcome_ = WizardOutcome::InProgress;
    progress_percent_ = 0;
    progress_message_.clear();

    // 掛上 E9-07 生命週期觀察者：實際使用 E9-07 逐階段回報路徑，映射為精靈進度。
    installer_.set_lifecycle_listener([this](const InstallLifecycleEvent& ev) {
        const int percent = percent_for_stage(ev.stage);
        emit_progress(WizardStep::Progress, percent, ev.message, /*failed=*/!ev.ok);
    });

    // 組出 E9-07 DropEvent 並**實際委派** handle_drop（跑完 E9-07 安裝生命週期）。
    DropEvent drop;
    drop.source = &source;
    drop.detail = detail_from_options(options_);
    const InstallResult result = installer_.handle_drop(drop);

    // 解除生命週期觀察者（避免懸掛回呼捕捉 this）。
    installer_.set_lifecycle_listener(nullptr);

    last_result_ = result;
    if (result.ok()) {
        current_ = WizardStep::Complete;
        outcome_ = WizardOutcome::Installed;
        emit_progress(WizardStep::Complete, 100, "安裝完成：" + result.package_name,
                      /*failed=*/false);
    } else {
        // 失敗：停於 Progress，明確標記失敗（不靜默、不前進至 Complete）。
        outcome_ = WizardOutcome::Failed;
        // 進度已由生命週期回報反映失敗階段；再明確設一次以防無觀察者路徑。
        progress_percent_ = percent_for_stage(result.stage);
        progress_message_ = result.message;
    }
    return result;
}

InstallResult InstallWizard::begin_install(const InstallSource& source) {
    return begin_install(source, options_);
}

}  // namespace ds::package
