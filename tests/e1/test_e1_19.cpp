// E1-19 顯示器熱插拔復原 — 契約測試（gtest）
//
// 驗證：
//   - 拓撲簽章由具名 ScreenId 導出、與列舉順序無關（NFR-02，非座標 / index / z-order）。
//   - 經 E5-08 事件驅動：DisplayChanged 觸發拓撲重算 + on_recover 回呼；其餘事件忽略。
//   - 佈局記錄 / 復原往返：組態 A 記錄 → 切 B → 切回 A 還原。
//   - 新顯示器 → 預設佈局；無記錄螢幕保守回預設（NFR-03 以 find/has 保護）。
//   - 拔除處理：拔除的螢幕記錄保留於原組態，重接即復原。
//   - 多組態記憶：各顯示器組態各自獨立記憶。
//   - 解構時解除 E5-08 訂閱。
// 相位 1：來源與拓撲皆注入式，不含平台分支 / 真實顯示器 API / 絕對座標。
#include "layout_recovery.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::NullSystemEventSource;
using ds::events::SystemEvent;
using ds::events::SystemEventType;
using ds::kernel::LayoutRecoveryManager;
using ds::kernel::Screen;
using ds::kernel::ScreenAnchor;
using ds::kernel::ScreenId;
using ds::kernel::ScreenRegistry;
using ds::kernel::ScreenRole;
using ds::kernel::topology_key;

namespace {

// 便利：以一組具名螢幕 id 造一個 ScreenRegistry（首個為 Primary/Center，其餘 Secondary/Right）。
ScreenRegistry make_topology(const std::vector<ScreenId>& ids) {
    std::vector<Screen> screens;
    bool first = true;
    for (const auto& id : ids) {
        screens.push_back(Screen{id, "screen",
                                 first ? ScreenRole::Primary : ScreenRole::Secondary,
                                 first ? ScreenAnchor::Center : ScreenAnchor::Right});
        first = false;
    }
    return ScreenRegistry(std::move(screens));
}

// 便利事件：顯示器組態變更。
SystemEvent display_changed(const std::string& detail = "") {
    return SystemEvent{SystemEventType::DisplayChanged, detail};
}

// ---- 拓撲簽章（NFR-02：具名導出、與順序無關） ----

TEST(TopologyKey, DerivedFromNamedIdsAndOrderIndependent) {
    const ScreenRegistry a = make_topology({"screen.laptop", "screen.hdmi"});
    const ScreenRegistry b = make_topology({"screen.hdmi", "screen.laptop"});  // 反序
    // 同一組具名螢幕不論列舉順序 → 相同簽章（拓撲恢復可辨識）。
    EXPECT_EQ(topology_key(a), topology_key(b));
    // 不同組具名螢幕 → 不同簽章。
    EXPECT_NE(topology_key(a), topology_key(make_topology({"screen.laptop"})));
    // 空拓撲 → 空字串。
    EXPECT_TRUE(topology_key(make_topology({})).empty());
}

// ---- 初始拓撲：建構時即取一次當前拓撲 ----

TEST(LayoutRecovery, InitialActiveTopologyFromProvider) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.primary"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("default"));

    EXPECT_EQ(mgr.active_topology_key(), topology_key(make_topology({"screen.primary"})));
    EXPECT_TRUE(mgr.is_active_screen("screen.primary"));
    EXPECT_FALSE(mgr.is_active_screen("screen.absent"));
    const std::vector<ScreenId> ids = mgr.active_screen_ids();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], "screen.primary");
}

// ---- 經 E5-08 事件驅動：DisplayChanged 觸發拓撲重算 + on_recover ----

TEST(LayoutRecovery, DisplayChangedEventDrivesTopologyRefresh) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.laptop"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("default"));

    int recover_calls = 0;
    std::string last_key;
    mgr.set_on_recover([&](const ScreenRegistry& topo) {
        ++recover_calls;
        last_key = topology_key(topo);
    });

    // 熱插：接上外接顯示器後，來源回報新拓撲，OS 送出 DisplayChanged。
    current = make_topology({"screen.laptop", "screen.hdmi"});
    bus.inject(display_changed("external display attached"));

    EXPECT_EQ(recover_calls, 1);
    EXPECT_EQ(mgr.active_topology_key(), topology_key(make_topology({"screen.laptop", "screen.hdmi"})));
    EXPECT_EQ(last_key, mgr.active_topology_key());
    EXPECT_TRUE(mgr.is_active_screen("screen.hdmi"));
}

// ---- 僅 DisplayChanged 觸發；其餘系統事件忽略 ----

