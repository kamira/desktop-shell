// E3-04 電源動作致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「電源相關動作」（休眠 sleep / 鎖定 lock / 登出 logout / 重新啟動 restart /
// 關機 shutdown）以具名命令掛上 E6-01 命令匯流排（`power.sleep` / `power.lock` /
// `power.logout` / `power.restart` / `power.shutdown`）。呼叫端只需 命令 id（電源動作無需
// 額外參數）即可觸發，不需相依本致動器或任何 OS 電源 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層），消費 E6-01 契約。
//   - 相位 1（Mac / null 期）：**絕不真的執行任何電源操作**，也不呼叫任何 OS 電源 API
//     （無 `shutdown` / IOKit / `#ifdef` / cocoa / win32）。所有請求交由可抽換的
//     `PowerBackend` 承接；預設 `NullPowerBackend` 只把請求「記錄」下來供測試 / 診斷驗證，
//     絕不觸碰 OS。相位 2 換上真實後端（win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實電源 API；唯一 `#ifndef` 為 header guard。
//
// 因此可完全以單元測試驗證：各電源動作註冊到匯流排、dispatch 觸發後端、null 後端記錄請求
// 且未實際動作、無效動作回結構化錯誤（不崩潰）、結果回報。
#ifndef DS_ACTUATORS_E3_04_POWER_ACTUATOR_HPP
#define DS_ACTUATORS_E3_04_POWER_ACTUATOR_HPP

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
const char* contract_version() noexcept;

// ---------------------------------------------------------------------------
// PowerAction — 電源動作列舉。
//
// 刻意限定為一組可長期承重的電源意圖；不含任何 OS handle / API 語意假設，讓相位 2 的
// 真實後端（或相位 1 的 null 後端）自行決定如何實現。`Unknown` 供由字串解析失敗時使用。
// ---------------------------------------------------------------------------
enum class PowerAction {
    Sleep,     // 休眠 / 待機
    Lock,      // 鎖定畫面
    Logout,    // 登出目前使用者
    Restart,   // 重新啟動
    Shutdown,  // 關機
    Unknown,   // 無法辨識的動作（無效請求哨兵）
};

// 五個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdPowerSleep    = "power.sleep";
inline constexpr const char* kCmdPowerLock     = "power.lock";
inline constexpr const char* kCmdPowerLogout   = "power.logout";
inline constexpr const char* kCmdPowerRestart  = "power.restart";
inline constexpr const char* kCmdPowerShutdown = "power.shutdown";

// 有效電源動作的穩定列舉（供內省 / 遍歷；不含 Unknown）。
inline constexpr std::array<PowerAction, 5> kAllPowerActions{
    PowerAction::Sleep, PowerAction::Lock, PowerAction::Logout,
    PowerAction::Restart, PowerAction::Shutdown};

// 動作 ↔ 具名字串互轉（穩定、跨邊界不變形）。
inline const char* to_string(PowerAction action) noexcept {
    switch (action) {
        case PowerAction::Sleep:    return "sleep";
        case PowerAction::Lock:     return "lock";
        case PowerAction::Logout:   return "logout";
        case PowerAction::Restart:  return "restart";
        case PowerAction::Shutdown: return "shutdown";
        case PowerAction::Unknown:  return "unknown";
    }
    return "unknown";
}

// 動作對應的具名命令 id（Unknown 回空字串）。
inline const char* command_id_of(PowerAction action) noexcept {
    switch (action) {
        case PowerAction::Sleep:    return kCmdPowerSleep;
        case PowerAction::Lock:     return kCmdPowerLock;
        case PowerAction::Logout:   return kCmdPowerLogout;
        case PowerAction::Restart:  return kCmdPowerRestart;
        case PowerAction::Shutdown: return kCmdPowerShutdown;
        case PowerAction::Unknown:  return "";
    }
    return "";
}

// 由具名字串解析電源動作；無法辨識回 PowerAction::Unknown（不丟例外）。
inline PowerAction power_action_from_string(const std::string& s) noexcept {
    if (s == "sleep")    return PowerAction::Sleep;
    if (s == "lock")     return PowerAction::Lock;
    if (s == "logout")   return PowerAction::Logout;
    if (s == "restart")  return PowerAction::Restart;
    if (s == "shutdown") return PowerAction::Shutdown;
    return PowerAction::Unknown;
}

// ---------------------------------------------------------------------------
// PowerRequest — 平台中立地描述一次「電源動作」意圖。
//
// 不含任何 OS handle / 電源 API 語意假設；只承載呼叫端表達的動作，讓相位 2 的真實後端
// （或相位 1 的 null 後端）自行決定如何實現。
// ---------------------------------------------------------------------------
struct PowerRequest {
    PowerAction action = PowerAction::Unknown;

