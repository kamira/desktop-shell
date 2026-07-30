// E7-11 群組與批次操作 — 讓宣告式格式定義項目群組並對整組批次施加操作（engine 層 / 平台中立）
//
// 描述子系統的宣告式格式（E7-01 的 `Value` 樹）常以「一組同類項目」表達 UI 元件、圖層、
// 動作目標等（例如一個 `layers:` 清單，其成員各帶 `group`/`tag` 標籤）。本單元提供：
//
//   1. **選擇（select）**：以選擇器（依標籤 / 鍵值 / 自訂述詞）從 `Value` 樹中挑出一組
//      Map 節點，組成一個 `Group`。每個成員帶穩定路徑（樹中唯一定位）與友善標籤。
//   2. **批次命令（apply_to_group）**：對群組每個成員經 **E6-01 CommandBus** 分派一個具名命令，
//      **逐成員**回報結果（`GroupResult{per_member[]}`）。部分失敗 / 未知命令逐一浮現，**不靜默**。
//   3. **批次屬性套用（apply_attributes）**：對整組套用共同屬性變更，產出每個成員的更新副本；
//      無法套用者（非 Map 成員）逐一回報原因，**不靜默吞掉**。
//
// 設計原則（承接 E7-01 / E6-01 的契約精神）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 系統呼叫 / 真實後端；engine 層換平台一行不動。
//   - **不靜默失敗**（NFR-04 精神）：任何成員的部分失敗都以結構化結果逐一回報。
//   - **非侵入**：`select` 對來源樹唯讀；`Group` 成員以非擁有指標指入來源樹（呼叫端持有其生命週期）。
//     批次屬性套用產出**新的 Value 副本**（E7-01 的 Value 無就地變更 API），不改動來源樹。
#ifndef DS_ENGINE_E7_11_GROUP_HPP
#define DS_ENGINE_E7_11_GROUP_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "command_bus.hpp"  // E6-01：ds::command::{CommandBus, CommandArgs, CommandResult, CommandId}
#include "document.hpp"      // E7-01：ds::format::Value

namespace ds::format {

// ---------------------------------------------------------------------------
// Selector — 從 Value 樹挑選 Map 節點的述詞包裝。
//
// 選擇器只針對 **Map 節點**求值（純量 / 清單本身不是可標籤的項目，但會被遞迴穿越）。
// 提供三種常見工廠 + 自訂述詞；帶一個人類可讀 label 供診斷。
// ---------------------------------------------------------------------------
class Selector {
public:
    using Predicate = std::function<bool(const Value&)>;

    // 依標籤選取：Map 節點的 [key] 為字串且等於 value 才命中。
    //   - key 預設為 "group"（最常見的群組標籤欄位）。
    //   - value 為空字串 → 退化為「凡含此鍵者皆命中」（等同 has(key)）。
    static Selector tagged(std::string value, std::string key = "group");

    // 依鍵存在性選取：凡 Map 節點含指定 key（不論值）即命中。
    static Selector has(std::string key);

    // 自訂述詞：完全由呼叫端決定命中條件（node 保證為 Map）。label 供診斷。
    static Selector where(Predicate pred, std::string label = "custom");

    // 對單一節點求值。非 Map 節點一律回 false（只有 Map 是可標籤的項目）。
    bool matches(const Value& node) const;

    // 診斷用人類可讀標籤（如 "group=buttons"）。
    const std::string& label() const noexcept { return label_; }

private:
    Selector(Predicate pred, std::string label)
        : pred_(std::move(pred)), label_(std::move(label)) {}

    Predicate pred_;
    std::string label_;
};

// ---------------------------------------------------------------------------
// GroupMember — 群組的單一成員。
//
// 以**非擁有指標**指入來源樹（select 的來源 root 之生命週期由呼叫端保證）。
//   - path：樹中的規範路徑，恆唯一且非空（如 "/layers/0"、"/panel/items/2"）。用於逐成員回報。
//   - label：友善識別字串——取自成員的 "id" 或 "name" 字串欄位；若皆無則等同 path。
// ---------------------------------------------------------------------------
struct GroupMember {
    std::string path;    // 規範樹路徑（恆唯一、非空）
    std::string label;   // 友善識別（"id"/"name" 欄位，退化為 path）
    const Value* node;   // 非擁有指標，指入來源樹（恆為 Map）
};

// ---------------------------------------------------------------------------
// Group — 依選擇器選出的一組成員（依樹的前序遍歷順序，決定性）。
// ---------------------------------------------------------------------------
class Group {
public:
    Group() = default;
    Group(std::string selector_label, std::vector<GroupMember> members)
        : selector_label_(std::move(selector_label)), members_(std::move(members)) {}