TEST(LayoutRecovery, NonDisplayEventsAreIgnored) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.laptop"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("default"));

    int recover_calls = 0;
    mgr.set_on_recover([&](const ScreenRegistry&) { ++recover_calls; });

    // 換掉來源拓撲，但送出的是非顯示器事件 → 不應觸發重算。
    current = make_topology({"screen.laptop", "screen.hdmi"});
    bus.inject(SystemEvent{SystemEventType::SystemSleep, ""});
    bus.inject(SystemEvent{SystemEventType::SessionLocked, ""});
    bus.inject(SystemEvent{SystemEventType::PowerStatusChanged, ""});

    EXPECT_EQ(recover_calls, 0);
    // 當前拓撲仍為初始（未因非顯示器事件更新）。
    EXPECT_EQ(mgr.active_topology_key(), topology_key(make_topology({"screen.laptop"})));
    EXPECT_FALSE(mgr.is_active_screen("screen.hdmi"));
}

// ---- 佈局記錄 / 復原往返 ----

TEST(LayoutRecovery, RecordAndRestoreRoundTrip) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.laptop", "screen.hdmi"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("default"));

    // 組態 A（雙螢幕）：使用者排好佈局並記錄。
    mgr.record("screen.laptop", "A-left-grid");
    mgr.record("screen.hdmi", "A-right-stack");
    EXPECT_EQ(mgr.layout_for("screen.laptop"), "A-left-grid");
    EXPECT_EQ(mgr.layout_for("screen.hdmi"), "A-right-stack");

    // 拔除外接 → 組態 B（單螢幕）。
    current = make_topology({"screen.laptop"});
    bus.inject(display_changed("external detached"));
    // B 尚無記錄 → laptop 回預設。
    EXPECT_EQ(mgr.layout_for("screen.laptop"), "default");
    mgr.record("screen.laptop", "B-solo");
    EXPECT_EQ(mgr.layout_for("screen.laptop"), "B-solo");

    // 重接外接 → 拓撲恢復為 A，A 的記錄還原（往返）。
    current = make_topology({"screen.hdmi", "screen.laptop"});  // 反序重接
    bus.inject(display_changed("external re-attached"));
    EXPECT_EQ(mgr.layout_for("screen.laptop"), "A-left-grid");
    EXPECT_EQ(mgr.layout_for("screen.hdmi"), "A-right-stack");

    // 再切回 B，B 的記錄也仍在（多組態記憶各自獨立）。
    current = make_topology({"screen.laptop"});
    bus.inject(display_changed());
    EXPECT_EQ(mgr.layout_for("screen.laptop"), "B-solo");
}

// ---- 新顯示器 → 預設佈局 ----

TEST(LayoutRecovery, NewDisplayGetsDefaultLayout) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.laptop"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("default-tiled"));

    mgr.record("screen.laptop", "custom");

    // 接上一顆從未見過的顯示器 → 全新組態，新螢幕無記錄。
    current = make_topology({"screen.laptop", "screen.projector"});
    bus.inject(display_changed());

    EXPECT_FALSE(mgr.has_recorded("screen.projector"));
    EXPECT_EQ(mgr.layout_for("screen.projector"), "default-tiled");  // 新顯示器預設
    // 同組態下 laptop 也尚無記錄（不同組態 → 不同記憶）→ 亦回預設。
    EXPECT_FALSE(mgr.has_recorded("screen.laptop"));
    EXPECT_EQ(mgr.layout_for("screen.laptop"), "default-tiled");
}

// ---- 無記錄 / 未知螢幕保守回預設（NFR-03） ----

TEST(LayoutRecovery, UnknownAndUnrecordedAreConservative) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.a"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("fallback"));

    // 完全未記錄任何東西：任何查詢回預設、has_recorded false。
    EXPECT_FALSE(mgr.has_recorded("screen.a"));
    EXPECT_EQ(mgr.layout_for("screen.a"), "fallback");
    // 未知螢幕（不在當前拓撲）亦保守回預設，不崩。
    EXPECT_FALSE(mgr.has_recorded("screen.nonexistent"));
    EXPECT_EQ(mgr.layout_for("screen.nonexistent"), "fallback");
    EXPECT_EQ(mgr.default_layout(), "fallback");
}

// ---- 拔除處理：拔除螢幕的記錄保留於原組態 ----

TEST(LayoutRecovery, RemovedScreenRecordPreservedForReconnect) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.main", "screen.ext"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("default"));

    mgr.record("screen.ext", "ext-layout");
    const std::string dual_key = mgr.active_topology_key();

    // 拔除 ext。
    current = make_topology({"screen.main"});
    bus.inject(display_changed("ext removed"));
    EXPECT_FALSE(mgr.is_active_screen("screen.ext"));  // 已不在當前拓撲
    // 但雙螢幕組態的記錄仍被記得。
    EXPECT_TRUE(mgr.remembers_topology(dual_key));

    // 重接 ext → 記錄還原。
    current = make_topology({"screen.main", "screen.ext"});
    bus.inject(display_changed("ext back"));
    EXPECT_EQ(mgr.active_topology_key(), dual_key);
    EXPECT_TRUE(mgr.has_recorded("screen.ext"));
    EXPECT_EQ(mgr.layout_for("screen.ext"), "ext-layout");
}

