// E7-11 群組與批次操作 — 實作（engine 層 / 平台中立純邏輯）
#include "group.hpp"

#include <utility>

namespace ds::format {

// ---------------------------------------------------------------------------
// Selector
// ---------------------------------------------------------------------------

Selector Selector::tagged(std::string value, std::string key) {
    // value 為空 → 退化為存在性選取。
    if (value.empty()) return has(std::move(key));
    std::string label = key + "=" + value;
    Predicate pred = [key = std::move(key), value = std::move(value)](const Value& node) {
        if (!node.is_map()) return false;
        const Value* v = node.find(key);
        return v != nullptr && v->is_string() && v->as_string() == value;
    };
    return Selector(std::move(pred), std::move(label));
}

Selector Selector::has(std::string key) {
    std::string label = "has:" + key;
    Predicate pred = [key = std::move(key)](const Value& node) {
        return node.is_map() && node.contains(key);
    };
    return Selector(std::move(pred), std::move(label));
}

Selector Selector::where(Predicate pred, std::string label) {
    // 包一層以保證 node 為 Map 才求值（與其他工廠語意一致）。
    Predicate guarded = [inner = std::move(pred)](const Value& node) {
        return node.is_map() && inner && inner(node);
    };
    return Selector(std::move(guarded), std::move(label));
}

bool Selector::matches(const Value& node) const {
    return pred_ && pred_(node);
}

// ---------------------------------------------------------------------------
// Group
// ---------------------------------------------------------------------------

const GroupMember* Group::find(const std::string& path) const {
    for (const auto& m : members_) {
        if (m.path == path) return &m;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// select — 前序遍歷樹，對每個 Map 節點求選擇器。
// ---------------------------------------------------------------------------

namespace {

// 取成員友善標籤：優先 "id"，其次 "name"（皆須為字串），否則退化為 path。
std::string derive_label(const Value& node, const std::string& path) {
    if (node.is_map()) {
        if (const Value* id = node.find("id"); id && id->is_string() && !id->as_string().empty()) {
            return id->as_string();
        }
        if (const Value* nm = node.find("name"); nm && nm->is_string() && !nm->as_string().empty()) {
            return nm->as_string();
        }
    }
    return path;
}

void walk(const Value& node, const std::string& path, const Selector& sel,
          std::vector<GroupMember>& out) {
    // 前序：先看本節點是否命中，再遞迴子節點。
    if (sel.matches(node)) {
        out.push_back(GroupMember{path, derive_label(node, path), &node});
    }
    if (node.is_map()) {
        for (const auto& kv : node.as_map()) {
            const std::string child = (path == "/") ? ("/" + kv.first) : (path + "/" + kv.first);
            walk(kv.second, child, sel, out);
        }
    } else if (node.is_list()) {
        const auto& items = node.as_list();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::string child =
                (path == "/") ? ("/" + std::to_string(i)) : (path + "/" + std::to_string(i));
            walk(items[i], child, sel, out);
        }
    }
}

}  // namespace

Group select(const Value& root, const Selector& selector) {
    std::vector<GroupMember> members;
    walk(root, "/", selector, members);
    return Group(selector.label(), std::move(members));
}

// ---------------------------------------------------------------------------
// GroupResult 彙總
// ---------------------------------------------------------------------------

std::size_t GroupResult::ok_count() const noexcept {
    std::size_t n = 0;
    for (const auto& m : per_member) {
        if (m.result.status == ds::command::CommandStatus::Ok) ++n;
    }
    return n;
}

std::size_t GroupResult::failed_count() const noexcept {
    std::size_t n = 0;
    for (const auto& m : per_member) {
        if (m.result.status == ds::command::CommandStatus::Failed) ++n;
    }
    return n;
}

std::size_t GroupResult::not_found_count() const noexcept {
    std::size_t n = 0;
    for (const auto& m : per_member) {
        if (m.result.status == ds::command::CommandStatus::NotFound) ++n;
    }
    return n;
}

bool GroupResult::all_ok() const noexcept {
    for (const auto& m : per_member) {
        if (m.result.status != ds::command::CommandStatus::Ok) return false;
    }
    return true;  // 空群組 → true
}

bool GroupResult::any_error() const noexcept {
    for (const auto& m : per_member) {
        if (m.result.status != ds::command::CommandStatus::Ok) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// apply_to_group — 批次命令分派
// ---------------------------------------------------------------------------

GroupResult apply_to_group(const ds::command::CommandBus& bus,
                           const Group& group,
                           const ds::command::CommandId& command,
                           const ds::command::CommandArgs& args) {
    GroupResult result;
    result.per_member.reserve(group.size());
    for (const auto& m : group.members()) {
        // 逐成員分派；未知命令由匯流排回 NotFound（不崩潰），處理器失敗回 Failed。
        result.per_member.push_back(
            GroupMemberResult{m.path, m.label, bus.dispatch(command, args)});
    }
    return result;
}

GroupResult apply_to_group(const ds::command::CommandBus& bus,
                           const Group& group,
                           const ds::command::CommandId& command,
                           const MemberArgsFn& make_args) {
    GroupResult result;
    result.per_member.reserve(group.size());
    for (const auto& m : group.members()) {
        ds::command::CommandArgs args = make_args ? make_args(m) : ds::command::CommandArgs{};
        result.per_member.push_back(
            GroupMemberResult{m.path, m.label, bus.dispatch(command, args)});
    }
    return result;
}

// ---------------------------------------------------------------------------
// GroupAttributeResult 彙總
// ---------------------------------------------------------------------------

std::size_t GroupAttributeResult::applied_count() const noexcept {
    std::size_t n = 0;
    for (const auto& m : per_member) {
        if (m.applied) ++n;
    }
    return n;
}

bool GroupAttributeResult::all_applied() const noexcept {
    for (const auto& m : per_member) {
        if (!m.applied) return false;
    }
    return true;  // 空群組 → true
}

// ---------------------------------------------------------------------------
// apply_attributes — 批次屬性套用（產出新副本，不改來源樹）
// ---------------------------------------------------------------------------

namespace {

// 對一個 Map Value 套用一組屬性變更，回傳新的 Map Value。
//   - 既有鍵：保序覆寫（值換新，位置不變）。
//   - 新鍵：附加於尾端（首次出現順序）。
Value apply_changes_to_map(const Value& map_node,
                           const std::vector<AttributeChange>& changes) {
    std::vector<Value::Member> members = map_node.as_map();  // 複製既有（保序）
    for (const auto& ch : changes) {
        bool replaced = false;
        for (auto& kv : members) {
            if (kv.first == ch.key) {
                kv.second = ch.value;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            members.emplace_back(ch.key, ch.value);
        }
    }
    return Value::map(std::move(members));
}

}  // namespace

GroupAttributeResult apply_attributes(const Group& group,
                                      const std::vector<AttributeChange>& changes) {
    GroupAttributeResult result;
    result.per_member.reserve(group.size());
    for (const auto& m : group.members()) {
        MemberAttributeResult mr;
        mr.path = m.path;
        mr.label = m.label;
        if (m.node != nullptr && m.node->is_map()) {
            mr.applied = true;
            mr.updated = apply_changes_to_map(*m.node, changes);
        } else {
            // 非 Map 成員無法套用屬性——逐一回報，不靜默。
            mr.applied = false;
            mr.message = "member is not a Map; cannot apply attributes";
        }
        result.per_member.push_back(std::move(mr));
    }
    return result;
}

}  // namespace ds::format
