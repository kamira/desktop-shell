// tests/c4/test_c4_04.cpp — C4-04 視窗總覽 — gtest 單元測試
//
// 涵蓋：load_windows（載入視窗清單、E4-02 縮圖渲染描述、平鋪版位、空清單、多視窗、空/重複
// window_id 拒絕不部分套用）、show_overview / close_overview（委派 C1-05 生命週期）、
// select（開啟中反白選取、未開啟 PanelClosed、找不到 NotFound）、activate（切換視窗並收起
// 總覽、找不到 NotFound 保持開啟、未開啟 PanelClosed）、close_window（自清單移除、清除懸置
// 選取、重新平鋪、找不到 no-op）、to_string 診斷字串穩定性、NFR-02（具名版位 + 正規化比例，
// 無絕對座標）。相位 1：只用注入式縮圖來源，不接真實視窗列舉 / 真實截圖。
#include "window_overview.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::apps::ActivateStatus;
using ds::apps::LoadStatus;
using ds::apps::SelectStatus;
using ds::apps::WindowEntry;
using ds::apps::WindowOverviewApp;
using ds::apps::WindowSpec;
using ds::elements::MemoryImageSource;
using ds::events::GlobalHotkeys;
using ds::events::NullGlobalHotkeys;
using ds::events::TimeoutTimer;
using ds::kernel::TransientProfileManager;
using ds::profiles::SummonPanelProfile;

namespace {

// 測試固定件：C1-05 基底 profile 所需的最小依賴鏈（計時器 + 短暫生命週期管理器 + 熱鍵），
// 供 WindowOverviewApp 借用其開啟 / 收起生命週期。
struct OverviewFixture {
    TimeoutTimer timer;
    TransientProfileManager manager{timer};
    NullGlobalHotkeys hotkeys{true};
    SummonPanelProfile base{"panel.window_overview", manager, hotkeys};
    WindowOverviewApp app{base};
};

// 兩個佔位縮圖來源（相位 1：無真實截圖）。
MemoryImageSource make_thumb_a() { return MemoryImageSource("res://win.a.thumb", {320, 200}); }
MemoryImageSource make_thumb_b() { return MemoryImageSource("res://win.b.thumb", {320, 200}); }

}  // namespace

// ===========================================================================
// 建構預設
// ===========================================================================
TEST(WindowOverview, ConstructedEmptyAndClosed) {
    OverviewFixture fx;
    EXPECT_TRUE(fx.app.empty());
    EXPECT_EQ(fx.app.window_count(), 0u);
    EXPECT_FALSE(fx.app.is_open());
    EXPECT_FALSE(fx.app.has_selection());
    EXPECT_TRUE(fx.app.last_activated().empty());
}

// ===========================================================================
// load_windows —— 載入視窗清單 + E4-02 縮圖渲染描述
// ===========================================================================
TEST(WindowOverview, LoadWindowsBuildsEntriesWithThumbnails) {
    OverviewFixture fx;
    MemoryImageSource thumb_a = make_thumb_a();
    MemoryImageSource thumb_b = make_thumb_b();

    std::vector<WindowSpec> specs;
    specs.push_back(WindowSpec{"win.a", "Finder", &thumb_a});
    specs.push_back(WindowSpec{"win.b", "Terminal", &thumb_b});

    EXPECT_EQ(fx.app.load_windows(specs), LoadStatus::Ok);
    ASSERT_EQ(fx.app.window_count(), 2u);

    const WindowEntry& a = fx.app.windows()[0];
    EXPECT_EQ(a.window_id, "win.a");
    EXPECT_EQ(a.title, "Finder");
    EXPECT_TRUE(a.thumbnail.has_source());
    EXPECT_EQ(a.thumbnail.source_reference(), "res://win.a.thumb");
    EXPECT_EQ(a.thumbnail.source_dimensions().width, 320);
    EXPECT_EQ(a.thumbnail.render_model().target, "surface.overview.win.a");

    const WindowEntry& b = fx.app.windows()[1];
    EXPECT_EQ(b.window_id, "win.b");
    EXPECT_TRUE(b.thumbnail.has_source());
    EXPECT_EQ(b.thumbnail.source_reference(), "res://win.b.thumb");
}

TEST(WindowOverview, LoadWindowsWithoutThumbnailSourceLeavesNoSource) {
    OverviewFixture fx;
    std::vector<WindowSpec> specs;
    specs.push_back(WindowSpec{"win.a", "No Thumbnail", nullptr});

    ASSERT_EQ(fx.app.load_windows(specs), LoadStatus::Ok);
    ASSERT_EQ(fx.app.window_count(), 1u);
    EXPECT_FALSE(fx.app.windows()[0].thumbnail.has_source());
}

