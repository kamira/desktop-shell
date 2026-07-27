// E9-07 拖放安裝 — 契約測試（gtest）
//
// 涵蓋：有效拖放 → 安裝成功並登錄、無效來源（空/無法讀取）報錯、不相容套件報錯、
// 安裝生命週期逐階段回報、重複安裝處理、以及以 E5-08 系統事件維護的安裝閘門
// （SessionLocked/SystemSleep 拒絕、SessionUnlocked/SystemWake 恢復）。
// 平台中立：不含任何平台分支。
#include "drop_install.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "system_event.hpp"

using ds::events::NullSystemEventSource;
using ds::events::SystemEvent;
using ds::events::SystemEventType;
using ds::package::DropEvent;
using ds::package::DropInstaller;
using ds::package::InstallLifecycleEvent;
using ds::package::InstallRegistry;
using ds::package::InstallStage;
using ds::package::InstallStatus;
using ds::package::MemoryInstallSource;

namespace {

// 一份最小合法套件描述（manifest + 內容清單），name 可注入。
std::string valid_package_text(const std::string& name) {
    return "format_version: 1.0\n"
           "name: " + name + "\n"
           "requires: host.tray_icon\n"
           "---\n"
           "asset: icons/tray.png\n"
           "code: main.wasm\n";
}

// 有效拖放 → 安裝成功、走到 Registered 階段、並登錄到記憶體登錄。
TEST(DropInstall, ValidDropInstallsAndRegisters) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("com.example.hello.pkg",
                                       valid_package_text("com.example.hello"));
    DropEvent drop;
    drop.source = &src;
    drop.detail = "dropped onto tray";

    const auto r = installer.handle_drop(drop);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.status, InstallStatus::Success);
    EXPECT_EQ(r.stage, InstallStage::Registered);
    EXPECT_EQ(r.package_name, "com.example.hello");

    EXPECT_EQ(registry.size(), 1u);
    EXPECT_TRUE(registry.contains("com.example.hello"));
    const auto* pkg = registry.find("com.example.hello");
    ASSERT_NE(pkg, nullptr);
    EXPECT_EQ(pkg->manifest.name, "com.example.hello");
    EXPECT_EQ(pkg->entries.size(), 2u);
}

// 無效來源：拖放事件無來源（null）→ Received 階段明確報錯，不登錄。
TEST(DropInstall, NullSourceFails) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    DropEvent drop;  // source == nullptr
    const auto r = installer.handle_drop(drop);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.stage, InstallStage::Received);
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(registry.size(), 0u);
}

// 無效來源：來源無法讀取（unavailable）→ Resolved 階段明確報錯，不登錄。
TEST(DropInstall, UnavailableSourceFails) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    const MemoryInstallSource src = MemoryInstallSource::unavailable("broken.pkg");
    DropEvent drop;
    drop.source = &src;

    const auto r = installer.handle_drop(drop);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.stage, InstallStage::Resolved);
    EXPECT_NE(r.message.find("broken.pkg"), std::string::npos);
    EXPECT_EQ(registry.size(), 0u);
}

// 來源可讀但套件文字無效（缺 manifest）→ Parsed 階段明確報錯。
TEST(DropInstall, MalformedPackageTextFailsAtParse) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    // 只有內容清單、無 manifest 實質內容 → E9-01 報「缺少 manifest 區段」。
    const MemoryInstallSource src =
        MemoryInstallSource::with_text("bad.pkg", "---\nasset: icons/tray.png\n");
    DropEvent drop;
    drop.source = &src;

    const auto r = installer.handle_drop(drop);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.stage, InstallStage::Parsed);
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(registry.size(), 0u);
}

// 不相容套件：format_version 主版號不相容 → Parsed 階段報錯（E9-02 格式相容性檢查）。
TEST(DropInstall, IncompatiblePackageFails) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    const std::string text =
        "format_version: 2.0\n"      // 主版號 2，本實作支援上限 1.x → 不相容。
        "name: com.example.future\n";
    const MemoryInstallSource src = MemoryInstallSource::with_text("future.pkg", text);
    DropEvent drop;
    drop.source = &src;

    const auto r = installer.handle_drop(drop);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.stage, InstallStage::Parsed);
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(registry.size(), 0u);
}

