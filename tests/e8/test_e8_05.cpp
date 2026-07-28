// E8-05 外部模組開發介面（SDK）— 契約測試（gtest）
//
// 驗證：ModuleInfo metadata 宣告 / 轉 manifest、生命週期狀態機（init/start/stop/
// teardown 合法與非法轉移）、透過 ModuleContext 存取宿主服務（has() 閘控 + 型別安全
// 轉型）、能力登記委派、SampleSensorModule 實作契約、經 make_module 與 E8-04
// ModuleLoader 的完整載入 / 卸載整合、錯誤處理（缺服務 / init 回 false / requires
// 未滿足）。平台中立：不含任何平台分支。
#include "module_sdk.hpp"

#include <gtest/gtest.h>

#include <string>

#include "manifest.hpp"        // E9-02：驗 ModuleInfo → Manifest 相容
#include "module_loader.hpp"   // E8-04：ModuleLoader / HostEnvironment 整合
#include "sample_module.hpp"   // E8-05 範例模組

using ds::ext::HostEnvironment;
using ds::ext::LoadStatus;
using ds::ext::Module;
using ds::ext::ModuleLoader;
using ds::ext::sdk::HostServiceRegistry;
using ds::ext::sdk::IHostService;
using ds::ext::sdk::IModule;
using ds::ext::sdk::make_module;
using ds::ext::sdk::ModuleBase;
using ds::ext::sdk::ModuleContext;
using ds::ext::sdk::ModuleInfo;
using ds::ext::sdk::ModuleState;
using ds::ext::sdk::sample::FixedClock;
using ds::ext::sdk::sample::IClockService;
using ds::ext::sdk::sample::SampleSensorModule;

namespace {

// 記錄生命週期呼叫序列的探針模組（不用宿主服務），用於觀察狀態機轉移。
class ProbeModule : public ModuleBase {
public:
    std::string trace;
    bool fail_init = false;
    bool fail_start = false;

    ModuleInfo info() const override {
        ModuleInfo i;
        i.name = "com.example.probe";
        i.description = "生命週期探針";
        return i;
    }

protected:
    bool on_init(ModuleContext& ctx) override {
        trace += "i";
        ctx.provide_action("probe.run");
        return !fail_init;
    }
    bool on_start() override {
        trace += "s";
        return !fail_start;
    }
    void on_stop() override { trace += "p"; }
    void on_teardown() override { trace += "t"; }
};

}  // namespace

// ---------------------------------------------------------------------------
// contract_version / to_string
// ---------------------------------------------------------------------------
TEST(E8_05_Meta, ContractVersion) {
    EXPECT_STREQ(ds::ext::sdk::contract_version(), "e8_05/1.0.0");
}

TEST(E8_05_Meta, StateToString) {
    EXPECT_STREQ(ds::ext::sdk::to_string(ModuleState::Created), "created");
    EXPECT_STREQ(ds::ext::sdk::to_string(ModuleState::Initialized), "initialized");
    EXPECT_STREQ(ds::ext::sdk::to_string(ModuleState::Started), "started");
    EXPECT_STREQ(ds::ext::sdk::to_string(ModuleState::Stopped), "stopped");
    EXPECT_STREQ(ds::ext::sdk::to_string(ModuleState::TornDown), "torn_down");
}

// ---------------------------------------------------------------------------
// ModuleInfo metadata 宣告 → E9-02 manifest
// ---------------------------------------------------------------------------
TEST(E8_05_ModuleInfo, ToManifestCarriesAllFields) {
    ModuleInfo i;
    i.name = "com.example.hello";
    i.version = "1.2.3";
    i.description = "hi";
    i.required_capabilities = {"host.tray", "host.hotkey"};
    i.permissions = {"fs.read"};
    i.format_version = {1, 0};

    ds::package::Manifest m = i.to_manifest();
    EXPECT_EQ(m.name, "com.example.hello");
    EXPECT_EQ(m.version, "1.2.3");
    EXPECT_EQ(m.description, "hi");
    ASSERT_EQ(m.required_capabilities.size(), 2u);
    EXPECT_EQ(m.required_capabilities[0], "host.tray");
    EXPECT_EQ(m.permissions.size(), 1u);
    EXPECT_TRUE(ds::package::is_format_compatible(m.format_version));
}

