// tests/c4/test_c4_01.cpp — C4-01 鍵位速查（gtest）
//
// 涵蓋：組裝正確（建構預設值）、load_shortcuts（多鍵位載入 / 空清單合法 / 無效項目整批
// 拒絕不留殘留）、filter（大小寫不敏感子字串比對鍵位 / 說明、清除篩選、無命中）、E4-01
// 排版整合（每個可見鍵位一行、行相對偏移、空清單排版結果為空、篩選後排版跟著變）、
// show / hide 委派 C1-04（含重複顯示失敗、ttl=0 失敗、面板顯示中 filter / load 即時刷新
// 面板內容、收起）、`to_string(LoadStatus)` 穩定字串。
#include "shortcut_cheat_sheet.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using ds::apps::LoadStatus;
using ds::apps::ShortcutCheatSheetApp;
using ds::apps::ShortcutEntry;
using ds::apps::to_string;
using ds::events::TimeoutTimer;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::TransientProfileManager;
using ds::profiles::OsdOverlayProfile;
using ds::render::FixedFontMetrics;
using ds::render::LayoutConstraints;
using ds::render::LayoutResult;

class ShortcutCheatSheetTest : public ::testing::Test {
protected:
    TimeoutTimer timer;
    TransientProfileManager manager{timer};
    LayerStack layers{CapabilityMatrix::defaults()};
    OsdOverlayProfile panel{"panel.shortcuts", manager, layers};
    // 等寬字型度量：每字元前進寬度 6.0、行高 12.0（ascent 未指定 → 採 line_height）。
    FixedFontMetrics metrics{6.0, 12.0};
    ShortcutCheatSheetApp app{panel, metrics};
};

std::vector<ShortcutEntry> sample_entries() {
    return {
        {"Cmd+K", "Open command palette"},
        {"Cmd+S", "Save file"},
        {"Cmd+Shift+P", "Show all commands"},
    };
}

// -----------------------------------------------------------------------------
// 組裝正確
// -----------------------------------------------------------------------------

TEST_F(ShortcutCheatSheetTest, ConstructedEmptyWithDefaults) {
    EXPECT_EQ(app.shortcut_count(), 0u);
    EXPECT_TRUE(app.shortcuts().empty());
    EXPECT_TRUE(app.filter_query().empty());
    EXPECT_FALSE(app.filter_active());
    EXPECT_TRUE(app.display_text().empty());
    EXPECT_FALSE(app.is_showing());
    EXPECT_EQ(app.panel_id(), "panel.shortcuts");
}

// -----------------------------------------------------------------------------
// load_shortcuts —— 多鍵位載入 / 空清單 / 無效項目整批拒絕
// -----------------------------------------------------------------------------

TEST_F(ShortcutCheatSheetTest, LoadShortcutsMultipleEntriesSucceeds) {
    EXPECT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    ASSERT_EQ(app.shortcut_count(), 3u);
    EXPECT_EQ(app.shortcuts()[0].keys, "Cmd+K");
    EXPECT_EQ(app.shortcuts()[1].keys, "Cmd+S");
    EXPECT_EQ(app.shortcuts()[2].keys, "Cmd+Shift+P");
    EXPECT_EQ(app.visible_count(), 3u);  // 無篩選 → 全部可見。
}

TEST_F(ShortcutCheatSheetTest, LoadShortcutsEmptyListIsOkAndClears) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    ASSERT_EQ(app.shortcut_count(), 3u);

    EXPECT_EQ(app.load_shortcuts({}), LoadStatus::Ok);
    EXPECT_EQ(app.shortcut_count(), 0u);
    EXPECT_TRUE(app.shortcuts().empty());
    EXPECT_TRUE(app.display_text().empty());
}

TEST_F(ShortcutCheatSheetTest, LoadShortcutsRejectsEntryWithEmptyKeys) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    std::vector<ShortcutEntry> invalid = {{"", "missing keys"}};
    EXPECT_EQ(app.load_shortcuts(invalid), LoadStatus::Invalid);
    // 整批拒絕：既有清單不變（不留半份殘留）。
    ASSERT_EQ(app.shortcut_count(), 3u);
    EXPECT_EQ(app.shortcuts()[0].keys, "Cmd+K");
}

TEST_F(ShortcutCheatSheetTest, LoadShortcutsRejectsEntryWithEmptyDescription) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    std::vector<ShortcutEntry> invalid = {{"Cmd+X", ""}};
    EXPECT_EQ(app.load_shortcuts(invalid), LoadStatus::Invalid);
    ASSERT_EQ(app.shortcut_count(), 3u);
}

