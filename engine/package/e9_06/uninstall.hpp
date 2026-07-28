// E9-06 套件卸載流程 — 把「卸載一個已安裝套件」的流程模型化（engine 層 / package 子系統）
//
// 語意：E9-06 是 E9-07 拖放安裝的**逆操作**。給定一個記憶體套件登錄與一個套件識別（package_id
// = manifest.name），跑完卸載流程：
//   定位(Located) → 相依檢查(DependencyChecked) → 清理元件/資源(Cleaned) → 反登錄(Deregistered)。
// 每一步皆可回報；失敗一律**明確報錯、絕不靜默**：
//   - 查無此套件 → NotFound（明確回報，不假裝成功）。
//   - 仍有他人相依此套件 → BlockedByDependents，並列出所有仍相依它的套件 id（拒絕卸載）。
//   - 重複卸載（同一 id 卸載兩次）→ 第二次即 NotFound（登錄中已無此項）。
// 成功時回報「被移除的項目」：套件 id + 其被清理的元件/資源清單（來自 E9-01 `Package.entries`）。
//
// 相依模型（為何不靠 manifest.requires）：
//   E9-02 manifest 的 `requires`（`required_capabilities`）是「所需能力 id」（如 host.tray_icon），
//   並非「相依哪個套件」。套件間的相依關係屬**登錄期的圖**：安裝某套件時可宣告它相依哪些其他
//   套件（以對方 package_id 表示）。因此本單元自帶記憶體登錄 `PackageRegistry`，於 `add` 時記錄
//   宣告的套件間相依邊，卸載時據此判斷「誰仍相依待卸載的套件」。此圖為記憶體模型、平台中立。
//
// 與 E9-07 的關係：
//   E9-07 以 `InstallRegistry` 為安裝目標並跑安裝生命週期；E9-06 為其逆操作。E9-06 僅相依 E9-01
//   （套件格式：`Package` / `PackageEntry`），不相依 E9-07（安裝器）——故自帶一個對稱的、支援
//   「移除 + 相依查詢」的記憶體登錄，而非改動已合併的 E9-07。命名空間與 E9-01 一致（`ds::package`）。
//
// 相位 1（Mac / null 期）約束：
//   - **平台中立、純邏輯**：無 `#ifdef` / `win32` / `cocoa`、無系統呼叫、**無真實檔案系統刪除**。
//   - 登錄為記憶體模型；「清理資源/元件」在相位 1 即「自登錄中移除該套件的內含項目清單」。
//   - 相位 2 換真實後端時：介面不動，只需以真實資源刪除後端替換「清理」步驟。
#ifndef DS_ENGINE_E9_06_UNINSTALL_HPP
#define DS_ENGINE_E9_06_UNINSTALL_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "package.hpp"  // E9-01（PUBLIC 傳遞：Package / PackageEntry；並經其傳遞 E9-02 Manifest）

namespace ds::package {

// 記憶體套件登錄（E9-06 的卸載目標；E9-07 `InstallRegistry` 的對稱體，額外支援移除與相依查詢）。
//
// 以 manifest.name 為鍵、不覆寫既有安裝；並持有套件間相依邊（某套件宣告相依哪些其他套件 id）。
// 純資料 / 平台中立：不接任何真實檔案系統或 OS。
class PackageRegistry {
public:
    // 是否已安裝同名套件。
    bool contains(const std::string& package_id) const;

    // 登錄一個套件，並宣告它相依哪些其他套件（以對方 package_id 表示；空 = 無套件間相依）。
    // 以 manifest.name 為鍵；同名已存在時**不覆寫**並回傳 false（重複安裝由呼叫端明確處理）。
    // 註：depends_on 記錄的是「本套件相依誰」；卸載某套件時，反向查詢即得「誰相依它」。
    bool add(const Package& pkg, std::vector<std::string> depends_on = {});

    // 依 package_id 查找；不存在回傳 nullptr。
    const Package* find(const std::string& package_id) const;

    // 列出「仍相依 package_id」的已安裝套件 id（即以 package_id 為卸載阻擋者）。
    // 結果去重且排序穩定（便於回報與測試）；package_id 不存在時仍照實回傳其相依者（若有）。
    std::vector<std::string> dependents_of(const std::string& package_id) const;

    // 目前已安裝套件數。
    std::size_t size() const noexcept { return by_name_.size(); }

private:
    friend class Uninstaller;