// ---------------------------------------------------------------------------
// 生命週期狀態機（ModuleBase）
// ---------------------------------------------------------------------------
TEST(E8_05_Lifecycle, HappyPathTransitions) {
    ProbeModule mod;
    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;              // E8-04 註冊面
    ModuleContext ctx(reg, svc);

    EXPECT_EQ(mod.state(), ModuleState::Created);
    EXPECT_TRUE(mod.init(ctx));
    EXPECT_EQ(mod.state(), ModuleState::Initialized);
    EXPECT_TRUE(mod.start());
    EXPECT_EQ(mod.state(), ModuleState::Started);
    mod.stop();
    EXPECT_EQ(mod.state(), ModuleState::Stopped);
    mod.teardown();
    EXPECT_EQ(mod.state(), ModuleState::TornDown);
    EXPECT_EQ(mod.trace, "ispt");
}

TEST(E8_05_Lifecycle, StopRestartFromStopped) {
    ProbeModule mod;
    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    ASSERT_TRUE(mod.init(ctx));
    ASSERT_TRUE(mod.start());
    mod.stop();
    EXPECT_EQ(mod.state(), ModuleState::Stopped);
    EXPECT_TRUE(mod.start());  // Stopped → Started 合法
    EXPECT_EQ(mod.state(), ModuleState::Started);
}

TEST(E8_05_Lifecycle, IllegalTransitionsRejected) {
    ProbeModule mod;
    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);

    // start 前未 init → 拒絕，狀態不變。
    EXPECT_FALSE(mod.start());
    EXPECT_EQ(mod.state(), ModuleState::Created);
    // stop 於非 Started → no-op。
    mod.stop();
    EXPECT_EQ(mod.state(), ModuleState::Created);

    ASSERT_TRUE(mod.init(ctx));
    // 重複 init → 拒絕。
    EXPECT_FALSE(mod.init(ctx));
    EXPECT_EQ(mod.state(), ModuleState::Initialized);
}

TEST(E8_05_Lifecycle, TeardownIdempotentAndSkipsFromCreated) {
    ProbeModule mod;
    mod.teardown();  // 自 Created → no-op（不呼叫鉤子）
    EXPECT_EQ(mod.state(), ModuleState::Created);
    EXPECT_EQ(mod.trace, "");

    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    ASSERT_TRUE(mod.init(ctx));
    mod.teardown();  // Initialized → TornDown（可跳過 start/stop）
    EXPECT_EQ(mod.state(), ModuleState::TornDown);
    mod.teardown();  // 冪等
    EXPECT_EQ(mod.state(), ModuleState::TornDown);
    EXPECT_EQ(mod.trace, "it");
}

TEST(E8_05_Lifecycle, OnInitFailureKeepsCreated) {
    ProbeModule mod;
    mod.fail_init = true;
    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    EXPECT_FALSE(mod.init(ctx));
    EXPECT_EQ(mod.state(), ModuleState::Created);  // 失敗不前進
}

TEST(E8_05_Lifecycle, OnStartFailureKeepsInitialized) {
    ProbeModule mod;
    mod.fail_start = true;
    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    ASSERT_TRUE(mod.init(ctx));
    EXPECT_FALSE(mod.start());
    EXPECT_EQ(mod.state(), ModuleState::Initialized);
}

// ---------------------------------------------------------------------------
// ModuleContext 宿主服務存取（has() 閘控 + 型別安全）
// ---------------------------------------------------------------------------
TEST(E8_05_HostServices, HasGetAndTypedService) {
    FixedClock clock(42);
    HostServiceRegistry svc;
    svc.add("host.clock", &clock);
    EXPECT_EQ(svc.size(), 1u);

    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);

    EXPECT_TRUE(ctx.has_service("host.clock"));
    EXPECT_FALSE(ctx.has_service("host.absent"));
    EXPECT_EQ(ctx.get_service("host.absent"), nullptr);

    IClockService* c = ctx.service<IClockService>("host.clock");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->now(), 42);
}

TEST(E8_05_HostServices, TypedServiceWrongTypeReturnsNull) {
    // 登記一個非 IClockService 的服務，型別安全取用回 nullptr。
    struct OtherService : public IHostService {};
    OtherService other;
    HostServiceRegistry svc;
    svc.add("host.other", &other);

    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    EXPECT_TRUE(ctx.has_service("host.other"));
    EXPECT_EQ(ctx.service<IClockService>("host.other"), nullptr);  // dynamic_cast 失敗
}

TEST(E8_05_HostServices, RegistryIgnoresEmptyNameAndNull) {
    FixedClock clock(1);
    HostServiceRegistry svc;
    svc.add("", &clock).add("host.clock", nullptr);
    EXPECT_EQ(svc.size(), 0u);
    EXPECT_FALSE(svc.has("host.clock"));
}

