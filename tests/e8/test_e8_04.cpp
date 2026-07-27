// E8-04 行程內模組載入 — 契約測試（gtest）
//
// 驗證：載入並註冊能力、requires 不滿足明確拒絕、permissions 不滿足拒絕、
// 卸載（含 on_unload 鉤子與能力移除）、重複載入拒絕、假模組註冊驗證、
// 跨模組能力衝突、format 不相容 / 缺 name 拒絕、註冊入口回 false / 拋例外、
// 卸載後可重載。平台中立：不含任何平台分支。
#include "module_loader.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "manifest.hpp"  // 以 parse_manifest 建構真實 manifest，順帶驗 E9-02 連結

using ds::ext::CapabilityKind;
using ds::ext::HostEnvironment;
using ds::ext::LoadResult;
using ds::ext::LoadStatus;
using ds::ext::Module;
using ds::ext::ModuleContext;
using ds::ext::ModuleLoader;
using ds::package::Manifest;
using ds::package::parse_manifest;

namespace {

// 直接建構一份 manifest（format 1.0、指定 name / requires / permissions）。
Manifest make_manifest(std::string name,
                       std::vector<std::string> requires_caps = {},
                       std::vector<std::string> perms = {}) {
    Manifest m;
    m.format_version = {1, 0};
    m.name = std::move(name);
    m.required_capabilities = std::move(requires_caps);
    m.permissions = std::move(perms);
    return m;
}

// 一個「假模組」：登記固定的一組能力（感測器 + 元件 + 動作）。
Module make_fake_module(std::string name,
                        std::vector<std::string> requires_caps = {},
                        std::vector<std::string> perms = {}) {
    Module mod;
    mod.manifest = make_manifest(std::move(name), std::move(requires_caps), std::move(perms));
    mod.on_register = [](ModuleContext& ctx) {
        return ctx.provide_sensor("cpu.usage") && ctx.provide_component("bar.widget") &&
               ctx.provide_action("volume.set");
    };
    return mod;
}

}  // namespace

// 載入註冊：能力被登記、模組被追蹤。
TEST(ModuleLoader, LoadsAndRegistersCapabilities) {
    HostEnvironment host;
    ModuleLoader loader(host);

    const LoadResult r = loader.load(make_fake_module("com.example.mod"));
    ASSERT_TRUE(r.ok()) << r.message;

    EXPECT_TRUE(loader.is_loaded("com.example.mod"));
    EXPECT_EQ(loader.loaded_count(), 1u);
    EXPECT_EQ(loader.capability_count(), 3u);
    EXPECT_TRUE(loader.provides(CapabilityKind::Sensor, "cpu.usage"));
    EXPECT_TRUE(loader.provides(CapabilityKind::Component, "bar.widget"));
    EXPECT_TRUE(loader.provides(CapabilityKind::Action, "volume.set"));
    EXPECT_EQ(loader.provider_of(CapabilityKind::Sensor, "cpu.usage"), "com.example.mod");
}

// 假模組註冊驗證：模組在 on_register 內登記的三類能力，皆可自載入器內省查得。
TEST(ModuleLoader, FakeModuleProvidesAllThreeKinds) {
    HostEnvironment host;
    ModuleLoader loader(host);
    ASSERT_TRUE(loader.load(make_fake_module("com.example.triple")).ok());

    EXPECT_EQ(loader.provider_of(CapabilityKind::Sensor, "cpu.usage"), "com.example.triple");
    EXPECT_EQ(loader.provider_of(CapabilityKind::Component, "bar.widget"), "com.example.triple");
    EXPECT_EQ(loader.provider_of(CapabilityKind::Action, "volume.set"), "com.example.triple");
    // 未登記者查不到。
    EXPECT_FALSE(loader.provides(CapabilityKind::Sensor, "gpu.usage"));
    EXPECT_EQ(loader.provider_of(CapabilityKind::Sensor, "gpu.usage"), "");
}

// requires 不滿足 → 明確拒絕（不靜默），缺項回報，且不留任何痕跡。
TEST(ModuleLoader, RejectsWhenRequirementUnsatisfied) {
    HostEnvironment host;  // host 不提供任何能力
    ModuleLoader loader(host);

    Module mod = make_fake_module("com.example.needs", {"host.tray_icon", "host.hotkey"});
    const LoadResult r = loader.load(mod);

    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::MissingRequirement);
    ASSERT_EQ(r.missing.size(), 2u);
    EXPECT_EQ(r.missing[0], "host.tray_icon");
    EXPECT_EQ(r.missing[1], "host.hotkey");
    // 拒絕即無痕：未載入、能力表為空、on_register 不應被提交。
    EXPECT_FALSE(loader.is_loaded("com.example.needs"));
    EXPECT_EQ(loader.loaded_count(), 0u);
    EXPECT_EQ(loader.capability_count(), 0u);
}

