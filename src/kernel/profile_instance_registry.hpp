// E1-13 多 profile 實例並存 — 平台中立介面（相位 1 = null）
//
// 同一份**具名 profile 定義**（如 "widget.clock"）可被**同時實例化多份**（同一 widget 開多
// 個視窗）。每一份實例：
//   - 有自己的**具名實例 id**（`InstanceId`），彼此互不相同、互不覆寫。
//   - 持有**獨立**的一份 `SurfaceProfile`（複製自定義，各自演化，互不影響 —— 隔離保證）。
//   - 建立時經上游 E1-01 `LayerStack` 指派到定義所述的**具名圖層**（`SurfaceLayer`）。
//   - 可各別查詢（`get`）、列舉（`list`）、銷毀（`destroy`），互不牽連其餘實例。
//
// 上游基座（已合併，可讀不可改）：
//   - E1-01（`ds::kernel::LayerStack`）：具名圖層指派 + z-order 維持。本單元組合一個
//     `LayerStack` 實例，每份 profile 實例建立 / 銷毀時同步指派 / 移除其圖層歸屬。
//     （E1-01 進一步建於 E1-21 能力矩陣與 E1-24 `SurfaceProfile` / `SurfaceLayer` 之上。）
//
// 硬約束（NFR-02）：對外**不出現數字索引 / 數字 handle**。實例一律以具名 `InstanceId`
//   （`std::string`）指涉與列舉；本單元內部生成新 id 時雖借助一個遞增序號，但該序號**不**
//   對外暴露為數字型別 —— 只用來拼出一個仍屬「具名字串」的 `<definition_id>#<n>` id
//   （人類可讀、可回溯其所屬定義），呼叫端永遠只看見字串。
// 硬約束（NFR-03）：改動狀態的操作（`instantiate` / `destroy`）透過組合的 `LayerStack` 之
//   `has(kernel.surface)` 閘控 —— 能力不可用時結構化拒絕、不改任何狀態、絕不崩潰。
//
// 相位 1（Mac / null 期）：純邏輯 + 記憶體維護，無 `#ifdef` / win32 / cocoa 平台分支，
//   不觸碰任何真實 OS 視窗 / 合成器 API。
#ifndef DS_KERNEL_E1_13_PROFILE_INSTANCE_REGISTRY_HPP
#define DS_KERNEL_E1_13_PROFILE_INSTANCE_REGISTRY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "layer_stack.hpp"  // E1-01（上游，可讀不可改）：LayerStack / has() / SurfaceLayer 等

namespace ds::kernel {

// 具名 profile 定義識別碼（如 "widget.clock" / "widget.timer"）—— 同一定義可被多次實例化。
using ProfileDefinitionId = std::string;

// 具名實例識別碼（本 registry 生成，見上方 NFR-02 說明）—— 唯一指涉單一份實例，永不重用。
using InstanceId = std::string;

// 一份 profile 定義：具名定義 id + 上游 E1-24 四參數 `SurfaceProfile`。
// 純資料。`instantiate` 以此為模板複製出彼此獨立的實例狀態。
struct ProfileDefinition {
    ProfileDefinitionId id;
    SurfaceProfile surface;
};

// `instantiate` 的結果狀態 —— 讓「成功 / 各類拒絕」在呼叫端可分辨（NFR-03 可觀測、不靜默）。
enum class InstantiateStatus {
    Ok,                     // 成功建立一份新實例
    RejectedEmptyDefinition,  // definition.id 為空：保守拒絕
    RejectedNoCapability,   // has(kernel.surface) == false：NFR-03 閘控拒絕，狀態不變
    RejectedInstanceLimit,  // 已達 max_instances 上限：保守拒絕，狀態不變
};

// 具名字串（NFR-02：診斷 / 記錄一律具名，不用數字）。未知回 "unknown"。
const char* to_string(InstantiateStatus s) noexcept;

// `instantiate` 的完整結果：狀態 + （成功時）新指派之具名實例 id。
// 失敗時 `id` 恆為空字串（與「有效 id 恆非空」互斥，呼叫端可據此判斷，但仍應優先檢查 status）。
struct InstantiateOutcome {
    InstantiateStatus status = InstantiateStatus::RejectedEmptyDefinition;
    InstanceId id;
};

// 單一份存活中的 profile 實例 —— 每份彼此獨立持有的狀態（隔離保證的具體體現）。
struct ProfileInstance {
    InstanceId id;
    ProfileDefinitionId definition_id;  // 來自哪一份具名定義（同一定義可對應多個實例）
    SurfaceProfile surface;             // 該實例獨立持有的四參數 profile（複製，互不影響）
    bool visible = true;                // 該實例獨立的可見狀態（show/hide 只影響自己）
};

// ---------------------------------------------------------------------------
// ProfileInstanceRegistry —— 多 profile 實例並存管理器（相位 1 記憶體實作）。
//
// 組合（非繼承）一個 E1-01 `LayerStack`：每份實例建立時指派到具名圖層，銷毀時移除指派；
// 能力閘控（NFR-03）單一資料來源即該 `LayerStack` 之能力矩陣，registry 本身的 `has()` /
// `capabilities()` 為透傳，避免兩份矩陣互相漂移。
// ---------------------------------------------------------------------------
class ProfileInstanceRegistry {
public:
    // 預設不設實際上限（`kUnlimited`）；亦可注入較小上限（供測試 NFR-03 以外的「額滿拒絕」
    // 路徑）。能力矩陣預設為 E1-21 內嵌預設（保守），亦可注入（供測試 NFR-03 兩條路徑）。
    static constexpr std::size_t kUnlimited = static_cast<std::size_t>(-1);

