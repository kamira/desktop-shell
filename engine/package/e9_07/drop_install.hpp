// E9-07 拖放安裝 — 把「拖放一個套件來源 → 安裝」的流程模型化（engine 層 / package 子系統）
//
// 語意：接收一個「拖放事件」（攜帶套件來源參照），將來源解析為 E9-01 套件，
// 並跑完安裝生命週期：接收(Received) → 解析來源(Resolved) → 解析套件(Parsed) →
// 結構驗證(Validated) → 登錄(Registered)，每一步皆回報，失敗一律明確報錯、絕不靜默。
//
// 與 E5-08（系統事件）的關係：
//   - **事件模型**：本單元的拖放事件 `DropEvent` 沿用 E5-08「純資料、平台中立、不含任何 OS 原生
//     型別或控制代碼」的事件風格；來源以可注入抽象 `InstallSource` 表達（相位 1 為記憶體實作）。
//   - **實際整合（genuine link）**：`DropInstaller` 在建構時訂閱一個 E5-08 `SystemEventSource`，
//     以系統事件維護「安裝閘門」——`SessionLocked` / `SystemSleep` 關閉閘門（session 不在場/系統
//     睡眠時拒絕拖放安裝，屬合理安全姿態），`SessionUnlocked` / `SystemWake` 重新開啟。閘門關閉時
//     `handle_drop` 明確以「已封鎖」失敗，而非靜默丟棄。這條路徑實際呼叫 E5-08 的分派實作。
//
// 相位 1（Mac / null 期）約束：
//   - **平台中立、純邏輯**：無 `#ifdef` / `win32` / `cocoa`、不接真實 OS 拖放或檔案系統。
//   - 來源皆為**可注入抽象**；安裝目標為**記憶體登錄** `InstallRegistry`。
//   - 相位 2 換真實後端時：介面不動，只需以真實拖放後端建構 `DropEvent`（來源改為讀真實檔案的
//     `InstallSource` 實作），並以真實 `SystemEventSource` 後端取代 null 後端。
#ifndef DS_ENGINE_E9_07_DROP_INSTALL_HPP
#define DS_ENGINE_E9_07_DROP_INSTALL_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <string>

#include "package.hpp"        // E9-01（PUBLIC 傳遞：Package / parse_package / validate_package）
#include "system_event.hpp"   // E5-08（PUBLIC 傳遞：SystemEventSource / SubscriptionId 等）

namespace ds::package {

// 開啟一個拖放來源的結果。純資料、平台中立。
struct SourceContent {
    bool available = false;  // false = 無效/無法讀取的來源（如來源損毀、不存在）。
    std::string text;        // 套件描述文字（E9-01 parse_package 的輸入）；available=false 時無意義。
};

// 拖放來源的抽象介面 —— 「拖放事件攜帶的套件來源參照」。
//
// 相位 1 唯一實作為記憶體 `MemoryInstallSource`（不接真實檔案系統）；相位 2 起可加入讀真實
// 檔案/URL 的實作，只需實作本介面，`DropInstaller` 一行不動。
class InstallSource {
public:
    virtual ~InstallSource() = default;

    // 人類可讀的來源識別（用於回報訊息，如 "com.example.hello.pkg"）。平台中立。
    virtual std::string label() const = 0;

    // 解析來源為套件描述文字。無法讀取（無效來源）時回傳 available=false。
    virtual SourceContent open() const = 0;
};

// 記憶體內可注入來源（相位 1）。不接任何真實檔案系統或 OS。
class MemoryInstallSource : public InstallSource {
public:
    // 建立一個「可用」來源，持有給定的套件描述文字。
    static MemoryInstallSource with_text(std::string label, std::string text);

    // 建立一個「無效/無法讀取」來源（模擬損毀或不存在的拖放來源）。
    static MemoryInstallSource unavailable(std::string label);