// requires 滿足時放行（host 提供所需能力）。
TEST(ModuleLoader, LoadsWhenRequirementsSatisfied) {
    HostEnvironment host;
    host.provide_capability("host.tray_icon").provide_capability("host.hotkey");
    ModuleLoader loader(host);

    const LoadResult r =
        loader.load(make_fake_module("com.example.ok", {"host.tray_icon", "host.hotkey"}));
    EXPECT_TRUE(r.ok()) << r.message;
    EXPECT_TRUE(loader.is_loaded("com.example.ok"));
}

// 部分滿足仍拒絕，且只回報真正缺的那項。
TEST(ModuleLoader, ReportsOnlyTheMissingRequirement) {
    HostEnvironment host;
    host.provide_capability("host.tray_icon");  // 只提供其一
    ModuleLoader loader(host);

    const LoadResult r =
        loader.load(make_fake_module("com.example.partial", {"host.tray_icon", "host.hotkey"}));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::MissingRequirement);
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "host.hotkey");
}

// permissions 未授予 → 拒絕（附缺項）。
TEST(ModuleLoader, RejectsWhenPermissionNotGranted) {
    HostEnvironment host;
    ModuleLoader loader(host);

    const LoadResult r =
        loader.load(make_fake_module("com.example.perm", {}, {"fs.read", "net.connect"}));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::MissingPermission);
    ASSERT_EQ(r.missing.size(), 2u);
    EXPECT_EQ(r.missing[0], "fs.read");
    EXPECT_EQ(r.missing[1], "net.connect");
}

// permissions 全授予 → 放行。
TEST(ModuleLoader, LoadsWhenPermissionsGranted) {
    HostEnvironment host;
    host.grant_permission("fs.read").grant_permission("net.connect");
    ModuleLoader loader(host);

    EXPECT_TRUE(loader.load(make_fake_module("com.example.permok", {}, {"fs.read", "net.connect"}))
                    .ok());
}

// 卸載：能力被移除、模組不再被追蹤、on_unload 鉤子被呼叫一次。
TEST(ModuleLoader, UnloadRemovesCapabilitiesAndCallsHook) {
    HostEnvironment host;
    ModuleLoader loader(host);

    int unload_calls = 0;
    Module mod = make_fake_module("com.example.unload");
    mod.on_unload = [&unload_calls]() { ++unload_calls; };

    ASSERT_TRUE(loader.load(mod).ok());
    ASSERT_EQ(loader.capability_count(), 3u);

    EXPECT_TRUE(loader.unload("com.example.unload"));
    EXPECT_EQ(unload_calls, 1);
    EXPECT_FALSE(loader.is_loaded("com.example.unload"));
    EXPECT_EQ(loader.loaded_count(), 0u);
    EXPECT_EQ(loader.capability_count(), 0u);
    EXPECT_FALSE(loader.provides(CapabilityKind::Sensor, "cpu.usage"));
}

// 卸載未載入的名稱 → 回 false，不崩潰。
TEST(ModuleLoader, UnloadUnknownReturnsFalse) {
    HostEnvironment host;
    ModuleLoader loader(host);
    EXPECT_FALSE(loader.unload("nope"));
}

// 重複載入：同名第二次載入被拒（不覆蓋），且第一份維持不變。
TEST(ModuleLoader, RejectsDuplicateLoad) {
    HostEnvironment host;
    ModuleLoader loader(host);

    ASSERT_TRUE(loader.load(make_fake_module("com.example.dup")).ok());

    const LoadResult r = loader.load(make_fake_module("com.example.dup"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::AlreadyLoaded);
    EXPECT_EQ(loader.loaded_count(), 1u);
    EXPECT_EQ(loader.capability_count(), 3u);
}

// 卸載後可重新載入同名模組。
TEST(ModuleLoader, CanReloadAfterUnload) {
    HostEnvironment host;
    ModuleLoader loader(host);

    ASSERT_TRUE(loader.load(make_fake_module("com.example.cycle")).ok());
    ASSERT_TRUE(loader.unload("com.example.cycle"));
    EXPECT_TRUE(loader.load(make_fake_module("com.example.cycle")).ok());
    EXPECT_TRUE(loader.is_loaded("com.example.cycle"));
}

// 跨模組能力衝突：兩模組提供同一 (kind,id) → 第二個載入失敗，且不部分提交。
TEST(ModuleLoader, RejectsCrossModuleCapabilityConflict) {
    HostEnvironment host;
    ModuleLoader loader(host);

    ASSERT_TRUE(loader.load(make_fake_module("com.example.first")).ok());

    // 第二模組也提供 cpu.usage（與第一個衝突），外加一個獨有能力。
    Module mod;
    mod.manifest = make_manifest("com.example.second");
    mod.on_register = [](ModuleContext& ctx) {
        return ctx.provide_sensor("cpu.usage") && ctx.provide_sensor("mem.usage");
    };
    const LoadResult r = loader.load(mod);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::RegistrationFailed);
    // 不部分提交：second 未載入、其獨有能力 mem.usage 未進表。
    EXPECT_FALSE(loader.is_loaded("com.example.second"));
    EXPECT_FALSE(loader.provides(CapabilityKind::Sensor, "mem.usage"));
    // 第一個模組的 cpu.usage 仍屬第一個。
    EXPECT_EQ(loader.provider_of(CapabilityKind::Sensor, "cpu.usage"), "com.example.first");
}