TEST(WindowOverview, LoadWindowsReplacesExistingList) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.old", "Old", nullptr}}), LoadStatus::Ok);
    ASSERT_EQ(fx.app.window_count(), 1u);

    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.new", "New", nullptr}}), LoadStatus::Ok);
    ASSERT_EQ(fx.app.window_count(), 1u);
    EXPECT_EQ(fx.app.windows()[0].window_id, "win.new");
    EXPECT_EQ(fx.app.find("win.old"), nullptr);
}

// --- 空清單：合法的「無視窗」狀態 ---
TEST(WindowOverview, LoadEmptyListYieldsEmptyOverview) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    ASSERT_EQ(fx.app.window_count(), 1u);

    EXPECT_EQ(fx.app.load_windows({}), LoadStatus::Ok);
    EXPECT_TRUE(fx.app.empty());
    EXPECT_EQ(fx.app.window_count(), 0u);
}

// --- 多視窗（5 個，跨越一個以上平鋪列）---
TEST(WindowOverview, LoadManyWindowsAllPresent) {
    OverviewFixture fx;
    std::vector<WindowSpec> specs;
    for (int i = 0; i < 5; ++i) {
        specs.push_back(WindowSpec{"win." + std::to_string(i), "Window " + std::to_string(i),
                                    nullptr});
    }
    ASSERT_EQ(fx.app.load_windows(specs), LoadStatus::Ok);
    ASSERT_EQ(fx.app.window_count(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_NE(fx.app.find("win." + std::to_string(i)), nullptr);
    }
}

// --- 拒絕：空 window_id / 重複 window_id，整批不部分套用 ---
TEST(WindowOverview, LoadWindowsRejectsEmptyIdAndPreservesExisting) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);

    std::vector<WindowSpec> bad;
    bad.push_back(WindowSpec{"", "No id", nullptr});
    EXPECT_EQ(fx.app.load_windows(bad), LoadStatus::Invalid);

    ASSERT_EQ(fx.app.window_count(), 1u);  // 既有清單不動。
    EXPECT_EQ(fx.app.windows()[0].window_id, "win.a");
}

TEST(WindowOverview, LoadWindowsRejectsDuplicateIdAndPreservesExisting) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);

    std::vector<WindowSpec> dup;
    dup.push_back(WindowSpec{"win.x", "X1", nullptr});
    dup.push_back(WindowSpec{"win.x", "X2", nullptr});
    EXPECT_EQ(fx.app.load_windows(dup), LoadStatus::Invalid);

    ASSERT_EQ(fx.app.window_count(), 1u);
    EXPECT_EQ(fx.app.windows()[0].window_id, "win.a");
}

// ===========================================================================
// 平鋪版位 —— NFR-02：具名區域 + 正規化比例 [0,1]，無絕對座標
// ===========================================================================
TEST(WindowOverview, TileLayoutSingleWindowFillsWholeCanvas) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.solo", "Solo", nullptr}}), LoadStatus::Ok);

    const auto& layout = fx.app.windows()[0].layout;
    EXPECT_EQ(layout.region, "region.overview.tile.win.solo");
    EXPECT_DOUBLE_EQ(layout.x, 0.0);
    EXPECT_DOUBLE_EQ(layout.y, 0.0);
    EXPECT_DOUBLE_EQ(layout.width, 1.0);
    EXPECT_DOUBLE_EQ(layout.height, 1.0);
}

TEST(WindowOverview, TileLayoutGridForMultipleWindows) {
    OverviewFixture fx;
    std::vector<WindowSpec> specs;
    for (int i = 0; i < 5; ++i) {
        specs.push_back(WindowSpec{"win." + std::to_string(i), "W", nullptr});
    }
    ASSERT_EQ(fx.app.load_windows(specs), LoadStatus::Ok);

    // kOverviewColumns == 4，5 個視窗 -> 4 欄 2 列；每格寬 = 1/4，第 0 列高 = 第 1 列高 = 1/2。
    const auto& tile0 = fx.app.windows()[0].layout;  // row 0, col 0
    const auto& tile3 = fx.app.windows()[3].layout;  // row 0, col 3
    const auto& tile4 = fx.app.windows()[4].layout;  // row 1, col 0

    EXPECT_DOUBLE_EQ(tile0.x, 0.0);
    EXPECT_DOUBLE_EQ(tile0.y, 0.0);
    EXPECT_DOUBLE_EQ(tile0.width, 0.25);
    EXPECT_DOUBLE_EQ(tile0.height, 0.5);

    EXPECT_DOUBLE_EQ(tile3.x, 0.75);
    EXPECT_DOUBLE_EQ(tile3.y, 0.0);

    EXPECT_DOUBLE_EQ(tile4.x, 0.0);
    EXPECT_DOUBLE_EQ(tile4.y, 0.5);
    EXPECT_EQ(tile4.region, "region.overview.tile.win.4");
}

