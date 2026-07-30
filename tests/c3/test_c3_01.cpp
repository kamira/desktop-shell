// tests/c3/test_c3_01.cpp — C3-01 啟動器選單樹（gtest）
//
// 涵蓋：載入選單樹（宣告式 / 程式化，含多層巢狀）、展開 / 收合（含多層各自獨立狀態、對葉
// 節點與不存在 id 的拒絕）、選取葉節點透過 E3-02（經注入的 E6-01 CommandBus + NullLaunchBackend
// 記錄）觸發啟動、activate() 直接呼叫、選取非葉節點不啟動、以及各類無效操作（不存在 id、
// 葉節點缺乏合法啟動宣告、命令未掛上匯流排）。
#include "launcher_menu_tree.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ds::actuators::LaunchActuator;
using ds::actuators::LaunchKind;
using ds::actuators::NullLaunchBackend;
using ds::command::CommandBus;
using ds::command::CommandStatus;
using ds::content::LauncherMenuTree;
using ds::content::SelectOutcome;
using ds::content::make_launch_program_value;
using ds::content::make_open_file_value;
using ds::content::make_web_search_value;
using ds::events::GlobalHotkeys;
using ds::events::NullGlobalHotkeys;
using ds::events::TimeoutTimer;
using ds::format::Item;
using ds::format::Value;
using ds::kernel::TransientProfileManager;
using ds::profiles::SummonPanelProfile;

namespace {

// 小工具：以 E7-13 保留鍵組一個宣告式項目 Map（不引入解析器，直接以 Value 工廠建構）。
Value make_item(const std::string& id, const std::string& label, Value value = Value::null(),
                 std::vector<Value> children = {}) {
    std::vector<Value::Member> members;
    members.emplace_back("id", Value::string(id));
    members.emplace_back("label", Value::string(label));
    if (!value.is_null()) {
        members.emplace_back("value", value);
    }
    if (!children.empty()) {
        members.emplace_back("children", Value::list(std::move(children)));
    }
    return Value::map(std::move(members));
}

// 樣本選單森林（3 層，供「多層」相關測試）：
//   apps（非葉）
//     productivity（非葉）
//       calc（葉，launch.program）
//     utilities（非葉）
//       notes（葉，open.file）
//   settings（葉，web.search，頂層直接是葉）
Value make_sample_forest() {
    Value calc = make_item("calc", "小算盤", make_launch_program_value("Calculator.app"));
    Value notes = make_item("notes", "備忘錄", make_open_file_value("/tmp/notes.txt"));
    Value productivity = make_item("productivity", "生產力", Value::null(), {calc});
    Value utilities = make_item("utilities", "工具", Value::null(), {notes});
    Value apps = make_item("apps", "應用程式", Value::null(), {productivity, utilities});
    Value settings =
        make_item("settings", "設定", make_web_search_value("desktop-shell settings", "ddg"));
    return Value::list({apps, settings});
}

class LauncherMenuTreeTest : public ::testing::Test {
protected:
    TimeoutTimer timer;
    TransientProfileManager manager{timer};
    NullGlobalHotkeys hotkeys{true};
    SummonPanelProfile panel{"panel.launcher", manager, hotkeys};

    CommandBus bus;
    std::shared_ptr<NullLaunchBackend> backend = std::make_shared<NullLaunchBackend>();
    LaunchActuator actuator{backend};

    LauncherMenuTreeTest() { EXPECT_TRUE(actuator.register_on(bus)); }

    LauncherMenuTree tree{panel, bus};
};

// -----------------------------------------------------------------------------
// 載入選單樹
// -----------------------------------------------------------------------------

TEST_F(LauncherMenuTreeTest, LoadMenuFromDeclarativeValueBuildsForest) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));
    ASSERT_EQ(tree.items().size(), 2u);
    EXPECT_EQ(tree.items()[0].id(), "apps");
    EXPECT_EQ(tree.items()[1].id(), "settings");
    // 委派 C1-05：items() 即 panel.items()。
    EXPECT_EQ(&tree.items(), &panel.items());
}

TEST_F(LauncherMenuTreeTest, LoadMenuInvalidValueFailsAndKeepsExisting) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));
    ASSERT_EQ(tree.items().size(), 2u);

    // 非 List → build_forest 失敗。
    EXPECT_FALSE(tree.load_menu(Value::string("not-a-list")));
    // 現有選單不動（不靜默覆寫壞資料）。
    ASSERT_EQ(tree.items().size(), 2u);
    EXPECT_EQ(tree.items()[0].id(), "apps");
    EXPECT_FALSE(tree.last_build_error().message.empty());
}

