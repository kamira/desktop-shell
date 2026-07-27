// E7-13 階層式項目結構 — 實作。見 item_tree.hpp。
//
// 平台中立純邏輯：無 `#ifdef` / 系統呼叫 / 真實後端。消費 E7-01 的 Value / Document。
#include "item_tree.hpp"

#include <unordered_set>
#include <utility>

namespace ds::format {

// -----------------------------------------------------------------------------
// Item
// -----------------------------------------------------------------------------

Item::Item(std::string id, std::string label, Value value)
    : id_(std::move(id)), label_(std::move(label)), value_(std::move(value)) {}

Item& Item::set_label(std::string label) {
    label_ = std::move(label);
    return *this;
}

Item& Item::set_value(Value v) {
    value_ = std::move(v);
    return *this;
}

Item& Item::add_child(Item child) {
    children_.push_back(std::move(child));
    return *this;
}

std::size_t Item::size() const noexcept {
    std::size_t n = 1;  // 自身。
    for (const Item& c : children_) {
        n += c.size();
    }
    return n;
}

std::size_t Item::depth() const noexcept {
    std::size_t deepest = 0;
    for (const Item& c : children_) {
        const std::size_t d = c.depth();
        if (d > deepest) {
            deepest = d;
        }
    }
    return 1 + deepest;
}

const Item* Item::find(const std::string& id) const noexcept {
    if (id_ == id) {
        return this;
    }
    for (const Item& c : children_) {
        if (const Item* hit = c.find(id)) {
            return hit;
        }
    }
    return nullptr;
}

Item* Item::find(const std::string& id) noexcept {
    // 委由 const 版本，再去 const（*this 非 const，去 const 安全）。
    return const_cast<Item*>(static_cast<const Item*>(this)->find(id));
}

bool Item::operator==(const Item& o) const {
    return id_ == o.id_ && label_ == o.label_ && value_ == o.value_ &&
           children_ == o.children_;
}

// -----------------------------------------------------------------------------
// 遍歷
// -----------------------------------------------------------------------------

namespace {

void preorder_impl(const Item& node, int depth,
                   const std::function<void(const Item&, int)>& visit) {
    visit(node, depth);
    for (const Item& c : node.children()) {
        preorder_impl(c, depth + 1, visit);
    }
}

}  // namespace

void for_each_preorder(const Item& root,
                       const std::function<void(const Item&, int depth)>& visit) {
    if (!visit) {
        return;
    }
    preorder_impl(root, 0, visit);
}

// -----------------------------------------------------------------------------
// 結果型別
// -----------------------------------------------------------------------------

ItemResult ItemResult::success(Item item) {
    ItemResult r;
    r.ok_ = true;
    r.item_ = std::move(item);
    return r;
}

ItemResult ItemResult::failure(BuildError e) {
    ItemResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

ForestResult ForestResult::success(std::vector<Item> items) {
    ForestResult r;
    r.ok_ = true;
    r.items_ = std::move(items);
    return r;
}

ForestResult ForestResult::failure(BuildError e) {
    ForestResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// -----------------------------------------------------------------------------
// 建構：宣告式 Value → Item（錯誤可定位、不靜默）
// -----------------------------------------------------------------------------

namespace {

// 保留鍵集合檢查。
bool is_reserved_key(const std::string& key) {
    return key == item_keys::kId || key == item_keys::kLabel ||
           key == item_keys::kValue || key == item_keys::kChildren;
}

// 遞迴建構單一節點。ctx = 給人看的定位脈絡（如 "root" / "item 'apps' child #1"）。
// seen = 整棵樹 / 森林已見的 id（用於偵測重複）。
// 成功回 true 並填 out；失敗回 false 並填 err。
bool build_node(const Value& node, const std::string& ctx,
                std::unordered_set<std::string>& seen, Item& out, BuildError& err) {
    if (!node.is_map()) {
        err.message = ctx + ": item must be a map";
        return false;
    }

    // 未知鍵：一律報錯（不靜默吞掉可能的拼字錯誤）。
    for (const std::string& key : node.keys()) {
        if (!is_reserved_key(key)) {
            err.message = ctx + ": unknown key '" + key + "'";
            return false;
        }
    }

    // id：必填、字串、非空、樹內唯一。
    const Value* id_v = node.find(item_keys::kId);
    if (id_v == nullptr) {
        err.message = ctx + ": missing required key 'id'";
        return false;
    }
    if (!id_v->is_string()) {
        err.message = ctx + ": 'id' must be a string";
        return false;
    }
    const std::string& id = id_v->as_string();
    if (id.empty()) {
        err.message = ctx + ": 'id' must not be empty";
        return false;
    }
    if (!seen.insert(id).second) {
        err.message = ctx + ": duplicate id '" + id + "'";
        return false;
    }

    // label：選填、字串。省略則為空字串。
    std::string label;
    if (const Value* label_v = node.find(item_keys::kLabel)) {
        if (!label_v->is_string()) {
            err.message = "item '" + id + "': 'label' must be a string";
            return false;
        }
        label = label_v->as_string();
    }

    // value：選填、任意 Value 作為附帶酬載。
    Value payload = Value::null();
    if (const Value* val_v = node.find(item_keys::kValue)) {
        payload = *val_v;
    }

    out = Item(id, std::move(label), std::move(payload));

    // children：選填、list，每元素為項目 Map（遞迴）。
    if (const Value* kids = node.find(item_keys::kChildren)) {
        if (!kids->is_list()) {
            err.message = "item '" + id + "': 'children' must be a list";
            return false;
        }
        const std::vector<Value>& elems = kids->as_list();
        for (std::size_t i = 0; i < elems.size(); ++i) {
            Item child;
            const std::string child_ctx =
                "item '" + id + "' child #" + std::to_string(i);
            if (!build_node(elems[i], child_ctx, seen, child, err)) {
                return false;
            }
            out.add_child(std::move(child));
        }
    }

    return true;
}

}  // namespace

ItemResult build_item(const Value& node) {
    std::unordered_set<std::string> seen;
    Item root;
    BuildError err;
    if (!build_node(node, "root", seen, root, err)) {
        return ItemResult::failure(std::move(err));
    }
    return ItemResult::success(std::move(root));
}

ItemResult build_item(const Document& doc) { return build_item(doc.root); }

ForestResult build_forest(const Value& list) {
    if (!list.is_list()) {
        return ForestResult::failure(BuildError{"forest: root must be a list"});
    }
    std::unordered_set<std::string> seen;  // id 於整座森林唯一。
    std::vector<Item> items;
    const std::vector<Value>& elems = list.as_list();
    items.reserve(elems.size());
    for (std::size_t i = 0; i < elems.size(); ++i) {
        Item item;
        BuildError err;
        const std::string ctx = "forest item #" + std::to_string(i);
        if (!build_node(elems[i], ctx, seen, item, err)) {
            return ForestResult::failure(std::move(err));
        }
        items.push_back(std::move(item));
    }
    return ForestResult::success(std::move(items));
}

}  // namespace ds::format