TEST(Nfr02Compliance, TileLayoutStaysWithinUnitIntervalAndIsNamed) {
    OverviewFixture fx;
    std::vector<WindowSpec> specs;
    for (int i = 0; i < 7; ++i) {
        specs.push_back(WindowSpec{"win." + std::to_string(i), "W", nullptr});
    }
    ASSERT_EQ(fx.app.load_windows(specs), LoadStatus::Ok);

    for (const WindowEntry& entry : fx.app.windows()) {
        EXPECT_FALSE(entry.layout.region.empty());  // 具名，非數字 index。
        EXPECT_GE(entry.layout.x, 0.0);
        EXPECT_GE(entry.layout.y, 0.0);
        EXPECT_LE(entry.layout.x + entry.layout.width, 1.0);
        EXPECT_LE(entry.layout.y + entry.layout.height, 1.0);
    }
}

// ===========================================================================
// show_overview / close_overview —— 委派 C1-05 生命週期
// ===========================================================================
TEST(WindowOverview, ShowOverviewOpensPanel) {
    OverviewFixture fx;
    EXPECT_FALSE(fx.app.is_open());
    EXPECT_TRUE(fx.app.show_overview(5));
    EXPECT_TRUE(fx.app.is_open());
}

TEST(WindowOverview, ShowOverviewWhileAlreadyOpenFails) {
    OverviewFixture fx;
    ASSERT_TRUE(fx.app.show_overview(5));
    EXPECT_FALSE(fx.app.show_overview(5));  // 委派 C1-05：不靜默重開。
}

TEST(WindowOverview, CloseOverviewClosesPanelAndClearsSelection) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));
    ASSERT_EQ(fx.app.select("win.a"), SelectStatus::Selected);
    ASSERT_TRUE(fx.app.has_selection());

    EXPECT_TRUE(fx.app.close_overview());
    EXPECT_FALSE(fx.app.is_open());
    EXPECT_FALSE(fx.app.has_selection());
}

TEST(WindowOverview, CloseOverviewWhileAlreadyClosedFails) {
    OverviewFixture fx;
    EXPECT_FALSE(fx.app.close_overview());  // no-op，不靜默。
}

// ===========================================================================
// select —— 開啟中反白選取，不收起總覽
// ===========================================================================
TEST(WindowOverview, SelectWhilePanelClosedReturnsPanelClosed) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    EXPECT_EQ(fx.app.select("win.a"), SelectStatus::PanelClosed);
    EXPECT_FALSE(fx.app.has_selection());
}

TEST(WindowOverview, SelectUnknownIdReturnsNotFoundAndPanelStaysOpen) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));

    EXPECT_EQ(fx.app.select("nonexistent"), SelectStatus::NotFound);
    EXPECT_FALSE(fx.app.has_selection());
    EXPECT_TRUE(fx.app.is_open());
}

TEST(WindowOverview, SelectSuccessHighlightsWindowAndKeepsOverviewOpen) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr},
                                    WindowSpec{"win.b", "B", nullptr}}),
              LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));

    EXPECT_EQ(fx.app.select("win.b"), SelectStatus::Selected);
    ASSERT_TRUE(fx.app.has_selection());
    ASSERT_NE(fx.app.selected(), nullptr);
    EXPECT_EQ(fx.app.selected()->window_id, "win.b");
    EXPECT_TRUE(fx.app.is_open());  // select 不收起總覽。
}

// ===========================================================================
// activate —— 切換視窗並收起總覽
// ===========================================================================
TEST(WindowOverview, ActivateWhilePanelClosedReturnsPanelClosed) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    EXPECT_EQ(fx.app.activate("win.a"), ActivateStatus::PanelClosed);
    EXPECT_TRUE(fx.app.last_activated().empty());
}

TEST(WindowOverview, ActivateUnknownIdReturnsNotFoundAndKeepsOverviewOpen) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));

    EXPECT_EQ(fx.app.activate("nonexistent"), ActivateStatus::NotFound);
    EXPECT_TRUE(fx.app.is_open());  // 找不到不得靜默收起。
    EXPECT_TRUE(fx.app.last_activated().empty());
}

TEST(WindowOverview, ActivateSuccessClosesOverviewAndRecordsLastActivated) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr},
                                    WindowSpec{"win.b", "B", nullptr}}),
              LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));
    ASSERT_EQ(fx.app.select("win.a"), SelectStatus::Selected);

    const WindowEntry* activated = nullptr;
    EXPECT_EQ(fx.app.activate("win.b", &activated), ActivateStatus::Activated);
    ASSERT_NE(activated, nullptr);
    EXPECT_EQ(activated->window_id, "win.b");
    EXPECT_EQ(fx.app.last_activated(), "win.b");

    EXPECT_FALSE(fx.app.is_open());     // activate 隨即收起總覽。
    EXPECT_FALSE(fx.app.has_selection());  // 選取一併清除。
}

