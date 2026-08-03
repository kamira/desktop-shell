// tests/c2/test_c2_09.cpp — C2-09 行事曆 / 待辦 widget（gtest）
//
// 涵蓋：組裝建構（掛 C1-01 殼層、預設空狀態）、load()（E7-13 建樹：程式化文字 / 缺省 days /
// 空狀態 round-trip）、add_item（新建日期分組 / 附加既有分組 / 日期分組排序 / 空 date 或
// item_id 為 Invalid / item_id 重複為 DuplicateId，含與日期分組 id 衝突）、toggle_done
// （翻轉 / 找不到為 NotFound / 對日期分組誤用為 Invalid）、list_items（含日期分組 / 不存在
// 日期回空清單）、save() 經 E7-12 序列化並以 load() round-trip（含多日期多事項、含空 widget）、
// render_model()（E4-01 文字呈現：標題 + 事項前綴 / 空森林回空結果）、以及 load() 對無效輸入
// （語法錯誤 / days 非 list / 森林建構契約違反）一律不靜默、不改動既有狀態。
#include "calendar_todo_widget.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::format::Item;
using ds::format::Value;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::profiles::SkinState;
using ds::render::FixedFontMetrics;
using ds::render::LayoutConstraints;
using ds::widgets::CalendarTodoWidget;
using ds::widgets::TodoStatus;

namespace {

// 等寬字型度量：advance=6、行高=14、ascent=11。供 render_model() 測試。
FixedFontMetrics mono() { return FixedFontMetrics(6.0, 14.0, 11.0); }

// 便利 fixture：獨立的注入相依（每個測試各自一套，互不干擾）。
struct Env {
    NullKernelBackend backend{alpha_capable_matrix()};
    bool backend_initialized_ = backend.init();  // CHG-20260803-11：成員依宣告順序初始化，故此行在其後成員建構前完成（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = mono();
    CalendarTodoWidget widget{"widget.todo", backend, layers, metrics};
};

}  // namespace

// -----------------------------------------------------------------------------
// 組裝建構
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, ConstructedEmptyWithShell) {
    Env env;
    EXPECT_EQ(env.widget.id(), std::string("widget.todo"));
    EXPECT_EQ(env.widget.shell().id(), std::string("widget.todo"));
    EXPECT_EQ(env.widget.shell().state(), SkinState::Unloaded);
    EXPECT_TRUE(env.widget.empty());
    EXPECT_EQ(env.widget.date_count(), static_cast<std::size_t>(0));
    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(0));
    EXPECT_TRUE(env.widget.days().empty());
}

// -----------------------------------------------------------------------------
// add_item / 日期分組
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, AddItemCreatesDateGroupAndItem) {
    Env env;
    EXPECT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);

    EXPECT_TRUE(env.widget.has_date("2026-07-24"));
    EXPECT_EQ(env.widget.date_count(), static_cast<std::size_t>(1));
    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(1));
    EXPECT_TRUE(env.widget.contains("item:1"));

    const std::vector<Item>& items = env.widget.list_items("2026-07-24");
    ASSERT_EQ(items.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(items[0].id(), std::string("item:1"));
    EXPECT_EQ(items[0].label(), std::string("買菜"));

    bool done = true;
    EXPECT_EQ(env.widget.is_done("item:1", done), TodoStatus::Ok);
    EXPECT_FALSE(done);
}

TEST(CalendarTodoWidget, AddItemAppendsToExistingDateGroupInOrder) {
    Env env;
    EXPECT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);
    EXPECT_EQ(env.widget.add_item("2026-07-24", "item:2", "交報告"), TodoStatus::Ok);

    EXPECT_EQ(env.widget.date_count(), static_cast<std::size_t>(1));  // 同一分組，不重複建立。
    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(2));

    const std::vector<Item>& items = env.widget.list_items("2026-07-24");
    ASSERT_EQ(items.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(items[0].id(), std::string("item:1"));
    EXPECT_EQ(items[1].id(), std::string("item:2"));
}

