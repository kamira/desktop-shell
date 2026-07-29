// E4-08 圖層合成 — engine 層 / render 子系統
//
// 語意：**多圖層合成**——依**具名圖層順序**，把多個（已透過 E4-06 定址的）具名 surface
// 合成為最終畫面的**合成計畫描述**：每層混合模式(normal/multiply/screen/overlay)、每層
// 透明度、依 E4-06 `SurfaceSwitcher` 定址與存在性檢查，產出的是**描述**（一份依序排列的
// 圖層清單），**不做真實像素合成**——那是後續相位繪製層的職責。
//
// 本單元建於上游 E4-06 `SurfaceSwitcher` 之上：每個圖層以**具名** `ds::kernel::SurfaceId`
// 指涉，加入圖層前必須先經 `SurfaceSwitcher` 定址存在（`has(id)`），確保「合成計畫只描述
// 已知的具名 surface」，不重造一套 surface 存在性判斷。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 圖層一律以**具名** `SurfaceId` 指涉，不用數字 handle / index。
//   - 圖層順序以**加入順序**表達（`vector` 的具名序列），對外只暴露「依序排列的具名清單」，
//     從未暴露任何數字 z-order / 圖層層級欄位；移除 / 重新加入即可調整順序，不需要「設定第
//     N 層」這種數字操作。
//   - 混合模式為**具名列舉**（Normal/Multiply/Screen/Overlay），非數字係數。
//   - 透明度為**正規化比例** [0,1]（沿用同子系統慣例，如 E4-02 的 opacity），非座標 / 非
//     z-order。
//
// 不靜默失敗：
//   - `add_layer`：空 id、非有限透明度、未知（不在列舉集合內的）混合模式、重複加入同一
//     具名 surface → `CompositeStatus::Invalid`（不套用、不部分加入）。
//   - `add_layer`：id 未經 `SurfaceSwitcher` 定址存在（`!switcher.has(id)`）→
//     `CompositeStatus::NotFound`（不加入）。
//   - `remove_layer`：本合成器內未知的圖層 id → `CompositeStatus::NotFound`（不崩潰）。
//
// 相位 1 平台中立：純記憶體邏輯 + 描述產出，無 `#ifdef` / win32 / cocoa / 任何真實繪圖或
// OS API、不做真實像素合成。
#ifndef DS_RENDER_E4_08_LAYER_COMPOSITOR_HPP
#define DS_RENDER_E4_08_LAYER_COMPOSITOR_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "surface_switcher.hpp"  // E4-06（上游，可讀不可改）：SurfaceSwitcher / ds::kernel::SurfaceId
                                  // 定址存在性檢查（has）；本單元透過它確保圖層皆指向已知 surface。