// ===========================================================================
// close_window —— 直接從總覽關閉某一視窗（非收起總覽本身）
// ===========================================================================
TEST(WindowOverview, CloseWindowRemovesEntryAndRelayoutsRemaining) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr},
                                    WindowSpec{"win.b", "B", nullptr}}),
              LoadStatus::Ok);

    EXPECT_TRUE(fx.app.close_window("win.a"));
    ASSERT_EQ(fx.app.window_count(), 1u);
    EXPECT_EQ(fx.app.find("win.a"), nullptr);
    ASSERT_NE(fx.app.find("win.b"), nullptr);

    // 剩下唯一視窗重新平鋪為滿版。
    const auto& layout = fx.app.windows()[0].layout;
    EXPECT_DOUBLE_EQ(layout.width, 1.0);
    EXPECT_DOUBLE_EQ(layout.height, 1.0);
}

TEST(WindowOverview, CloseWindowUnknownIdReturnsFalse) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    EXPECT_FALSE(fx.app.close_window("nonexistent"));
    EXPECT_EQ(fx.app.window_count(), 1u);
}

TEST(WindowOverview, CloseWindowClearsSelectionWhenClosingSelectedWindow) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr},
                                    WindowSpec{"win.b", "B", nullptr}}),
              LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));
    ASSERT_EQ(fx.app.select("win.a"), SelectStatus::Selected);

    EXPECT_TRUE(fx.app.close_window("win.a"));
    EXPECT_FALSE(fx.app.has_selection());
    EXPECT_TRUE(fx.app.is_open());  // close_window 不等於收起總覽本身。
}

TEST(WindowOverview, CloseWindowKeepsSelectionOnUnrelatedWindow) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr},
                                    WindowSpec{"win.b", "B", nullptr}}),
              LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));
    ASSERT_EQ(fx.app.select("win.b"), SelectStatus::Selected);

    EXPECT_TRUE(fx.app.close_window("win.a"));
    ASSERT_TRUE(fx.app.has_selection());
    EXPECT_EQ(fx.app.selected()->window_id, "win.b");
}

// --- load_windows 之後，若舊選取的視窗仍在新清單內，保留選取；不在則清除 ---
TEST(WindowOverview, ReloadWindowsClearsSelectionWhenSelectedWindowRemoved) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr}}), LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));
    ASSERT_EQ(fx.app.select("win.a"), SelectStatus::Selected);

    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.b", "B", nullptr}}), LoadStatus::Ok);
    EXPECT_FALSE(fx.app.has_selection());
}

TEST(WindowOverview, ReloadWindowsPreservesSelectionWhenWindowStillPresent) {
    OverviewFixture fx;
    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.a", "A", nullptr},
                                    WindowSpec{"win.b", "B", nullptr}}),
              LoadStatus::Ok);
    ASSERT_TRUE(fx.app.show_overview(5));
    ASSERT_EQ(fx.app.select("win.b"), SelectStatus::Selected);

    ASSERT_EQ(fx.app.load_windows({WindowSpec{"win.b", "B renamed", nullptr}}), LoadStatus::Ok);
    ASSERT_TRUE(fx.app.has_selection());
    EXPECT_EQ(fx.app.selected()->window_id, "win.b");
}

// ===========================================================================
// to_string()：診斷用穩定字串
// ===========================================================================
TEST(WindowOverviewStrings, LoadStatusToStringIsStable) {
    EXPECT_EQ(std::string(ds::apps::to_string(LoadStatus::Ok)), "Ok");
    EXPECT_EQ(std::string(ds::apps::to_string(LoadStatus::Invalid)), "Invalid");
}

TEST(WindowOverviewStrings, SelectStatusToStringIsStable) {
    EXPECT_EQ(std::string(ds::apps::to_string(SelectStatus::Selected)), "Selected");
    EXPECT_EQ(std::string(ds::apps::to_string(SelectStatus::NotFound)), "NotFound");
    EXPECT_EQ(std::string(ds::apps::to_string(SelectStatus::PanelClosed)), "PanelClosed");
}

TEST(WindowOverviewStrings, ActivateStatusToStringIsStable) {
    EXPECT_EQ(std::string(ds::apps::to_string(ActivateStatus::Activated)), "Activated");
    EXPECT_EQ(std::string(ds::apps::to_string(ActivateStatus::NotFound)), "NotFound");
    EXPECT_EQ(std::string(ds::apps::to_string(ActivateStatus::PanelClosed)), "PanelClosed");
}
