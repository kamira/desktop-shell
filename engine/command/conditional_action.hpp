// E6-03 條件動作與去抖 — 平台中立契約（engine 層 / command）
//
// 本單元建構於兩個已合併的上游契約之上，且**不重造輪子**：
//   - E6-01（`ds::command::CommandBus`）：具名命令的註冊與分派（擴充點 3「動作」）。
//   - E7-05（`ds::format::Evaluator`）：宣告式格式的公式 / 運算式求值引擎。
//
// 在 E6-01 的「無條件、即時分派」之上，本單元加兩層編排能力：
//
//   1. 條件動作（ConditionalAction）
//      —— 動作綁一條**條件運算式**（E7-05 公式，如 `"count > 0"` 或 `"= visible && ready"`）；
//         只有當條件求值為真時，才經 E6-01 分派其命令。條件以注入的 `VariableScope`
//         求值。**條件求值失敗（語法錯誤 / 未定義變數 / 型別誤用）明確回報、絕不靜默**
//         （不會被誤當成 false 而悄悄跳過）——對齊 NFR-04 精神。
//
//   2. 去抖 / 節流（Debouncer / Throttler）
//      —— 抑制過於頻繁的觸發。時間一律以**注入式邏輯 tick**（單調遞增整數）表達，
//         **不使用 wall-clock / thread / sleep**：呼叫端自行決定「現在幾點」，因此
//         整個單元可完全以單元測試決定性驗證，且平台中立（換平台一行不動）。
//
// 平台中立：無 `#ifdef` / 平台分支 / OS 後端 / 系統呼叫。命名空間與 E6-01 一致（`ds::command`）。
#ifndef DS_COMMAND_E6_03_CONDITIONAL_ACTION_HPP
#define DS_COMMAND_E6_03_CONDITIONAL_ACTION_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "command_bus.hpp"  // E6-01：CommandBus / CommandId / CommandArgs / CommandResult
#include "formula.hpp"      // E7-05：Evaluator / EvalResult（傳遞取得 E7-01 Value、E7-02 VariableScope）

namespace ds::command {

// 擴充點契約版本標記（見 E6-01 同名慣例）。定義於 .cpp。
const char* conditional_contract_version() noexcept;

// -----------------------------------------------------------------------------
// 條件真值橋接：E7-05 求得的 Value → 布林真值
// -----------------------------------------------------------------------------
//
// 條件運算式的求值結果須能判定真假。採最小、明確的真值規則：
//   - Bool   → 原值。
//   - Number → 非零為真（整數 / 浮點皆然）。
//   - 其餘（Null / String / List / Map）→ **非布林條件，明確回報型別錯誤，不臆測真假**。
//
// 成功時 `out` 置為真值並回 true；型別不適用時回 false（`out` 不動），由呼叫端回報。
bool value_truthiness(const ds::format::Value& v, bool& out) noexcept;

// -----------------------------------------------------------------------------
// 條件動作
// -----------------------------------------------------------------------------

// 條件動作編排的結果狀態。
enum class ConditionalStatus {
    Dispatched,      // 條件為真 → 已經 E6-01 分派；`result` 持分派結果。
    Skipped,         // 條件為假 → 未分派（非錯誤，是正常的「不觸發」）。
    ConditionError,  // 條件求值失敗 / 結果非布林 → 未分派，`message` 帶原因、`error_position` 帶欄位。
};

// 條件動作編排的結果。互斥地承載三種情形，讓呼叫端不需再猜測。
struct ConditionalOutcome {
    ConditionalStatus status = ConditionalStatus::Skipped;

    // 條件的布林真值；僅在 status != ConditionError 時有意義。
    bool condition_value = false;

    // 分派結果；僅在 status == Dispatched 時有效（來自 E6-01 CommandBus::dispatch）。
    CommandResult result{};

    // 人類可讀訊息。ConditionError 時帶 E7-05 的錯誤原因；其餘情形可為空。
    std::string message{};

    // 條件求值錯誤的 0-based 欄索引（僅 ConditionError 且源自求值錯誤時有意義）。
    std::size_t error_position = 0;

    bool dispatched() const noexcept { return status == ConditionalStatus::Dispatched; }
    bool skipped() const noexcept { return status == ConditionalStatus::Skipped; }
    bool is_error() const noexcept { return status == ConditionalStatus::ConditionError; }

    // 工廠。
    static ConditionalOutcome make_dispatched(CommandResult r) {
        ConditionalOutcome o;
        o.status = ConditionalStatus::Dispatched;
        o.condition_value = true;
        o.result = std::move(r);
        return o;
    }
    static ConditionalOutcome make_skipped() {
        ConditionalOutcome o;
        o.status = ConditionalStatus::Skipped;
        o.condition_value = false;
        return o;
    }
    static ConditionalOutcome make_condition_error(std::string message, std::size_t position = 0) {
        ConditionalOutcome o;
        o.status = ConditionalStatus::ConditionError;
        o.message = std::move(message);
        o.error_position = position;
        return o;
    }
};

// 條件動作：一條條件運算式 + 一個具名命令（含參數）。
//
// 條件以 E7-05 公式表達（可含前導 '=' 或 `${ }` 標記，會被自動去除）。條件為真時，
// 命令經 E6-01 匯流排以 (command, args) 分派。設計刻意與 E6-01 的 `Command`（id+args）
// 對稱：條件動作 = 條件 + Command。
struct ConditionalAction {
    std::string condition_expr;  // E7-05 條件運算式（如 "count > 0"、"= ready && !busy"）。
    CommandId command;           // 條件成立時要分派的具名命令 id。
    CommandArgs args;            // 分派所用參數（可空）。