namespace ds::render {

// 混合模式 —— 具名列舉，非數字係數（NFR-02）。描述本層如何與其下方已合成結果混合。
enum class BlendMode {
    Normal,    // 正常：本層依透明度覆蓋於下方結果之上
    Multiply,  // 色彩相乘：使結果變暗（常見於陰影 / 材質疊加）
    Screen,    // 反相相乘：使結果變亮（常見於光暈 / 高光疊加）
    Overlay,   // 疊加：依下方明暗程度混合 Multiply / Screen（常見於對比強化）
};

// 操作結果碼 —— 與同子系統 E4-06 `SwitchStatus` 同精神：明確、不靜默。
enum class CompositeStatus {
    Ok,        // 操作成功
    Invalid,   // 前置條件不合法：空 id、非有限透明度、未知混合模式、重複加入同一 surface
    NotFound,  // `add_layer`：id 未經 SurfaceSwitcher 定址存在；`remove_layer`：本合成器內未知圖層
};

// 合成計畫中的**單一圖層**描述 —— 純資料，具名取值，無絕對座標 / 無數字 z-order。
struct LayerEntry {
    ds::kernel::SurfaceId surface_id;        // 具名來源 surface（經 E4-06 定址存在）
    BlendMode blend_mode = BlendMode::Normal;  // 具名混合模式
    float opacity = 1.0f;                      // 本層透明度，正規化比例 [0,1]
};

// **合成計畫描述** —— `compose()` 的產出：依具名圖層順序排列的圖層清單。純描述，不含任何
// 真實像素資料；後續相位的繪製層據此執行真正的合成。
struct CompositionPlan {
    std::vector<LayerEntry> layers;  // 依加入順序（即具名圖層序）排列；非 z-order 數值
};

// ---------------------------------------------------------------------------
// LayerCompositor —— 多圖層合成的**計畫產出器**。
//
// 以參考持有一個 `SurfaceSwitcher`（不取得所有權；須存活於本物件之外的生命週期內），管理
// 一份**依加入順序**排列的具名圖層清單：加入 / 移除圖層、查詢、產出合成計畫描述。純記憶體
// 狀態 + 描述產出，不觸碰任何真實 surface 內容、不做真實像素合成（那是後續相位的職責）。
// ---------------------------------------------------------------------------
class LayerCompositor {
public:
    // 綁定一個 `SurfaceSwitcher`，作為圖層 surface 的定址存在性來源（E4-06）。
    explicit LayerCompositor(SurfaceSwitcher& switcher) : switcher_(switcher) {}

    // --- 加入 / 移除圖層 ---
    // 於目前圖層序**末端**加入一層（具名順序，非數字 z-order；欲調整順序請 remove 後
    // 依新順序重新 add）。
    //   - `surface_id` 為空 → `Invalid`（不加入）。
    //   - `blend_mode` 不在 Normal/Multiply/Screen/Overlay 集合內（如以 `static_cast` 硬塞
    //     非法值）→ `Invalid`（不加入）。
    //   - `opacity` 非有限值（NaN / Inf）→ `Invalid`（不加入）；成功時自動 clamp 至 [0,1]。
    //   - 同一 `surface_id` 已在本合成器中 → `Invalid`（不覆蓋、不新增第二筆；避免同一
    //     surface 的圖層順序產生歧義）。
    //   - `surface_id` 未經 `SurfaceSwitcher::has()` 定址存在 → `NotFound`（不加入）。
    // 成功 → `Ok`，圖層數 +1。
    CompositeStatus add_layer(const ds::kernel::SurfaceId& surface_id, BlendMode blend_mode,
                               float opacity);
    // 移除已加入的具名圖層。本合成器內未知的 id → `NotFound`（不崩潰）。成功 → `Ok`，
    // 其餘圖層維持原相對順序（不重新編號，因為從未有數字編號）。
    CompositeStatus remove_layer(const ds::kernel::SurfaceId& surface_id);

    // --- 查詢 ---
    // 該具名 surface 是否已是本合成器的一層。
    bool has_layer(const ds::kernel::SurfaceId& surface_id) const;
    // 目前圖層數。
    std::size_t layer_count() const noexcept { return layers_.size(); }
    // 依加入順序列舉圖層的具名 surface id（具名清單；順序僅表示加入先後，非 z-order，NFR-02）。
    std::vector<ds::kernel::SurfaceId> layer_order() const;

    // --- 合成計畫 ---
    // 產出目前圖層序的**合成計畫描述**（依加入順序排列的 `LayerEntry` 清單）。純描述、不做
    // 真實像素合成；未加入任何圖層時回傳空清單（明確，不假裝有資料）。
    CompositionPlan compose() const;

private:
    SurfaceSwitcher& switcher_;         // 圖層 surface 的定址存在性來源（E4-06，不持有所有權）
    std::vector<LayerEntry> layers_;    // 依加入順序排列，非數字索引語意

    std::vector<LayerEntry>::iterator find(const ds::kernel::SurfaceId& id);
    std::vector<LayerEntry>::const_iterator find(const ds::kernel::SurfaceId& id) const;
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_08_LAYER_COMPOSITOR_HPP
