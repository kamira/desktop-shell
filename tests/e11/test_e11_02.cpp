// E11-02 自繪選單呈現 — 契約測試（gtest）
//
// 驗證相位 1（Mac / null 期）語意，全程不含任何平台分支 / 真實選單 API：
//   - 從 E7-13 Item 森林建選單（kind 推斷 / 顯式 kind、分隔線、勾選項、子選單、停用）
//   - 無效項處理：未知 kind、checked 用於非 checkbox、非 submenu 帶子項、value 型別不符
//   - 以 E4-01 排版每列標籤（render_model）
//   - 子選單展開 / 收合、分隔線、勾選、停用態
//   - 鍵盤巡覽（move_next/move_prev/enter/exit/activate）與滑鼠巡覽（hover/select）
//   - render_model() 面板 / 列的相對幾何與具名 surface
//   - 邊界情形結構化回報，不崩潰
#include "menu_renderer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "item_tree.hpp"
#include "text_layout.hpp"

using ds::format::Item;
using ds::format::Value;
using ds::host::CustomMenuRenderer;
using ds::host::MenuModel;
using ds::host::MenuNavStatus;
using ds::host::MenuNode;
using ds::host::MenuNodeKind;
using ds::host::MenuRenderModel;
using ds::render::FixedFontMetrics;

namespace {

// --- 建構 Value 選單中繼資料的小工具 ---------------------------------------

Value meta(std::vector<Value::Member> members) { return Value::map(std::move(members)); }

// 一般命令項（省略 kind：無子項 → 推斷 Action）。
Item action_item(std::string id, std::string label, bool enabled = true) {
    if (enabled) return Item(std::move(id), std::move(label));
    return Item(std::move(id), std::move(label),
               meta({{"enabled", Value::boolean(false)}}));
}

Item separator_item(std::string id) {
    return Item(std::move(id), std::string(), meta({{"kind", Value::string("separator")}}));
}

Item checkbox_item(std::string id, std::string label, bool checked, bool enabled = true) {
    std::vector<Value::Member> members{{"kind", Value::string("checkbox")},
                                       {"checked", Value::boolean(checked)}};
    if (!enabled) members.push_back({"enabled", Value::boolean(false)});
    return Item(std::move(id), std::move(label), meta(std::move(members)));
}

// 建一個具代表性的選單森林：
//   File (submenu)
//     - New          (action)
//     - Open         (action, disabled)
//     - --- separator ---
//   View (submenu)
//     - ShowGrid     (checkbox, checked=true)
//     - ShowRuler    (checkbox, checked=false)
//   --- separator ---
//   Quit (action)
std::vector<Item> make_sample_forest() {
    Item file("file", "File");
    file.add_child(action_item("new", "New"));
    file.add_child(action_item("open", "Open", /*enabled=*/false));

    Item view("view", "View");
    view.add_child(checkbox_item("show_grid", "Show Grid", /*checked=*/true));
    view.add_child(checkbox_item("show_ruler", "Show Ruler", /*checked=*/false));

    std::vector<Item> forest;
    forest.push_back(std::move(file));
    forest.push_back(std::move(view));
    forest.push_back(separator_item("sep1"));
    forest.push_back(action_item("quit", "Quit"));
    return forest;
}

CustomMenuRenderer make_sample_renderer(const FixedFontMetrics& metrics) {
    CustomMenuRenderer renderer(metrics, "surface.menu.root");
    auto result = renderer.set_menu(make_sample_forest());
    EXPECT_TRUE(result.ok());
    return renderer;
}

}  // namespace

// --- build_menu：kind 推斷 / 顯式 -------------------------------------------

TEST(BuildMenu, InfersActionWhenNoChildrenAndNoKind) {
    auto result = ds::host::build_menu(std::vector<Item>{action_item("a", "A")});
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.model().size(), 1u);
    EXPECT_EQ(result.model().items()[0].kind(), MenuNodeKind::Action);
    EXPECT_EQ(result.model().items()[0].label(), "A");
    EXPECT_TRUE(result.model().items()[0].enabled());
}

TEST(BuildMenu, InfersSubmenuWhenHasChildrenAndNoKind) {
    Item root("m", "Menu");
    root.add_child(action_item("a", "A"));
    auto result = ds::host::build_menu(std::vector<Item>{root});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.model().items()[0].kind(), MenuNodeKind::Submenu);
    ASSERT_EQ(result.model().items()[0].children().size(), 1u);
    EXPECT_EQ(result.model().items()[0].children()[0].label(), "A");
}

