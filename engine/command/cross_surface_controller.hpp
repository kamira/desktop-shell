// E6-05 跨 surface 控制 — 平台中立契約（engine 層 / command）
//
// E1-13 管理**多個 profile 實例並存**（同一 widget 開多個視窗，各自具名 `InstanceId`）；
// E6-01 提供**命令匯流排**（具名命令 id + 參數 → 處理器）。本單元把兩者接起來：對
// E1-13 管理的一批實例下達**跨 surface 的命令控制**——
//   - **廣播**：對 registry 目前列舉的「全部」存活實例送出同一命令。
//   - **單一具名目標**：對指定的一份實例送出命令。
//   - **群組命令**：對一組具名目標送出同一命令，各目標互不影響（部分成功可能）。
//   - **逐目標結果回報**：每個目標各自的分派結果，呼叫端可分辨整體與個別成敗。
//
// 命令本身仍由呼叫端在 E6-01 `CommandBus` 註冊；本單元不新增執行語意，只負責「目標選擇」
// 與「逐目標分派」——每次分派前先經 E1-13 `ProfileInstanceRegistry::contains()` 驗證具名
// 目標是否存活，再把目標 id 併入參數（鍵 `"target"`）交給 `CommandBus::dispatch`，讓已註冊
// 的處理器可依 `args.get_string("target")` 得知本次呼叫針對哪一份實例。
//
// 硬約束（NFR-02）：目標一律以 E1-13 的具名 `InstanceId`（字串）指涉、以具名清單
//   （`std::vector<InstanceId>`）表達群組，不出現數字索引 / 數字 handle。
// 硬約束：**未知目標 / 無效命令一律結構化報錯，絕不靜默**——
//   - 未知目標（不在 registry 中）→ `TargetDispatchStatus::UnknownTarget`，**不呼叫**
//     `CommandBus::dispatch`（避免處理器誤以為目標存在）。
//   - 命令本身未在 E6-01 註冊 → `CommandBus::dispatch` 回 `CommandStatus::NotFound`，
//     本單元原樣映射為 `TargetDispatchStatus::CommandNotFound`。
//   - 空目標集合（registry 無存活實例 / 具名清單為空）合法，回空的逐目標報告，不是錯誤。
//
// 相位 1：純邏輯，不含 `#ifdef` / win32 / cocoa 等平台分支，不觸碰任何真實 OS 呼叫。
#ifndef DS_COMMAND_E6_05_CROSS_SURFACE_CONTROLLER_HPP
#define DS_COMMAND_E6_05_CROSS_SURFACE_CONTROLLER_HPP

#include "command_bus.hpp"                  // 上游 E6-01（可讀不可改）
#include "profile_instance_registry.hpp"    // 上游 E1-13（可讀不可改）

#include <cstddef>
#include <string>
#include <vector>

namespace ds::command {

// 跨 surface 控制契約版本標記（實體符號定義在 .cpp）。
const char* cross_surface_contract_version() noexcept;

// ---------------------------------------------------------------------------
// TargetDispatchStatus — 單一目標分派結果的分類。
//
// 讓呼叫端能分辨「目標根本不存在」（本單元自身產生，不涉及匯流排）與「目標存在但
// 命令 / 處理器本身失敗」（來自 E6-01 CommandBus）——兩者原因不同，不應混為一談。
// ---------------------------------------------------------------------------
enum class TargetDispatchStatus {
    Ok,               // 目標存在、已分派、處理器回報成功
    Failed,           // 目標存在、已分派，但處理器回報失敗
    CommandNotFound,  // 目標存在，但命令本身未在 E6-01 CommandBus 註冊
    UnknownTarget,    // 具名目標不存在於 E1-13 registry（本單元產生，未呼叫匯流排）
};

// 具名字串（診斷 / 記錄用，NFR-02：不用數字）。未知回 "unknown"。
const char* to_string(TargetDispatchStatus s) noexcept;

// ---------------------------------------------------------------------------
// TargetResult — 單一目標的完整分派結果：目標 id + 分類 + 原始 CommandResult。
//
// `result` 於 `status == UnknownTarget` 時為本單元合成的失敗結果（message 說明未知目標），
// 其餘狀態下為 `CommandBus::dispatch` 的原始回傳（value 可能承載處理器的查詢結果）。
// ---------------------------------------------------------------------------
struct TargetResult {
    ds::kernel::InstanceId target;
    TargetDispatchStatus status = TargetDispatchStatus::UnknownTarget;
    CommandResult result{};

