// E7-11 群組與批次操作 — 契約測試（gtest）
//
// 涵蓋：依選擇器選群組（tag / has / where）、批次命令經 E6-01 分派、逐成員結果、
// 部分失敗回報（Failed / NotFound 混合）、空群組、批次屬性套用（含非 Map 成員回報）。
#include <string>
#include <vector>

#include "command_bus.hpp"
#include "document.hpp"
#include "group.hpp"
#include "gtest/gtest.h"

using ds::format::AttributeChange;
using ds::format::apply_attributes;
using ds::format::apply_to_group;
using ds::format::Group;
using ds::format::GroupMember;
using ds::format::GroupResult;
using ds::format::select;
using ds::format::Selector;
using ds::format::Value;

using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::command::CommandValue;

namespace {

// 建一棵含「一組帶 group 標籤的項目」的 Value 樹：
//   layers: [ {name:base, group:bg}, {name:overlay, group:fg}, {name:hud, group:fg} ]
//   panel: { title: main, group: fg }
Value make_tree() {
    std::vector<Value> layers;
    layers.push_back(Value::map({{"name", Value::string("base")}, {"group", Value::string("bg")}}));
    layers.push_back(
        Value::map({{"name", Value::string("overlay")}, {"group", Value::string("fg")}}));
    layers.push_back(
        Value::map({{"id", Value::string("hud-1")}, {"name", Value::string("hud")},
                    {"group", Value::string("fg")}}));

    return Value::map({
        {"layers", Value::list(std::move(layers))},
        {"panel", Value::map({{"title", Value::string("main")}, {"group", Value::string("fg")}})},
        {"count", Value::integer(3)},
    });
}

}  // namespace

// --- select：依標籤選群組 -----------------------------------------------------

TEST(E7_11_Select, TaggedSelectorPicksMatchingMaps) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));  // key 預設 "group"
    ASSERT_EQ(g.size(), 3u);  // overlay, hud, panel
    EXPECT_EQ(g.selector_label(), "group=fg");
    // 前序遍歷：layers/1、layers/2、panel。
    EXPECT_EQ(g.members()[0].path, "/layers/1");
    EXPECT_EQ(g.members()[1].path, "/layers/2");
    EXPECT_EQ(g.members()[2].path, "/panel");
}

TEST(E7_11_Select, MemberLabelFromIdThenNameThenPath) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));
    // overlay：無 id → 取 name。
    EXPECT_EQ(g.members()[0].label, "overlay");
    // hud：有 id → 取 id。
    EXPECT_EQ(g.members()[1].label, "hud-1");
    // panel：無 id/name → 退化為 path。
    EXPECT_EQ(g.members()[2].label, "/panel");
}

TEST(E7_11_Select, TaggedWithCustomKey) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("bg", "group"));
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g.members()[0].path, "/layers/0");
    EXPECT_EQ(g.members()[0].label, "base");
}

TEST(E7_11_Select, HasKeySelector) {
    Value root = make_tree();
    Group g = select(root, Selector::has("group"));
    // 全部 4 個帶 group 的 map（3 layers + panel）。
    EXPECT_EQ(g.size(), 4u);
}

TEST(E7_11_Select, TaggedEmptyValueDegradesToHas) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("", "group"));
    EXPECT_EQ(g.size(), 4u);
}

TEST(E7_11_Select, WhereCustomPredicate) {
    Value root = make_tree();
    Group g = select(root, Selector::where(
                               [](const Value& n) {
                                   const Value* nm = n.find("name");
                                   return nm && nm->is_string() && nm->as_string() == "base";
                               },
                               "name=base"));
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g.members()[0].path, "/layers/0");
    EXPECT_EQ(g.selector_label(), "name=base");
}

TEST(E7_11_Select, NoMatchYieldsEmptyGroup) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("nonexistent"));
    EXPECT_TRUE(g.empty());
    EXPECT_EQ(g.size(), 0u);
}

TEST(E7_11_Select, FindByPath) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));
    const GroupMember* m = g.find("/panel");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->label, "/panel");
    EXPECT_EQ(g.find("/nope"), nullptr);
}

