// E9-06 套件卸載流程 — 契約測試（gtest）
//
// 涵蓋：卸載已安裝套件 → 移除、查無套件報錯（NotFound）、有相依者阻擋卸載 + 列出相依者、
// 清理元件/資源（removed.components）、卸載結果與生命週期逐階段回報、重複卸載處理、
// 以及 force 略過相依（警告）與相依解除後可正常卸載。
// 是 E9-07 安裝的逆操作。平台中立：不含任何平台分支。
#include "uninstall.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "package.hpp"  // E9-01 parse_package / Package

using ds::package::Package;
using ds::package::parse_package;
using ds::package::PackageRegistry;
using ds::package::Uninstaller;
using ds::package::UninstallLifecycleEvent;
using ds::package::UninstallOutcome;
using ds::package::UninstallStage;
using ds::package::UninstallStatus;

namespace {

// 一份最小合法套件描述（manifest + 內容清單），name 可注入。
std::string package_text(const std::string& name) {
    return "format_version: 1.0\n"
           "name: " + name + "\n"
           "requires: host.tray_icon\n"
           "---\n"
           "asset: icons/tray.png\n"
           "code: main.wasm\n";
}

// 解析出一個真實 E9-01 Package（解析即驗證）。測試前置條件。
Package make_package(const std::string& name) {
    const auto r = parse_package(package_text(name));
    EXPECT_TRUE(r.ok()) << r.error().message;
    return r.package();
}

// 便利：安裝一個套件（可宣告相依），回傳是否成功登錄。
bool install(PackageRegistry& reg, const std::string& name,
             std::vector<std::string> depends_on = {}) {
    return reg.add(make_package(name), std::move(depends_on));
}

}  // namespace

// 卸載已安裝套件 → 成功、走到 Deregistered、自登錄移除。
TEST(Uninstall, RemovesInstalledPackage) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.hello"));
    ASSERT_EQ(reg.size(), 1u);

    Uninstaller un;
    const auto r = un.uninstall(reg, "com.example.hello");

    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.status, UninstallStatus::Success);
    EXPECT_EQ(r.outcome, UninstallOutcome::Removed);
    EXPECT_EQ(r.stage, UninstallStage::Deregistered);
    EXPECT_EQ(r.removed.package_id, "com.example.hello");
    EXPECT_FALSE(r.message.empty());

    EXPECT_EQ(reg.size(), 0u);
    EXPECT_FALSE(reg.contains("com.example.hello"));
    EXPECT_EQ(reg.find("com.example.hello"), nullptr);
}

// 查無套件 → 明確 NotFound、stage=Located、登錄不變、訊息非空（不靜默）。
TEST(Uninstall, MissingPackageReportsNotFound) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.present"));

    Uninstaller un;
    const auto r = un.uninstall(reg, "com.example.absent");

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, UninstallStatus::Failed);
    EXPECT_EQ(r.outcome, UninstallOutcome::NotFound);
    EXPECT_EQ(r.stage, UninstallStage::Located);
    EXPECT_FALSE(r.message.empty());
    EXPECT_TRUE(r.removed.components.empty());
    EXPECT_EQ(reg.size(), 1u);  // 未動到既有安裝。
}

// 有相依者 → 阻擋卸載、列出所有相依者、登錄不變（拒絕不靜默）。
TEST(Uninstall, BlockedByDependentsListsThem) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.lib"));
    // app_a 與 app_b 皆相依 lib。
    ASSERT_TRUE(install(reg, "com.example.app_a", {"com.example.lib"}));
    ASSERT_TRUE(install(reg, "com.example.app_b", {"com.example.lib"}));

    Uninstaller un;
    const auto r = un.uninstall(reg, "com.example.lib");

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.outcome, UninstallOutcome::BlockedByDependents);
    EXPECT_EQ(r.stage, UninstallStage::DependencyChecked);
    ASSERT_EQ(r.blocked_by_dependents.size(), 2u);
    // 排序穩定（map 依鍵序）：app_a、app_b。
    EXPECT_EQ(r.blocked_by_dependents[0], "com.example.app_a");
    EXPECT_EQ(r.blocked_by_dependents[1], "com.example.app_b");
    EXPECT_FALSE(r.message.empty());

    EXPECT_TRUE(reg.contains("com.example.lib"));  // 未被移除。
    EXPECT_EQ(reg.size(), 3u);
}

// 清理元件/資源：removed.components 反映套件的內含項目清單。
TEST(Uninstall, CleansUpComponents) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.assets"));  // package_text 帶 2 個 entry。

    Uninstaller un;
    const auto r = un.uninstall(reg, "com.example.assets");

    ASSERT_TRUE(r.ok()) << r.message;
    ASSERT_EQ(r.removed.components.size(), 2u);
    EXPECT_EQ(r.removed.components[0].kind, "asset");
    EXPECT_EQ(r.removed.components[0].logical_path, "icons/tray.png");
    EXPECT_EQ(r.removed.components[1].kind, "code");
    EXPECT_EQ(r.removed.components[1].logical_path, "main.wasm");
}

