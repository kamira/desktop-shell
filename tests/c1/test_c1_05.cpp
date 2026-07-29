// tests/c1/test_c1_05.cpp — C1-05 召喚面板 profile（gtest）
//
// 涵蓋：profile 組裝正確（建構預設、E1-02 輸入策略透傳）、open / filter / select / close
// 行為、E1-14 短暫生命週期整合（逾時自動收起、多面板共用管理器互不干擾、解構安全）、
// E1-02 輸入策略（Capture / ClickThrough 對映後端策略與命中結果）、E7-13 項目階層
// （宣告式建構、巢狀篩選、依 id 選取）、E5-05 事件（能力閘控、綁定 / 解除、inject
// 觸發事件驅動的召喚）、以及各類無效操作（重複開啟、對已關閉面板操作、選取不存在 id、
// 能力不存在時綁定熱鍵、重複綁定熱鍵）。
#include "summon_panel_profile.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::GlobalHotkeys;
using ds::events::Hotkey;
using ds::events::Key;
using ds::events::Modifier;
using ds::events::NullGlobalHotkeys;
using ds::events::TimeoutTimer;
using ds::format::Item;
using ds::format::Value;
using ds::kernel::HitResult;
using ds::kernel::InputPolicy;
using ds::kernel::InputStrategy;
using ds::kernel::TransientProfileManager;
using ds::profiles::CloseReason;
using ds::profiles::PanelState;
using ds::profiles::SelectResult;
using ds::profiles::SummonPanelProfile;

namespace {

// 小工具：以 E7-13 保留鍵組一個宣告式項目 Map（不引入解析器，直接以 Value 工廠建構）。
Value make_item(const std::string& id, const std::string& label,
                 std::vector<Value> children = {}) {
    std::vector<Value::Member> members;
    members.emplace_back("id", Value::string(id));
    members.emplace_back("label", Value::string(label));
    if (!children.empty()) {
        members.emplace_back("children", Value::list(std::move(children)));
    }
    return Value::map(std::move(members));
}

// 樣本森林：apps(小算盤, 備忘錄) + settings。共 4 個節點。
Value make_sample_forest() {
    Value calc = make_item("calc", "小算盤");
    Value notes = make_item("notes", "備忘錄");
    Value apps = make_item("apps", "應用程式", {calc, notes});
    Value settings = make_item("settings", "設定");
    return Value::list({apps, settings});
}

class SummonPanelProfileTest : public ::testing::Test {
protected:
    TimeoutTimer timer;
    TransientProfileManager manager{timer};
    NullGlobalHotkeys hotkeys{true};  // 預設模擬能力可用；不可用情境於個別測試自建。
};

// -----------------------------------------------------------------------------
// 組裝正確
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, ConstructedClosedWithDefaults) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    EXPECT_EQ(panel.state(), PanelState::Closed);
    EXPECT_FALSE(panel.is_open());
    EXPECT_EQ(panel.id(), "panel.spotlight");
    EXPECT_EQ(panel.strategy(), InputStrategy::Capture);
    EXPECT_TRUE(panel.items().empty());
    EXPECT_FALSE(panel.hotkey_bound());
}

// --- E7-13 項目階層：宣告式建構 ---

TEST_F(SummonPanelProfileTest, SetItemsFromDeclarativeValueBuildsForest) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    ASSERT_EQ(panel.items().size(), 2u);
    EXPECT_EQ(panel.items()[0].id(), "apps");
    EXPECT_EQ(panel.items()[0].child_count(), 2u);
    EXPECT_TRUE(panel.items()[0].contains("calc"));
    EXPECT_EQ(panel.items()[1].id(), "settings");
}

TEST_F(SummonPanelProfileTest, SetItemsFromDeclarativeValueFailurePreservesExisting) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    const std::size_t before = panel.items().size();

    Value bad = Value::list({Value::integer(1)});  // 非 Map 元素 -> BuildError
    EXPECT_FALSE(panel.set_items(bad));
    EXPECT_EQ(panel.items().size(), before);  // 不得靜默覆寫壞資料。
    EXPECT_FALSE(panel.last_build_error().message.empty());
}

TEST_F(SummonPanelProfileTest, SetItemsProgrammatic) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    std::vector<Item> items;
    items.emplace_back("only");
    panel.set_items(items);
    ASSERT_EQ(panel.items().size(), 1u);
    EXPECT_TRUE(panel.items()[0].contains("only"));
}

// -----------------------------------------------------------------------------
// open() — E1-14 短暫生命週期整合
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, OpenTransitionsToOpenAndFiresOnOpen) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    bool fired = false;
    panel.on_open([&] { fired = true; });

    EXPECT_TRUE(panel.open(5));
    EXPECT_TRUE(panel.is_open());
    EXPECT_TRUE(fired);
    EXPECT_EQ(manager.alive_count(), 1u);
}

