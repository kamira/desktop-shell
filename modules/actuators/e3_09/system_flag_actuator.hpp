// E3-09 系統旗標切換致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「切換系統層級的具名布林旗標 / 模式」這組副作用（如 勿擾模式 Do-Not-Disturb、
// 深色模式、夜覽 / Night Shift、飛航模式、螢幕自動旋轉鎖 等具名開關）以具名命令掛上
// E6-01 命令匯流排。每個具名旗標 `<name>` 對外提供四個動詞命令：
//   `flag.<name>.on` / `flag.<name>.off` / `flag.<name>.toggle` / `flag.<name>.status`
// 呼叫端只需 命令 id 即可觸發，不需相依本致動器或任何 OS 系統設定 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層 / 子系統 actuators），消費 E6-01 契約
// （與 E3-05 音量致動器一致的「注入式後端 + null 樣式」範式）。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 系統設定 API**（無 `defaults` /
//     NSUserDefaults / 任何 cocoa / win32 開關 API）。所有旗標狀態交由可抽換的
//     `SystemFlagBackend` 承接；預設 `NullSystemFlagBackend` 以純記憶體 map 模擬
//     （旗標字串鍵 → 布林），供測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實後端
//     （win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實設定 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（旗標鍵驗證、命令 id 組裝、經 E6-01 分派、結果回報）與後端解耦：驗證與
// 動詞語意在致動器層完成，後端只承接「已合法化」的 set(flag,bool) / get(flag) 原語，
// 因此可完全以單元測試驗證。
#ifndef DS_ACTUATORS_E3_09_SYSTEM_FLAG_ACTUATOR_HPP
#define DS_ACTUATORS_E3_09_SYSTEM_FLAG_ACTUATOR_HPP

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// 前綴 `system_flag_` 避免與同命名空間其他致動器的版本函式衝突。
const char* system_flag_contract_version() noexcept;

// 命令命名空間前綴：`flag.<name>.<verb>`（穩定、可讀字串，不使用數字 opcode）。
inline constexpr const char* kFlagCommandPrefix = "flag.";

// 四個動詞：開 / 關 / 切換 / 查詢。Status 為唯讀（不改變狀態）。
enum class FlagVerb { On, Off, Toggle, Status };

// 動詞 → 命令後綴（含前導點）。
inline const char* flag_verb_suffix(FlagVerb verb) noexcept {
    switch (verb) {
        case FlagVerb::On:     return ".on";
        case FlagVerb::Off:    return ".off";
        case FlagVerb::Toggle: return ".toggle";
        case FlagVerb::Status: return ".status";
    }
    return ".status";  // 不可達；防禦性回退（唯讀動詞最安全）。
}