TEST(E7_11_Select, RootItselfCanMatch) {
    Value root = Value::map({{"group", Value::string("root-grp")}, {"x", Value::integer(1)}});
    Group g = select(root, Selector::tagged("root-grp"));
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g.members()[0].path, "/");
}

TEST(E7_11_Select, MemberNodePointsIntoSourceTree) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("bg"));
    ASSERT_EQ(g.size(), 1u);
    ASSERT_NE(g.members()[0].node, nullptr);
    EXPECT_TRUE(g.members()[0].node->is_map());
    EXPECT_EQ(g.members()[0].node->at("name").as_string(), "base");
}

// --- apply_to_group：批次命令經 E6-01 分派 ------------------------------------

TEST(E7_11_Apply, BatchDispatchPerMemberResults) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));  // 3 成員

    CommandBus bus;
    int calls = 0;
    bus.register_command("show", [&calls](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok();
    });

    GroupResult r = apply_to_group(bus, g, "show");
    EXPECT_EQ(calls, 3);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_TRUE(r.all_ok());
    EXPECT_FALSE(r.any_error());
    EXPECT_EQ(r.ok_count(), 3u);
    // 逐成員結果保留成員路徑 / 標籤。
    EXPECT_EQ(r.per_member[0].path, "/layers/1");
    EXPECT_EQ(r.per_member[1].label, "hud-1");
    EXPECT_EQ(r.per_member[2].path, "/panel");
}

TEST(E7_11_Apply, UnknownCommandYieldsNotFoundPerMember) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));
    CommandBus bus;  // 未註冊任何命令
    GroupResult r = apply_to_group(bus, g, "missing");
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r.not_found_count(), 3u);
    EXPECT_FALSE(r.all_ok());
    EXPECT_TRUE(r.any_error());
    for (const auto& m : r.per_member) {
        EXPECT_EQ(m.result.status, CommandStatus::NotFound);
    }
}

TEST(E7_11_Apply, PartialFailureReportedPerMember) {
    // 處理器對某些成員成功、某些失敗 —— 逐成員回報，不靜默。
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));  // paths: /layers/1, /layers/2, /panel

    CommandBus bus;
    // make_args 注入成員 path；處理器據 path 決定成敗。
    bus.register_command("toggle", [](const CommandArgs& a) {
        auto path = a.get_string("path").value_or("");
        if (path == "/panel") return CommandResult::make_failed("panel is locked");
        return CommandResult::make_ok(CommandValue{path});
    });

    GroupResult r = apply_to_group(bus, g, "toggle", [](const GroupMember& m) {
        return CommandArgs{}.set("path", m.path);
    });

    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r.ok_count(), 2u);
    EXPECT_EQ(r.failed_count(), 1u);
    EXPECT_FALSE(r.all_ok());
    EXPECT_TRUE(r.any_error());
    // /panel 成員為 Failed，帶處理器訊息。
    const auto& panel = r.per_member[2];
    EXPECT_EQ(panel.path, "/panel");
    EXPECT_EQ(panel.result.status, CommandStatus::Failed);
    EXPECT_EQ(panel.result.message, "panel is locked");
    // 成功成員回傳值為其 path。
    EXPECT_EQ(r.per_member[0].result.value, CommandValue{std::string("/layers/1")});
}

TEST(E7_11_Apply, MixedFailedAndOk) {
    // 不同成員可得不同狀態：以 make_args 帶 path，處理器對一個成員回 Failed，其餘 Ok。
    Value root = make_tree();
    Group g = select(root, Selector::has("group"));  // 4 成員

    CommandBus bus;
    bus.register_command("act", [](const CommandArgs& a) {
        auto p = a.get_string("path").value_or("");
        if (p == "/layers/0") return CommandResult::make_failed("bg immutable");
        return CommandResult::make_ok();
    });
    GroupResult r = apply_to_group(
        bus, g, "act", [](const GroupMember& m) { return CommandArgs{}.set("path", m.path); });
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r.failed_count(), 1u);
    EXPECT_EQ(r.ok_count(), 3u);
}