// ---------------------------------------------------------------------------
// 能力登記委派（ModuleContext → E8-04 註冊面）
// ---------------------------------------------------------------------------
TEST(E8_05_Capabilities, ProvideDelegatesToRegistration) {
    HostServiceRegistry svc;
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    EXPECT_TRUE(ctx.provide_sensor("cpu.usage"));
    EXPECT_TRUE(ctx.provide_component("bar.widget"));
    EXPECT_TRUE(ctx.provide_action("volume.set"));
    EXPECT_FALSE(ctx.provide_sensor("cpu.usage"));  // 模組內重複，E8-04 擋
    EXPECT_EQ(ctx.provided_count(), 3u);
    EXPECT_EQ(reg.size(), 3u);  // 委派確實落到 E8-04 註冊面
}

// ---------------------------------------------------------------------------
// SampleSensorModule 契約可用性（單機生命週期）
// ---------------------------------------------------------------------------
TEST(E8_05_Sample, ImplementsContractWithHostService) {
    SampleSensorModule mod;
    FixedClock clock(7);
    HostServiceRegistry svc;
    svc.add(SampleSensorModule::kClockService, &clock);

    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);

    ASSERT_TRUE(mod.init(ctx));
    EXPECT_TRUE(mod.clock_bound());
    EXPECT_EQ(reg.size(), 1u);  // 登記了 sample.tick 感測器
    ASSERT_TRUE(mod.start());
    EXPECT_EQ(mod.last_reading(), 7);  // 讀取了宿主時脈
    mod.stop();
    EXPECT_EQ(mod.last_reading(), 0);
    mod.teardown();
    EXPECT_FALSE(mod.clock_bound());  // teardown 釋放借用指標
}

TEST(E8_05_Sample, InitFailsWhenHostServiceMissing) {
    SampleSensorModule mod;
    HostServiceRegistry svc;  // 未登記 host.clock
    ds::ext::ModuleContext reg;
    ModuleContext ctx(reg, svc);
    EXPECT_FALSE(mod.init(ctx));  // has() 閘控失敗 → 明確拒絕
    EXPECT_EQ(mod.state(), ModuleState::Created);
    EXPECT_FALSE(mod.clock_bound());
}

// ---------------------------------------------------------------------------
// make_module + E8-04 ModuleLoader 整合（完整載入 / 卸載）
// ---------------------------------------------------------------------------
TEST(E8_05_Integration, LoadDrivesInitAndStart) {
    SampleSensorModule mod;
    FixedClock clock(99);
    HostServiceRegistry services;
    services.add(SampleSensorModule::kClockService, &clock);

    // host 能力閘控：SampleSensorModule.info().requires = {"host.time"}。
    HostEnvironment host;
    host.provide_capability("host.time");
    ModuleLoader loader(host);

    Module m = make_module(mod, services);
    auto r = loader.load(m);
    ASSERT_TRUE(r.ok()) << r.message;

    // 載入器已驅動 init → start：狀態為 Started、宿主時脈已讀。
    EXPECT_EQ(mod.state(), ModuleState::Started);
    EXPECT_EQ(mod.last_reading(), 99);
    // E8-04 能力表登記了範例模組提供的感測器。
    EXPECT_TRUE(loader.provides(ds::ext::CapabilityKind::Sensor, SampleSensorModule::kSensorId));
    EXPECT_EQ(loader.provider_of(ds::ext::CapabilityKind::Sensor, SampleSensorModule::kSensorId),
              std::string(SampleSensorModule::kName));

    // 卸載 → on_unload 驅動 stop → teardown。
    EXPECT_TRUE(loader.unload(SampleSensorModule::kName));
    EXPECT_EQ(mod.state(), ModuleState::TornDown);
    EXPECT_FALSE(loader.provides(ds::ext::CapabilityKind::Sensor, SampleSensorModule::kSensorId));
}

TEST(E8_05_Integration, LoadRejectedWhenHostCapabilityMissing) {
    SampleSensorModule mod;
    FixedClock clock(1);
    HostServiceRegistry services;
    services.add(SampleSensorModule::kClockService, &clock);

    HostEnvironment host;  // 未提供 host.time
    ModuleLoader loader(host);

    Module m = make_module(mod, services);
    auto r = loader.load(m);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::MissingRequirement);  // E8-04 閘控攔下
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "host.time");
    // 閘控在 on_register 前，故模組從未 init。
    EXPECT_EQ(mod.state(), ModuleState::Created);
}

TEST(E8_05_Integration, RegistrationFailsWhenServiceMissing) {
    SampleSensorModule mod;
    HostServiceRegistry services;  // 未登記 host.clock → on_init 回 false
    HostEnvironment host;
    host.provide_capability("host.time");
    ModuleLoader loader(host);

    Module m = make_module(mod, services);
    auto r = loader.load(m);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, LoadStatus::RegistrationFailed);  // init 回 false 冒泡為 RegistrationFailed
    EXPECT_FALSE(loader.is_loaded(SampleSensorModule::kName));  // 不留痕
}