// 旗標鍵驗證：致動器層唯一的「合法化」點。
// 規則：非空，且每個字元為小寫英數或底線 [a-z0-9_]。刻意排除點號（`.` 是命令分隔符，
// 允許會破壞 `flag.<name>.<verb>` 的可解析性）與空白 / 大寫，確保命令 id 穩定可讀。
inline bool is_valid_flag_key(const std::string& key) noexcept {
    if (key.empty()) return false;
    for (const char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

// 組裝具名旗標的動詞命令 id：`flag.<name>.<verb>`。
inline std::string flag_command_id(const std::string& name, FlagVerb verb) {
    return std::string(kFlagCommandPrefix) + name + flag_verb_suffix(verb);
}

// ---------------------------------------------------------------------------
// SystemFlagBackend — 執行實際旗標副作用的抽象後端。
//
// 相位 1 僅提供 NullSystemFlagBackend；相位 2 由平台後端以真實系統設定 API 實作。
// 介面刻意最小：以字串鍵 set / get 一個布林旗標。致動器層以此組合出 on/off/toggle/status。
// 契約保證：傳入 set/get 的 flag 已由致動器驗證為合法且為已註冊的具名旗標（後端可信任）。
// ---------------------------------------------------------------------------
class SystemFlagBackend {
public:
    virtual ~SystemFlagBackend() = default;

    // 設定具名旗標為 value（true=開 / false=關）。
    virtual void set(const std::string& flag, bool value) = 0;
    // 查詢具名旗標目前狀態（未曾設定者視為關 / false）。
    virtual bool get(const std::string& flag) const = 0;
};

// ---------------------------------------------------------------------------
// NullSystemFlagBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體 map 模擬。
//
// 讓致動器在無真實平台後端時仍可完整跑通（開 → 查一致、切換、冪等），並讓測試 / 診斷
// 驗證狀態一致性。未曾設定的旗標查詢回 false（預設關）。初始狀態可注入。
// ---------------------------------------------------------------------------
class NullSystemFlagBackend : public SystemFlagBackend {
public:
    NullSystemFlagBackend() = default;
    explicit NullSystemFlagBackend(std::map<std::string, bool> initial)
        : states_(std::move(initial)) {}

    void set(const std::string& flag, bool value) override { states_[flag] = value; }

    bool get(const std::string& flag) const override {
        auto it = states_.find(flag);
        return it == states_.end() ? false : it->second;
    }

    // 內省（供測試 / 診斷）：是否曾寫入該旗標、目前已知旗標數。
    bool has(const std::string& flag) const { return states_.find(flag) != states_.end(); }
    std::size_t size() const noexcept { return states_.size(); }

private:
    std::map<std::string, bool> states_;  // 記憶體模擬的旗標狀態表
};

// ---------------------------------------------------------------------------
// SystemFlagActuator — 把具名旗標的四動詞命令掛上 E6-01 命令匯流排的旗標致動器。
//
// 建構時綁定一個 SystemFlagBackend（相位 1 為 NullSystemFlagBackend）。以 add_flag(name)
// 登記已知的具名旗標（旗標鍵須通過 is_valid_flag_key）。register_on(bus) 為每個已登記旗標
// 註冊 `flag.<name>.on` / `.off` / `.toggle` / `.status` 到匯流排；呼叫端之後只需
// bus.dispatch("flag.<name>.toggle") 即可觸發，完全不需相依本型別。
//
// 命令語意（皆無需參數，旗標鍵已烘焙進命令 id）：
//   - flag.<name>.on    ：設該旗標為開（冪等：已開仍為開）。回操作後狀態（bool=true）。
//   - flag.<name>.off   ：設該旗標為關（冪等：已關仍為關）。回操作後狀態（bool=false）。
//   - flag.<name>.toggle：翻轉該旗標（其自身之逆，切換兩次回原狀態）。回操作後狀態。
//   - flag.<name>.status：唯讀查詢目前狀態（不改變）。回目前狀態（bool）。
// 未登記 / 非法旗標鍵：不會被註冊上匯流排 → dispatch 回 CommandResult{NotFound}（不崩潰）；
// 直接呼叫處理器時亦以 has_flag() 保護，未登記旗標鍵回 CommandResult{Failed}（不丟例外）。
// 所有成功結果的 value 皆為操作後的旗標布林狀態，供呼叫端 / 測試驗證。
// ---------------------------------------------------------------------------
class SystemFlagActuator {
public:
    explicit SystemFlagActuator(std::shared_ptr<SystemFlagBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullSystemFlagBackend（相位 1）。
    SystemFlagActuator() : backend_(std::make_shared<NullSystemFlagBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<SystemFlagBackend>& backend() const noexcept { return backend_; }

    // 登記一個已知的具名旗標。旗標鍵須通過 is_valid_flag_key 且尚未登記時成功並回 true；
    // 否則不變更狀態並回 false（非法鍵 / 重複登記皆拒絕）。
    bool add_flag(const std::string& name) {
        if (!is_valid_flag_key(name)) return false;
        if (flags_.find(name) != flags_.end()) return false;
        flags_.insert(name);
        return true;
    }

    // 是否已登記指定具名旗標。
    bool has_flag(const std::string& name) const { return flags_.find(name) != flags_.end(); }

    // 已登記旗標數。
    std::size_t flag_count() const noexcept { return flags_.size(); }

    // 已登記旗標鍵（有序）。供內省 / 診斷。
    std::vector<std::string> flag_names() const {
        return std::vector<std::string>(flags_.begin(), flags_.end());  // std::set 已排序
    }

    // 為每個已登記旗標把四動詞命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被
    // 占用）則回滾本次已註冊者並回 false（不留半掛狀態、不遮蔽既有致動器）。無後端或
    // 無任何已登記旗標一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        if (flags_.empty()) return false;

        std::vector<ds::command::CommandId> registered;
        registered.reserve(flags_.size() * 4);
        auto self = this;

        for (const auto& name : flags_) {
            for (const FlagVerb verb : {FlagVerb::On, FlagVerb::Off, FlagVerb::Toggle,
                                        FlagVerb::Status}) {
                const std::string id = flag_command_id(name, verb);
                const bool ok = bus.register_command(
                    id, [self, name, verb](const ds::command::CommandArgs&) {
                        return self->apply(name, verb);
                    });
                if (!ok) {
                    // 回滾：移除本次已成功掛上的所有命令。
                    for (const auto& rid : registered) bus.unregister(rid);
                    return false;
                }
                registered.push_back(id);
            }
        }
        return true;
    }

    // 從匯流排移除所有已登記旗標的四動詞命令。回傳確有移除的命令數。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        for (const auto& name : flags_) {
            for (const FlagVerb verb : {FlagVerb::On, FlagVerb::Off, FlagVerb::Toggle,
                                        FlagVerb::Status}) {
                n += bus.unregister(flag_command_id(name, verb)) ? 1 : 0;
            }
        }
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----
    // 皆以 has_flag() 保護：未登記旗標鍵回 Failed（不崩潰、不改後端狀態）。

    ds::command::CommandResult handle_on(const std::string& flag) {
        return apply(flag, FlagVerb::On);
    }
    ds::command::CommandResult handle_off(const std::string& flag) {
        return apply(flag, FlagVerb::Off);
    }
    ds::command::CommandResult handle_toggle(const std::string& flag) {
        return apply(flag, FlagVerb::Toggle);
    }
    ds::command::CommandResult handle_status(const std::string& flag) {
        return apply(flag, FlagVerb::Status);
    }

    // 便捷查詢：某具名旗標目前狀態（未登記 / 無後端回 false）。供呼叫端 / 測試內省。
    bool current(const std::string& flag) const {
        if (!backend_ || !has_flag(flag)) return false;
        return backend_->get(flag);
    }

private:
    // 動詞套用核心：驗證後端與旗標登記，執行動詞語意，回操作後狀態。
    ds::command::CommandResult apply(const std::string& flag, FlagVerb verb) {
        if (!backend_) {
            return ds::command::CommandResult::make_failed("flag: no backend bound");
        }
        if (!has_flag(flag)) {
            return ds::command::CommandResult::make_failed("flag: unregistered flag key: " + flag);
        }
        switch (verb) {
            case FlagVerb::On:
                backend_->set(flag, true);
                break;
            case FlagVerb::Off:
                backend_->set(flag, false);
                break;
            case FlagVerb::Toggle:
                backend_->set(flag, !backend_->get(flag));
                break;
            case FlagVerb::Status:
                break;  // 唯讀：不改變狀態
        }
        return ok_with_state(flag);
    }

    // 成功結果統一帶「操作後的旗標布林狀態」為回傳值，訊息帶 flag=on/off 供診斷。
    ds::command::CommandResult ok_with_state(const std::string& flag) const {
        const bool state = backend_->get(flag);
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{state}, flag + "=" + (state ? "on" : "off"));
    }

    std::shared_ptr<SystemFlagBackend> backend_;
    std::set<std::string> flags_;  // 已登記的具名旗標鍵（有序，決定性遍歷）
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_09_SYSTEM_FLAG_ACTUATOR_HPP