TEST(BuildMenu, ExplicitKindOverridesInference) {
    // 顯式 kind=action 但無子項——與推斷一致；用來驗證顯式路徑可行。
    Item item("a", "A", meta({{"kind", Value::string("action")}}));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.model().items()[0].kind(), MenuNodeKind::Action);
}

TEST(BuildMenu, SeparatorAndCheckboxAndSubmenuFromSampleForest) {
    auto result = ds::host::build_menu(make_sample_forest());
    ASSERT_TRUE(result.ok());
    const MenuModel& model = result.model();
    ASSERT_EQ(model.size(), 4u);

    EXPECT_EQ(model.items()[0].kind(), MenuNodeKind::Submenu);
    EXPECT_EQ(model.items()[0].id(), "file");
    ASSERT_EQ(model.items()[0].children().size(), 2u);
    EXPECT_EQ(model.items()[0].children()[1].enabled(), false);  // Open disabled

    EXPECT_EQ(model.items()[1].kind(), MenuNodeKind::Submenu);
    ASSERT_EQ(model.items()[1].children().size(), 2u);
    EXPECT_TRUE(model.items()[1].children()[0].checked());   // ShowGrid checked
    EXPECT_FALSE(model.items()[1].children()[1].checked());  // ShowRuler unchecked

    EXPECT_EQ(model.items()[2].kind(), MenuNodeKind::Separator);
    EXPECT_EQ(model.items()[3].kind(), MenuNodeKind::Action);
    EXPECT_EQ(model.items()[3].label(), "Quit");
}

TEST(BuildMenu, EmptyForestBuildsEmptyModel) {
    auto result = ds::host::build_menu(std::vector<Item>{});
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.model().empty());
}

TEST(BuildMenu, RootConvenienceOverloadUsesChildren) {
    Item root("root", "Root");
    root.add_child(action_item("a", "A"));
    root.add_child(separator_item("sep"));
    auto result = ds::host::build_menu(root);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.model().size(), 2u);
    EXPECT_EQ(result.model().items()[0].id(), "a");
}

// --- build_menu：無效項處理（可定位訊息，不靜默）---------------------------

TEST(BuildMenu, UnknownKindFails) {
    Item item("a", "A", meta({{"kind", Value::string("bogus")}}));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("unknown kind"), std::string::npos);
}

TEST(BuildMenu, KindMustBeString) {
    Item item("a", "A", meta({{"kind", Value::integer(1)}}));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("'kind'"), std::string::npos);
}

TEST(BuildMenu, CheckedOnNonCheckboxFails) {
    Item item("a", "A", meta({{"kind", Value::string("action")},
                            {"checked", Value::boolean(true)}}));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("checked"), std::string::npos);
}

TEST(BuildMenu, CheckedMustBeBool) {
    Item item("a", "A", meta({{"kind", Value::string("checkbox")},
                            {"checked", Value::string("yes")}}));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("'checked'"), std::string::npos);
}

TEST(BuildMenu, EnabledMustBeBool) {
    Item item("a", "A", meta({{"enabled", Value::string("nope")}}));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("'enabled'"), std::string::npos);
}

TEST(BuildMenu, NonSubmenuWithChildrenFails) {
    Item item("a", "A", meta({{"kind", Value::string("action")}}));
    item.add_child(action_item("b", "B"));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("only submenu items may have children"),
             std::string::npos);
}

TEST(BuildMenu, SeparatorWithChildrenFails) {
    Item item("a", "A", meta({{"kind", Value::string("separator")}}));
    item.add_child(action_item("b", "B"));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
}

TEST(BuildMenu, NonMapNonNullValueFails) {
    Item item("a", "A", Value::string("oops"));
    auto result = ds::host::build_menu(std::vector<Item>{item});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("value must be a map"), std::string::npos);
}

TEST(BuildMenu, ErrorPropagatesFromNestedChild) {
    Item root("m", "Menu");
    root.add_child(action_item("ok", "OK"));
    root.add_child(Item("bad", "Bad", meta({{"kind", Value::string("bogus")}})));
    auto result = ds::host::build_menu(std::vector<Item>{root});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().message.find("'bad'"), std::string::npos);
}