TEST_F(LauncherMenuTreeTest, LoadMenuProgrammaticOverload) {
    Item leaf("only", "唯一項目", make_open_file_value("/tmp/a.txt"));
    tree.load_menu(std::vector<Item>{leaf});
    ASSERT_EQ(tree.items().size(), 1u);
    EXPECT_EQ(tree.items()[0].id(), "only");
    EXPECT_TRUE(tree.items()[0].is_leaf());
}

// -----------------------------------------------------------------------------
// 展開 / 收合（含多層各自獨立、對葉節點與不存在 id 的拒絕）
// -----------------------------------------------------------------------------

TEST_F(LauncherMenuTreeTest, ExpandAndCollapseNonLeaf) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_FALSE(tree.is_expanded("apps"));
    EXPECT_TRUE(tree.expand("apps"));
    EXPECT_TRUE(tree.is_expanded("apps"));

    // 重複展開 → false（不靜默）。
    EXPECT_FALSE(tree.expand("apps"));
    EXPECT_TRUE(tree.is_expanded("apps"));  // 狀態不受影響。

    EXPECT_TRUE(tree.collapse("apps"));
    EXPECT_FALSE(tree.is_expanded("apps"));

    // 重複收合 → false（no-op）。
    EXPECT_FALSE(tree.collapse("apps"));
}

TEST_F(LauncherMenuTreeTest, ExpandLeafRejected) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));
    EXPECT_FALSE(tree.expand("settings"));  // settings 為葉節點。
    EXPECT_FALSE(tree.is_expanded("settings"));
    EXPECT_FALSE(tree.collapse("settings"));
}

TEST_F(LauncherMenuTreeTest, ExpandUnknownIdRejected) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));
    EXPECT_FALSE(tree.expand("does-not-exist"));
    EXPECT_FALSE(tree.collapse("does-not-exist"));
    EXPECT_FALSE(tree.is_expanded("does-not-exist"));
}

TEST_F(LauncherMenuTreeTest, MultiLevelExpandStatesAreIndependent) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_TRUE(tree.expand("apps"));
    EXPECT_TRUE(tree.expand("productivity"));
    EXPECT_TRUE(tree.expand("utilities"));
    EXPECT_EQ(tree.expanded_count(), 3u);

    // 收合最外層不影響巢狀較深層的展開狀態（各自獨立以 id 追蹤）。
    EXPECT_TRUE(tree.collapse("apps"));
    EXPECT_FALSE(tree.is_expanded("apps"));
    EXPECT_TRUE(tree.is_expanded("productivity"));
    EXPECT_TRUE(tree.is_expanded("utilities"));
    EXPECT_EQ(tree.expanded_count(), 2u);
}

TEST_F(LauncherMenuTreeTest, LoadMenuClearsExpandedState) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));
    ASSERT_TRUE(tree.expand("apps"));
    ASSERT_TRUE(tree.is_expanded("apps"));

    ASSERT_TRUE(tree.load_menu(make_sample_forest()));  // 換一棵新樹（即使 id 相同）。
    EXPECT_FALSE(tree.is_expanded("apps"));
    EXPECT_EQ(tree.expanded_count(), 0u);
}

// -----------------------------------------------------------------------------
// 選取葉節點 → 透過 E3-02 啟動
// -----------------------------------------------------------------------------

TEST_F(LauncherMenuTreeTest, SelectLeafActivatesLaunchProgramViaE3_02) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));
    ASSERT_TRUE(backend->empty());

    EXPECT_EQ(tree.select("calc"), SelectOutcome::Activated);
    EXPECT_TRUE(tree.last_command_result().ok());

    ASSERT_EQ(backend->record_count(), 1u);
    const auto* rec = backend->last();
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->kind, LaunchKind::Program);
    EXPECT_EQ(rec->target, "Calculator.app");
}

TEST_F(LauncherMenuTreeTest, SelectLeafActivatesOpenFileViaE3_02) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_EQ(tree.select("notes"), SelectOutcome::Activated);
    ASSERT_EQ(backend->record_count(), 1u);
    EXPECT_EQ(backend->last()->kind, LaunchKind::File);
    EXPECT_EQ(backend->last()->target, "/tmp/notes.txt");
}