    std::string label() const override { return label_; }
    SourceContent open() const override;

private:
    MemoryInstallSource() = default;
    std::string label_;
    bool available_ = false;
    std::string text_;
};

// 一個拖放事件。純資料、平台中立 —— 不含任何 OS 原生型別或控制代碼。
struct DropEvent {
    // 套件來源參照（不持有所有權；生命週期由呼叫端保證）。null = 格式不正的拖放（無來源）。
    const InstallSource* source = nullptr;
    // 人類可讀補充說明（如 "dropped onto tray icon"）。平台中立、可為空。
    std::string detail;
};

// 安裝生命週期的階段。
enum class InstallStage {
    Received,    // 已接收拖放事件
    Resolved,    // 來源已解析為套件描述文字
    Parsed,      // 已解析為 E9-01 Package（含格式相容性檢查）
    Validated,   // 結構完整性驗證通過（E9-01 validate_package）
    Registered,  // 已登錄至記憶體登錄（安裝完成）
};

// 安裝結果狀態。
enum class InstallStatus { Success, Failed };

// 安裝結果。成功 = 走到 Registered；失敗 = stage 指出失敗發生的階段、message 給出明確原因。
struct InstallResult {
    InstallStatus status = InstallStatus::Failed;
    InstallStage stage = InstallStage::Received;  // 成功=Registered；失敗=失敗發生的階段。
    std::string package_name;                     // 成功時為 manifest.name；早期失敗時為空。
    std::string message;                          // 人類可讀說明；失敗時為原因（絕不為空/靜默）。

    bool ok() const noexcept { return status == InstallStatus::Success; }
    explicit operator bool() const noexcept { return ok(); }
};

// 生命週期回報事件（逐階段）。ok=false 表示該階段失敗。
struct InstallLifecycleEvent {
    InstallStage stage;
    bool ok = true;
    std::string message;
    std::string package_name;  // 已知時填入（Parsed 起）。
};

// 生命週期觀察者回呼；於每一階段轉換時被呼叫。
using InstallLifecycleListener = std::function<void(const InstallLifecycleEvent&)>;

// 記憶體套件登錄（相位 1 的安裝目標）。以 manifest.name 為鍵；不覆寫既有安裝。
class InstallRegistry {
public:
    // 是否已安裝同名套件。
    bool contains(const std::string& name) const;

    // 登錄一個套件。同名已存在時**不覆寫**並回傳 false（重複安裝由呼叫端明確處理）。
    bool add(const Package& pkg);

    // 依名稱查找；不存在回傳 nullptr。
    const Package* find(const std::string& name) const;

    // 目前已安裝套件數。
    std::size_t size() const noexcept { return by_name_.size(); }

private:
    std::map<std::string, Package> by_name_;
};

// 拖放安裝器：把拖放事件驅動的安裝流程模型化。
//
// 建構時訂閱一個 E5-08 系統事件來源以維護安裝閘門（見檔首）；解構時解除訂閱。
class DropInstaller {
public:
    // registry / system_events 的生命週期須長於本物件（不持有所有權）。
    DropInstaller(InstallRegistry& registry, ds::events::SystemEventSource& system_events);
    ~DropInstaller();

    DropInstaller(const DropInstaller&) = delete;
    DropInstaller& operator=(const DropInstaller&) = delete;

    // 設定生命週期觀察者（可選）。傳空以清除。
    void set_lifecycle_listener(InstallLifecycleListener listener);

    // 處理一個拖放事件，跑完安裝生命週期並回傳結果。
    // 流程：Received → (閘門/來源檢查) → Resolved → Parsed → Validated → Registered。
    // 任一步失敗 → 明確 InstallResult（status=Failed、stage=失敗階段、message=原因）。
    InstallResult handle_drop(const DropEvent& drop);

    // 目前是否接受安裝（閘門開啟）。session 鎖定 / 系統睡眠時為 false。
    bool accepting() const noexcept { return gate_open_; }

private:
    InstallResult fail(InstallStage stage, std::string message, std::string name = {});
    void report(InstallStage stage, bool ok, const std::string& message, const std::string& name);
    void on_system_event(const ds::events::SystemEvent& ev);

    InstallRegistry& registry_;
    ds::events::SystemEventSource& system_events_;
    ds::events::SubscriptionId sub_ = 0;
    bool gate_open_ = true;
    InstallLifecycleListener lifecycle_;
};

}  // namespace ds::package

#endif  // DS_ENGINE_E9_07_DROP_INSTALL_HPP