// --- MenuModel::at_path ------------------------------------------------------

TEST(MenuModelAtPath, ResolvesNestedAndReportsInvalidAsNullptr) {
    auto result = ds::host::build_menu(make_sample_forest());
    ASSERT_TRUE(result.ok());
    MenuModel model = result.model();

    const MenuNode* file = model.at_path({0});
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(file->id(), "file");

    const MenuNode* open = model.at_path({0, 1});
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->id(), "open");

    EXPECT_EQ(model.at_path({}), nullptr);
    EXPECT_EQ(model.at_path({99}), nullptr);
    EXPECT_EQ(model.at_path({3, 0}), nullptr);  // Quit 非子選單，穿越即 nullptr
}

// --- CustomMenuRenderer：模型 --------------------------------------------

TEST(CustomMenuRenderer, SetMenuSucceedsAndResetsHasMenu) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer(metrics, "surface.menu.root");
    EXPECT_FALSE(renderer.has_menu());

    auto result = renderer.set_menu(make_sample_forest());
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(renderer.has_menu());
    EXPECT_TRUE(renderer.current().empty());
}

TEST(CustomMenuRenderer, SetMenuFailureKeepsPriorModel) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer(metrics, "surface.menu.root");
    ASSERT_TRUE(renderer.set_menu(make_sample_forest()).ok());
    const std::size_t before = renderer.model().size();

    Item bad("bad", "Bad", meta({{"kind", Value::string("bogus")}}));
    auto result = renderer.set_menu(std::vector<Item>{bad});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(renderer.model().size(), before);  // 未被破壞式取代
}

// --- render_model：排版 / 面板 / 具名 surface ------------------------------

TEST(RenderModel, EmptyMenuYieldsNoPanels) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer(metrics, "surface.menu.root");
    MenuRenderModel rm = renderer.render_model();
    EXPECT_TRUE(rm.panels.empty());
}

TEST(RenderModel, RootPanelListsTopLevelRowsWithLayout) {
    FixedFontMetrics metrics(/*advance=*/6.0, /*line_height=*/16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);

    MenuRenderModel rm = renderer.render_model();
    ASSERT_EQ(rm.panels.size(), 1u);
    const auto& root_panel = rm.panels[0];
    EXPECT_EQ(root_panel.surface, "surface.menu.root");
    EXPECT_TRUE(root_panel.owner_path.empty());
    ASSERT_EQ(root_panel.rows.size(), 4u);

    // "File" 4 個字元 * advance 6.0 = 24.0 寬；一般列高 = line_height。
    const auto& file_row = root_panel.rows[0];
    EXPECT_EQ(file_row.kind, MenuNodeKind::Submenu);
    EXPECT_TRUE(file_row.has_submenu);
    EXPECT_DOUBLE_EQ(file_row.label.size.width, 4.0 * 6.0);
    EXPECT_DOUBLE_EQ(file_row.row_height, 16.0);
    EXPECT_DOUBLE_EQ(file_row.y, 0.0);
    EXPECT_EQ(file_row.label.surface, "surface.menu.root");

    // 第二列（View）y 累積在第一列之後。
    EXPECT_DOUBLE_EQ(root_panel.rows[1].y, 16.0);

    // 分隔線列（索引 2）高度為 line_height 的一半，無標籤內容。
    const auto& sep_row = root_panel.rows[2];
    EXPECT_EQ(sep_row.kind, MenuNodeKind::Separator);
    EXPECT_DOUBLE_EQ(sep_row.row_height, 8.0);

    // 面板高度 = 各列高度總和。
    EXPECT_DOUBLE_EQ(root_panel.size.height, 16.0 + 16.0 + 8.0 + 16.0);
}