    bool ok() const noexcept { return status == TargetDispatchStatus::Ok; }
};

// ---------------------------------------------------------------------------
// CrossDispatchReport — 一次跨 surface 分派（broadcast / send_to_group）的逐目標報告。
//
// 依目標處理序保存（broadcast 依 registry 列舉序；send_to_group 依呼叫端傳入清單序），
// 穩定、決定性。空目標集合回空報告（合法，不是錯誤）。
// ---------------------------------------------------------------------------
struct CrossDispatchReport {
    std::vector<TargetResult> per_target;

    std::size_t size() const noexcept { return per_target.size(); }
    bool empty() const noexcept { return per_target.empty(); }

    // 是否每個目標皆成功（空報告視為「全部成功」——沒有目標即沒有失敗者）。
    bool all_ok() const noexcept {
        for (const auto& r : per_target) {
            if (!r.ok()) return false;
        }
        return true;
    }
    std::size_t ok_count() const noexcept {
        std::size_t n = 0;
        for (const auto& r : per_target) {
            if (r.ok()) ++n;
        }
        return n;
    }
    // 失敗計數：含 Failed / CommandNotFound / UnknownTarget 三類皆算失敗。
    std::size_t failed_count() const noexcept { return size() - ok_count(); }
};

// ---------------------------------------------------------------------------
// CrossSurfaceController —— 跨 surface / profile 實例的命令控制器。
//
// 組合（非擁有）一個 E6-01 `CommandBus` 與一個 E1-13 `ProfileInstanceRegistry`：
// 前者提供命令的實際分派，後者提供「目前有哪些存活實例」的具名目標來源。本單元
// 本身不持有任何命令處理器、不新增執行語意，純粹是「目標選擇 + 逐目標分派」的
// 協調層，換掉底層匯流排 / registry 實作也不影響此層契約。
// ---------------------------------------------------------------------------
class CrossSurfaceController {
public:
    CrossSurfaceController(CommandBus& bus, const ds::kernel::ProfileInstanceRegistry& registry)
        : bus_(bus), registry_(registry) {}

    // --- 廣播：對 registry 目前列舉的「全部」存活實例送出同一命令 ---
    // 依 registry.list()（建立序，穩定）逐一分派；registry 目前無任何存活實例時
    // 回空報告（合法：語意上「全部」= 0 份仍成立，不是錯誤）。
    CrossDispatchReport broadcast(const CommandId& command, const CommandArgs& args = {}) const;

    // --- 單一具名目標 ---
    // 目標不存在於 registry → UnknownTarget，不呼叫匯流排；存在則正常分派。
    TargetResult send_to(const ds::kernel::InstanceId& target, const CommandId& command,
                         const CommandArgs& args = {}) const;

    // --- 群組命令：對一組具名目標送出同一命令 ---
    // 依呼叫端傳入的 targets 順序逐一分派；清單中每個 id 各自判定是否存在，
    // 未知者回 UnknownTarget、其餘正常分派，彼此互不影響（部分成功可能）。
    // 空清單回空報告（合法，不是錯誤）。
    CrossDispatchReport send_to_group(const std::vector<ds::kernel::InstanceId>& targets,
                                      const CommandId& command, const CommandArgs& args = {}) const;

private:
    CommandBus& bus_;
    const ds::kernel::ProfileInstanceRegistry& registry_;
};

}  // namespace ds::command

#endif  // DS_COMMAND_E6_05_CROSS_SURFACE_CONTROLLER_HPP
