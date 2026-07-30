// E8-04 行程內模組載入 — 平台中立契約（擴充點 5「腳本 / 模組」的載入機制）。
//
// 平台的第五個擴充點是「腳本 / 模組」：第三方以模組形式掛上自己的能力
// （感測器 / 元件 / 動作），不需修改核心。E8-04 是這個擴充點的**載入機制**——
// 把一個由 E9-02 manifest 描述的模組載入行程，並註冊其提供的能力。
//
// **相位 1：行程內註冊模型，不用 dlopen / 動態庫。**
//   模組是「已連結進本行程的 C++ 物件」，透過註冊介面（`Module` 的 on_register）
//   把自己提供的能力掛上。第二相位再引入動態庫載入時，本契約（validate → register →
//   track → unload）不變，只換「取得 Module 的方式」。因此本單元純邏輯、平台中立：
//   無 `#ifdef`、無系統呼叫、無真實後端 —— 換平台一行不動。
//
// 載入流程（ModuleLoader::load）：
//   1. 驗證 manifest：name 非空、format_version 相容（沿用 E9-02 is_format_compatible）。
//   2. 重複載入偵測：同名模組已載入 → 拒絕（AlreadyLoaded），不覆蓋。
//   3. requires 閘控：manifest.required_capabilities 每一項都須由 host 提供，
//      **任一不滿足即明確拒絕（MissingRequirement，附缺項清單）——絕不靜默載入。**
//   4. permissions 閘控：manifest.permissions 每一項都須由 host 授予，
//      任一未授予即拒絕（MissingPermission，附缺項清單）。
//   5. 呼叫模組註冊入口 on_register(ctx)：模組把提供的能力登記進 ctx。
//      回 false 或拋例外 → RegistrationFailed（不提交、不留痕）。
//   6. 提交：把該模組登記的能力併入共享能力表；若與其他模組既有能力 (kind,id) 衝突
//      → RegistrationFailed 並整批回滾（不部分提交）。成功則追蹤為已載入。
//
// 卸載（unload）：呼叫模組 on_unload、自共享能力表移除該模組登記的全部能力、
//   自已載入集合移除。可重複載入 / 卸載。
#ifndef DS_EXT_E8_04_MODULE_LOADER_HPP
#define DS_EXT_E8_04_MODULE_LOADER_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "manifest.hpp"  // E9-02（PUBLIC 相依）：Manifest / FormatVersion / is_format_compatible

namespace ds::ext {

// 擴充點契約版本標記。載入機制承重（第三方模組靠它掛上），版本欄位讓消費者可在
// 演進（如第二相位引入 dlopen）時做相容性判斷。定義在 .cpp。
const char* contract_version() noexcept;

// ---------------------------------------------------------------------------
// CapabilityKind — 模組可提供的能力種類，對應平台前三個擴充點。
//   Sensor    = 擴充點 1「指標」（感測器）
//   Component = 擴充點 2「元件」（視覺元件）
//   Action    = 擴充點 3「動作」（致動器 / 命令）
// ---------------------------------------------------------------------------
enum class CapabilityKind { Sensor, Component, Action };

const char* to_string(CapabilityKind kind) noexcept;

// 一項能力登記：種類 + 穩定字串 id（如 "cpu.usage"、"bar.widget"、"volume.set"）。
struct Capability {
    CapabilityKind kind;
    std::string id;

    bool operator==(const Capability& o) const noexcept {
        return kind == o.kind && id == o.id;
    }
    bool operator<(const Capability& o) const noexcept {
        if (kind != o.kind) return kind < o.kind;
        return id < o.id;
    }
};

// ---------------------------------------------------------------------------
// HostEnvironment — host 對模組閘控時所依據的事實。
//
//   - capabilities：host 能滿足的能力 id（對接 manifest 的 `requires`）。
//   - permissions：host 授予的權限 id（對接 manifest 的 `permissions`）。
//
// 由呼叫端在載入前組好；ModuleLoader 只讀不改。空集合 = 什麼都不提供 / 不授予。
// ---------------------------------------------------------------------------
class HostEnvironment {
public:
    HostEnvironment() = default;

    // 宣告 host 提供某能力（滿足模組 requires 的一項）。回傳 *this 以便鏈式呼叫。
    HostEnvironment& provide_capability(std::string id) {
        capabilities_.insert(std::move(id));
        return *this;
    }
    // 宣告 host 授予某權限（滿足模組 permissions 的一項）。
    HostEnvironment& grant_permission(std::string id) {
        permissions_.insert(std::move(id));
        return *this;
    }

    bool has_capability(const std::string& id) const {
        return capabilities_.find(id) != capabilities_.end();
    }
    bool grants_permission(const std::string& id) const {
        return permissions_.find(id) != permissions_.end();
    }

private:
    std::set<std::string> capabilities_;
    std::set<std::string> permissions_;
};

// ---------------------------------------------------------------------------
// ModuleContext — 傳給模組註冊入口的登記面。
//
// 模組在 on_register 內以 provide_sensor/provide_component/provide_action 登記自己
// 提供的能力。此處只收集到「模組本地清單」並擋掉**模組內重複**；跨模組衝突由
// ModuleLoader 在提交階段判定（兩階段：先收集、驗無跨模組衝突才整批提交）。
// ---------------------------------------------------------------------------
class ModuleContext {
public:
    // 登記一項提供的能力。id 非空、且此模組尚未登記同 (kind,id) 時成功回 true；
    // 否則不變更並回 false（空 id / 模組內重複皆拒）。
    bool provide(CapabilityKind kind, std::string id) {
        if (id.empty()) return false;
        Capability cap{kind, std::move(id)};
        for (const auto& c : provided_) {
            if (c == cap) return false;  // 模組內重複
        }
        provided_.push_back(std::move(cap));
        return true;
    }
    bool provide_sensor(std::string id) { return provide(CapabilityKind::Sensor, std::move(id)); }
    bool provide_component(std::string id) {
        return provide(CapabilityKind::Component, std::move(id));
    }
    bool provide_action(std::string id) { return provide(CapabilityKind::Action, std::move(id)); }

