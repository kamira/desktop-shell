// E9-06 套件卸載流程 — 實作（平台中立、純邏輯，無任何平台分支、無真實檔案系統刪除）。
#include "uninstall.hpp"

#include <utility>

namespace ds::package {

// ---- PackageRegistry --------------------------------------------------------

bool PackageRegistry::contains(const std::string& package_id) const {
    return by_name_.find(package_id) != by_name_.end();
}

bool PackageRegistry::add(const Package& pkg, std::vector<std::string> depends_on) {
    // 以 manifest.name 為鍵；同名已存在則不覆寫（重複安裝由呼叫端明確處理）。
    const auto inserted = by_name_.emplace(pkg.manifest.name, pkg);
    if (!inserted.second) {
        return false;
    }
    // 記錄本套件宣告的套件間相依邊（略過空項與自我相依，避免污染相依圖）。
    std::set<std::string> deps;
    for (auto& d : depends_on) {
        if (!d.empty() && d != pkg.manifest.name) {
            deps.insert(std::move(d));
        }
    }
    if (!deps.empty()) {
        depends_on_.emplace(pkg.manifest.name, std::move(deps));
    }
    return true;
}

const Package* PackageRegistry::find(const std::string& package_id) const {
    const auto it = by_name_.find(package_id);
    return it == by_name_.end() ? nullptr : &it->second;
}

std::vector<std::string> PackageRegistry::dependents_of(const std::string& package_id) const {
    // 反向查詢相依圖：凡「其相依集合含 package_id」的套件，皆為 package_id 的相依者。
    // std::map 迭代本身即依鍵排序，故結果排序穩定、天然去重。
    std::vector<std::string> result;
    for (const auto& kv : depends_on_) {
        if (kv.second.find(package_id) != kv.second.end()) {
            result.push_back(kv.first);
        }
    }
    return result;
}

bool PackageRegistry::remove(const std::string& package_id) {
    const auto it = by_name_.find(package_id);
    if (it == by_name_.end()) {
        return false;
    }
    by_name_.erase(it);
    depends_on_.erase(package_id);  // 連同本套件對外宣告的相依邊一併清除。
    return true;
}

// ---- Uninstaller ------------------------------------------------------------

void Uninstaller::set_lifecycle_listener(UninstallLifecycleListener listener) {
    lifecycle_ = std::move(listener);
}

void Uninstaller::report(UninstallStage stage, bool ok, const std::string& message,
                         const std::string& package_id) {
    if (lifecycle_) {
        lifecycle_(UninstallLifecycleEvent{stage, ok, message, package_id});
    }
}

UninstallResult Uninstaller::uninstall(PackageRegistry& registry, const std::string& package_id,
                                       bool force) {
    // 階段 1：定位。查無此套件（含重複卸載的第二次）即明確報 NotFound，不假裝成功。
    const Package* pkg = registry.find(package_id);
    if (pkg == nullptr) {
        UninstallResult r;
        r.status = UninstallStatus::Failed;
        r.outcome = UninstallOutcome::NotFound;
        r.stage = UninstallStage::Located;
        r.removed.package_id = package_id;
        r.message = "查無此套件：" + package_id + "（未安裝或已卸載）";
        report(UninstallStage::Located, /*ok=*/false, r.message, package_id);
        return r;
    }
    report(UninstallStage::Located, /*ok=*/true, "已定位待卸載套件：" + package_id, package_id);

    // 先快照被移除項目（元件/資源清單）——反登錄後 pkg 指標即失效，故此處先取。
    RemovedItems removed;
    removed.package_id = package_id;
    for (const PackageEntry& e : pkg->entries) {
        removed.components.push_back(RemovedComponent{e.kind, e.logical_path});
    }

    // 階段 2：相依檢查。列出仍相依此套件者。
    const std::vector<std::string> dependents = registry.dependents_of(package_id);
    if (!dependents.empty() && !force) {
        // 拒絕卸載：明確列出所有相依者、不靜默、登錄不變。
        UninstallResult r;
        r.status = UninstallStatus::Failed;
        r.outcome = UninstallOutcome::BlockedByDependents;
        r.stage = UninstallStage::DependencyChecked;
        r.removed.package_id = package_id;  // 未移除，components 留空。
        r.blocked_by_dependents = dependents;
        std::string names;
        for (std::size_t i = 0; i < dependents.size(); ++i) {
            names += (i == 0 ? "" : ", ") + dependents[i];
        }
        r.message = "卸載受阻：仍有 " + std::to_string(dependents.size()) +
                    " 個套件相依 " + package_id + "（" + names + "）";
        report(UninstallStage::DependencyChecked, /*ok=*/false, r.message, package_id);
        return r;
    }
    if (dependents.empty()) {
        report(UninstallStage::DependencyChecked, /*ok=*/true, "相依檢查通過：無他人相依",
               package_id);
    } else {
        // force：略過阻擋，但保留相依者清單作為「留下懸空相依」的警告。
        report(UninstallStage::DependencyChecked, /*ok=*/true,
               "相依檢查略過（force）：將留下 " + std::to_string(dependents.size()) + " 個懸空相依",
               package_id);
    }

    // 階段 3：清理元件/資源。相位 1 = 記憶體模型，清理即「登出其內含項目清單」（無真實檔案刪除）。
    report(UninstallStage::Cleaned, /*ok=*/true,
           "已清理 " + std::to_string(removed.components.size()) + " 個元件/資源", package_id);

    // 階段 4：反登錄。自登錄移除該套件（安裝的逆操作）。
    if (!registry.remove(package_id)) {
        // 理論上階段 1 已定位到；防禦性處理，仍明確報錯不靜默。
        UninstallResult r;
        r.status = UninstallStatus::Failed;
        r.outcome = UninstallOutcome::NotFound;
        r.stage = UninstallStage::Deregistered;
        r.removed.package_id = package_id;
        r.message = "反登錄失敗：" + package_id;
        report(UninstallStage::Deregistered, /*ok=*/false, r.message, package_id);
        return r;
    }
    report(UninstallStage::Deregistered, /*ok=*/true, "卸載完成：" + package_id, package_id);

    UninstallResult r;
    r.status = UninstallStatus::Success;
    r.outcome = UninstallOutcome::Removed;
    r.stage = UninstallStage::Deregistered;
    r.removed = std::move(removed);
    if (!dependents.empty()) {
        // force 卸載：成功但附警告，並記錄被留下懸空的相依者。
        r.blocked_by_dependents = dependents;
        std::string names;
        for (std::size_t i = 0; i < dependents.size(); ++i) {
            names += (i == 0 ? "" : ", ") + dependents[i];
        }
        r.message = "卸載成功（警告：force 略過相依，留下懸空相依 " + names + "）：" + package_id;
    } else {
        r.message = "卸載成功：" + package_id;
    }
    return r;
}

}  // namespace ds::package
