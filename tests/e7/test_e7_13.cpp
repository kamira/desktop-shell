// E7-13 階層式項目結構 — 契約測試（gtest）
//
// 涵蓋：程式化建樹 + 查詢（child_count / size / depth / is_leaf）、前序遍歷（收集 id 與
// depth）、依 id 尋址（命中 / 未命中 / 深層 / const 與非 const）、從宣告式文件建構
// （單節點 / 巢狀 / label 省略 / 附帶值 / children 遞迴 / Document 多載）、森林、
// 空樹與空森林、以及非靜默錯誤（非 Map / 缺 id / 空 id / 非字串 id / 重複 id /
// label 型別錯 / children 型別錯 / 未知鍵）。平台中立：不含任何平台分支。
#include "item_tree.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::format::build_forest;
using ds::format::build_item;
using ds::format::Document;
using ds::format::for_each_preorder;
using ds::format::ForestResult;
using ds::format::Item;
using ds::format::ItemResult;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::Value;

namespace {

// 小工具：解析一段文字，斷言成功並回傳 Document。
Document must_parse(const std::string& text) {
    ParseResult r = parse(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.document();
}

// 小工具：以宣告式文字建一棵樹，斷言成功並回傳 Item。
Item must_build(const std::string& text) {
    ItemResult r = build_item(must_parse(text));
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.item();
}

// -----------------------------------------------------------------------------
// 程式化建樹 + 查詢
// -----------------------------------------------------------------------------

TEST(ItemBasic, LeafDefaults) {
    Item leaf("a");
    EXPECT_EQ(leaf.id(), "a");
    EXPECT_TRUE(leaf.label().empty());
    EXPECT_TRUE(leaf.value().is_null());
    EXPECT_TRUE(leaf.is_leaf());
    EXPECT_EQ(leaf.child_count(), 0u);
    EXPECT_EQ(leaf.size(), 1u);
    EXPECT_EQ(leaf.depth(), 1u);
}

TEST(ItemBasic, LabelAndValue) {
    Item n("cfg", "設定", Value::integer(42));
    EXPECT_EQ(n.label(), "設定");
    ASSERT_TRUE(n.value().is_integer());
    EXPECT_EQ(n.value().as_int(), 42);
}

TEST(ItemBasic, FluentBuildersAndCounts) {
    Item root("root");
    root.set_label("根").set_value(Value::boolean(true));
    Item child("c1");
    child.add_child(Item("g1")).add_child(Item("g2"));
    root.add_child(std::move(child));
    root.add_child(Item("c2"));

    EXPECT_EQ(root.label(), "根");
    EXPECT_TRUE(root.value().as_bool());
    EXPECT_FALSE(root.is_leaf());
    EXPECT_EQ(root.child_count(), 2u);
    EXPECT_EQ(root.size(), 5u);   // root + c1 + g1 + g2 + c2
    EXPECT_EQ(root.depth(), 3u);  // root -> c1 -> g1
}

TEST(ItemBasic, Equality) {
    Item a("x", "L", Value::string("v"));
    a.add_child(Item("y"));
    Item b("x", "L", Value::string("v"));
    b.add_child(Item("y"));
    EXPECT_EQ(a, b);
    b.add_child(Item("z"));
    EXPECT_NE(a, b);
}

// -----------------------------------------------------------------------------
// 遍歷（前序，含 depth）
// -----------------------------------------------------------------------------

TEST(Traverse, PreorderIdsAndDepth) {
    // root(0) -> [a(1) -> [a1(2), a2(2)], b(1)]
    Item root("root");
    Item a("a");
    a.add_child(Item("a1")).add_child(Item("a2"));
    root.add_child(std::move(a));
    root.add_child(Item("b"));

    std::vector<std::string> ids;
    std::vector<int> depths;
    for_each_preorder(root, [&](const Item& n, int depth) {
        ids.push_back(n.id());
        depths.push_back(depth);
    });

    const std::vector<std::string> expect_ids{"root", "a", "a1", "a2", "b"};
    const std::vector<int> expect_depths{0, 1, 2, 2, 1};
    EXPECT_EQ(ids, expect_ids);
    EXPECT_EQ(depths, expect_depths);
}

TEST(Traverse, SingleNodeVisitsOnce) {
    Item only("solo");
    int visits = 0;
    for_each_preorder(only, [&](const Item&, int) { ++visits; });
    EXPECT_EQ(visits, 1);
}

// -----------------------------------------------------------------------------
// 依 id 尋址
// -----------------------------------------------------------------------------

TEST(FindById, HitSelfChildAndDescendant) {
    Item root("root");
    Item a("a");
    a.add_child(Item("deep"));
    root.add_child(std::move(a));

    EXPECT_EQ(root.find("root"), &root);          // 自身
    ASSERT_NE(root.find("a"), nullptr);            // 直接子
    EXPECT_EQ(root.find("a")->id(), "a");
    ASSERT_NE(root.find("deep"), nullptr);         // 深層後代
    EXPECT_EQ(root.find("deep")->id(), "deep");
    EXPECT_TRUE(root.contains("deep"));
}

TEST(FindById, Miss) {
    Item root("root");
    root.add_child(Item("a"));
    EXPECT_EQ(root.find("nope"), nullptr);
    EXPECT_FALSE(root.contains("nope"));
}

TEST(FindById, NonConstFindAllowsMutation) {
    Item root("root");
    root.add_child(Item("target"));
    Item* t = root.find("target");
    ASSERT_NE(t, nullptr);
    t->set_label("改過");
    EXPECT_EQ(root.find("target")->label(), "改過");
}

// -----------------------------------------------------------------------------
// 從宣告式文件建構
// -----------------------------------------------------------------------------

TEST(Build, SingleNode) {
    Item root = must_build(
        "format_version: 1.0\n"
        "id: only\n"
        "label: 唯一\n");
    EXPECT_EQ(root.id(), "only");
    EXPECT_EQ(root.label(), "唯一");
    EXPECT_TRUE(root.is_leaf());
    EXPECT_EQ(root.size(), 1u);
}

TEST(Build, LabelOmittedIsEmpty) {
    Item root = must_build(
        "format_version: 1.0\n"
        "id: bare\n");
    EXPECT_EQ(root.id(), "bare");
    EXPECT_TRUE(root.label().empty());  // 不自 id 臆造 label
}

TEST(Build, AttachedValueScalarAndMap) {
    Item root = must_build(
        "format_version: 1.0\n"
        "id: item\n"
        "value:\n"
        "  weight: 3\n"
        "  enabled: true\n");
    ASSERT_TRUE(root.value().is_map());
    ASSERT_TRUE(root.value().contains("weight"));
    EXPECT_EQ(root.value().at("weight").as_int(), 3);
    EXPECT_TRUE(root.value().at("enabled").as_bool());
}

TEST(Build, NestedChildrenRecursive) {
    Item root = must_build(
        "format_version: 1.0\n"
        "id: launcher\n"
        "label: 啟動器\n"
        "children:\n"
        "  -\n"
        "    id: apps\n"
        "    label: 應用程式\n"
        "    children:\n"
        "      -\n"
        "        id: editor\n"
        "        label: 編輯器\n"
        "  -\n"
        "    id: settings\n"
        "    label: 設定\n");

    EXPECT_EQ(root.id(), "launcher");
    EXPECT_EQ(root.child_count(), 2u);
    EXPECT_EQ(root.size(), 4u);   // launcher + apps + editor + settings
    EXPECT_EQ(root.depth(), 3u);

    // 遍歷 id 順序（前序）
    std::vector<std::string> ids;
    for_each_preorder(root, [&](const Item& n, int) { ids.push_back(n.id()); });
    const std::vector<std::string> expect{"launcher", "apps", "editor", "settings"};
    EXPECT_EQ(ids, expect);

    // 依 id 尋址深層節點
    ASSERT_NE(root.find("editor"), nullptr);
    EXPECT_EQ(root.find("editor")->label(), "編輯器");
}

TEST(Build, FromDocumentOverload) {
    Document doc = must_parse(
        "format_version: 1.0\n"
        "id: d\n"
        "children:\n"
        "  -\n"
        "    id: c\n");
    ItemResult r = build_item(doc);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.item().id(), "d");
    EXPECT_EQ(r.item().child_count(), 1u);
}

// -----------------------------------------------------------------------------
// 空樹（單節點葉）與森林
// -----------------------------------------------------------------------------

TEST(EmptyTree, LeafOnlyIsValid) {
    // format_version 為文件唯一必填，root 內容僅 id → 建出一棵沒有子項目的樹（葉）。
    Item root = must_build(
        "format_version: 1.0\n"
        "id: empty\n");
    EXPECT_TRUE(root.is_leaf());
    EXPECT_EQ(root.child_count(), 0u);
    EXPECT_EQ(root.size(), 1u);
    EXPECT_EQ(root.depth(), 1u);
}

TEST(EmptyTree, EmptyChildrenList) {
    // children: 其下無更深縮排 → E7-01 解析為 Null（非 List）。應報錯而非靜默。
    ItemResult r = build_item(must_parse(
        "format_version: 1.0\n"
        "id: x\n"
        "children:\n"));
    EXPECT_FALSE(r.ok());
}

TEST(Forest, BuildMultipleRoots) {
    Value list = Value::list({
        Value::map({{"id", Value::string("one")}, {"label", Value::string("L1")}}),
        Value::map({{"id", Value::string("two")}}),
    });
    ForestResult r = build_forest(list);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.items().size(), 2u);
    EXPECT_EQ(r.items()[0].id(), "one");
    EXPECT_EQ(r.items()[0].label(), "L1");
    EXPECT_EQ(r.items()[1].id(), "two");
}