TEST(E7_11_Apply, EmptyGroupDispatchesNothing) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("nonexistent"));
    CommandBus bus;
    bool called = false;
    bus.register_command("x", [&called](const CommandArgs&) {
        called = true;
        return CommandResult::make_ok();
    });
    GroupResult r = apply_to_group(bus, g, "x");
    EXPECT_TRUE(r.empty());
    EXPECT_FALSE(called);
    EXPECT_TRUE(r.all_ok());     // 空 → 無失敗
    EXPECT_FALSE(r.any_error());
}

TEST(E7_11_Apply, FixedArgsSharedAcrossMembers) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));
    CommandBus bus;
    std::vector<std::string> seen;
    bus.register_command("set", [&seen](const CommandArgs& a) {
        seen.push_back(a.get_string("color").value_or(""));
        return CommandResult::make_ok();
    });
    GroupResult r = apply_to_group(bus, g, "set", CommandArgs{}.set("color", "red"));
    EXPECT_TRUE(r.all_ok());
    ASSERT_EQ(seen.size(), 3u);
    for (const auto& c : seen) EXPECT_EQ(c, "red");
}

// --- apply_attributes：批次屬性套用 -------------------------------------------

TEST(E7_11_Attributes, AppliesCommonAttributesToEachMember) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("fg"));  // 3 個 map 成員

    auto res = apply_attributes(g, {{"visible", Value::boolean(true)},
                                    {"group", Value::string("fg-active")}});
    ASSERT_EQ(res.size(), 3u);
    EXPECT_TRUE(res.all_applied());
    EXPECT_EQ(res.applied_count(), 3u);

    // 新鍵 visible 附加、既有鍵 group 覆寫（保序：group 仍在原位）。
    const Value& u0 = res.per_member[0].updated;  // overlay
    ASSERT_TRUE(u0.is_map());
    EXPECT_TRUE(u0.at("visible").as_bool());
    EXPECT_EQ(u0.at("group").as_string(), "fg-active");
    EXPECT_EQ(u0.at("name").as_string(), "overlay");
    // group 為既有鍵，位置不變（原順序 name, group）；新鍵 visible 附加於尾。
    ASSERT_EQ(u0.keys().size(), 3u);
    EXPECT_EQ(u0.keys()[0], "name");
    EXPECT_EQ(u0.keys()[1], "group");
    EXPECT_EQ(u0.keys()[2], "visible");
}

TEST(E7_11_Attributes, SourceTreeUnchanged) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("bg"));  // /layers/0 = base
    apply_attributes(g, {{"group", Value::string("MUTATED")}});
    // 來源樹不被改動（apply_attributes 產出副本）。
    EXPECT_EQ(root.at("layers").as_list()[0].at("group").as_string(), "bg");
}

TEST(E7_11_Attributes, NonMapMemberReportedNotSilent) {
    // 選擇器只命中 Map，故直接構造一個成員指向清單節點的 Group，
    // 驗證 apply 逐一回報「非 Map」而非靜默。
    Value root = make_tree();
    const Value* list_node = root.find("layers");
    ASSERT_NE(list_node, nullptr);
    ASSERT_TRUE(list_node->is_list());
    Group g("manual", {GroupMember{"/layers", "/layers", list_node}});

    auto res = apply_attributes(g, {{"x", Value::integer(1)}});
    ASSERT_EQ(res.size(), 1u);
    EXPECT_FALSE(res.all_applied());
    EXPECT_EQ(res.applied_count(), 0u);
    EXPECT_FALSE(res.per_member[0].applied);
    EXPECT_FALSE(res.per_member[0].message.empty());  // 回報原因，不靜默
}

TEST(E7_11_Attributes, EmptyChangesProducesCopy) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("bg"));
    auto res = apply_attributes(g, {});
    ASSERT_EQ(res.size(), 1u);
    EXPECT_TRUE(res.per_member[0].applied);
    // 副本等同原成員內容。
    EXPECT_EQ(res.per_member[0].updated, *g.members()[0].node);
}

TEST(E7_11_Attributes, EmptyGroupAllAppliedTrue) {
    Value root = make_tree();
    Group g = select(root, Selector::tagged("none"));
    auto res = apply_attributes(g, {{"a", Value::integer(1)}});
    EXPECT_TRUE(res.empty());
    EXPECT_TRUE(res.all_applied());  // 空 → 無未套用者
}
