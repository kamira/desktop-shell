// E4-06 Surface 編號定址與切換 — 單元測試（gtest）
//
// 涵蓋：註冊 / 列舉、切換、目前查詢、切換通知、未知 id 報錯（switch_to / unregister）、
// 重複註冊、空 id 拒絕、移除目前 surface、切到目前已是的 surface、多監聽器順序、
// NFR-02（具名清單，不外露數字索引 / z-order）。全程純記憶體邏輯，不涉及任何平台後端。
#include "surface_switcher.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::render::SurfaceSwitcher;
using ds::render::SwitchStatus;

namespace {

using SurfaceId = ds::kernel::SurfaceId;

}  // namespace

// --- 註冊 / 列舉 -----------------------------------------------------------

TEST(Registration, RegisterSucceedsAndIsListedInOrder) {
    SurfaceSwitcher sw;
    EXPECT_EQ(sw.count(), 0u);

    EXPECT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    EXPECT_EQ(sw.register_surface("page.settings"), SwitchStatus::Ok);
    EXPECT_EQ(sw.register_surface("page.about"), SwitchStatus::Ok);

    EXPECT_EQ(sw.count(), 3u);
    EXPECT_TRUE(sw.has("page.home"));
    EXPECT_TRUE(sw.has("page.settings"));
    EXPECT_TRUE(sw.has("page.about"));

    std::vector<SurfaceId> expected = {"page.home", "page.settings", "page.about"};
    EXPECT_EQ(sw.list(), expected);  // 依註冊順序，具名清單
}

TEST(Registration, EmptyIdRejected) {
    SurfaceSwitcher sw;
    EXPECT_EQ(sw.register_surface(""), SwitchStatus::Invalid);
    EXPECT_EQ(sw.count(), 0u);
    EXPECT_TRUE(sw.list().empty());
}

TEST(Registration, DuplicateRegistrationRejectedAndDoesNotOverwrite) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    EXPECT_EQ(sw.register_surface("page.home"), SwitchStatus::Invalid);  // 重複註冊
    EXPECT_EQ(sw.count(), 1u);  // 未被覆蓋 / 未新增第二筆
}

TEST(Registration, HasReturnsFalseForUnknownId) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    EXPECT_FALSE(sw.has("page.unknown"));
}

// --- 移除 -------------------------------------------------------------------

TEST(Unregister, RemovesEntryAndAllowsReRegistration) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    EXPECT_EQ(sw.unregister_surface("page.home"), SwitchStatus::Ok);
    EXPECT_FALSE(sw.has("page.home"));
    EXPECT_EQ(sw.count(), 0u);

    // 移除後可重新註冊（非殘留「已存在」狀態）。
    EXPECT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
}

TEST(Unregister, UnknownIdReportsNotFoundNotSilent) {
    SurfaceSwitcher sw;
    EXPECT_EQ(sw.unregister_surface("page.ghost"), SwitchStatus::NotFound);
}

TEST(Unregister, RemovingCurrentSurfaceClearsCurrentWithoutNotification) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);
    ASSERT_TRUE(sw.has_current());

    int notify_count = 0;
    sw.on_switch([&](const SurfaceId&, const SurfaceId&) { ++notify_count; });

    EXPECT_EQ(sw.unregister_surface("page.home"), SwitchStatus::Ok);
    EXPECT_FALSE(sw.has_current());
    EXPECT_TRUE(sw.current().empty());
    EXPECT_EQ(notify_count, 0);  // unregister 不觸發 on_switch（僅 switch_to 觸發）
}

// --- 切換 / 目前查詢 ---------------------------------------------------------

TEST(CurrentQuery, NoCurrentBeforeAnySwitch) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    EXPECT_FALSE(sw.has_current());
    EXPECT_TRUE(sw.current().empty());
}

TEST(SwitchTo, SwitchesToRegisteredSurfaceAndUpdatesCurrent) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(sw.register_surface("page.settings"), SwitchStatus::Ok);

    EXPECT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);
    EXPECT_TRUE(sw.has_current());
    EXPECT_EQ(sw.current(), "page.home");

    EXPECT_EQ(sw.switch_to("page.settings"), SwitchStatus::Ok);
    EXPECT_EQ(sw.current(), "page.settings");
}

