// E9-08 圖形安裝器 — 契約測試（gtest）
//
// 涵蓋：步驟序列與初始狀態、前進/後退導覽、授權閘門、取消、選項傳遞、
// begin_install 委派 E9-07 成功/失敗、逐階段進度回報、無效步驟轉移報錯、完成狀態終態。
// 平台中立：不含任何平台分支、無真實 GUI / 檔案 I/O。
#include "install_wizard.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "drop_install.hpp"
#include "system_event.hpp"

using ds::events::NullSystemEventSource;
using ds::package::DropInstaller;
using ds::package::InstallOptions;
using ds::package::InstallRegistry;
using ds::package::InstallStatus;
using ds::package::InstallWizard;
using ds::package::MemoryInstallSource;
using ds::package::StepState;
using ds::package::WizardOutcome;
using ds::package::WizardProgress;
using ds::package::WizardStep;

namespace {

// 一份最小合法套件描述（manifest + 內容清單）。
std::string valid_package_text(const std::string& name) {
    return "format_version: 1.0\n"
           "name: " + name + "\n"
           "requires: host.tray_icon\n"
           "---\n"
           "asset: icons/tray.png\n"
           "code: main.wasm\n";
}

// 測試夾具：組好 registry + null 系統事件 + DropInstaller + InstallWizard。
struct WizardFixture {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer{registry, sys};
    InstallWizard wizard{installer};
};

// 導覽輔助：走到 Options 步驟（Welcome→License→接受授權→Options）。
void advance_to_options(InstallWizard& w) {
    ASSERT_TRUE(w.next());  // Welcome -> License
    w.accept_license();
    ASSERT_TRUE(w.next());  // License -> Options
    ASSERT_EQ(w.current_step(), WizardStep::Options);
}

// 步驟序列固定，初始位於 Welcome、進行中。
TEST(InstallWizard, StepsListedInOrderAndInitialState) {
    WizardFixture f;
    const std::vector<WizardStep> expected = {
        WizardStep::Welcome, WizardStep::License, WizardStep::Options,
        WizardStep::Progress, WizardStep::Complete};
    EXPECT_EQ(f.wizard.steps(), expected);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Welcome);
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::InProgress);
    EXPECT_EQ(f.wizard.progress_percent(), 0);
}

// 前進：Welcome→License→Options（授權接受後）。
TEST(InstallWizard, NextAdvancesThroughSteps) {
    WizardFixture f;
    auto t1 = f.wizard.next();
    EXPECT_TRUE(t1);
    EXPECT_EQ(t1.step, WizardStep::License);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::License);

    f.wizard.accept_license();
    auto t2 = f.wizard.next();
    EXPECT_TRUE(t2);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Options);
}

// 授權閘門：未接受授權時 next() 於 License 明確報錯、不前進。
TEST(InstallWizard, LicenseGateBlocksWithoutAccept) {
    WizardFixture f;
    ASSERT_TRUE(f.wizard.next());  // -> License
    EXPECT_FALSE(f.wizard.license_accepted());

    auto blocked = f.wizard.next();
    EXPECT_FALSE(blocked);
    EXPECT_FALSE(blocked.message.empty());
    EXPECT_EQ(f.wizard.current_step(), WizardStep::License);  // 未前進。

    f.wizard.accept_license();
    EXPECT_TRUE(f.wizard.next());
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Options);
}

// 後退：Options→License→Welcome。
TEST(InstallWizard, BackReturnsToPreviousStep) {
    WizardFixture f;
    advance_to_options(f.wizard);

    auto b1 = f.wizard.back();
    EXPECT_TRUE(b1);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::License);

    auto b2 = f.wizard.back();
    EXPECT_TRUE(b2);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Welcome);
}

// 後退：於第一步 back() 明確報錯。
TEST(InstallWizard, BackAtFirstStepErrors) {
    WizardFixture f;
    auto b = f.wizard.back();
    EXPECT_FALSE(b);
    EXPECT_FALSE(b.message.empty());
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Welcome);
}