TEST_F(LauncherMenuTreeTest, SelectTopLevelLeafActivatesWebSearchViaE3_02) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_EQ(tree.select("settings"), SelectOutcome::Activated);
    ASSERT_EQ(backend->record_count(), 1u);
    EXPECT_EQ(backend->last()->kind, LaunchKind::WebSearch);
    EXPECT_EQ(backend->last()->target, "desktop-shell settings");
    EXPECT_EQ(backend->last()->engine, "ddg");
}

// -----------------------------------------------------------------------------
// activate() 直接呼叫（跳過 select 的「選取」語意，與 select 對葉節點分支等價）
// -----------------------------------------------------------------------------

TEST_F(LauncherMenuTreeTest, ActivateDirectCallEquivalentToSelectOnLeaf) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_EQ(tree.activate("calc"), SelectOutcome::Activated);
    ASSERT_EQ(backend->record_count(), 1u);
    EXPECT_EQ(backend->last()->target, "Calculator.app");
}

TEST_F(LauncherMenuTreeTest, ActivateNonLeafReturnsNotLeaf) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_EQ(tree.activate("apps"), SelectOutcome::NotLeaf);
    EXPECT_TRUE(backend->empty());
}

// -----------------------------------------------------------------------------
// 選取非葉節點 → 不啟動
// -----------------------------------------------------------------------------

TEST_F(LauncherMenuTreeTest, SelectNonLeafDoesNotActivate) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_EQ(tree.select("apps"), SelectOutcome::NotLeaf);
    EXPECT_EQ(tree.select("productivity"), SelectOutcome::NotLeaf);
    EXPECT_TRUE(backend->empty());  // 完全沒有 dispatch 發生。
}

// -----------------------------------------------------------------------------
// 無效節點 / 無效啟動宣告
// -----------------------------------------------------------------------------

TEST_F(LauncherMenuTreeTest, SelectUnknownIdReturnsNotFound) {
    ASSERT_TRUE(tree.load_menu(make_sample_forest()));

    EXPECT_EQ(tree.select("does-not-exist"), SelectOutcome::NotFound);
    EXPECT_EQ(tree.activate("does-not-exist"), SelectOutcome::NotFound);
    EXPECT_TRUE(backend->empty());
}

TEST_F(LauncherMenuTreeTest, ActivateLeafWithNonMapValueFails) {
    Item broken("broken", "壞掉的項目", Value::string("oops-not-a-map"));
    tree.load_menu(std::vector<Item>{broken});

    EXPECT_EQ(tree.activate("broken"), SelectOutcome::ActivationFailed);
    EXPECT_EQ(tree.last_command_result().status, CommandStatus::Failed);
    EXPECT_FALSE(tree.last_command_result().message.empty());
    EXPECT_TRUE(backend->empty());  // 解析失敗於 dispatch 之前，匯流排未被觸及。
}

TEST_F(LauncherMenuTreeTest, ActivateLeafMissingCommandKeyFails) {
    Item no_command("no_command", "缺 command", Value::map({}));
    tree.load_menu(std::vector<Item>{no_command});

    EXPECT_EQ(tree.activate("no_command"), SelectOutcome::ActivationFailed);
    EXPECT_TRUE(backend->empty());
}

TEST_F(LauncherMenuTreeTest, ActivateLeafWithArgsNotMapFails) {
    std::vector<Value::Member> members;
    members.emplace_back("command", Value::string("launch.program"));
    members.emplace_back("args", Value::string("should-be-a-map"));
    Item bad_args("bad_args", "args 型別錯", Value::map(std::move(members)));
    tree.load_menu(std::vector<Item>{bad_args});

    EXPECT_EQ(tree.activate("bad_args"), SelectOutcome::ActivationFailed);
    EXPECT_TRUE(backend->empty());
}

TEST_F(LauncherMenuTreeTest, ActivateLeafWithUnregisteredCommandFails) {
    std::vector<Value::Member> members;
    members.emplace_back("command", Value::string("does.not.exist"));
    Item unknown_cmd("unknown_cmd", "未知命令", Value::map(std::move(members)));
    tree.load_menu(std::vector<Item>{unknown_cmd});

    // 解析成功、實際 dispatch 到匯流排，但匯流排回 NotFound（未掛任何處理器）。
    EXPECT_EQ(tree.activate("unknown_cmd"), SelectOutcome::ActivationFailed);
    EXPECT_EQ(tree.last_command_result().status, CommandStatus::NotFound);
    EXPECT_TRUE(backend->empty());  // 從未真的分派到 NullLaunchBackend。
}

}  // namespace
