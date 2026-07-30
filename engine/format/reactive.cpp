// E7-04 動態變數與執行期重算 — 實作。見 reactive.hpp 檔首語意。平台中立、無任何 `#ifdef`。
#include "reactive.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ds::format {

// -----------------------------------------------------------------------------
// DerivedInputs
// -----------------------------------------------------------------------------

bool DerivedInputs::has(const std::string& name) const {
    if (deps_ == nullptr) {
        return false;
    }
    return std::find(deps_->begin(), deps_->end(), name) != deps_->end();
}

const Value& DerivedInputs::get(const std::string& name) const {
    // 只允許讀取已宣告的 dep：讀未宣告名稱會使相依追蹤失真 → 契約違反。
    if (!has(name)) {
        throw std::runtime_error(
            "DerivedInputs::get: '" + name + "' 不是本衍生變數宣告的相依（deps）");
    }
    const ReactiveScope::Node* n = scope_->find_node(name);
    if (n == nullptr || !n->has_value) {
        throw std::runtime_error(
            "DerivedInputs::get: 相依變數 '" + name + "' 尚無可用值");
    }
    return n->value;
}

// -----------------------------------------------------------------------------
// ReactiveResult
// -----------------------------------------------------------------------------

ReactiveResult ReactiveResult::success(Value v) {
    ReactiveResult r;
    r.ok_ = true;
    r.value_ = std::move(v);
    return r;
}