// 卸載結果與生命週期逐階段回報：成功序列 Located→DependencyChecked→Cleaned→Deregistered。
TEST(Uninstall, ReportsLifecycleStages) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.hello"));

    std::vector<UninstallStage> stages;
    std::vector<bool> oks;
    Uninstaller un;
    un.set_lifecycle_listener([&](const UninstallLifecycleEvent& ev) {
        stages.push_back(ev.stage);
        oks.push_back(ev.ok);
        EXPECT_EQ(ev.package_id, "com.example.hello");
    });

    const auto r = un.uninstall(reg, "com.example.hello");
    ASSERT_TRUE(r.ok()) << r.message;

    ASSERT_EQ(stages.size(), 4u);
    EXPECT_EQ(stages[0], UninstallStage::Located);
    EXPECT_EQ(stages[1], UninstallStage::DependencyChecked);
    EXPECT_EQ(stages[2], UninstallStage::Cleaned);
    EXPECT_EQ(stages[3], UninstallStage::Deregistered);
    for (bool ok : oks) EXPECT_TRUE(ok);
}

// 生命週期在失敗階段回報 ok=false（NotFound 於 Located 階段）。
TEST(Uninstall, LifecycleReportsFailureStage) {
    PackageRegistry reg;
    std::vector<UninstallStage> stages;
    std::vector<bool> oks;
    Uninstaller un;
    un.set_lifecycle_listener([&](const UninstallLifecycleEvent& ev) {
        stages.push_back(ev.stage);
        oks.push_back(ev.ok);
    });

    const auto r = un.uninstall(reg, "com.example.absent");
    EXPECT_FALSE(r.ok());
    ASSERT_EQ(stages.size(), 1u);
    EXPECT_EQ(stages[0], UninstallStage::Located);
    EXPECT_FALSE(oks[0]);
}

// 重複卸載：第一次成功、第二次即 NotFound（不假裝成功、不靜默）。
TEST(Uninstall, DoubleUninstallSecondIsNotFound) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.hello"));

    Uninstaller un;
    const auto r1 = un.uninstall(reg, "com.example.hello");
    ASSERT_TRUE(r1.ok()) << r1.message;

    const auto r2 = un.uninstall(reg, "com.example.hello");
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.outcome, UninstallOutcome::NotFound);
    EXPECT_EQ(r2.stage, UninstallStage::Located);
}

// force：仍有相依者時，force 卸載成功但附警告並記錄被留下的懸空相依。
TEST(Uninstall, ForceRemovesDespiteDependents) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.lib"));
    ASSERT_TRUE(install(reg, "com.example.app", {"com.example.lib"}));

    Uninstaller un;
    const auto r = un.uninstall(reg, "com.example.lib", /*force=*/true);

    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.outcome, UninstallOutcome::Removed);
    ASSERT_EQ(r.blocked_by_dependents.size(), 1u);  // 記錄留下的懸空相依。
    EXPECT_EQ(r.blocked_by_dependents[0], "com.example.app");
    EXPECT_FALSE(reg.contains("com.example.lib"));
    EXPECT_TRUE(reg.contains("com.example.app"));  // 相依者仍在（現為懸空）。
}

// 相依解除後可正常卸載：先卸載相依者，再卸載原被阻擋的套件即成功。
TEST(Uninstall, UnblockedAfterDependentRemoved) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.lib"));
    ASSERT_TRUE(install(reg, "com.example.app", {"com.example.lib"}));

    Uninstaller un;
    // 先卸載 lib → 被 app 阻擋。
    EXPECT_EQ(un.uninstall(reg, "com.example.lib").outcome,
              UninstallOutcome::BlockedByDependents);
    // 卸載 app（無人相依它）→ 成功。
    ASSERT_TRUE(un.uninstall(reg, "com.example.app").ok());
    // 現在 lib 無相依者 → 卸載成功。
    const auto r = un.uninstall(reg, "com.example.lib");
    EXPECT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(reg.size(), 0u);
}

// PackageRegistry：dependents_of 反向相依查詢、add 不覆寫同名。
TEST(Uninstall, RegistryDependencyQueries) {
    PackageRegistry reg;
    ASSERT_TRUE(install(reg, "com.example.lib"));
    ASSERT_TRUE(install(reg, "com.example.app", {"com.example.lib"}));

    // 同名不覆寫。
    EXPECT_FALSE(install(reg, "com.example.lib"));
    EXPECT_EQ(reg.size(), 2u);

    const auto deps = reg.dependents_of("com.example.lib");
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], "com.example.app");
    // 無人相依 app。
    EXPECT_TRUE(reg.dependents_of("com.example.app").empty());
}