    const std::vector<Capability>& provided() const noexcept { return provided_; }
    std::size_t size() const noexcept { return provided_.size(); }

private:
    std::vector<Capability> provided_;
};

// 模組註冊入口：把提供的能力登記進 ctx。回 false = 模組自行拒絕註冊（載入失敗）。
using ModuleRegister = std::function<bool(ModuleContext&)>;

// ---------------------------------------------------------------------------
// Module — 呈交給載入器的行程內模組（相位 1：已連結的 C++ 物件，無 dlopen）。
//
//   - manifest：E9-02 描述（name / format_version / requires / permissions…）。
//   - on_register：註冊入口（必填）。
//   - on_unload：卸載時的清理鉤子（選填；預設無動作）。
// 以值型別（含 std::function）承載，讓「假模組」在測試中極易建構。
// ---------------------------------------------------------------------------
struct Module {
    ds::package::Manifest manifest;
    ModuleRegister on_register;
    std::function<void()> on_unload;
};

// ---------------------------------------------------------------------------
// LoadStatus / LoadResult — 載入結果。拒絕一律帶明確原因（不得靜默失敗）。
// ---------------------------------------------------------------------------
enum class LoadStatus {
    Ok,                  // 載入並註冊成功
    InvalidManifest,     // manifest 無效（name 空）
    IncompatibleFormat,  // manifest.format_version 不相容（沿用 E9-02 判定）
    AlreadyLoaded,       // 同名模組已載入（不覆蓋）
    MissingRequirement,  // 有 requires 未被 host 滿足（missing 附缺項）
    MissingPermission,   // 有 permissions 未被 host 授予（missing 附缺項）
    RegistrationFailed,  // on_register 回 false / 拋例外，或提交時跨模組能力衝突
};

const char* to_string(LoadStatus status) noexcept;

struct LoadResult {
    LoadStatus status = LoadStatus::Ok;
    std::string message;                // 人類可讀原因
    std::vector<std::string> missing;   // 缺少的 requires / permissions（對應 Missing* 狀態）

    bool ok() const noexcept { return status == LoadStatus::Ok; }
    explicit operator bool() const noexcept { return ok(); }
};

// ---------------------------------------------------------------------------
// ModuleLoader — 行程內模組載入器：驗證 → 註冊 → 追蹤 → 卸載。
//
// 持有一份跨模組共享的能力表（(kind,id) → 提供它的模組名），據此偵測跨模組能力
// 衝突並支援卸載時精準移除。ModuleLoader 只讀 host 環境、不改之。
// ---------------------------------------------------------------------------
class ModuleLoader {
public:
    // host：閘控依據（requires / permissions）。以 const 參考持有，載入器不改 host。
    explicit ModuleLoader(const HostEnvironment& host) : host_(host) {}

    // 載入一個模組。流程見檔首。任一步失敗回帶原因的 LoadResult，且不留任何痕跡
    //（不部分提交、不追蹤）。成功回 {Ok}。
    LoadResult load(const Module& module);

    // 卸載具名模組：呼叫其 on_unload、移除其登記的全部能力、自已載入集合移除。
    // 回傳是否確有卸載（未載入的名稱回 false）。
    bool unload(const std::string& name);

    // 是否已載入具名模組。
    bool is_loaded(const std::string& name) const {
        return loaded_.find(name) != loaded_.end();
    }

    // 目前已載入模組數。
    std::size_t loaded_count() const noexcept { return loaded_.size(); }

    // 列舉已載入模組名（有序）。供內省 / 診斷。
    std::vector<std::string> loaded_names() const {
        std::vector<std::string> names;
        names.reserve(loaded_.size());
        for (const auto& kv : loaded_) names.push_back(kv.first);
        return names;  // std::map 已排序
    }

    // 能力表查詢：是否有任一已載入模組提供該 (kind,id)。
    bool provides(CapabilityKind kind, const std::string& id) const {
        return capabilities_.find(Capability{kind, id}) != capabilities_.end();
    }
    // 提供該能力的模組名；無則回空字串。
    std::string provider_of(CapabilityKind kind, const std::string& id) const {
        auto it = capabilities_.find(Capability{kind, id});
        return it == capabilities_.end() ? std::string{} : it->second;
    }

    // 目前已登記能力總數（跨所有已載入模組）。
    std::size_t capability_count() const noexcept { return capabilities_.size(); }

private:
    // 每個已載入模組的追蹤紀錄：它登記的能力清單（供卸載時精準移除）+ 卸載鉤子。
    struct LoadedModule {
        std::vector<Capability> capabilities;
        std::function<void()> on_unload;
    };

    const HostEnvironment& host_;
    // 已載入模組：name → 追蹤紀錄。
    std::map<std::string, LoadedModule> loaded_;
    // 共享能力表：Capability → 提供它的模組名。用於跨模組衝突偵測與內省。
    std::map<Capability, std::string> capabilities_;
};

}  // namespace ds::ext

#endif  // DS_EXT_E8_04_MODULE_LOADER_HPP