    const std::string& selector_label() const noexcept { return selector_label_; }
    const std::vector<GroupMember>& members() const noexcept { return members_; }

    std::size_t size() const noexcept { return members_.size(); }
    bool empty() const noexcept { return members_.empty(); }

    // 以路徑查找成員（不存在回 nullptr）。
    const GroupMember* find(const std::string& path) const;

private:
    std::string selector_label_;
    std::vector<GroupMember> members_;
};

// 從 Value 樹選出符合選擇器的所有 Map 節點，組成 Group。
//   - 遍歷整棵樹（含巢狀 Map / List），對每個 Map 節點以選擇器求值。
//   - 前序、決定性順序；成員路徑於樹中唯一。
//   - 無命中 → 回空 Group（合法，非錯誤）。
Group select(const Value& root, const Selector& selector);

// ---------------------------------------------------------------------------
// 批次命令：對群組每個成員經 E6-01 CommandBus 分派命令，逐成員回報。
// ---------------------------------------------------------------------------

// 單一成員的命令分派結果。
struct GroupMemberResult {
    std::string path;                     // 成員規範路徑
    std::string label;                    // 成員友善識別
    ds::command::CommandResult result;    // 該成員的分派結果（Ok / Failed / NotFound）
};

// 整組批次命令的結果：逐成員結果 + 彙總查詢。
struct GroupResult {
    std::vector<GroupMemberResult> per_member;

    std::size_t size() const noexcept { return per_member.size(); }
    bool empty() const noexcept { return per_member.empty(); }

    std::size_t ok_count() const noexcept;
    std::size_t failed_count() const noexcept;
    std::size_t not_found_count() const noexcept;

    // 是否全部成員皆 Ok。空群組 → true（無成員失敗）。
    bool all_ok() const noexcept;
    // 是否有任一成員非 Ok（Failed 或 NotFound）。
    bool any_error() const noexcept;
};

// 每成員參數建構器：由呼叫端依成員（可讀其節點屬性）決定該成員的分派參數。
using MemberArgsFn = std::function<ds::command::CommandArgs(const GroupMember&)>;

// 對群組每個成員分派同一具名命令，所有成員共用同一組固定參數。
//   - 逐成員呼叫 bus.dispatch(command, args)，收集每個成員的 CommandResult。
//   - 未知命令 → 該成員得 NotFound（匯流排產生，不崩潰）；處理器失敗 → 該成員得 Failed。
//     部分失敗逐成員回報、不靜默。
//   - 空群組 → 回空 GroupResult（合法）。
GroupResult apply_to_group(const ds::command::CommandBus& bus,
                           const Group& group,
                           const ds::command::CommandId& command,
                           const ds::command::CommandArgs& args = {});

// 多載：以每成員參數建構器產生各成員的參數（可據成員節點屬性客製）。
GroupResult apply_to_group(const ds::command::CommandBus& bus,
                           const Group& group,
                           const ds::command::CommandId& command,
                           const MemberArgsFn& make_args);

// ---------------------------------------------------------------------------
// 批次屬性套用：對整組套用共同屬性變更，產出每個成員的更新副本。
//
// E7-01 的 Value 無就地變更 API，故本操作**不改動來源樹**，而是為每個 Map 成員產出
// 套用變更後的**新副本**（既有鍵保序覆寫、新鍵附加於後）。非 Map 成員無法套用，
// 逐一回報原因（不靜默）。
// ---------------------------------------------------------------------------

// 一項屬性變更：設定 / 覆寫 key 為 value。
struct AttributeChange {
    std::string key;
    Value value;
};

// 單一成員的屬性套用結果。
struct MemberAttributeResult {
    std::string path;     // 成員規範路徑
    std::string label;    // 成員友善識別
    bool applied;         // 是否成功套用（非 Map 成員為 false）
    std::string message;  // 未套用時的原因（applied==true 時為空）
    Value updated;        // 套用後的新 Value（僅 applied==true 時有效）
};

// 整組批次屬性套用結果。
struct GroupAttributeResult {
    std::vector<MemberAttributeResult> per_member;

    std::size_t size() const noexcept { return per_member.size(); }
    bool empty() const noexcept { return per_member.empty(); }

    std::size_t applied_count() const noexcept;
    // 是否全部成員皆成功套用。空群組 → true。
    bool all_applied() const noexcept;
};

// 對群組每個成員套用一組共同屬性變更。
//   - Map 成員：複製其成員清單，對每個 change 覆寫既有鍵（保序）或附加新鍵，產出新 Value。
//   - 非 Map 成員：applied=false，message 說明原因（不靜默）。
//   - 空 changes 對 Map 成員視為成功套用（等同不變更的副本）。
GroupAttributeResult apply_attributes(const Group& group,
                                      const std::vector<AttributeChange>& changes);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_11_GROUP_HPP
