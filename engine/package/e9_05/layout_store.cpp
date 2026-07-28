// E9-05 佈局存檔與還原 — 實作。見 layout_store.hpp。
#include "layout_store.hpp"

#include <stdexcept>
#include <utility>

namespace ds::layout {

using ds::format::Document;
using ds::format::ParseResult;
using ds::format::Value;

namespace {

// 空 / Null properties 一律以空 Map 呈現，確保序列化 / 還原語意一致。
Value normalized_properties(const Value& v) {
    if (v.is_map()) {
        return v;
    }
    if (v.is_null()) {
        return Value::map({});
    }
    throw std::runtime_error(
        "E9-05: LayoutElement.properties 必須為 Map（或 Null 視為空 Map）");
}

// LayoutState -> Value（Map）。結構：{ elements: [ { id, type, properties }, ... ] }。
Value layout_to_value(const LayoutState& state) {
    std::vector<Value> elems;
    elems.reserve(state.elements.size());
    for (const LayoutElement& e : state.elements) {
        if (e.id.empty()) {
            throw std::runtime_error("E9-05: LayoutElement.id 不得為空");
        }
        if (e.type.empty()) {
            throw std::runtime_error("E9-05: LayoutElement.type 不得為空");
        }
        std::vector<Value::Member> members;
        members.emplace_back("id", Value::string(e.id));
        members.emplace_back("type", Value::string(e.type));
        members.emplace_back("properties", normalized_properties(e.properties));
        elems.push_back(Value::map(std::move(members)));
    }
    std::vector<Value::Member> root;
    root.emplace_back("elements", Value::list(std::move(elems)));
    return Value::map(std::move(root));
}

// 取 Map 內某鍵之字串（缺鍵 / 型別不符 → throw，明確不靜默）。
std::string require_string(const Value& map, const std::string& key) {
    const Value* v = map.find(key);
    if (v == nullptr) {
        throw std::runtime_error("E9-05: 佈局元件缺少必要欄位 '" + key + "'");
    }
    if (!v->is_string()) {
        throw std::runtime_error("E9-05: 佈局元件欄位 '" + key + "' 必須為字串");
    }
    return v->as_string();
}

// Value（root Map）-> LayoutState。結構違反 → throw。
LayoutState value_to_layout(const Value& root) {
    if (!root.is_map()) {
        throw std::runtime_error("E9-05: 佈局根節點必須為 Map");
    }
    LayoutState state;

    const Value* elems = root.find("elements");
    // 缺 elements 或為 Null（空清單於 E7-01 塌為 Null）→ 視為空佈局。
    if (elems == nullptr || elems->is_null()) {
        return state;
    }
    if (!elems->is_list()) {
        throw std::runtime_error("E9-05: 'elements' 必須為清單");
    }
    for (const Value& item : elems->as_list()) {
        if (!item.is_map()) {
            throw std::runtime_error("E9-05: 'elements' 的每個項目必須為 Map");
        }
        LayoutElement e;
        e.id = require_string(item, "id");
        e.type = require_string(item, "type");
        const Value* props = item.find("properties");
        e.properties = (props != nullptr) ? normalized_properties(*props) : Value::map({});
        state.elements.push_back(std::move(e));
    }
    return state;
}

}  // namespace

// -----------------------------------------------------------------------------
// LayoutElement 相等
// -----------------------------------------------------------------------------

bool LayoutElement::operator==(const LayoutElement& o) const {
    return id == o.id && type == o.type &&
           normalized_properties(properties) == normalized_properties(o.properties);
}

// -----------------------------------------------------------------------------
// 序列化 / 反序列化（透過 E7-12 / E7-01）
// -----------------------------------------------------------------------------

std::string serialize_layout(const LayoutState& state) {
    // 以 E7-12 的 serialize(root, version)：root 為 Map，產出帶 format_version 的宣告式文字。
    return ds::format::serialize(layout_to_value(state), kLayoutFormat);
}

LayoutState deserialize_layout(const std::string& text) {
    ParseResult result = ds::format::parse(text);
    if (!result.ok()) {
        const ds::format::ParseError& err = result.error();
        throw std::runtime_error("E9-05: 佈局文字解析失敗（行 " +
                                 std::to_string(err.line) + "）：" + err.message);
    }
    const Document& doc = result.document();
    return value_to_layout(doc.root);
}

// -----------------------------------------------------------------------------
// MemoryLayoutStorage
// -----------------------------------------------------------------------------

void MemoryLayoutStorage::put(const std::string& name, const std::string& text) {
    data_[name] = text;  // 覆寫既有。
}

bool MemoryLayoutStorage::get(const std::string& name, std::string& out) const {
    auto it = data_.find(name);
    if (it == data_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool MemoryLayoutStorage::has(const std::string& name) const {
    return data_.find(name) != data_.end();
}

bool MemoryLayoutStorage::erase(const std::string& name) {
    return data_.erase(name) > 0;
}

std::vector<std::string> MemoryLayoutStorage::names() const {
    std::vector<std::string> out;
    out.reserve(data_.size());
    for (const auto& kv : data_) {
        out.push_back(kv.first);  // std::map 有序 → 字典序。
    }
    return out;
}

// -----------------------------------------------------------------------------
// LayoutStore
// -----------------------------------------------------------------------------

LayoutStore::LayoutStore(LayoutStorage& storage) : storage_(storage) {}

void LayoutStore::save(const std::string& name, const LayoutState& state) {
    if (name.empty()) {
        throw std::runtime_error("E9-05: 佈局名稱不得為空");
    }
    storage_.put(name, serialize_layout(state));  // 既有同名 → 覆寫。
}

LayoutState LayoutStore::load(const std::string& name) const {
    std::string text;
    if (!storage_.get(name, text)) {
        throw std::runtime_error("E9-05: 查無名為 '" + name + "' 的佈局");
    }
    return deserialize_layout(text);
}

bool LayoutStore::contains(const std::string& name) const {
    return storage_.has(name);
}

std::vector<std::string> LayoutStore::list() const {
    return storage_.names();
}

void LayoutStore::remove(const std::string& name) {
    if (!storage_.erase(name)) {
        throw std::runtime_error("E9-05: 查無名為 '" + name + "' 的佈局，無法刪除");
    }
}

}  // namespace ds::layout