TEST(RenderModel, OpenSubmenuAddsNamedPanel) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);

    // 巡覽游標移到 File，進入其子選單。
    ASSERT_EQ(renderer.move_next().status, MenuNavStatus::Moved);  // -> file
    ASSERT_EQ(renderer.enter().status, MenuNavStatus::Entered);    // -> file/new

    MenuRenderModel rm = renderer.render_model();
    ASSERT_EQ(rm.panels.size(), 2u);
    EXPECT_EQ(rm.panels[0].surface, "surface.menu.root");
    // 子面板具名尾碼取自擁有者項目的具名 id（"file"），非數字深度。
    EXPECT_EQ(rm.panels[1].surface, "surface.menu.root.menu:file");
    ASSERT_EQ(rm.panels[1].rows.size(), 2u);
    EXPECT_EQ(rm.panels[1].rows[0].kind, MenuNodeKind::Action);

    // 根面板中 File 列標記為 selected（其為目前展開鏈的一部分）。
    EXPECT_TRUE(rm.panels[0].rows[0].selected);
    // 子面板中第一列（New）為目前高亮列。
    EXPECT_TRUE(rm.panels[1].rows[0].selected);
}

// --- 鍵盤巡覽 ---------------------------------------------------------------

TEST(Navigation, MoveNextSkipsSeparatorsAndCycles) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);

    auto r1 = renderer.move_next();  // file
    ASSERT_EQ(r1.status, MenuNavStatus::Moved);
    EXPECT_EQ(r1.path, (std::vector<std::size_t>{0}));

    auto r2 = renderer.move_next();  // view
    EXPECT_EQ(r2.path, (std::vector<std::size_t>{1}));

    auto r3 = renderer.move_next();  // 跳過 sep1（索引 2）直達 quit（索引 3）
    EXPECT_EQ(r3.path, (std::vector<std::size_t>{3}));

    auto r4 = renderer.move_next();  // 循環回 file
    EXPECT_EQ(r4.path, (std::vector<std::size_t>{0}));
}

TEST(Navigation, MovePrevFromNoSelectionLandsOnLast) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    auto r = renderer.move_prev();
    EXPECT_EQ(r.status, MenuNavStatus::Moved);
    EXPECT_EQ(r.path, (std::vector<std::size_t>{3}));  // quit（跳過尾端分隔線）
}

TEST(Navigation, EnterOnSubmenuThenExitReturnsToParent) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    renderer.move_next();  // file

    auto entered = renderer.enter();
    ASSERT_EQ(entered.status, MenuNavStatus::Entered);
    EXPECT_EQ(entered.path, (std::vector<std::size_t>{0, 0}));  // file/new（Open 停用被跳過）

    auto exited = renderer.exit();
    ASSERT_EQ(exited.status, MenuNavStatus::Exited);
    EXPECT_EQ(exited.path, (std::vector<std::size_t>{0}));

    // 根層再次 exit：Invalid（無可收合的面板）。
    EXPECT_EQ(renderer.exit().status, MenuNavStatus::Invalid);
}

TEST(Navigation, EnterOnNonSubmenuIsNotSelectable) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    renderer.move_next();  // file
    renderer.move_next();  // view
    renderer.move_next();  // quit（Action）
    EXPECT_EQ(renderer.enter().status, MenuNavStatus::NotSelectable);
}

TEST(Navigation, EnterOnSubmenuWithNoNavigableChildrenIsNotSelectable) {
    // 子選單內只有一個已停用項目：無可進入項。
    Item sub("sub", "Sub");
    sub.add_child(action_item("only", "Only", /*enabled=*/false));
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer(metrics, "surface.menu.root");
    ASSERT_TRUE(renderer.set_menu(std::vector<Item>{sub}).ok());

    renderer.move_next();  // sub
    EXPECT_EQ(renderer.enter().status, MenuNavStatus::NotSelectable);
}

TEST(Navigation, ActivateOnActionSelects) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    renderer.move_prev();  // quit
    auto r = renderer.activate();
    EXPECT_EQ(r.status, MenuNavStatus::Selected);
    EXPECT_EQ(r.path, (std::vector<std::size_t>{3}));
}

TEST(Navigation, ActivateOnCheckboxTogglesAndReports) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    renderer.move_next();  // file
    renderer.enter();      // file/new
    renderer.exit();       // 回 file
    renderer.move_next();  // view
    renderer.enter();      // view/show_grid（checked=true）

    auto r1 = renderer.activate();
    EXPECT_EQ(r1.status, MenuNavStatus::Selected);
    EXPECT_FALSE(r1.checked);  // 原本 true，切換後為 false

    auto r2 = renderer.activate();
    EXPECT_EQ(r2.status, MenuNavStatus::Selected);
    EXPECT_TRUE(r2.checked);  // 再切換回 true
}