// 步驟呈現狀態：位於 Options 時，前面 Done、當前 Active、後面 Upcoming。
TEST(InstallWizard, StepStateReflectsProgress) {
    WizardFixture f;
    advance_to_options(f.wizard);
    EXPECT_EQ(f.wizard.step_state(WizardStep::Welcome), StepState::Done);
    EXPECT_EQ(f.wizard.step_state(WizardStep::License), StepState::Done);
    EXPECT_EQ(f.wizard.step_state(WizardStep::Options), StepState::Active);
    EXPECT_EQ(f.wizard.step_state(WizardStep::Progress), StepState::Upcoming);
    EXPECT_EQ(f.wizard.step_state(WizardStep::Complete), StepState::Upcoming);
}

// 取消：任何步驟可取消；取消後導覽與安裝一律明確報錯。
TEST(InstallWizard, CancelSetsCancelledAndBlocksEverything) {
    WizardFixture f;
    ASSERT_TRUE(f.wizard.next());  // -> License

    auto c = f.wizard.cancel();
    EXPECT_TRUE(c);
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::Cancelled);

    EXPECT_FALSE(f.wizard.next());
    EXPECT_FALSE(f.wizard.back());
    EXPECT_FALSE(f.wizard.cancel());  // 重複取消亦明確回報。

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("x.pkg", valid_package_text("com.example.x"));
    const auto r = f.wizard.begin_install(src, InstallOptions{});
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(f.registry.size(), 0u);  // 未委派安裝。
}

// begin_install 委派 E9-07 成功：安裝登錄、前進至 Complete、outcome=Installed、進度 100。
TEST(InstallWizard, BeginInstallDelegatesAndSucceeds) {
    WizardFixture f;
    advance_to_options(f.wizard);

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("ok.pkg", valid_package_text("com.example.ok"));
    InstallOptions opts;
    opts.install_path = "/apps/ok";
    opts.selected_components = {"core", "docs"};

    const auto r = f.wizard.begin_install(src, opts);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.status, InstallStatus::Success);
    EXPECT_EQ(r.package_name, "com.example.ok");

    // E9-07 實際委派：套件登錄至記憶體登錄。
    EXPECT_EQ(f.registry.size(), 1u);
    EXPECT_TRUE(f.registry.contains("com.example.ok"));

    // 完成狀態。
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Complete);
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::Installed);
    EXPECT_EQ(f.wizard.progress_percent(), 100);
    EXPECT_EQ(f.wizard.step_state(WizardStep::Complete), StepState::Active);
}

// begin_install 委派 E9-07 失敗：停於 Progress、outcome=Failed，且可後退至 Options 重試。
TEST(InstallWizard, BeginInstallFailureStaysAtProgressAndCanRetry) {
    WizardFixture f;
    advance_to_options(f.wizard);

    const MemoryInstallSource bad = MemoryInstallSource::unavailable("broken.pkg");
    const auto r = f.wizard.begin_install(bad, InstallOptions{});
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Progress);
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::Failed);
    EXPECT_EQ(f.registry.size(), 0u);

    // 失敗後可退回 Options（重置為進行中）並以有效來源重試成功。
    auto b = f.wizard.back();
    EXPECT_TRUE(b);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Options);
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::InProgress);

    const MemoryInstallSource good =
        MemoryInstallSource::with_text("ok.pkg", valid_package_text("com.example.retry"));
    const auto r2 = f.wizard.begin_install(good, InstallOptions{});
    ASSERT_TRUE(r2.ok()) << r2.message;
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::Installed);
    EXPECT_EQ(f.registry.size(), 1u);
}

// 選項傳遞：begin_install(source, options) 儲存選項；便捷多載採用 set_options 暫存選項。
TEST(InstallWizard, OptionsArePassedToInstall) {
    WizardFixture f;
    advance_to_options(f.wizard);

    InstallOptions opts;
    opts.install_path = "/opt/here";
    opts.selected_components = {"a", "b", "c"};
    f.wizard.set_options(opts);

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("p.pkg", valid_package_text("com.example.opt"));
    const auto r = f.wizard.begin_install(src);  // 便捷多載：用暫存選項。
    ASSERT_TRUE(r.ok()) << r.message;

    EXPECT_EQ(f.wizard.options().install_path, "/opt/here");
    ASSERT_EQ(f.wizard.options().selected_components.size(), 3u);
    EXPECT_EQ(f.wizard.options().selected_components[1], "b");
    EXPECT_EQ(f.registry.size(), 1u);
}