    // 自登錄中移除一個套件（連同其相依邊）。不存在回傳 false。僅供 Uninstaller 於反登錄階段使用。
    bool remove(const std::string& package_id);

    std::map<std::string, Package> by_name_;                    // package_id -> 套件本體
    std::map<std::string, std::set<std::string>> depends_on_;   // package_id -> 它相依的套件 id 集合
};

// 卸載流程的階段。
enum class UninstallStage {
    Located,            // 已於登錄中定位到待卸載套件
    DependencyChecked,  // 相依檢查通過（無他人相依，或以 force 略過）
    Cleaned,            // 元件/資源已清理（相位 1 = 自登錄移除其內含項目）
    Deregistered,       // 已自登錄移除（卸載完成）
};

// 卸載結果狀態。
enum class UninstallStatus { Success, Failed };

// 卸載結果的原因分類（成功時為 Removed）。
enum class UninstallOutcome {
    Removed,              // 成功：套件已卸載
    NotFound,            // 查無此套件（含重複卸載的第二次）
    BlockedByDependents,  // 仍有他人相依此套件，拒絕卸載
};

// 一個被清理的元件/資源（對應 E9-01 `Package.entries` 的一項）。純資料。
struct RemovedComponent {
    std::string kind;          // 資源類別（如 "asset" / "code"）。
    std::string logical_path;  // 套件內相對邏輯路徑。
};

// 被移除的項目集合（成功卸載時回報）。
struct RemovedItems {
    std::string package_id;                     // 被移除的套件識別（manifest.name）。
    std::vector<RemovedComponent> components;   // 被清理的元件/資源清單（可為空——僅含 manifest 的套件）。
};

// 卸載結果。成功 = 走到 Deregistered；失敗 = outcome 指出原因、message 給出明確說明（絕不為空）。
struct UninstallResult {
    UninstallStatus status = UninstallStatus::Failed;
    UninstallOutcome outcome = UninstallOutcome::NotFound;
    UninstallStage stage = UninstallStage::Located;   // 成功=Deregistered；失敗=失敗發生的階段。

    RemovedItems removed;                              // 成功時填入被移除項目；失敗時 components 為空。
    std::vector<std::string> blocked_by_dependents;   // BlockedByDependents 時列出仍相依此套件的 id。
    std::string message;                              // 人類可讀說明；失敗時為原因（絕不靜默）。

    bool ok() const noexcept { return status == UninstallStatus::Success; }
    explicit operator bool() const noexcept { return ok(); }
};

// 卸載生命週期回報事件（逐階段）。ok=false 表示該階段失敗。
struct UninstallLifecycleEvent {
    UninstallStage stage;
    bool ok = true;
    std::string message;
    std::string package_id;  // 已知時填入（Located 起）。
};

// 卸載生命週期觀察者回呼；於每一階段轉換時被呼叫。
using UninstallLifecycleListener = std::function<void(const UninstallLifecycleEvent&)>;

// 卸載器：把「卸載一個已安裝套件」的流程模型化。是 E9-07 安裝的逆操作。
//
// 無狀態（除可選的生命週期觀察者外）；登錄以參數傳入，故一個卸載器可服務多個登錄。
class Uninstaller {
public:
    Uninstaller() = default;

    // 設定生命週期觀察者（可選）。傳空以清除。
    void set_lifecycle_listener(UninstallLifecycleListener listener);

    // 卸載 registry 中識別為 package_id 的套件，跑完卸載生命週期並回傳結果。
    // 流程：Located → DependencyChecked → Cleaned → Deregistered。
    //   - 查無此套件 → 失敗（NotFound，stage=Located）。
    //   - force=false 且仍有他人相依 → 失敗（BlockedByDependents，stage=DependencyChecked），
    //     並於 blocked_by_dependents 列出所有相依者；登錄不變。
    //   - force=true 且仍有他人相依 → 仍卸載（警告：留下懸空相依），blocked_by_dependents 記錄
    //     被留下懸空的相依者，message 標明為警告。
    // 成功時 removed 給出被移除套件 id 與其被清理的元件/資源清單。
    UninstallResult uninstall(PackageRegistry& registry, const std::string& package_id,
                              bool force = false);

private:
    void report(UninstallStage stage, bool ok, const std::string& message,
                const std::string& package_id);

    UninstallLifecycleListener lifecycle_;
};

}  // namespace ds::package

#endif  // DS_ENGINE_E9_06_UNINSTALL_HPP