ReactiveResult ReactiveResult::failure(ReactiveError e) {
    ReactiveResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// -----------------------------------------------------------------------------
// 節點查找
// -----------------------------------------------------------------------------

ReactiveScope::Node* ReactiveScope::find_node(const std::string& name) {
    for (auto& n : nodes_) {
        if (n.name == name) {
            return &n;
        }
    }
    return nullptr;
}

const ReactiveScope::Node* ReactiveScope::find_node(const std::string& name) const {
    for (const auto& n : nodes_) {
        if (n.name == name) {
            return &n;
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// 循環偵測（於 define 當下、以將要套用的 new_deps 檢查）
// -----------------------------------------------------------------------------

bool ReactiveScope::reaches(const std::string& from, const std::string& target,
                            const std::string& override_name,
                            const std::vector<std::string>& override_deps,
                            std::vector<std::string>& visiting) const {
    if (from == target) {
        return true;
    }
    if (std::find(visiting.begin(), visiting.end(), from) != visiting.end()) {
        return false;  // 既有圖中的環（不含 target）——非本次要判定的循環，剪枝。
    }
    visiting.push_back(from);

    // from 的相依邊：若 from 正是要（重）定義的節點，用即將套用的 override_deps。
    const std::vector<std::string>* edges = &override_deps;
    if (from != override_name) {
        const Node* n = find_node(from);
        edges = (n != nullptr) ? &n->deps : nullptr;
    }
    if (edges != nullptr) {
        for (const auto& d : *edges) {
            if (reaches(d, target, override_name, override_deps, visiting)) {
                visiting.pop_back();
                return true;
            }
        }
    }
    visiting.pop_back();
    return false;
}

bool ReactiveScope::would_create_cycle(const std::string& name,
                                       const std::vector<std::string>& new_deps) const {
    for (const auto& d : new_deps) {
        if (d == name) {
            return true;  // 直接自我相依。
        }
        std::vector<std::string> visiting;
        // name 依賴 d；若 d 能（遞迴，經 name 的 new_deps）回到 name → 形成環。
        if (reaches(d, name, name, new_deps, visiting)) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// 髒標記傳播 + 拓撲重算
// -----------------------------------------------------------------------------

void ReactiveScope::mark_dependents_dirty(const std::string& root) {
    std::vector<std::string> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        std::string cur = stack.back();
        stack.pop_back();
        const Node* n = find_node(cur);
        if (n == nullptr) {
            continue;
        }
        for (const auto& dep_name : n->dependents) {
            Node* d = find_node(dep_name);
            if (d != nullptr && !d->dirty) {
                d->dirty = true;  // 只標記一次即入堆疊，避免重複展開。
                stack.push_back(dep_name);
            }
        }
    }
}

const Value& ReactiveScope::ensure_computed(Node& node, std::vector<std::string>& changed) {
    if (node.kind == Kind::Source) {
        if (!node.has_value) {
            throw ReactiveException{
                ReactiveError{"來源變數 '" + node.name + "' 尚未設值", node.name}};
        }
        node.dirty = false;
        return node.value;
    }

    // Derived：已算過且不髒 → 直接回傳快取（「只重算受影響者」的核心）。
    if (node.has_value && !node.dirty) {
        return node.value;
    }

    // 執行期防禦性循環偵測（define 已擋，但變更後仍守一道）。
    if (std::find(computing_.begin(), computing_.end(), node.name) != computing_.end()) {
        throw ReactiveException{
            ReactiveError{"偵測到執行期循環相依：'" + node.name + "'", node.name}};
    }
    computing_.push_back(node.name);

    // 先確保所有相依為最新（遞迴 = 拓撲序：相依先於被依賴者求值）。
    for (const auto& dep_name : node.deps) {
        Node* d = find_node(dep_name);
        if (d == nullptr) {
            computing_.pop_back();
            throw ReactiveException{
                ReactiveError{"衍生變數 '" + node.name + "' 的相依 '" + dep_name +
                                  "' 不存在",
                              node.name}};
        }
        ensure_computed(*d, changed);
    }

    DerivedInputs inputs(this, &node.deps);
    Value newval = node.compute(inputs);

    bool value_changed = !node.has_value || node.value != newval;
    node.value = std::move(newval);
    node.has_value = true;
    node.dirty = false;

    computing_.pop_back();

    if (value_changed) {
        changed.push_back(node.name);
    }
    return node.value;
}

void ReactiveScope::notify(const std::vector<std::string>& changed) {
    if (callbacks_.empty() || changed.empty()) {
        return;
    }
    // 依建立序去重觸發：對每個建立序中的名稱，若其在本次變更集合內則通知一次。
    for (const auto& name : order_) {
        if (std::find(changed.begin(), changed.end(), name) == changed.end()) {
            continue;
        }
        for (const auto& cb : callbacks_) {
            if (cb) {
                cb(name);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// 公開 API
// -----------------------------------------------------------------------------

ReactiveResult ReactiveScope::set(const std::string& name, Value value) {
    if (const Node* existing = find_node(name)) {
        if (existing->kind == Kind::Derived) {
            return ReactiveResult::failure(
                {"'" + name + "' 是衍生變數，不可直接 set（其值由 compute 決定）", name});
        }
    } else {
        Node n;
        n.name = name;
        n.kind = Kind::Source;
        nodes_.push_back(std::move(n));  // 可能 realloc → 之後一律以 find_node 重取。
        order_.push_back(name);
    }

    Node* node = find_node(name);
    bool root_changed = !node->has_value || node->value != value;
    node->value = std::move(value);
    node->has_value = true;
    node->dirty = false;

    // 相依追蹤：沿反向邊把下游全部標髒，再以拓撲序只重算受影響者。
    mark_dependents_dirty(name);

    std::vector<std::string> changed;
    if (root_changed) {
        changed.push_back(name);
    }
    try {
        for (const auto& nm : order_) {
            Node* nd = find_node(nm);
            if (nd->kind == Kind::Derived && nd->dirty) {
                ensure_computed(*nd, changed);
            }
        }
    } catch (const ReactiveException& ex) {
        return ReactiveResult::failure(ex.err);
    }

    notify(changed);
    return ReactiveResult::success(find_node(name)->value);
}

ReactiveResult ReactiveScope::define_derived(const std::string& name,
                                             std::vector<std::string> deps,
                                             ComputeFn compute) {
    if (!compute) {
        return ReactiveResult::failure({"define_derived: compute 不可為空", name});
    }
    // 每個 dep 必須已存在（相依圖須完整，便於自底向上建構）。
    for (const auto& d : deps) {
        if (find_node(d) == nullptr) {
            return ReactiveResult::failure(
                {"衍生變數 '" + name + "' 的相依 '" + d + "' 尚未定義", d});
        }
    }
    if (const Node* existing = find_node(name)) {
        if (existing->kind == Kind::Source) {
            return ReactiveResult::failure(
                {"'" + name + "' 已是來源變數，無法重定義為衍生變數", name});
        }
    }
    // 循環偵測——在改動任何狀態之前判定（失敗則作用域維持原樣）。
    if (would_create_cycle(name, deps)) {
        return ReactiveResult::failure(
            {"偵測到循環相依：定義 '" + name + "' 會使其（遞迴）依賴自身", name});
    }

    // 套用：重定義先清掉舊的反向邊。
    if (Node* existing = find_node(name)) {
        for (const auto& old_dep : existing->deps) {
            if (Node* dn = find_node(old_dep)) {
                auto& v = dn->dependents;
                v.erase(std::remove(v.begin(), v.end(), name), v.end());
            }
        }
        existing->deps = std::move(deps);
        existing->compute = std::move(compute);
        existing->dirty = true;
        existing->has_value = false;
    } else {
        Node n;
        n.name = name;
        n.kind = Kind::Derived;
        n.deps = std::move(deps);
        n.compute = std::move(compute);
        n.dirty = true;
        nodes_.push_back(std::move(n));  // 可能 realloc。
        order_.push_back(name);
    }

    // 建立新的反向邊：每個 dep 記錄 name 為其 dependent（去重）。
    {
        Node* node = find_node(name);
        for (const auto& d : node->deps) {
            Node* dn = find_node(d);
            auto& v = dn->dependents;
            if (std::find(v.begin(), v.end(), name) == v.end()) {
                v.push_back(name);
            }
        }
    }

    // 求值本節點與（重定義時）受影響的下游。
    mark_dependents_dirty(name);
    std::vector<std::string> changed;
    try {
        ensure_computed(*find_node(name), changed);
        for (const auto& nm : order_) {
            Node* nd = find_node(nm);
            if (nd->kind == Kind::Derived && nd->dirty) {
                ensure_computed(*nd, changed);
            }
        }
    } catch (const ReactiveException& ex) {
        return ReactiveResult::failure(ex.err);
    }

    notify(changed);
    return ReactiveResult::success(find_node(name)->value);
}

ReactiveResult ReactiveScope::get(const std::string& name) const {
    const Node* node = find_node(name);
    if (node == nullptr) {
        return ReactiveResult::failure({"未知變數 '" + name + "'", name});
    }
    if (node->kind == Kind::Source) {
        if (!node->has_value) {
            return ReactiveResult::failure({"來源變數 '" + name + "' 尚未設值", name});
        }
        return ReactiveResult::success(node->value);
    }
    // Derived：若為髒（延遲求值路徑），就地重算。快取更新在邏輯上仍為 const。
    if (node->dirty || !node->has_value) {
        ReactiveScope* self = const_cast<ReactiveScope*>(this);
        std::vector<std::string> changed;
        try {
            self->ensure_computed(*self->find_node(name), changed);
        } catch (const ReactiveException& ex) {
            return ReactiveResult::failure(ex.err);
        }
        self->notify(changed);
    }
    return ReactiveResult::success(find_node(name)->value);
}

bool ReactiveScope::has(const std::string& name) const {
    return find_node(name) != nullptr;
}

bool ReactiveScope::is_derived(const std::string& name) const {
    const Node* n = find_node(name);
    return n != nullptr && n->kind == Kind::Derived;
}

std::vector<std::string> ReactiveScope::names() const {
    return order_;  // 建立序。
}

void ReactiveScope::on_change(ChangeCallback cb) {
    callbacks_.push_back(std::move(cb));
}

}  // namespace ds::format
