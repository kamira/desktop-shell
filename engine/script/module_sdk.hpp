// E8-05 外部模組開發介面（SDK）— 平台中立契約（擴充點 5「腳本 / 模組」的開發面）。
//
// E8-04 定義「載入機制」（validate → register → track → unload）。本單元（E8-05）
// 定義**外部模組開發者面對的穩定 API/SDK**——「載入後模組長什麼樣、如何與宿主互動」：
//
//   1. `IModule`：第三方模組要實作的抽象介面，含明確**生命週期**
//      （init → start → stop → teardown）與 metadata 宣告（`info()`）。
//   2. `ModuleInfo`：模組自行宣告的 metadata / 能力需求（name / version / requires /
//      permissions / format_version），可轉為 E9-02 `ds::package::Manifest` 交給 E8-04 載入。
//   3. `ModuleContext`（SDK 面）：模組在生命週期中存取**宿主提供的服務**（`HostServiceRegistry`）
//      並登記自己提供的能力（委派 E8-04 的註冊面）。宿主服務存取一律以 `has()` 閘控（NFR-03）。
//   4. `make_module`：把一個 `IModule` 轉接成 E8-04 的 `ds::ext::Module`，讓現成的
//      `ds::ext::ModuleLoader` 直接驅動其生命週期——這是 SDK 與 E8-04 載入機制的整合點。
//
// **相位 1：平台中立、純邏輯。** 無 `#ifdef`、無系統呼叫、無 dlopen / 真實動態載入。
// 第二相位引入動態庫時，本開發介面（IModule 契約）不變，只換「取得 IModule 的方式」。
//
// 命名空間 `ds::ext::sdk`：與 E8-04（`ds::ext`）一致的前綴，並以巢狀 `sdk` 子命名空間
// 與 E8-04 既有的 `ds::ext::ModuleContext`（能力註冊面）區隔，避免符號衝突。
#ifndef DS_EXT_E8_05_MODULE_SDK_HPP
#define DS_EXT_E8_05_MODULE_SDK_HPP

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "manifest.hpp"        // E9-02（透過 e8_04 PUBLIC 相依取得）：Manifest / FormatVersion
#include "module_loader.hpp"   // E8-04（PUBLIC 相依）：Module / ModuleContext（註冊面）/ ModuleLoader

namespace ds::ext::sdk {

// 擴充點開發介面（SDK）契約版本標記。第三方模組靠此 API 開發；版本欄位讓消費者可在
// 演進（如相位 2 引入 dlopen 或擴充生命週期）時做相容性判斷。定義在 .cpp。
const char* contract_version() noexcept;

// ---------------------------------------------------------------------------
// ModuleState — 模組生命週期狀態機。
//
//   Created ──init──▶ Initialized ──start──▶ Started ──stop──▶ Stopped
//                          │                    ▲                 │
//                          │                    └──────start──────┘
//                          └──teardown──┐   ┌────teardown────┐   │
//                                       ▼   ▼                ▼   ▼
//                                          TornDown（終態，不可再轉移）
//
// 合法轉移：init 僅自 Created；start 自 Initialized 或 Stopped；stop 僅自 Started；
// teardown 自 Initialized / Started / Stopped（Created / TornDown 為 no-op）。
// ---------------------------------------------------------------------------
enum class ModuleState { Created, Initialized, Started, Stopped, TornDown };

const char* to_string(ModuleState state) noexcept;

// ---------------------------------------------------------------------------
// ModuleInfo — 模組自行宣告的 metadata 與能力需求（開發者填寫）。
//
// 對接 E9-02 manifest 的欄位；`to_manifest()` 產出等價 `ds::package::Manifest`，
// 交由 E8-04 `ModuleLoader` 做 requires / permissions / format 閘控與載入。
// ---------------------------------------------------------------------------
struct ModuleInfo {
    std::string name;                                 // 必填。模組穩定識別（如 "com.example.hello"）。
    std::string version;                              // 選填。模組自身版本字串。
    std::string description;                          // 選填。人類可讀說明。
    std::vector<std::string> required_capabilities;   // 所需宿主能力 id（對接 manifest `requires`）。
    std::vector<std::string> permissions;             // 所需權限 id（對接 manifest `permissions`）。
    ds::package::FormatVersion format_version{1, 0};  // 宣告的格式版本（預設當前支援版）。