TEST(Forest, EmptyListIsEmptyForest) {
    ForestResult r = build_forest(Value::list({}));
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.items().empty());
}

TEST(Forest, DuplicateIdAcrossRootsFails) {
    Value list = Value::list({
        Value::map({{"id", Value::string("dup")}}),
        Value::map({{"id", Value::string("dup")}}),
    });
    ForestResult r = build_forest(list);
    EXPECT_FALSE(r.ok());
}

TEST(Forest, NonListFails) {
    ForestResult r = build_forest(Value::string("nope"));
    EXPECT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// 非靜默錯誤（承 E7-01 NFR-04）
// -----------------------------------------------------------------------------

TEST(BuildError, NotAMap) {
    ItemResult r = build_item(Value::string("scalar"));
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}

TEST(BuildError, MissingId) {
    ItemResult r = build_item(Value::map({{"label", Value::string("no id")}}));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("id"), std::string::npos);
}

TEST(BuildError, EmptyId) {
    ItemResult r = build_item(Value::map({{"id", Value::string("")}}));
    EXPECT_FALSE(r.ok());
}

TEST(BuildError, NonStringId) {
    ItemResult r = build_item(Value::map({{"id", Value::integer(7)}}));
    EXPECT_FALSE(r.ok());
}

TEST(BuildError, DuplicateIdInSubtree) {
    Value node = Value::map({
        {"id", Value::string("root")},
        {"children", Value::list({
            Value::map({{"id", Value::string("root")}}),  // 與父同 id
        })},
    });
    ItemResult r = build_item(node);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("duplicate"), std::string::npos);
}

TEST(BuildError, LabelWrongType) {
    ItemResult r = build_item(Value::map({
        {"id", Value::string("a")},
        {"label", Value::integer(1)},
    }));
    EXPECT_FALSE(r.ok());
}

TEST(BuildError, ChildrenWrongType) {
    ItemResult r = build_item(Value::map({
        {"id", Value::string("a")},
        {"children", Value::string("not a list")},
    }));
    EXPECT_FALSE(r.ok());
}

TEST(BuildError, UnknownKey) {
    ItemResult r = build_item(Value::map({
        {"id", Value::string("a")},
        {"typo", Value::string("oops")},
    }));
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("unknown key"), std::string::npos);
}

}  // namespace