TEST(CalendarTodoWidget, AddItemGroupsByMultipleDatesInInsertionOrder) {
    Env env;
    EXPECT_EQ(env.widget.add_item("2026-07-25", "item:b", "B"), TodoStatus::Ok);
    EXPECT_EQ(env.widget.add_item("2026-07-24", "item:a", "A"), TodoStatus::Ok);

    EXPECT_EQ(env.widget.date_count(), static_cast<std::size_t>(2));
    const std::vector<std::string> dates = env.widget.dates();
    ASSERT_EQ(dates.size(), static_cast<std::size_t>(2));
    // 分組依「首次出現」的加入序排列（非日期字面排序）——與 E7-13 森林保序語意一致。
    EXPECT_EQ(dates[0], std::string("2026-07-25"));
    EXPECT_EQ(dates[1], std::string("2026-07-24"));

    ASSERT_EQ(env.widget.list_items("2026-07-25").size(), static_cast<std::size_t>(1));
    ASSERT_EQ(env.widget.list_items("2026-07-24").size(), static_cast<std::size_t>(1));
    EXPECT_EQ(env.widget.list_items("2026-07-25")[0].id(), std::string("item:b"));
    EXPECT_EQ(env.widget.list_items("2026-07-24")[0].id(), std::string("item:a"));
}

TEST(CalendarTodoWidget, AddItemInvalidEmptyDateOrId) {
    Env env;
    EXPECT_EQ(env.widget.add_item("", "item:1", "x"), TodoStatus::Invalid);
    EXPECT_EQ(env.widget.add_item("2026-07-24", "", "x"), TodoStatus::Invalid);
    EXPECT_TRUE(env.widget.empty());  // 無效呼叫不改動狀態。
}

TEST(CalendarTodoWidget, AddItemDuplicateIdRejected) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);
    EXPECT_EQ(env.widget.add_item("2026-07-25", "item:1", "重複"), TodoStatus::DuplicateId);

    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(1));  // 狀態未被第二次呼叫污染。
    EXPECT_FALSE(env.widget.has_date("2026-07-25"));
}

TEST(CalendarTodoWidget, AddItemDuplicateAgainstDateGroupIdRejected) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "A"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.date_count(), static_cast<std::size_t>(1));
    const std::string group_id = env.widget.days()[0].id();  // 內部日期分組 id（不透過內部命名假設）。

    EXPECT_EQ(env.widget.add_item("2026-07-25", group_id, "B"), TodoStatus::DuplicateId);
    EXPECT_FALSE(env.widget.has_date("2026-07-25"));
}

// -----------------------------------------------------------------------------
// toggle_done
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, ToggleDoneFlipsState) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);

    EXPECT_EQ(env.widget.toggle_done("item:1"), TodoStatus::Ok);
    bool done = false;
    ASSERT_EQ(env.widget.is_done("item:1", done), TodoStatus::Ok);
    EXPECT_TRUE(done);

    EXPECT_EQ(env.widget.toggle_done("item:1"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.is_done("item:1", done), TodoStatus::Ok);
    EXPECT_FALSE(done);
}

TEST(CalendarTodoWidget, ToggleDoneNotFound) {
    Env env;
    EXPECT_EQ(env.widget.toggle_done("missing"), TodoStatus::NotFound);

    bool done = false;
    EXPECT_EQ(env.widget.is_done("missing", done), TodoStatus::NotFound);
}

TEST(CalendarTodoWidget, ToggleDoneOnDateGroupIsInvalid) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);
    const std::string group_id = env.widget.days()[0].id();

    EXPECT_EQ(env.widget.toggle_done(group_id), TodoStatus::Invalid);
    bool done = false;
    EXPECT_EQ(env.widget.is_done(group_id, done), TodoStatus::Invalid);
}