    // 轉為 E9-02 manifest，供 E8-04 載入器消費。
    ds::package::Manifest to_manifest() const {
        ds::package::Manifest m;
        m.format_version = format_version;
        m.name = name;
        m.version = version;
        m.description = description;
        m.required_capabilities = required_capabilities;
        m.permissions = permissions;
        return m;
    }
};

// ---------------------------------------------------------------------------
// IHostService — 宿主提供給模組的服務之基底型別。
//
// 宿主以具體服務（實作某 IHostService 衍生介面，如 IClockService）登記進
// HostServiceRegistry；模組在生命週期中透過 ModuleContext 依名取得並向下轉型使用。
// 相位 1 為行程內物件指標，宿主保有所有權（模組只借用，不持有 / 不釋放）。
// ---------------------------------------------------------------------------
class IHostService {
public:
    virtual ~IHostService() = default;
};

// ---------------------------------------------------------------------------
// HostServiceRegistry — 宿主服務登記處。宿主在載入前登記服務；模組透過 ModuleContext 讀取。
//
// 只存指標、不擁有服務物件（生命週期由宿主保證涵蓋模組存活期）。空名 / 空指標略過。
// ---------------------------------------------------------------------------
class HostServiceRegistry {
public:
    HostServiceRegistry() = default;

    // 登記一個具名宿主服務。回傳 *this 以便鏈式呼叫。空名 / nullptr 靜默略過（不覆蓋既有）。
    HostServiceRegistry& add(std::string name, IHostService* service) {
        if (!name.empty() && service != nullptr) {
            services_.emplace(std::move(name), service);
        }
        return *this;
    }

    bool has(const std::string& name) const { return services_.find(name) != services_.end(); }

    // 依名取得服務基底指標；不存在回 nullptr（呼叫端應以 has() 或 nullptr 檢查閘控——NFR-03）。
    IHostService* get(const std::string& name) const {
        auto it = services_.find(name);
        return it == services_.end() ? nullptr : it->second;
    }

    std::size_t size() const noexcept { return services_.size(); }

private:
    std::map<std::string, IHostService*> services_;
};

// ---------------------------------------------------------------------------
// ModuleContext（SDK 面）— 傳給模組生命週期的宿主互動面。
//
// 聚合兩件事：
//   (a) 存取宿主提供的服務（HostServiceRegistry，唯讀）；一律以 has_service() 閘控（NFR-03）。
//   (b) 登記本模組提供的能力（委派 E8-04 `ds::ext::ModuleContext` 註冊面）。
//
// 由 make_module 產生的轉接層在 on_register 內建構並交給 IModule::init。
// 注意：與 E8-04 的 `ds::ext::ModuleContext`（純能力註冊面）不同名層級——此為 SDK 對外面。
// ---------------------------------------------------------------------------
class ModuleContext {
public:
    ModuleContext(ds::ext::ModuleContext& registration, const HostServiceRegistry& services)
        : registration_(registration), services_(services) {}

    // -- (a) 宿主服務存取 -----------------------------------------------------
    // 是否有具名服務（存取前的閘控查詢）。
    bool has_service(const std::string& name) const { return services_.has(name); }
    // 取得具名宿主服務基底指標；不存在回 nullptr。
    IHostService* get_service(const std::string& name) const { return services_.get(name); }
    // 型別安全取得：依名取得並向下轉型為 T*；不存在 / 型別不符回 nullptr。
    template <class T>
    T* service(const std::string& name) const {
        return dynamic_cast<T*>(services_.get(name));
    }