// 不同種類但同 id 不算衝突（sensor:x 與 action:x 各自獨立）。
TEST(ModuleLoader, SameIdDifferentKindDoesNotConflict) {
    HostEnvironment host;
    ModuleLoader loader(host);

    Module a;
    a.manifest = make_manifest("com.example.a");
    a.on_register = [](ModuleContext& ctx) { return ctx.provide_sensor("power"); };
    Module b;
    b.manifest = make_manifest("com.example.b");
    b.on_register = [](ModuleContext& ctx) { return ctx.provide_action("power"); };

    ASSERT_TRUE(loader.load(a).ok());
    EXPECT_TRUE(loader.load(b).ok());
    EXPECT_EQ(loader.provider_of(CapabilityKind::Sensor, "power"), "com.example.a");
    EXPECT_EQ(loader.provider_of(CapabilityKind::Action, "power"), "com.example.b");
}

// manifest 缺 name → InvalidManifest。
TEST(ModuleLoader, RejectsManifestWithoutName) {
    HostEnvironment host;
    ModuleLoader loader(host);

    Module mod = make_fake_module("");  // 空 name
    const LoadResult r = loader.load(mod);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::InvalidManifest);
}

// format_version 不相容（主版不同）→ IncompatibleFormat。
TEST(ModuleLoader, RejectsIncompatibleFormat) {
    HostEnvironment host;
    ModuleLoader loader(host);

    Module mod = make_fake_module("com.example.future");
    mod.manifest.format_version = {2, 0};  // 主版超過本實作支援（1.x）
    const LoadResult r = loader.load(mod);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::IncompatibleFormat);
    EXPECT_FALSE(loader.is_loaded("com.example.future"));
}

// 註冊入口回 false → RegistrationFailed，不留痕。
TEST(ModuleLoader, RejectsWhenRegisterReturnsFalse) {
    HostEnvironment host;
    ModuleLoader loader(host);

    Module mod;
    mod.manifest = make_manifest("com.example.declines");
    mod.on_register = [](ModuleContext& ctx) {
        ctx.provide_sensor("half.registered");  // 即使登記了東西
        return false;                           // 但回報失敗
    };
    const LoadResult r = loader.load(mod);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::RegistrationFailed);
    EXPECT_FALSE(loader.is_loaded("com.example.declines"));
    EXPECT_EQ(loader.capability_count(), 0u);
}

// 缺註冊入口 → RegistrationFailed。
TEST(ModuleLoader, RejectsWhenNoRegisterEntry) {
    HostEnvironment host;
    ModuleLoader loader(host);

    Module mod;
    mod.manifest = make_manifest("com.example.noentry");
    // on_register 未設定
    const LoadResult r = loader.load(mod);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::RegistrationFailed);
}

// 註冊入口拋例外 → 被攔為 RegistrationFailed，載入器不崩潰、不留痕。
TEST(ModuleLoader, RegisterExceptionIsContained) {
    HostEnvironment host;
    ModuleLoader loader(host);

    Module mod;
    mod.manifest = make_manifest("com.example.throws");
    mod.on_register = [](ModuleContext&) -> bool { throw std::runtime_error("boom"); };
    const LoadResult r = loader.load(mod);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::RegistrationFailed);
    EXPECT_FALSE(loader.is_loaded("com.example.throws"));
    EXPECT_EQ(loader.capability_count(), 0u);
}

// ModuleContext 擋模組內重複登記（同 kind+id 第二次回 false）。
TEST(ModuleContextTest, RejectsIntraModuleDuplicate) {
    ModuleContext ctx;
    EXPECT_TRUE(ctx.provide_sensor("cpu.usage"));
    EXPECT_FALSE(ctx.provide_sensor("cpu.usage"));  // 重複
    EXPECT_FALSE(ctx.provide_sensor(""));           // 空 id
    EXPECT_TRUE(ctx.provide_action("cpu.usage"));   // 不同 kind 可
    EXPECT_EQ(ctx.size(), 2u);
}

// 以 E9-02 parse_manifest 建構的真實 manifest 也能被載入（驗跨單元連結與資料流）。
TEST(ModuleLoader, LoadsModuleFromParsedManifest) {
    const std::string text =
        "format_version: 1.0\n"
        "name: com.example.parsed\n"
        "requires: host.tray_icon\n"
        "permissions: fs.read\n";
    const auto pr = parse_manifest(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    HostEnvironment host;
    host.provide_capability("host.tray_icon").grant_permission("fs.read");
    ModuleLoader loader(host);

    Module mod;
    mod.manifest = pr.manifest();
    mod.on_register = [](ModuleContext& ctx) { return ctx.provide_sensor("net.rx"); };
    const LoadResult r = loader.load(mod);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_TRUE(loader.provides(CapabilityKind::Sensor, "net.rx"));
    EXPECT_EQ(loader.loaded_names().front(), "com.example.parsed");
}