TEST_F(SummonPanelProfileTest, OpenWhileAlreadyOpenFails) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.open(5));
    EXPECT_FALSE(panel.open(5));  // 不靜默重開。
    EXPECT_TRUE(panel.is_open());
}

TEST_F(SummonPanelProfileTest, OpenWithZeroTtlFails) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    EXPECT_FALSE(panel.open(0));  // 委派 E1-14：ttl 必須 > 0。
    EXPECT_FALSE(panel.is_open());
}

// -----------------------------------------------------------------------------
// filter() — E7-13 巢狀篩選 + 開啟狀態閘控
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, FilterWhileClosedReturnsEmpty) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    EXPECT_TRUE(panel.filter("").empty());
    EXPECT_TRUE(panel.filter("calc").empty());
}

TEST_F(SummonPanelProfileTest, FilterAfterOpenMatchesNestedItemsByIdOrLabel) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    ASSERT_TRUE(panel.open(5));

    auto by_id = panel.filter("calc");
    ASSERT_EQ(by_id.size(), 1u);
    EXPECT_EQ(by_id[0]->id(), "calc");

    auto by_label = panel.filter("備忘");
    ASSERT_EQ(by_label.size(), 1u);
    EXPECT_EQ(by_label[0]->id(), "notes");

    EXPECT_TRUE(panel.filter("nonexistent").empty());
}

TEST_F(SummonPanelProfileTest, FilterEmptyQueryReturnsAllFlattened) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    ASSERT_TRUE(panel.open(5));
    EXPECT_EQ(panel.filter("").size(), 4u);  // apps, calc, notes, settings
}

// -----------------------------------------------------------------------------
// select() / close() — 行為 + 無效操作
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, SelectSuccessClosesPanelWithSelectedReason) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    ASSERT_TRUE(panel.open(5));

    CloseReason reason = CloseReason::Manual;
    bool close_fired = false;
    panel.on_close([&](CloseReason r) {
        close_fired = true;
        reason = r;
    });
    bool select_fired = false;
    panel.on_select([&](const Item& item) {
        select_fired = true;
        EXPECT_EQ(item.id(), "calc");
    });

    const Item* selected = nullptr;
    EXPECT_EQ(panel.select("calc", &selected), SelectResult::Selected);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->id(), "calc");
    EXPECT_TRUE(select_fired);
    EXPECT_TRUE(close_fired);
    EXPECT_EQ(reason, CloseReason::Selected);
    EXPECT_FALSE(panel.is_open());
}

TEST_F(SummonPanelProfileTest, SelectWhileClosedReturnsPanelClosed) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    EXPECT_EQ(panel.select("calc"), SelectResult::PanelClosed);
}

TEST_F(SummonPanelProfileTest, SelectUnknownIdReturnsNotFoundAndStaysOpen) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.set_items(make_sample_forest()));
    ASSERT_TRUE(panel.open(5));
    EXPECT_EQ(panel.select("nonexistent"), SelectResult::NotFound);
    EXPECT_TRUE(panel.is_open());
}

TEST_F(SummonPanelProfileTest, CloseManualFiresOnCloseManual) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.open(5));
    CloseReason reason = CloseReason::Timeout;
    panel.on_close([&](CloseReason r) { reason = r; });

    EXPECT_TRUE(panel.close());
    EXPECT_EQ(reason, CloseReason::Manual);
    EXPECT_FALSE(panel.is_open());
}

TEST_F(SummonPanelProfileTest, CloseWhileAlreadyClosedFails) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    EXPECT_FALSE(panel.close());  // no-op，不靜默。
}

// -----------------------------------------------------------------------------
// E1-14 逾時自動收起 + 多面板共用管理器 + 解構安全
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, TimeoutAutoExpiryClosesPanel) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    ASSERT_TRUE(panel.open(3));
    int close_count = 0;
    CloseReason reason = CloseReason::Manual;
    panel.on_close([&](CloseReason r) {
        ++close_count;
        reason = r;
    });

    EXPECT_EQ(manager.advance(2), 0u);  // 尚未到期
    EXPECT_TRUE(panel.is_open());
    EXPECT_EQ(manager.advance(1), 1u);  // 跨過 ttl
    EXPECT_FALSE(panel.is_open());
    EXPECT_EQ(close_count, 1);
    EXPECT_EQ(reason, CloseReason::Timeout);
}