// 進度回報：成功安裝逐階段回報，涵蓋起始與 100% 完成，最後一筆落於 Complete。
TEST(InstallWizard, ProgressReportsEachStage) {
    WizardFixture f;
    advance_to_options(f.wizard);

    std::vector<int> percents;
    std::vector<WizardStep> steps;
    f.wizard.set_progress_listener([&](const WizardProgress& p) {
        percents.push_back(p.percent);
        steps.push_back(p.step);
        EXPECT_FALSE(p.failed);
    });

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("prog.pkg", valid_package_text("com.example.prog"));
    const auto r = f.wizard.begin_install(src, InstallOptions{});
    ASSERT_TRUE(r.ok()) << r.message;

    ASSERT_FALSE(percents.empty());
    // 涵蓋 E9-07 生命週期映射的關鍵百分比。
    EXPECT_NE(std::find(percents.begin(), percents.end(), 10), percents.end());
    EXPECT_NE(std::find(percents.begin(), percents.end(), 55), percents.end());
    EXPECT_EQ(percents.back(), 100);
    EXPECT_EQ(steps.back(), WizardStep::Complete);
    // 百分比單調不減。
    for (std::size_t i = 1; i < percents.size(); ++i) {
        EXPECT_GE(percents[i], percents[i - 1]);
    }
}

// 進度回報：失敗時觀察者收到 failed=true 的回報。
TEST(InstallWizard, ProgressReportsFailure) {
    WizardFixture f;
    advance_to_options(f.wizard);

    bool saw_failed = false;
    f.wizard.set_progress_listener([&](const WizardProgress& p) {
        if (p.failed) {
            saw_failed = true;
        }
    });

    const MemoryInstallSource bad = MemoryInstallSource::unavailable("broken.pkg");
    const auto r = f.wizard.begin_install(bad, InstallOptions{});
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(saw_failed);
}

// 無效步驟轉移：begin_install 於非 Options 步驟呼叫 → 明確報錯、不委派。
TEST(InstallWizard, BeginInstallOutsideOptionsErrors) {
    WizardFixture f;  // 位於 Welcome。
    const MemoryInstallSource src =
        MemoryInstallSource::with_text("e.pkg", valid_package_text("com.example.e"));
    const auto r = f.wizard.begin_install(src, InstallOptions{});
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(f.registry.size(), 0u);
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Welcome);  // 未轉移。
}

// 無效步驟轉移：於 Options 步驟以 next() 前進 → 明確報錯（須改用 begin_install）。
TEST(InstallWizard, NextAtOptionsErrors) {
    WizardFixture f;
    advance_to_options(f.wizard);
    auto t = f.wizard.next();
    EXPECT_FALSE(t);
    EXPECT_FALSE(t.message.empty());
    EXPECT_EQ(f.wizard.current_step(), WizardStep::Options);  // 未轉移。
}

// 完成狀態為終態：安裝成功後 next()/back()/cancel()/begin_install 皆明確報錯，狀態維持 Installed。
TEST(InstallWizard, CompleteStateIsTerminal) {
    WizardFixture f;
    advance_to_options(f.wizard);

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("t.pkg", valid_package_text("com.example.term"));
    ASSERT_TRUE(f.wizard.begin_install(src, InstallOptions{}).ok());
    ASSERT_EQ(f.wizard.outcome(), WizardOutcome::Installed);

    EXPECT_FALSE(f.wizard.next());
    EXPECT_FALSE(f.wizard.back());
    EXPECT_FALSE(f.wizard.cancel());

    // 重複安裝亦明確報錯、不改變終態。
    const auto again = f.wizard.begin_install(src, InstallOptions{});
    EXPECT_FALSE(again.ok());
    EXPECT_EQ(f.wizard.outcome(), WizardOutcome::Installed);
    EXPECT_EQ(f.registry.size(), 1u);
}

}  // namespace