// 安裝生命週期回報：成功安裝逐階段回報 Received→Resolved→Parsed→Validated→Registered。
TEST(DropInstall, LifecycleReportsEachStage) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    std::vector<InstallStage> stages;
    std::vector<bool> oks;
    installer.set_lifecycle_listener([&](const InstallLifecycleEvent& ev) {
        stages.push_back(ev.stage);
        oks.push_back(ev.ok);
    });

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("life.pkg", valid_package_text("com.example.life"));
    DropEvent drop;
    drop.source = &src;

    const auto r = installer.handle_drop(drop);
    ASSERT_TRUE(r.ok()) << r.message;

    const std::vector<InstallStage> expected = {
        InstallStage::Received, InstallStage::Resolved, InstallStage::Parsed,
        InstallStage::Validated, InstallStage::Registered};
    EXPECT_EQ(stages, expected);
    for (bool ok : oks) {
        EXPECT_TRUE(ok);
    }
}

// 生命週期回報：失敗時最後一筆回報 ok=false 且落在失敗階段。
TEST(DropInstall, LifecycleReportsFailureStage) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    std::vector<InstallLifecycleEvent> events;
    installer.set_lifecycle_listener(
        [&](const InstallLifecycleEvent& ev) { events.push_back(ev); });

    const MemoryInstallSource src = MemoryInstallSource::unavailable("broken.pkg");
    DropEvent drop;
    drop.source = &src;

    const auto r = installer.handle_drop(drop);
    ASSERT_FALSE(r.ok());
    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().stage, InstallStage::Resolved);
    EXPECT_FALSE(events.back().ok);
}

// 重複安裝：同名套件第二次拖放 → Registered 階段明確報錯，登錄維持 1 筆、不覆寫。
TEST(DropInstall, DuplicateInstallIsRejected) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("dup.pkg", valid_package_text("com.example.dup"));
    DropEvent drop;
    drop.source = &src;

    const auto first = installer.handle_drop(drop);
    ASSERT_TRUE(first.ok()) << first.message;
    EXPECT_EQ(registry.size(), 1u);

    const auto second = installer.handle_drop(drop);
    EXPECT_FALSE(second.ok());
    EXPECT_EQ(second.stage, InstallStage::Registered);
    EXPECT_NE(second.message.find("com.example.dup"), std::string::npos);
    EXPECT_EQ(registry.size(), 1u);  // 未覆寫、未新增。
}

// 安裝閘門：E5-08 SessionLocked 關閉閘門 → 拖放被拒；SessionUnlocked 恢復 → 拖放成功。
TEST(DropInstall, SessionLockGatesInstall) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    EXPECT_TRUE(installer.accepting());

    // 注入 SessionLocked（經 E5-08 分派）→ 閘門關閉。
    sys.inject(SystemEvent{SystemEventType::SessionLocked, "user away"});
    EXPECT_FALSE(installer.accepting());

    const MemoryInstallSource src =
        MemoryInstallSource::with_text("gate.pkg", valid_package_text("com.example.gate"));
    DropEvent drop;
    drop.source = &src;

    const auto blocked = installer.handle_drop(drop);
    EXPECT_FALSE(blocked.ok());
    EXPECT_EQ(blocked.stage, InstallStage::Received);
    EXPECT_FALSE(blocked.message.empty());
    EXPECT_EQ(registry.size(), 0u);

    // 注入 SessionUnlocked → 閘門重新開啟，安裝成功。
    sys.inject(SystemEvent{SystemEventType::SessionUnlocked, "user back"});
    EXPECT_TRUE(installer.accepting());

    const auto allowed = installer.handle_drop(drop);
    EXPECT_TRUE(allowed.ok()) << allowed.message;
    EXPECT_EQ(registry.size(), 1u);
}

// 安裝閘門：SystemSleep 關閉、SystemWake 恢復；無關事件（DisplayChanged）不影響閘門。
TEST(DropInstall, SleepGatesAndUnrelatedEventsIgnored) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    DropInstaller installer(registry, sys);

    sys.inject(SystemEvent{SystemEventType::DisplayChanged, "external display"});
    EXPECT_TRUE(installer.accepting());  // 無關事件不改變閘門。

    sys.inject(SystemEvent{SystemEventType::SystemSleep, ""});
    EXPECT_FALSE(installer.accepting());

    sys.inject(SystemEvent{SystemEventType::PowerStatusChanged, "on battery"});
    EXPECT_FALSE(installer.accepting());  // 無關事件不改變（仍關閉）。

    sys.inject(SystemEvent{SystemEventType::SystemWake, ""});
    EXPECT_TRUE(installer.accepting());
}

// 解構後解除 E5-08 訂閱：installer 消滅後事件來源不再持有訂閱者。
TEST(DropInstall, UnsubscribesOnDestruction) {
    NullSystemEventSource sys;
    InstallRegistry registry;
    {
        DropInstaller installer(registry, sys);
        EXPECT_EQ(sys.listener_count(), 1u);
    }
    EXPECT_EQ(sys.listener_count(), 0u);
}

}  // namespace
