// E6-04 延遲與批次執行 — 平台中立契約（擴充 E6-01 命令匯流排）
//
// 在 E6-01 的具名命令 / 分派基礎上，補上兩種「非即時」的執行語意：
//
//   1. 延遲執行（DeferredScheduler）：把一個命令排程在「N 個時間單位後」執行。時間是
//      **可注入的邏輯時鐘 / tick**，由呼叫端以 advance(ticks) 明確推進——**不綁真實
//      wall-clock、不 sleep、不開 thread**。到期命令在 advance 時才分派，且以決定性順序
//      （先到期者先執行；同一 tick 者依排程先後 FIFO）觸發。可在到期前 cancel。
//
//   2. 批次執行（Batch / run_batch）：把多個命令組成一個序列一次提交，**保證依序執行**。
//      兩種模式：
//        - Atomic：遇第一個失敗即中止，回報是哪一步（index）失敗（「全成功或回報哪一步失敗」）。
//        - Continue：不論成敗跑完全部，逐步回報。
//      注意：一般命令匯流排無法對已產生的副作用做補償 / 回滾，故「原子」在此語意為
//      **首次失敗即中止後續**並明確定位失敗步驟（非事務回滾）。
//
// 失敗一律明確回報（沿用 E6-01 的 CommandStatus：NotFound / Failed），**絕不靜默**。
// 本單元屬 engine 層（平台中立純邏輯）：無 `#ifdef` / 平台分支 / 後端 / 真實時間，
// 可完全以單元測試驗證。命名空間與 E6-01 一致：`ds::command`。
#ifndef DS_COMMAND_E6_04_DEFERRED_BATCH_HPP
#define DS_COMMAND_E6_04_DEFERRED_BATCH_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：CommandBus / Command / CommandResult / CommandStatus

namespace ds::command {

// 延遲與批次契約版本標記。與 E6-01 的 contract_version 分流；消費者可據此判相容性。
// 定義在 .cpp（使 STATIC 程式庫非空並集中版本字串）。
const char* deferred_batch_contract_version() noexcept;

// ===========================================================================
// 邏輯時鐘型別
// ===========================================================================

// 邏輯時間單位（tick）。純邏輯、由 advance() 推進，不對應任何真實時間。
using Tick = std::int64_t;

// 已排程延遲命令的識別碼（供取消）。0 保留為「無效 / 排程失敗」哨兵；有效值由 1 起遞增。
using DeferredId = std::uint64_t;

// 無效延遲命令識別碼哨兵（schedule 失敗時回傳）。
inline constexpr DeferredId kInvalidDeferredId = 0;

// ---------------------------------------------------------------------------
// DeferredDispatch — advance() 期間單一到期命令的分派結果。
// ---------------------------------------------------------------------------
struct DeferredDispatch {
    DeferredId id{kInvalidDeferredId};  // 對應的排程識別碼
    Tick due{0};                        // 到期 tick（排程時算定）
    CommandId command_id;               // 被分派的命令 id
    CommandResult result;               // 分派結果（沿用 E6-01；NotFound 亦如實回報）

    bool ok() const noexcept { return result.ok(); }
};

// ---------------------------------------------------------------------------
// DeferredScheduler — 延遲命令排程器（注入式邏輯時鐘）。
//
// 綁定一個 E6-01 CommandBus（唯讀分派）。呼叫端以 schedule() 排入「delay 個 tick 後
// 執行」的命令，再以 advance(ticks) 推進邏輯時鐘；每次推進會把所有已到期（due <= now）
// 的命令**依序**分派並回傳結果。到期前可 cancel。
//
// 契約保證：
//   - 時間僅由 advance() 推進；不使用真實時鐘 / sleep / thread。
//   - delay 必須 >= 1（排在未來）；否則排程失敗回 kInvalidDeferredId（不靜默）。
//   - 到期順序決定性：先到期者先執行；同 tick 者依 schedule 先後（FIFO）。
//   - cancel 未到期命令使其不再執行；回是否確有移除。
// ---------------------------------------------------------------------------
class DeferredScheduler {
public:
    // 綁定分派用匯流排。bus 的生命週期須涵蓋本排程器（僅持有參考，不擁有）。
    explicit DeferredScheduler(const CommandBus& bus) : bus_(&bus) {}

    // 排程一個命令於「delay 個 tick 後」執行（due = now() + delay）。
    // delay 必須 >= 1；否則不排入並回 kInvalidDeferredId。成功回可用於 cancel 的識別碼。
    DeferredId schedule(Tick delay, Command cmd) {
        if (delay < 1) return kInvalidDeferredId;
        const DeferredId id = ++last_id_;
        entries_.push_back(Entry{id, now_ + delay, seq_++, std::move(cmd)});
        return id;
    }

    // 便捷多載：以 id + 參數排程。
    DeferredId schedule(Tick delay, CommandId id, CommandArgs args = {}) {
        return schedule(delay, Command{std::move(id), std::move(args)});
    }