TEST_F(SummonPanelProfileTest, MultiplePanelsShareManagerWithoutInterference) {
    SummonPanelProfile panel_a("panel.a", manager, hotkeys);
    SummonPanelProfile panel_b("panel.b", manager, hotkeys);

    int a_close_count = 0;
    int b_close_count = 0;
    panel_a.on_close([&](CloseReason) { ++a_close_count; });
    panel_b.on_close([&](CloseReason) { ++b_close_count; });

    ASSERT_TRUE(panel_a.open(3));
    ASSERT_TRUE(panel_b.open(10));

    manager.advance(3);  // 只有 panel_a 逾時；panel_b 的過期事件應被 panel_a 過濾掉，反之亦然。
    EXPECT_FALSE(panel_a.is_open());
    EXPECT_TRUE(panel_b.is_open());
    EXPECT_EQ(a_close_count, 1);
    EXPECT_EQ(b_close_count, 0);

    EXPECT_TRUE(panel_b.close());
    EXPECT_EQ(b_close_count, 1);
    EXPECT_EQ(a_close_count, 1);  // panel_a 的計數不受 panel_b 關閉影響。
}

TEST_F(SummonPanelProfileTest, DestructorWhileOpenClosesLifecycleEntry) {
    {
        SummonPanelProfile panel("panel.transient", manager, hotkeys);
        ASSERT_TRUE(panel.open(5));
        EXPECT_EQ(manager.alive_count(), 1u);
    }  // 解構時面板仍「開啟」；dtor 應強制 close()，避免懸置計時器 / 回呼。
    EXPECT_EQ(manager.alive_count(), 0u);
    EXPECT_EQ(manager.advance(100), 0u);  // 不應再有殘留過期觸發，亦不崩潰。
}

// -----------------------------------------------------------------------------
// E1-02 輸入策略整合
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, InputStrategyCaptureMapsToModalAndSolid) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys, InputStrategy::Capture);
    EXPECT_EQ(panel.backend_input_policy(), InputPolicy::Modal);
    EXPECT_EQ(panel.hit_result(), HitResult::Solid);
}

TEST_F(SummonPanelProfileTest, InputStrategyClickThroughMapsToPassThroughAndTransparent) {
    SummonPanelProfile panel("panel.hint", manager, hotkeys, InputStrategy::ClickThrough);
    EXPECT_EQ(panel.backend_input_policy(), InputPolicy::PassThrough);
    EXPECT_EQ(panel.hit_result(), HitResult::Transparent);
}

// -----------------------------------------------------------------------------
// E5-05 事件驅動召喚 + NFR-03 能力閘控 + 無效操作
// -----------------------------------------------------------------------------

TEST_F(SummonPanelProfileTest, BindHotkeySucceedsWhenCapabilityAvailable) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    Hotkey hk{Modifier::Meta, Key::Space};
    EXPECT_TRUE(panel.bind_hotkey(hk, 5));
    EXPECT_TRUE(panel.hotkey_bound());
}

TEST_F(SummonPanelProfileTest, BindHotkeyFailsWhenCapabilityUnavailable) {
    NullGlobalHotkeys unavailable(false);
    SummonPanelProfile panel("panel.spotlight", manager, unavailable);
    Hotkey hk{Modifier::Meta, Key::Space};
    EXPECT_FALSE(panel.bind_hotkey(hk, 5));  // NFR-03：能力不存在，不呼叫 register_hotkey()。
    EXPECT_FALSE(panel.hotkey_bound());
}

TEST_F(SummonPanelProfileTest, InjectingBoundHotkeyOpensViaEventDrivenPath) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    Hotkey hk{Modifier::Meta, Key::Space};
    ASSERT_TRUE(panel.bind_hotkey(hk, 5));
    EXPECT_FALSE(panel.is_open());

    hotkeys.inject(hk);  // 模擬 OS 熱鍵按下（E5-05 null 後端的測試觸發入口）。
    EXPECT_TRUE(panel.is_open());  // 事件驅動：未直接呼叫 open()。
}

TEST_F(SummonPanelProfileTest, BindHotkeyTwiceWithoutUnbindFails) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    Hotkey hk1{Modifier::Meta, Key::Space};
    Hotkey hk2{Modifier::Control, Key::P};
    ASSERT_TRUE(panel.bind_hotkey(hk1, 5));
    EXPECT_FALSE(panel.bind_hotkey(hk2, 5));  // 已綁定，不靜默覆寫。
}

TEST_F(SummonPanelProfileTest, UnbindHotkeyAllowsRebindAndStopsInjectFromOpening) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    Hotkey hk{Modifier::Meta, Key::Space};
    ASSERT_TRUE(panel.bind_hotkey(hk, 5));
    EXPECT_TRUE(panel.unbind_hotkey());
    EXPECT_FALSE(panel.hotkey_bound());

    hotkeys.inject(hk);  // 已解除註冊，注入應為 no-op。
    EXPECT_FALSE(panel.is_open());

    Hotkey hk2{Modifier::Control, Key::P};
    EXPECT_TRUE(panel.bind_hotkey(hk2, 5));  // 解除後可重新綁定。
}

TEST_F(SummonPanelProfileTest, UnbindHotkeyWhenNeverBoundFails) {
    SummonPanelProfile panel("panel.spotlight", manager, hotkeys);
    EXPECT_FALSE(panel.unbind_hotkey());  // no-op，不靜默。
}

}  // namespace