    // 以給定作用域求條件、成立則分派。
    //   - 條件求值失敗 / 結果非布林 → ConditionError（不分派、不靜默）。
    //   - 條件為假 → Skipped（不分派）。
    //   - 條件為真 → Dispatched（result 為 bus.dispatch(command, args) 的結果，
    //     未知命令由 E6-01 回 NotFound，不崩潰）。
    ConditionalOutcome evaluate_and_dispatch(const CommandBus& bus,
                                             const ds::format::VariableScope& scope) const;

    // 無變數作用域版本：條件中任何裸變數引用 → 未定義錯誤（ConditionError）。
    // 適用於純字面量條件（如 "1 < 2"）。
    ConditionalOutcome evaluate_and_dispatch(const CommandBus& bus) const;
};

// -----------------------------------------------------------------------------
// 邏輯時間：注入式 tick
// -----------------------------------------------------------------------------

// 邏輯時鐘刻度。單調遞增整數，由呼叫端注入（不用 wall-clock / thread）。
using Tick = std::int64_t;

// -----------------------------------------------------------------------------
// 去抖（Debouncer）—— 尾緣 / 合併語意
// -----------------------------------------------------------------------------
//
// 去抖把「短時間內的連續觸發」合併成單一次放行：每次 `trigger(tick)` 只是登記 / 刷新
// 一個待決放行（deadline = tick + window），本身**不立即放行**（回 false，屬延後）。
// 唯有經 `advance(tick)` 把邏輯時鐘推進到「自最後一次 trigger 起已靜默滿 window」時，
// 待決放行才被釋放一次（回 true）。因此連續觸發只會放行一次（合併），符合
// 「短時間內重複觸發只執行一次（最後一次）」。
//
// 邏輯時鐘單調：傳入早於目前的 tick 會被夾住（視為無時間推進），避免時間倒流造成的怪異。
class Debouncer {
public:
    // window 為靜默視窗長度（tick 數），須 >= 1；<1 會被夾為 1。
    explicit Debouncer(Tick window) : window_(window < 1 ? 1 : window) {}

    // 登記一次觸發於邏輯時間 tick：刷新待決放行的 deadline 為 now + window。
    // 回 false（去抖延後放行，不即時觸發）。連續呼叫會不斷把 deadline 往後推（合併）。
    bool trigger(Tick tick) {
        advance_clock(tick);
        pending_ = true;
        deadline_ = now_ + window_;
        return false;  // 去抖：不即時放行。
    }

    // 推進邏輯時鐘至 tick。若有待決放行且已靜默滿 window（now >= deadline），
    // 釋放該放行一次並回 true；否則回 false。釋放後待決狀態清除，需再 trigger 才會重新武裝。
    bool advance(Tick tick) {
        advance_clock(tick);
        if (pending_ && now_ >= deadline_) {
            pending_ = false;
            return true;  // 放行合併後的單一次。
        }
        return false;
    }

    bool pending() const noexcept { return pending_; }
    Tick window() const noexcept { return window_; }
    Tick now() const noexcept { return now_; }

private:
    void advance_clock(Tick tick) noexcept {
        if (tick > now_) now_ = tick;  // 單調：不倒流。
    }

    Tick window_;
    Tick now_ = 0;
    Tick deadline_ = 0;
    bool pending_ = false;
};

// -----------------------------------------------------------------------------
// 節流（Throttler）—— 前緣 / 頻率限制語意
// -----------------------------------------------------------------------------
//
// 節流限制放行「頻率」：第一次觸發即放行，其後在 interval 個 tick 內的觸發一律擋下
// （回 false），直到距上次放行已滿 interval 個 tick，下一次觸發才再放行。與去抖不同，
// 節流**即時**於 `trigger(tick)` 做放行決策（回值即「是否放行」）。
//
// `advance(tick)` 純推進邏輯時鐘（不放行），供呼叫端在無觸發時推進時間；推進足夠後，
// 下一次 trigger 便會再放行（「advance tick 後重新放行」）。時鐘同樣單調。
class Throttler {
public:
    // interval 為兩次放行間的最小 tick 間隔，須 >= 1；<1 會被夾為 1。
    explicit Throttler(Tick interval) : interval_(interval < 1 ? 1 : interval) {}

    // 嘗試於邏輯時間 tick 放行一次。首次觸發、或距上次放行已滿 interval → 放行（回 true）
    // 並記錄本次為最後放行時刻；否則擋下（回 false）。
    bool trigger(Tick tick) {
        advance_clock(tick);
        if (!has_passed_ || now_ - last_pass_ >= interval_) {
            has_passed_ = true;
            last_pass_ = now_;
            return true;
        }
        return false;
    }

    // 不放行、僅推進邏輯時鐘至 tick。回是否確有推進（時鐘前進）。
    bool advance(Tick tick) {
        Tick before = now_;
        advance_clock(tick);
        return now_ > before;
    }

    // 查詢：若此刻於 tick 觸發是否會放行（不改變狀態）。
    bool would_pass(Tick tick) const noexcept {
        Tick n = tick > now_ ? tick : now_;
        return !has_passed_ || n - last_pass_ >= interval_;
    }

    Tick interval() const noexcept { return interval_; }
    Tick now() const noexcept { return now_; }

private:
    void advance_clock(Tick tick) noexcept {
        if (tick > now_) now_ = tick;  // 單調：不倒流。
    }

    Tick interval_;
    Tick now_ = 0;
    Tick last_pass_ = 0;
    bool has_passed_ = false;
};

}  // namespace ds::command

#endif  // DS_COMMAND_E6_03_CONDITIONAL_ACTION_HPP