TEST_F(ShortcutCheatSheetTest, LoadManyShortcutsPreservesAllInOrder) {
    std::vector<ShortcutEntry> many = {
        {"Cmd+1", "Switch to tab 1"}, {"Cmd+2", "Switch to tab 2"},
        {"Cmd+3", "Switch to tab 3"}, {"Cmd+4", "Switch to tab 4"},
        {"Cmd+5", "Switch to tab 5"},
    };
    EXPECT_EQ(app.load_shortcuts(many), LoadStatus::Ok);
    ASSERT_EQ(app.shortcut_count(), 5u);
    for (std::size_t i = 0; i < many.size(); ++i) {
        EXPECT_EQ(app.shortcuts()[i].keys, many[i].keys);
        EXPECT_EQ(app.shortcuts()[i].description, many[i].description);
    }
}

// -----------------------------------------------------------------------------
// filter —— 大小寫不敏感子字串比對（鍵位 / 說明）
// -----------------------------------------------------------------------------

TEST_F(ShortcutCheatSheetTest, FilterMatchesKeysCaseInsensitive) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    app.filter("SHIFT");  // 混合大小寫，僅 "Cmd+Shift+P" 命中。
    EXPECT_TRUE(app.filter_active());
    EXPECT_EQ(app.filter_query(), "SHIFT");
    ASSERT_EQ(app.visible_count(), 1u);
    EXPECT_EQ(app.visible_shortcuts()[0].keys, "Cmd+Shift+P");
}

TEST_F(ShortcutCheatSheetTest, FilterMatchesDescription) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    app.filter("save");
    ASSERT_EQ(app.visible_count(), 1u);
    EXPECT_EQ(app.visible_shortcuts()[0].description, "Save file");
}

TEST_F(ShortcutCheatSheetTest, FilterMatchingAllViaCommonPrefix) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    app.filter("cmd");  // 全部鍵位皆以 "Cmd" 開頭。
    EXPECT_EQ(app.visible_count(), 3u);
}

TEST_F(ShortcutCheatSheetTest, FilterEmptyQueryClearsFilter) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    app.filter("save");
    ASSERT_EQ(app.visible_count(), 1u);

    app.filter("");
    EXPECT_FALSE(app.filter_active());
    EXPECT_EQ(app.visible_count(), 3u);
}

TEST_F(ShortcutCheatSheetTest, FilterNoMatchYieldsEmptyVisible) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    app.filter("zzz-does-not-exist");
    EXPECT_EQ(app.visible_count(), 0u);
    EXPECT_TRUE(app.visible_shortcuts().empty());
    EXPECT_TRUE(app.display_text().empty());
}

TEST_F(ShortcutCheatSheetTest, VisibleShortcutsPreservesOriginalOrder) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    app.filter("cmd");
    const auto visible = app.visible_shortcuts();
    ASSERT_EQ(visible.size(), 3u);
    EXPECT_EQ(visible[0].keys, "Cmd+K");
    EXPECT_EQ(visible[1].keys, "Cmd+S");
    EXPECT_EQ(visible[2].keys, "Cmd+Shift+P");
}

// -----------------------------------------------------------------------------
// display_text —— E4-01 排版輸入的字串格式
// -----------------------------------------------------------------------------

TEST_F(ShortcutCheatSheetTest, DisplayTextJoinsVisibleEntriesWithSeparatorAndNewline) {
    std::vector<ShortcutEntry> two = {{"K1", "D1"}, {"K2", "D2"}};
    ASSERT_EQ(app.load_shortcuts(two), LoadStatus::Ok);

    EXPECT_EQ(app.display_text(), "K1  -  D1\nK2  -  D2");
}

// -----------------------------------------------------------------------------
// layout —— E4-01 排版整合
// -----------------------------------------------------------------------------

TEST_F(ShortcutCheatSheetTest, LayoutOnEmptyListIsEmptyResult) {
    LayoutResult result = app.layout();
    EXPECT_TRUE(result.lines.empty());
    EXPECT_TRUE(result.glyphs.empty());
    EXPECT_EQ(result.size.width, 0.0);
    EXPECT_EQ(result.size.height, 0.0);
    EXPECT_FALSE(result.truncated);
}

TEST_F(ShortcutCheatSheetTest, LayoutProducesOneLinePerVisibleEntry) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    LayoutResult result = app.layout();
    ASSERT_EQ(result.lines.size(), 3u);
    EXPECT_FALSE(result.glyphs.empty());
    EXPECT_FALSE(result.truncated);
    // 行高 12.0：行頂相對偏移 = 行索引 * 行高（NFR-02 相對佈局）。
    EXPECT_EQ(result.lines[0].y, 0.0);
    EXPECT_EQ(result.lines[1].y, 12.0);
    EXPECT_EQ(result.lines[2].y, 24.0);
    EXPECT_EQ(result.size.height, 36.0);
}