    // 取消一個尚未執行的排程命令。回傳是否確有移除（未知 / 已執行的 id 回 false）。
    bool cancel(DeferredId handle) {
        if (handle == kInvalidDeferredId) return false;
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == handle) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    // 推進邏輯時鐘 ticks 個單位，並依序分派所有到期（due <= 新的 now）的命令。
    // ticks < 0 視為無效（不倒轉時間、不執行），回空向量。回傳依執行順序排列的結果。
    std::vector<DeferredDispatch> advance(Tick ticks) {
        std::vector<DeferredDispatch> fired;
        if (ticks < 0) return fired;
        now_ += ticks;

        // 收集到期項目，依 (due, seq) 決定性排序。
        std::vector<Entry*> due_entries;
        for (auto& e : entries_) {
            if (e.due <= now_) due_entries.push_back(&e);
        }
        std::sort(due_entries.begin(), due_entries.end(), [](const Entry* a, const Entry* b) {
            if (a->due != b->due) return a->due < b->due;
            return a->seq < b->seq;
        });

        fired.reserve(due_entries.size());
        for (Entry* e : due_entries) {
            fired.push_back(DeferredDispatch{e->id, e->due, e->command.id,
                                             bus_->dispatch(e->command)});
        }

        // 移除已執行項目（以 id 集合過濾，保留未到期者原相對順序）。
        if (!due_entries.empty()) {
            std::vector<Entry> remaining;
            remaining.reserve(entries_.size());
            for (auto& e : entries_) {
                if (e.due > now_) remaining.push_back(std::move(e));
            }
            entries_.swap(remaining);
        }
        return fired;
    }

    // 便捷：直接推進到指定的絕對 tick（等同 advance(target - now())；target <= now 為 no-op）。
    std::vector<DeferredDispatch> advance_to(Tick target) {
        if (target <= now_) return {};
        return advance(target - now_);
    }

    // 目前邏輯時間。
    Tick now() const noexcept { return now_; }

    // 尚待執行（未取消、未到期）的排程數。
    std::size_t pending() const noexcept { return entries_.size(); }

    // 是否仍有指定識別碼的待執行排程。
    bool has(DeferredId handle) const {
        for (const auto& e : entries_) {
            if (e.id == handle) return true;
        }
        return false;
    }

private:
    struct Entry {
        DeferredId id;
        Tick due;
        std::uint64_t seq;  // 排程序號（同 tick 之 FIFO 決定性 tie-break）
        Command command;
    };

    const CommandBus* bus_;
    Tick now_{0};
    DeferredId last_id_{kInvalidDeferredId};
    std::uint64_t seq_{0};
    std::vector<Entry> entries_;
};

// ===========================================================================
// 批次執行
// ===========================================================================

// 批次執行模式。
enum class BatchMode {
    Atomic,    // 遇第一個失敗即中止後續，回報失敗步驟（「全成功或回報哪一步失敗」）
    Continue,  // 不論成敗跑完全部，逐步回報
};

// 批次中單一步驟的執行結果。
struct BatchStepResult {
    std::size_t index{0};   // 步驟在批次中的位置（0 起）
    CommandId command_id;   // 該步驟的命令 id
    CommandResult result;   // 分派結果

    bool ok() const noexcept { return result.ok(); }
};

// ---------------------------------------------------------------------------
// BatchResult — 批次執行結果彙總。
// ---------------------------------------------------------------------------
struct BatchResult {
    bool ok{true};                             // 已執行步驟是否全部成功
    std::size_t executed{0};                   // 實際執行的步驟數（Atomic 中止時 < 總數）
    std::size_t total{0};                       // 批次總步驟數
    std::optional<std::size_t> failed_index;    // 首個失敗步驟的位置（若有）
    std::vector<BatchStepResult> steps;         // 依執行順序的逐步結果

    // Atomic 模式下是否因失敗而中止（仍有未執行步驟）。
    bool aborted() const noexcept { return executed < total; }
};

// 批次建構器：以決定性順序收集命令，供 run_batch 執行。
class Batch {
public:
    Batch() = default;

    // 追加一個命令；回 *this 以便鏈式呼叫。
    Batch& add(Command cmd) {
        commands_.push_back(std::move(cmd));
        return *this;
    }

    // 便捷多載：以 id + 參數追加。
    Batch& add(CommandId id, CommandArgs args = {}) {
        commands_.push_back(Command{std::move(id), std::move(args)});
        return *this;
    }

    const std::vector<Command>& commands() const noexcept { return commands_; }
    std::size_t size() const noexcept { return commands_.size(); }
    bool empty() const noexcept { return commands_.empty(); }
    void clear() { commands_.clear(); }

private:
    std::vector<Command> commands_;
};

// 依序執行一個命令序列並彙總結果。
//   - 順序保證：嚴格依 cmds 的順序分派。
//   - Atomic：遇第一個非 Ok（Failed / NotFound）即中止後續，failed_index 定位該步。
//   - Continue：全部執行；ok 反映是否全成功，failed_index 為首個失敗步。
// 空序列回 ok=true、executed=0（無步驟即無失敗）。
inline BatchResult run_batch(const CommandBus& bus, const std::vector<Command>& cmds,
                             BatchMode mode = BatchMode::Atomic) {
    BatchResult out;
    out.total = cmds.size();
    out.steps.reserve(cmds.size());

    for (std::size_t i = 0; i < cmds.size(); ++i) {
        CommandResult r = bus.dispatch(cmds[i]);
        const bool step_ok = r.ok();
        out.steps.push_back(BatchStepResult{i, cmds[i].id, std::move(r)});
        ++out.executed;

        if (!step_ok) {
            out.ok = false;
            if (!out.failed_index.has_value()) out.failed_index = i;
            if (mode == BatchMode::Atomic) break;  // 中止後續（原子：全成功或回報哪一步失敗）
        }
    }
    return out;
}

// 便捷多載：直接執行 Batch。
inline BatchResult run_batch(const CommandBus& bus, const Batch& batch,
                             BatchMode mode = BatchMode::Atomic) {
    return run_batch(bus, batch.commands(), mode);
}

}  // namespace ds::command

#endif  // DS_COMMAND_E6_04_DEFERRED_BATCH_HPP