    explicit ProfileInstanceRegistry(CapabilityMatrix caps = CapabilityMatrix::defaults(),
                                      std::size_t max_instances = kUnlimited);

    // --- 能力查詢（NFR-03，透傳組合之 LayerStack）---
    const CapabilityMatrix& capabilities() const noexcept { return layer_stack_.capabilities(); }
    bool has(const CapabilityId& id) const { return layer_stack_.has(id); }

    // --- 實例化（改動狀態 → 經 LayerStack.assign 之 has() 閘控）---
    // 由 `definition` 建立一份**新的、獨立的**實例：
    //   - definition.id 為空 → RejectedEmptyDefinition（不改狀態）。
    //   - 已達 max_instances 上限 → RejectedInstanceLimit（不改狀態）。
    //   - has(kernel.surface) == false → RejectedNoCapability（不改狀態，含未指派圖層）。
    //   - 成功：生成全新具名 `InstanceId`、複製一份獨立 `SurfaceProfile`、經 E1-01 指派到
    //     `definition.surface.layer` 所屬具名圖層、預設可見（visible = true）。回 Ok + 新 id。
    // 同一 definition 可重複呼叫任意次，每次都是一份彼此獨立的新實例（NFR 語意核心）。
    InstantiateOutcome instantiate(const ProfileDefinition& definition);

    // --- 查詢（唯讀，不需閘控）---
    // 查詢單一存活實例；未知 / 已銷毀 id 回 nullptr（保守）。指標於該實例存活期間有效。
    const ProfileInstance* get(const InstanceId& id) const;
    // 該具名實例目前是否存活。
    bool contains(const InstanceId& id) const;
    // 全部存活實例的具名 id（建立序，穩定）—— 對外列舉入口，永不暴露數字 index。
    std::vector<InstanceId> list() const;
    // 目前存活實例總數。
    std::size_t size() const noexcept { return instances_.size(); }
    // 是否無任何存活實例。
    bool empty() const noexcept { return instances_.empty(); }
    // 目前設定的實例上限（`kUnlimited` = 無實際上限）。
    std::size_t max_instances() const noexcept { return max_instances_; }
    // 某具名 definition 目前存活的實例數（同一定義可有 0..N 份，各自獨立）。
    std::size_t count_of_definition(const ProfileDefinitionId& definition_id) const;
    // 該具名實例經 E1-01 指派之具名圖層；未知 / 已銷毀 id 回 nullptr（保守，透傳 LayerStack）。
    const SurfaceLayer* layer_of(const InstanceId& id) const;

    // --- 銷毀（改動狀態 → 經 LayerStack.remove 之 has() 閘控）---
    // 銷毀單一具名實例：先自組合的 LayerStack 移除其圖層指派，成功才移除本地記錄。
    //   - 未知 id / 已銷毀（重複銷毀） → 回 false（**明確不靜默**，狀態不變）。
    //   - has(kernel.surface) == false → 回 false（NFR-03 閘控拒絕，狀態不變）。
    //   - 成功回 true；只影響該筆實例，其餘存活實例完全不受影響（隔離保證）。
    bool destroy(const InstanceId& id);

    // --- 個別可見狀態（純本地狀態，不涉及能力閘控；示範「各實例獨立狀態」）---
    // 顯示 / 隱藏單一具名實例；未知 id 回 false。只影響該筆實例。
    bool show(const InstanceId& id);
    bool hide(const InstanceId& id);
    // 該具名實例目前是否可見；未知 / 已銷毀 id 回 false（保守）。
    bool is_visible(const InstanceId& id) const;

private:
    ProfileInstance* find(const InstanceId& id);
    const ProfileInstance* find(const InstanceId& id) const;
    // 由 definition_id 生成一個全新、保證目前未被使用的具名實例 id（見檔頭 NFR-02 說明）。
    InstanceId make_instance_id(const ProfileDefinitionId& definition_id);

    LayerStack layer_stack_;                // E1-01（組合）：具名圖層指派 + 能力矩陣單一資料來源
    std::vector<ProfileInstance> instances_;  // 建立序（穩定）；永不以數字 index 對外暴露
    std::size_t max_instances_;
    std::size_t next_seq_ = 0;              // 內部生成 id 用；純內部，不對外暴露為數字型別
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_13_PROFILE_INSTANCE_REGISTRY_HPP