TEST(SwitchTo, UnknownIdReportsNotFoundAndDoesNotChangeCurrent) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);

    EXPECT_EQ(sw.switch_to("page.ghost"), SwitchStatus::NotFound);
    EXPECT_EQ(sw.current(), "page.home");  // 未變更
}

TEST(SwitchTo, UnknownIdOnEmptyRegistryReportsNotFoundNotCrash) {
    SurfaceSwitcher sw;
    EXPECT_EQ(sw.switch_to("page.anything"), SwitchStatus::NotFound);
    EXPECT_FALSE(sw.has_current());
}

TEST(SwitchTo, SwitchingToAlreadyCurrentStillSucceedsAndNotifies) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);

    int notify_count = 0;
    sw.on_switch([&](const SurfaceId&, const SurfaceId&) { ++notify_count; });

    EXPECT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);
    EXPECT_EQ(sw.current(), "page.home");
    EXPECT_EQ(notify_count, 1);  // 即使目標與目前相同，仍觸發一次通知
}

// --- 切換通知 -----------------------------------------------------------------

TEST(SwitchNotification, ListenerReceivesFromAndTo) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);
    ASSERT_EQ(sw.register_surface("page.settings"), SwitchStatus::Ok);

    SurfaceId seen_from = "unset";
    SurfaceId seen_to = "unset";
    int calls = 0;
    sw.on_switch([&](const SurfaceId& from, const SurfaceId& to) {
        seen_from = from;
        seen_to = to;
        ++calls;
    });

    ASSERT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(seen_from.empty());  // 首次切換前無目前 surface -> from 為空
    EXPECT_EQ(seen_to, "page.home");

    ASSERT_EQ(sw.switch_to("page.settings"), SwitchStatus::Ok);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(seen_from, "page.home");
    EXPECT_EQ(seen_to, "page.settings");
}

TEST(SwitchNotification, FailedSwitchDoesNotNotify) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);

    int calls = 0;
    sw.on_switch([&](const SurfaceId&, const SurfaceId&) { ++calls; });

    EXPECT_EQ(sw.switch_to("page.ghost"), SwitchStatus::NotFound);
    EXPECT_EQ(calls, 0);  // 未知 id 不觸發通知
}

TEST(SwitchNotification, MultipleListenersCalledInRegistrationOrder) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.home"), SwitchStatus::Ok);

    std::vector<int> call_order;
    sw.on_switch([&](const SurfaceId&, const SurfaceId&) { call_order.push_back(1); });
    sw.on_switch([&](const SurfaceId&, const SurfaceId&) { call_order.push_back(2); });
    sw.on_switch([&](const SurfaceId&, const SurfaceId&) { call_order.push_back(3); });
    EXPECT_EQ(sw.listener_count(), 3u);

    ASSERT_EQ(sw.switch_to("page.home"), SwitchStatus::Ok);
    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(call_order, expected);
}

TEST(SwitchNotification, EmptyListenerNotRegistered) {
    SurfaceSwitcher sw;
    ds::render::SwitchListener empty_listener;  // 預設建構的 std::function 為「空」
    sw.on_switch(empty_listener);
    EXPECT_EQ(sw.listener_count(), 0u);
}

// --- NFR-02：具名、無數字索引 / z-order 外露 ------------------------------------

TEST(NamedAddressing, ListContainsOnlyNamedIdsNoNumericIndexSurface) {
    SurfaceSwitcher sw;
    ASSERT_EQ(sw.register_surface("page.a"), SwitchStatus::Ok);
    ASSERT_EQ(sw.register_surface("page.b"), SwitchStatus::Ok);

    // list() 的元素型別即 ds::kernel::SurfaceId（std::string）；順序反映註冊先後，
    // 但介面本身不提供任何「以數字取第 N 個」的存取方式（無 operator[](int) / at(int) 之類 API）。
    for (const auto& id : sw.list()) {
        EXPECT_FALSE(id.empty());
    }
    EXPECT_EQ(sw.list().size(), sw.count());
}