// -----------------------------------------------------------------------------
// list_items —— 空清單（不存在的日期非錯誤）
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, ListItemsUnknownDateReturnsEmpty) {
    Env env;
    EXPECT_TRUE(env.widget.list_items("2099-01-01").empty());
    EXPECT_FALSE(env.widget.has_date("2099-01-01"));

    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);
    EXPECT_TRUE(env.widget.list_items("2026-07-25").empty());  // 其他日期仍為空，互不干擾。
}

// -----------------------------------------------------------------------------
// load() —— E7-13 建樹
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, LoadBuildsForestFromDeclarativeText) {
    Env env;
    const std::string text =
        "format_version: 1.0\n"
        "days:\n"
        "  -\n"
        "    id: day:2026-07-24\n"
        "    label: 2026-07-24\n"
        "    children:\n"
        "      -\n"
        "        id: item:1\n"
        "        label: 買菜\n"
        "        value:\n"
        "          done: false\n"
        "      -\n"
        "        id: item:2\n"
        "        label: 交報告\n"
        "        value:\n"
        "          done: true\n"
        "  -\n"
        "    id: day:2026-07-25\n"
        "    label: 2026-07-25\n";

    ASSERT_EQ(env.widget.load(text), TodoStatus::Ok);
    EXPECT_EQ(env.widget.date_count(), static_cast<std::size_t>(2));
    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(2));

    ASSERT_EQ(env.widget.list_items("2026-07-24").size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(env.widget.list_items("2026-07-25").empty());  // 第二個分組刻意不含事項。

    bool done = false;
    ASSERT_EQ(env.widget.is_done("item:1", done), TodoStatus::Ok);
    EXPECT_FALSE(done);
    ASSERT_EQ(env.widget.is_done("item:2", done), TodoStatus::Ok);
    EXPECT_TRUE(done);
}

TEST(CalendarTodoWidget, LoadMissingDaysKeyIsEmptyOk) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "殘留"), TodoStatus::Ok);

    EXPECT_EQ(env.widget.load("format_version: 1.0\n"), TodoStatus::Ok);
    EXPECT_TRUE(env.widget.empty());  // 缺省 days → 整份取代為空森林（非合併）。
}

TEST(CalendarTodoWidget, LoadInvalidSyntaxIsParseErrorAndStateUnchanged) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "既有"), TodoStatus::Ok);

    EXPECT_EQ(env.widget.load("not a valid document"), TodoStatus::ParseError);
    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(1));  // 失敗不改動既有狀態。
    EXPECT_TRUE(env.widget.contains("item:1"));
}

TEST(CalendarTodoWidget, LoadDaysNotAListIsInvalidAndStateUnchanged) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "既有"), TodoStatus::Ok);

    EXPECT_EQ(env.widget.load("format_version: 1.0\ndays: 5\n"), TodoStatus::Invalid);
    EXPECT_EQ(env.widget.item_count(), static_cast<std::size_t>(1));
}

TEST(CalendarTodoWidget, LoadForestContractViolationIsInvalid) {
    Env env;
    // 重複 id（同一元素於森林內重複）→ E7-13 build_forest 契約違反。
    const std::string dup_id_text =
        "format_version: 1.0\n"
        "days:\n"
        "  -\n"
        "    id: same\n"
        "  -\n"
        "    id: same\n";
    EXPECT_EQ(env.widget.load(dup_id_text), TodoStatus::Invalid);

    // 缺必填 id。
    const std::string missing_id_text =
        "format_version: 1.0\n"
        "days:\n"
        "  -\n"
        "    label: 沒有 id\n";
    EXPECT_EQ(env.widget.load(missing_id_text), TodoStatus::Invalid);
}