    // -- (b) 能力登記（委派 E8-04 註冊面）------------------------------------
    bool provide_sensor(std::string id) { return registration_.provide_sensor(std::move(id)); }
    bool provide_component(std::string id) {
        return registration_.provide_component(std::move(id));
    }
    bool provide_action(std::string id) { return registration_.provide_action(std::move(id)); }
    // 已登記能力數（此模組本地）。
    std::size_t provided_count() const noexcept { return registration_.size(); }

private:
    ds::ext::ModuleContext& registration_;
    const HostServiceRegistry& services_;
};

// ---------------------------------------------------------------------------
// IModule — 第三方模組要實作的穩定抽象介面（SDK 契約核心）。
//
// 開發者實作此介面即可被宿主載入 / 驅動，無需碰核心。生命週期由宿主（經 make_module +
// E8-04 載入器）呼叫，模組不自行驅動狀態。
//
// 生命週期語意：
//   info()       宣告 metadata / 能力需求（可於任何時點被查詢，須無副作用）。
//   init(ctx)    取得宿主服務、登記提供的能力。回 false = 模組拒絕初始化（載入失敗）。
//   start()      進入運行態（開始主動運作）。回 false = 啟動失敗。
//   stop()       停止運行（可再 start()；相位 1 為可逆暫停）。
//   teardown()   最終清理，卸載前呼叫（之後不再被使用）。
//   state()      目前生命週期狀態（內省 / 契約驗證）。
// ---------------------------------------------------------------------------
class IModule {
public:
    virtual ~IModule() = default;

    virtual ModuleInfo info() const = 0;
    virtual bool init(ModuleContext& ctx) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void teardown() = 0;
    virtual ModuleState state() const = 0;
};

// ---------------------------------------------------------------------------
// ModuleBase — 選用的便利基底：實作生命週期狀態機與合法轉移防護，開發者只覆寫 on_* 鉤子。
//
// 公開的 init/start/stop/teardown 為 final：先驗狀態合法性（非法轉移直接拒絕 / no-op，
// 不呼叫鉤子），再委派受保護的 on_*，成功才推進狀態。這把「錯誤處理 / 狀態一致性」
// 集中於 SDK，第三方模組只需專注業務鉤子。info() 仍為純虛擬——metadata 必須自填。
// ---------------------------------------------------------------------------
class ModuleBase : public IModule {
public:
    ModuleState state() const final { return state_; }

    // init 僅自 Created 合法；on_init 回 false 視為模組拒絕（狀態不前進）。
    bool init(ModuleContext& ctx) final {
        if (state_ != ModuleState::Created) return false;  // 非法轉移
        if (!on_init(ctx)) return false;                   // 模組側失敗，不前進
        state_ = ModuleState::Initialized;
        return true;
    }

    // start 自 Initialized 或 Stopped 合法；on_start 回 false 視為啟動失敗。
    bool start() final {
        if (state_ != ModuleState::Initialized && state_ != ModuleState::Stopped) return false;
        if (!on_start()) return false;
        state_ = ModuleState::Started;
        return true;
    }

    // stop 僅自 Started 有效；其餘狀態為安全 no-op（不呼叫鉤子）。
    void stop() final {
        if (state_ != ModuleState::Started) return;
        on_stop();
        state_ = ModuleState::Stopped;
    }

    // teardown 自 Initialized / Started / Stopped 有效；Created / TornDown 為 no-op（冪等）。
    void teardown() final {
        if (state_ == ModuleState::Created || state_ == ModuleState::TornDown) return;
        on_teardown();
        state_ = ModuleState::TornDown;
    }

protected:
    // 業務鉤子；預設無動作 / 成功。開發者按需覆寫。
    virtual bool on_init(ModuleContext& /*ctx*/) { return true; }
    virtual bool on_start() { return true; }
    virtual void on_stop() {}
    virtual void on_teardown() {}

private:
    ModuleState state_ = ModuleState::Created;
};

// ---------------------------------------------------------------------------
// make_module — SDK ↔ E8-04 整合點。
//
// 把一個 IModule（借用參考，宿主保有所有權）轉接成 E8-04 的 `ds::ext::Module`：
//   - manifest   = module.info().to_manifest()
//   - on_register= 建構 SDK ModuleContext（含 services）→ init(ctx) → start()；任一失敗回 false
//                  （E8-04 載入器據此回 RegistrationFailed，不留痕）。
//   - on_unload  = stop() → teardown()（載入器 unload 時呼叫）。
//
// 之後即可 `ds::ext::ModuleLoader::load(make_module(mod, services))` 走完整載入流程。
// module 與 services 須存活至對應 ModuleLoader 載入 / 卸載完成（回傳的 Module 借用之）。
// ---------------------------------------------------------------------------
ds::ext::Module make_module(IModule& module, const HostServiceRegistry& services);

}  // namespace ds::ext::sdk

#endif  // DS_EXT_E8_05_MODULE_SDK_HPP