TEST_F(ShortcutCheatSheetTest, LayoutFollowsActiveFilter) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    app.filter("save");

    LayoutResult result = app.layout();
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.size.height, 12.0);
}

TEST_F(ShortcutCheatSheetTest, LayoutRespectsGivenConstraints) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    LayoutConstraints constraints;
    constraints.max_lines = 2;
    LayoutResult result = app.layout(constraints);
    EXPECT_EQ(result.lines.size(), 2u);
    EXPECT_TRUE(result.truncated);
}

// -----------------------------------------------------------------------------
// show / hide —— 委派 C1-04 基底 profile
// -----------------------------------------------------------------------------

TEST_F(ShortcutCheatSheetTest, ShowDelegatesToPanelWithCurrentDisplayText) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    EXPECT_TRUE(app.show(5));
    EXPECT_TRUE(app.is_showing());
    EXPECT_TRUE(panel.is_showing());
    EXPECT_EQ(panel.message(), app.display_text());
}

TEST_F(ShortcutCheatSheetTest, ShowWhileAlreadyShowingFails) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    ASSERT_TRUE(app.show(5));

    EXPECT_FALSE(app.show(5));
    EXPECT_TRUE(app.is_showing());  // 仍維持顯示中，不受失敗呼叫影響。
}

TEST_F(ShortcutCheatSheetTest, ShowWithZeroTtlFails) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);

    EXPECT_FALSE(app.show(0));  // 委派 E1-14：ttl == 0 拒絕。
    EXPECT_FALSE(app.is_showing());
}

TEST_F(ShortcutCheatSheetTest, HideDismissesShownPanel) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    ASSERT_TRUE(app.show(5));

    EXPECT_TRUE(app.hide());
    EXPECT_FALSE(app.is_showing());
    EXPECT_FALSE(panel.is_showing());
}

TEST_F(ShortcutCheatSheetTest, HideWhileNotShowingFails) {
    EXPECT_FALSE(app.hide());  // no-op，不靜默。
    EXPECT_FALSE(app.is_showing());
}

TEST_F(ShortcutCheatSheetTest, FilterWhileShowingLiveUpdatesPanelMessage) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    ASSERT_TRUE(app.show(5));
    ASSERT_EQ(panel.message(), app.display_text());

    app.filter("save");
    EXPECT_TRUE(app.is_showing());  // 篩選不影響顯隱狀態。
    EXPECT_EQ(panel.message(), "Cmd+S  -  Save file");
    EXPECT_EQ(panel.message(), app.display_text());
}

TEST_F(ShortcutCheatSheetTest, LoadShortcutsWhileShowingLiveUpdatesPanelMessage) {
    std::vector<ShortcutEntry> first = {{"Cmd+K", "Open command palette"}};
    ASSERT_EQ(app.load_shortcuts(first), LoadStatus::Ok);
    ASSERT_TRUE(app.show(5));
    ASSERT_EQ(panel.message(), "Cmd+K  -  Open command palette");

    std::vector<ShortcutEntry> second = {{"Cmd+Q", "Quit application"}};
    ASSERT_EQ(app.load_shortcuts(second), LoadStatus::Ok);
    EXPECT_EQ(panel.message(), "Cmd+Q  -  Quit application");
}

TEST_F(ShortcutCheatSheetTest, InvalidLoadWhileShowingDoesNotChangePanelMessage) {
    ASSERT_EQ(app.load_shortcuts(sample_entries()), LoadStatus::Ok);
    ASSERT_TRUE(app.show(5));
    const std::string before = panel.message();

    std::vector<ShortcutEntry> invalid = {{"", "bad"}};
    EXPECT_EQ(app.load_shortcuts(invalid), LoadStatus::Invalid);
    EXPECT_EQ(panel.message(), before);
    EXPECT_EQ(app.shortcut_count(), 3u);
}

// -----------------------------------------------------------------------------
// to_string(LoadStatus) —— NFR-02 具名結果穩定字串
// -----------------------------------------------------------------------------

TEST(ShortcutCheatSheetLoadStatus, ToStringIsStable) {
    EXPECT_EQ(std::string(to_string(LoadStatus::Ok)), "Ok");
    EXPECT_EQ(std::string(to_string(LoadStatus::Invalid)), "Invalid");
}

}  // namespace