// ---- 多組態記憶：組態數量、各組態獨立列舉 ----

TEST(LayoutRecovery, MultipleConfigurationsRememberedIndependently) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.a"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("d"));

    // 組態 1：單螢幕。
    mgr.record("screen.a", "one");
    EXPECT_EQ(mgr.topology_count(), 1u);

    // 組態 2：雙螢幕。
    current = make_topology({"screen.a", "screen.b"});
    bus.inject(display_changed());
    mgr.record("screen.a", "two-a");
    mgr.record("screen.b", "two-b");
    EXPECT_EQ(mgr.topology_count(), 2u);
    // 當前（組態 2）記錄列舉。
    EXPECT_EQ(mgr.recorded_screen_ids().size(), 2u);
    EXPECT_EQ(mgr.layout_for("screen.a"), "two-a");

    // 回組態 1：其記錄與組態 2 互不干擾。
    current = make_topology({"screen.a"});
    bus.inject(display_changed());
    EXPECT_EQ(mgr.recorded_screen_ids().size(), 1u);
    EXPECT_EQ(mgr.layout_for("screen.a"), "one");
    EXPECT_EQ(mgr.topology_count(), 2u);  // 仍是兩個組態
}

// ---- 記錄覆蓋（同組態同螢幕後記錄者為準） ----

TEST(LayoutRecovery, RecordOverwritesWithinSameConfig) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.a"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("d"));

    mgr.record("screen.a", "v1");
    mgr.record("screen.a", "v2");  // 覆蓋
    EXPECT_EQ(mgr.layout_for("screen.a"), "v2");
    EXPECT_EQ(mgr.recorded_screen_ids().size(), 1u);
}

// ---- clear 清空記憶但不影響當前拓撲 ----

TEST(LayoutRecovery, ClearForgetsAllConfigsButKeepsActiveTopology) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.a"});
    LayoutRecoveryManager<std::string> mgr(
        bus, [&current]() { return current; }, std::string("d"));

    mgr.record("screen.a", "x");
    EXPECT_EQ(mgr.topology_count(), 1u);
    mgr.clear();
    EXPECT_EQ(mgr.topology_count(), 0u);
    EXPECT_FALSE(mgr.has_recorded("screen.a"));
    EXPECT_EQ(mgr.layout_for("screen.a"), "d");
    // 當前拓撲不受影響。
    EXPECT_TRUE(mgr.is_active_screen("screen.a"));
}

// ---- 解構時解除 E5-08 訂閱 ----

TEST(LayoutRecovery, UnsubscribesOnDestruction) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.a"});
    EXPECT_EQ(bus.listener_count(), 0u);
    {
        LayoutRecoveryManager<std::string> mgr(
            bus, [&current]() { return current; }, std::string("d"));
        EXPECT_EQ(bus.listener_count(), 1u);  // 建構即訂閱
    }
    EXPECT_EQ(bus.listener_count(), 0u);  // 解構即解除
}

// ---- 泛型佈局：以具名結構（非座標）作為 Layout，驗證 NFR-02 精神 ----

struct ElementLayout {
    std::string arrangement;  // 具名排列權杖（如 "grid" / "stacked"），非座標
    std::string focus;        // 具名焦點元件 id
    bool operator==(const ElementLayout& o) const {
        return arrangement == o.arrangement && focus == o.focus;
    }
};

TEST(LayoutRecovery, WorksWithNamedStructLayoutPayload) {
    NullSystemEventSource bus;
    ScreenRegistry current = make_topology({"screen.a", "screen.b"});
    const ElementLayout kDefault{"grid", ""};
    LayoutRecoveryManager<ElementLayout> mgr(
        bus, [&current]() { return current; }, kDefault);

    mgr.record("screen.a", ElementLayout{"stacked", "widget.clock"});
    EXPECT_TRUE(mgr.has_recorded("screen.a"));
    EXPECT_TRUE(mgr.layout_for("screen.a") == (ElementLayout{"stacked", "widget.clock"}));
    // b 未記錄 → 預設。
    EXPECT_TRUE(mgr.layout_for("screen.b") == kDefault);

    // 切換組態後回來仍還原。
    current = make_topology({"screen.a"});
    bus.inject(display_changed());
    EXPECT_TRUE(mgr.layout_for("screen.a") == kDefault);  // 單螢幕組態新記憶 → 預設
    current = make_topology({"screen.a", "screen.b"});
    bus.inject(display_changed());
    EXPECT_TRUE(mgr.layout_for("screen.a") == (ElementLayout{"stacked", "widget.clock"}));
}

}  // namespace