// -----------------------------------------------------------------------------
// save() 經 E7-12 round-trip
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, SaveRoundTripsThroughLoad) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:2", "交報告"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.add_item("2026-07-25", "item:3", "運動"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.toggle_done("item:2"), TodoStatus::Ok);

    const std::string saved = env.widget.save();

    NullKernelBackend backend2{alpha_capable_matrix()};
    backend2.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers2{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics2 = mono();
    CalendarTodoWidget widget2{"widget.todo.copy", backend2, layers2, metrics2};

    ASSERT_EQ(widget2.load(saved), TodoStatus::Ok);
    EXPECT_EQ(widget2.date_count(), env.widget.date_count());
    EXPECT_EQ(widget2.item_count(), env.widget.item_count());
    EXPECT_TRUE(widget2.days() == env.widget.days());  // E7-13 Item 深層相等：完整結構 round-trip。

    bool done = false;
    ASSERT_EQ(widget2.is_done("item:1", done), TodoStatus::Ok);
    EXPECT_FALSE(done);
    ASSERT_EQ(widget2.is_done("item:2", done), TodoStatus::Ok);
    EXPECT_TRUE(done);
    ASSERT_EQ(widget2.is_done("item:3", done), TodoStatus::Ok);
    EXPECT_FALSE(done);
}

TEST(CalendarTodoWidget, SaveRoundTripsEmptyWidget) {
    Env env;
    EXPECT_TRUE(env.widget.empty());

    const std::string saved = env.widget.save();

    NullKernelBackend backend2{alpha_capable_matrix()};
    backend2.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    LayerStack layers2{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics2 = mono();
    CalendarTodoWidget widget2{"widget.todo.empty", backend2, layers2, metrics2};

    ASSERT_EQ(widget2.load(saved), TodoStatus::Ok);
    EXPECT_TRUE(widget2.empty());
}

// -----------------------------------------------------------------------------
// render_model() —— E4-01 文字呈現
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, RenderModelProducesTitleAndCheckboxLines) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "買菜"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:2", "交報告"), TodoStatus::Ok);
    ASSERT_EQ(env.widget.toggle_done("item:2"), TodoStatus::Ok);

    const ds::render::LayoutResult result = env.widget.render_model();

    // 三行：日期標題 + 兩個事項（各附 [ ]/[x] 前綴），排版目標 surface 綁定 widget id。
    EXPECT_EQ(result.lines.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(result.surface, env.widget.id());
    EXPECT_FALSE(result.truncated);
}

TEST(CalendarTodoWidget, RenderModelEmptyForestIsEmptyResult) {
    Env env;
    const ds::render::LayoutResult result = env.widget.render_model();
    EXPECT_TRUE(result.lines.empty());
    EXPECT_TRUE(result.glyphs.empty());
}

TEST(CalendarTodoWidget, RenderModelRespectsInjectedConstraints) {
    Env env;
    ASSERT_EQ(env.widget.add_item("2026-07-24", "item:1", "A"), TodoStatus::Ok);

    LayoutConstraints constraints;
    constraints.align = ds::render::TextAlign::Left;
    constraints.wrap = ds::render::WrapMode::None;

    const ds::render::LayoutResult result = env.widget.render_model(constraints);
    EXPECT_EQ(result.lines.size(), static_cast<std::size_t>(2));  // 標題 + 一個事項。
}

// -----------------------------------------------------------------------------
// to_string(TodoStatus) —— 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(CalendarTodoWidget, ToStringNamesAllStatuses) {
    EXPECT_STREQ(ds::widgets::to_string(TodoStatus::Ok), "Ok");
    EXPECT_STREQ(ds::widgets::to_string(TodoStatus::Invalid), "Invalid");
    EXPECT_STREQ(ds::widgets::to_string(TodoStatus::NotFound), "NotFound");
    EXPECT_STREQ(ds::widgets::to_string(TodoStatus::DuplicateId), "DuplicateId");
    EXPECT_STREQ(ds::widgets::to_string(TodoStatus::ParseError), "ParseError");
}