TEST(Navigation, ActivateOnSubmenuActsAsEnter) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    renderer.move_next();  // file
    auto r = renderer.activate();
    EXPECT_EQ(r.status, MenuNavStatus::Entered);
    EXPECT_EQ(renderer.current(), (std::vector<std::size_t>{0, 0}));
}

TEST(Navigation, ActivateWithoutCursorIsInvalid) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    EXPECT_EQ(renderer.activate().status, MenuNavStatus::Invalid);
}

TEST(Navigation, MoveOnEmptyMenuReportsEmpty) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer(metrics, "surface.menu.root");
    EXPECT_EQ(renderer.move_next().status, MenuNavStatus::Empty);
}

// --- 滑鼠巡覽 ----------------------------------------------------------------

TEST(Navigation, HoverOnRootRowAlwaysVisible) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    auto r = renderer.hover({1});  // view
    EXPECT_EQ(r.status, MenuNavStatus::Moved);
    EXPECT_EQ(renderer.current(), (std::vector<std::size_t>{1}));
}

TEST(Navigation, HoverOnDisabledRowIsNotSelectable) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    auto r = renderer.hover({0, 1});  // file/open（停用），面板尚未展開 → Invalid
    EXPECT_EQ(r.status, MenuNavStatus::Invalid);

    renderer.hover({0});
    renderer.enter();
    auto r2 = renderer.hover({0, 1});  // 面板已展開，open 存在但停用
    EXPECT_EQ(r2.status, MenuNavStatus::NotSelectable);
}

TEST(Navigation, HoverOutsideOpenPanelsIsInvalid) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    // 尚未展開任何子選單面板，直接 hover 巢狀路徑應為 Invalid。
    auto r = renderer.hover({0, 0});
    EXPECT_EQ(r.status, MenuNavStatus::Invalid);
}

TEST(Navigation, HoverOnUnknownPathIsInvalid) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    EXPECT_EQ(renderer.hover({99}).status, MenuNavStatus::Invalid);
    EXPECT_EQ(renderer.hover({}).status, MenuNavStatus::Invalid);
}

TEST(Navigation, SelectCombinesHoverAndActivate) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    auto r = renderer.select({3});  // quit，直接滑鼠點擊（未先 hover）
    EXPECT_EQ(r.status, MenuNavStatus::Selected);
    EXPECT_EQ(r.path, (std::vector<std::size_t>{3}));
}

TEST(Navigation, SelectOnInvalidPathReportsInvalidWithoutActivating) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    auto r = renderer.select({0, 0});  // 面板未展開
    EXPECT_EQ(r.status, MenuNavStatus::Invalid);
    EXPECT_TRUE(renderer.current().empty());  // 游標未被更動
}

TEST(Navigation, SelectOnSeparatorIsNotSelectable) {
    FixedFontMetrics metrics(6.0, 16.0);
    CustomMenuRenderer renderer = make_sample_renderer(metrics);
    auto r = renderer.select({2});  // sep1
    EXPECT_EQ(r.status, MenuNavStatus::NotSelectable);
}

// --- MenuNode 便捷查詢 -------------------------------------------------------

TEST(MenuNode, IsNavigableAndIsActivatableSemantics) {
    MenuNode action("a", "A", MenuNodeKind::Action, /*enabled=*/true, /*checked=*/false);
    EXPECT_TRUE(action.is_navigable());
    EXPECT_TRUE(action.is_activatable());

    MenuNode disabled_action("b", "B", MenuNodeKind::Action, /*enabled=*/false, false);
    EXPECT_FALSE(disabled_action.is_navigable());
    EXPECT_FALSE(disabled_action.is_activatable());

    MenuNode sep("s", "", MenuNodeKind::Separator, true, false);
    EXPECT_FALSE(sep.is_navigable());
    EXPECT_FALSE(sep.is_activatable());

    MenuNode submenu("m", "M", MenuNodeKind::Submenu, true, false);
    EXPECT_TRUE(submenu.is_navigable());
    EXPECT_FALSE(submenu.is_activatable());  // Submenu 走 enter()，非直接致動

    MenuNode checkbox("c", "C", MenuNodeKind::Checkbox, true, false);
    EXPECT_TRUE(checkbox.toggle_checked());
    EXPECT_FALSE(checkbox.toggle_checked());
}
