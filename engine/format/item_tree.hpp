// E7-13 階層式項目結構 — 樹狀項目（描述子系統 / E7-01 的應用 / engine 層 / 平台中立）
//
// 本單元在 E7-01 宣告式格式核心之上，提供一個**與領域無關的階層式項目結構**：
// 由 `Item` 節點組成的樹，供啟動器選單樹、對話本、清單等多個上層單元共用。每個 `Item` 帶：
//   - `id`   ：樹內唯一識別字串（供依 id 尋址）。
//   - `label`：人類可讀顯示字串（可空）。
//   - `value`：附帶值（任意 E7-01 `Value`，預設 Null）——承載該項目的領域酬載。
//   - `children`：有序子項目（遞迴）。
//
// 本單元不自造格式模型：完全消費 E7-01 的 `Value` / `Document` 契約，是「更通用格式」的一個
// 應用（項目樹）。可從宣告式文件建構、遍歷、依 id 尋址。屬 engine 層（平台中立純邏輯）：
// 無任何 `#ifdef` / 系統呼叫 / 真實後端，可完全單元測試。
//
// 宣告式建構契約（每個項目 = 一個 Map，鍵僅限下列四個保留鍵；其餘鍵一律報錯，不靜默）：
//     id: launcher            # 必填；非空字串。樹內唯一。
//     label: 啟動器            # 選填；字串。省略則為空字串（不自 id 臆造）。
//     value:                  # 選填；任意 Value（純量 / list / map）作為附帶酬載。
//       weight: 3
//     children:               # 選填；list，每個元素為一個項目 Map（遞迴）。
//       -
//         id: apps
//         label: 應用程式
//
// 錯誤模型（承 E7-01 NFR-04「不得靜默失敗」）：任何結構違反（非 Map、缺 / 空 / 非字串 id、
// 重複 id、label / children 型別不符、未知鍵）一律回傳帶可定位訊息的 `BuildError`，不吞掉。
#ifndef DS_ENGINE_E7_13_ITEM_TREE_HPP
#define DS_ENGINE_E7_13_ITEM_TREE_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "document.hpp"  // E7-01：Value / Document

namespace ds::format {

// -----------------------------------------------------------------------------
// 宣告式建構的保留鍵
// -----------------------------------------------------------------------------
namespace item_keys {
inline constexpr const char* kId = "id";
inline constexpr const char* kLabel = "label";
inline constexpr const char* kValue = "value";
inline constexpr const char* kChildren = "children";
}  // namespace item_keys

// -----------------------------------------------------------------------------
// Item：階層式項目樹的節點
// -----------------------------------------------------------------------------
//
// 值語意（可複製）。子項目以有序 vector 遞迴持有。`value` 為附帶酬載（預設 Null）。
class Item {
public:
    Item() = default;
    explicit Item(std::string id, std::string label = std::string(),
                  Value value = Value::null());

    // --- 查詢 ---
    const std::string& id() const noexcept { return id_; }
    const std::string& label() const noexcept { return label_; }  // 可為空字串。
    const Value& value() const noexcept { return value_; }         // 附帶酬載；預設 Null。
    const std::vector<Item>& children() const noexcept { return children_; }

    bool is_leaf() const noexcept { return children_.empty(); }
    std::size_t child_count() const noexcept { return children_.size(); }
    // 整棵子樹（含自身）的節點總數。
    std::size_t size() const noexcept;
    // 樹的層數（單一節點 = 1；空樹概念不存在——Item 恆為一個節點）。
    std::size_t depth() const noexcept;

    // --- 程式化建構（供呼叫端 / 測試）---
    Item& set_label(std::string label);
    Item& set_value(Value v);
    Item& add_child(Item child);

    // --- 依 id 尋址 ---
    // 前序遍歷尋找 id 相符的「自身或後代」，回傳指標；找不到回 nullptr。
    // 由宣告式文件建構者其 id 於全樹唯一，故至多一個相符。
    const Item* find(const std::string& id) const noexcept;
    Item* find(const std::string& id) noexcept;
    bool contains(const std::string& id) const noexcept { return find(id) != nullptr; }

    // 深層相等（id + label + value + children 遞迴比較，保序）。便於測試。
    bool operator==(const Item& o) const;
    bool operator!=(const Item& o) const { return !(*this == o); }

private:
    std::string id_;
    std::string label_;
    Value value_{};
    std::vector<Item> children_;
};

// -----------------------------------------------------------------------------
// 遍歷
// -----------------------------------------------------------------------------

// 前序（pre-order）遍歷：對 root 及其每個後代呼叫 visit(node, depth)。
// root 的 depth 為 0，其直接子項目為 1，依此類推。
void for_each_preorder(const Item& root,
                       const std::function<void(const Item&, int depth)>& visit);

// -----------------------------------------------------------------------------
// 從宣告式文件建構（錯誤可定位）
// -----------------------------------------------------------------------------

// 建構錯誤 —— 一律帶可定位的結構脈絡訊息（不得靜默失敗）。
struct BuildError {
    std::string message;  // 人類可讀原因，含結構脈絡（如 item '<id>' child #n）。
};

// 建構單一項目樹的結果：成功持有 Item，失敗持有 BuildError。二者互斥。
class ItemResult {
public:
    static ItemResult success(Item item);
    static ItemResult failure(BuildError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Item& item() const { return item_; }        // 僅 ok() 為 true 時有效。
    const BuildError& error() const { return error_; }  // 僅 ok() 為 false 時有效。

private:
    ItemResult() = default;
    bool ok_ = false;
    Item item_;
    BuildError error_;
};

// 建構森林（多個頂層項目）的結果。
class ForestResult {
public:
    static ForestResult success(std::vector<Item> items);
    static ForestResult failure(BuildError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const std::vector<Item>& items() const { return items_; }  // 僅 ok() 為 true 時有效。
    const BuildError& error() const { return error_; }          // 僅 ok() 為 false 時有效。

private:
    ForestResult() = default;
    bool ok_ = false;
    std::vector<Item> items_;
    BuildError error_;
};

// 從一個宣告式 Value 節點（須為 Map）建構一棵項目樹。id 於整棵樹須唯一。
ItemResult build_item(const Value& node);

// 從一份已解析的 Document 建構：以 doc.root（純內容 Map）作為根項目的 Map。
ItemResult build_item(const Document& doc);

// 從一個 List Value（每個元素為項目 Map）建構森林（頂層多根）。id 於**整座森林**須唯一。
// 空 list → 空森林（成功、items 為空）。非 List → 失敗。
ForestResult build_forest(const Value& list);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_13_ITEM_TREE_HPP