    bool operator==(const PowerRequest& o) const noexcept { return action == o.action; }
    bool operator!=(const PowerRequest& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// PowerBackend — 執行實際電源副作用的抽象後端。
//
// 相位 1 僅提供 NullPowerBackend；相位 2 由平台後端實作 perform() 真的休眠 / 鎖定 /
// 登出 / 重啟 / 關機。perform() 回 E6-01 CommandResult，讓匯流排呼叫端拿到一致的
// 成功 / 失敗結構。
// ---------------------------------------------------------------------------
class PowerBackend {
public:
    virtual ~PowerBackend() = default;
    virtual ds::command::CommandResult perform(const PowerRequest& request) = 0;
};

// ---------------------------------------------------------------------------
// NullPowerBackend — 相位 1 預設後端：不觸碰 OS、不執行任何電源操作，只「記錄」每個請求。
//
// 讓致動器在無真實平台後端時仍可完整跑通（註冊 → 分派 → 記錄），並讓測試 / 診斷驗證
// 「呼叫端到底請求了哪個電源動作」。每次 perform 記錄請求並回 Ok；**絕不真的關機 / 重啟等**。
// ---------------------------------------------------------------------------
class NullPowerBackend : public PowerBackend {
public:
    ds::command::CommandResult perform(const PowerRequest& request) override {
        records_.push_back(request);
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{std::string{to_string(request.action)}},
            "recorded (null backend, no real power action)");
    }

    // 已記錄的請求（依發生序）。供測試 / 診斷內省。
    const std::vector<PowerRequest>& records() const noexcept { return records_; }
    std::size_t record_count() const noexcept { return records_.size(); }
    bool empty() const noexcept { return records_.empty(); }
    void clear() noexcept { records_.clear(); }

    // 最近一次記錄（無記錄回 nullptr）。
    const PowerRequest* last() const noexcept {
        return records_.empty() ? nullptr : &records_.back();
    }

private:
    std::vector<PowerRequest> records_;
};

// ---------------------------------------------------------------------------
// PowerActuator — 把五個具名電源命令掛上 E6-01 命令匯流排的致動器。
//
// 建構時綁定一個 PowerBackend（相位 1 為 NullPowerBackend）。register_on(bus)
// 將 power.sleep / power.lock / power.logout / power.restart / power.shutdown 註冊到匯流排；
// 呼叫端之後只需 bus.dispatch("power.shutdown") 即可觸發，完全不需相依本型別。
//
// 電源動作不需額外參數：命令處理器直接把對應動作交後端。無效 / 未知動作（透過
// handle_action 直呼且傳入 PowerAction::Unknown）回 CommandResult{Failed}（不崩潰）。
// ---------------------------------------------------------------------------
class PowerActuator {
public:
    explicit PowerActuator(std::shared_ptr<PowerBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullPowerBackend（相位 1）。
    PowerActuator() : backend_(std::make_shared<NullPowerBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<PowerBackend>& backend() const noexcept { return backend_; }

    // 將五個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）
    // 則回滾已註冊者並回 false（不留半掛狀態）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        std::vector<ds::command::CommandId> done;
        for (const PowerAction action : kAllPowerActions) {
            const ds::command::CommandId id = command_id_of(action);
            const bool ok = bus.register_command(
                id, [self, action](const ds::command::CommandArgs&) {
                    return self->handle_action(action);
                });
            if (!ok) {
                // 回滾：只移除本次成功掛上的，避免遮蔽既有其他致動器。
                for (const auto& d : done) bus.unregister(d);
                return false;
            }
            done.push_back(id);
        }
        return true;
    }

    // 從匯流排移除五個具名命令。回傳確有移除的數量（0..5）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        for (const PowerAction action : kAllPowerActions) {
            n += bus.unregister(command_id_of(action)) ? 1 : 0;
        }
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----
    //
    // 執行單一電源動作：無效 / 未知動作回 Failed（不崩潰、不記錄）；否則交後端。
    ds::command::CommandResult handle_action(PowerAction action) {
        if (action == PowerAction::Unknown) {
            return ds::command::CommandResult::make_failed("power: unknown/invalid action");
        }
        if (!backend_) {
            return ds::command::CommandResult::make_failed("power: no backend bound");
        }
        return backend_->perform(PowerRequest{action});
    }

    // 便捷：以動作字串執行（無法辨識即回 Failed）。
    ds::command::CommandResult handle_action(const std::string& action_name) {
        return handle_action(power_action_from_string(action_name));
    }

private:
    std::shared_ptr<PowerBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_04_POWER_ACTUATOR_HPP
