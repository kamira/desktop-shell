// E4-30 全螢幕調光覆蓋 — 平台中立介面（module 層 / 子系統 elements / 相位 1 null）
//
// 語意：一個覆蓋整個螢幕的**半透明暗色層**（調光 / 遮罩覆蓋），用於專注模式、彈窗背景
// 變暗、夜間調光等情境。可設定：
//   - 調光強度 intensity [0,1]（= 覆蓋層整體不透明度）；
//   - 調光顏色（正規化 RGB；預設純黑）；
//   - 顯示 / 隱藏、淡入淡出至目標強度；
//   - 可選**挖洞**（cutout）：以具名區域指涉「不調光（透明）」的區域。
//
// 相位 1 不做真實繪製：本單元只產出覆蓋層的**渲染描述**（`DimRenderModel`）＋狀態，並：
//   - 以上游 **E1-03** `AlphaSurfaceService`（per-pixel alpha surface）表達半透明——覆蓋層即
//     一個帶不透明度的 alpha surface，intensity 反映到其 `AlphaProfile::opacity`；
//   - 以上游 **E1-01** `LayerStack` 把覆蓋層置於**具名頂層** `SurfaceLayer::Topmost`（NFR-02：
//     用具名圖層置頂，不用絕對座標 / 數字 z-order）。
//
// NFR-03（能力閘控）：建立半透明覆蓋層（及挖洞、淡入淡出）需 per-pixel alpha 能力，一律先經
// `AlphaSurfaceService::supported()`（= `KernelBackend::has(kPerPixelAlphaCapability)`）閘控；
// 能力不可用時回結構化 `Unsupported`，不改狀態、不崩潰（降級路徑）。
//
// 相位 1 硬約束：無 `#ifdef` / `win32` / `cocoa` / 真實繪圖 API；無絕對座標 / 數字 z-order。
#ifndef DS_ELEMENTS_E4_30_DIM_OVERLAY_HPP
#define DS_ELEMENTS_E4_30_DIM_OVERLAY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "alpha_surface.hpp"  // 上游 E1-03（可讀不可改）：AlphaSurfaceService / AlphaProfile / AlphaMode
#include "layer_stack.hpp"    // 上游 E1-01（可讀不可改）：LayerStack / SurfaceLayer / layer_name
#include "null_backend.hpp"   // 上游 E1-24（可讀不可改）：SurfaceId / SurfaceProfile / SurfaceLayer

namespace ds::elements {

// 調光顏色 —— 正規化 RGB 各 [0,1]，無 alpha 通道（透明度由 intensity 表達，非顏色的一部分）。
// 預設純黑：對應夜間 / 專注 / 彈窗背景最常見的「壓暗」遮罩。
struct DimColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// 淡入淡出方向（具名，非數字）。
//   - In：調光加深（intensity 上升，例如淡入專注遮罩）。
//   - Out：調光減弱（intensity 下降，例如淡出回到明亮）。
//   - None：無進行中的淡入淡出（強度未變或已抵達目標）。
enum class DimFade {
    None,
    In,
    Out,
};

// 操作結果碼 —— 聚合上游 E1-03 `AlphaStatus` / E1-01 `LayerAssign` 為單一元件層語意。
enum class DimStatus {
    Ok,           // 操作成功
    Unsupported,  // per-pixel alpha 能力於後端不可用（NFR-03 閘控；呼叫端走降級路徑）
    Invalid,      // 前置條件不滿足（非有限輸入、空挖洞名等），報錯不靜默
};

// 相位 1 的**渲染描述**：覆蓋層在此刻該如何被畫出的宣告式快照（不做真實繪製）。
// 呼叫端 / 後續相位的繪製後端據此渲染；本相位僅產出此結構供斷言與交接。
struct DimRenderModel {
    bool visible = false;               // 覆蓋層是否顯示
    float intensity = 0.0f;             // 調光強度 [0,1]（= alpha 不透明度乘數）
    float effective_opacity = 0.0f;     // 綜合可見性後的有效不透明度（隱藏時恆為 0）
    DimColor color{};                   // 調光顏色
    std::string layer_name;             // 覆蓋層所在的**具名頂層**名（"layer.topmost"）——NFR-02
    DimFade fade = DimFade::None;       // 目前淡入淡出方向
    float fade_target = 0.0f;           // 淡入淡出目標強度 [0,1]
    std::vector<std::string> cutouts;   // 具名挖洞區域（不調光 / 透明）
    bool alpha_supported = false;       // per-pixel alpha 能力是否可用（降級可觀測）
};

// ---------------------------------------------------------------------------
// DimOverlayElement —— 全螢幕調光覆蓋元件。
//
// 以參考持有上游 E1-03 `AlphaSurfaceService`（表達半透明）與 E1-01 `LayerStack`（置於具名頂層）。
// 維護覆蓋層自身狀態（強度 / 顏色 / 可見性 / 淡入淡出 / 挖洞），並惰性建立一個置頂的半透明
// alpha surface。所有需要建立 / 改動半透明 surface 的操作皆先經 per-pixel alpha 能力閘控（NFR-03）。
// 相位 1 的核心產出是 `render_model()`：覆蓋層的宣告式渲染描述。
// ---------------------------------------------------------------------------
class DimOverlayElement {
public:
    // 綁定上游服務（不取得所有權；兩者須存活於本元件之外的生命週期內）。
    //   - alpha：E1-03 per-pixel alpha surface 服務，表達覆蓋層半透明。
    //   - layers：E1-01 具名圖層堆疊，把覆蓋層置於 Topmost。
    //   - surface_id：覆蓋層的具名 surface id（NFR-02：具名，非數字 handle）。
    DimOverlayElement(ds::kernel::AlphaSurfaceService& alpha,
                      ds::kernel::LayerStack& layers,
                      ds::kernel::SurfaceId surface_id = "surface.dim_overlay");

    // --- 調光強度 ---
    // 設定調光強度 [0,1]（自動夾限）。非有限值 → Invalid（報錯不靜默）。純狀態更新，不需能力；
    // 若覆蓋層已建立則同步反映到 alpha surface 的不透明度。成功 → Ok。
    DimStatus set_intensity(float intensity);
    float intensity() const noexcept { return intensity_; }

    // --- 調光顏色 ---
    // 設定調光顏色（各通道夾至 [0,1]）。任一通道非有限值 → Invalid。純狀態更新，不需能力。成功 → Ok。
    DimStatus set_color(const DimColor& color);
    const DimColor& color() const noexcept { return color_; }

    // --- 顯示 / 隱藏 ---
    // 顯示覆蓋層：惰性建立置頂的半透明 alpha surface 並標記可見。
    //   - per-pixel alpha 能力不可用 → Unsupported（不建立、不標記可見；降級路徑）。
    //   - 具名圖層置頂被能力閘控拒絕 → Unsupported（回滾已建立的 surface，不留半份狀態）。
    DimStatus show();
    // 隱藏覆蓋層（標記不可見）。釋放 / 隱藏恆為安全，不需能力閘控。成功 → Ok。
    DimStatus hide();
    bool visible() const noexcept { return visible_; }

    // --- 淡入淡出 ---
    // 淡入淡出至目標強度 [0,1]。相位 1 為宣告式：設定淡入淡出方向與目標，並直接抵達目標強度
    // （真實逐格動畫由後續相位承接）。target>0 視為顯示、target==0 視為淡出至隱藏。
    //   - target 非有限 → Invalid；per-pixel alpha 能力不可用 → Unsupported。成功 → Ok。
    DimStatus fade_to(float target);
    DimFade fade() const noexcept { return fade_; }
    float fade_target() const noexcept { return fade_target_; }

    // --- 挖洞（不調光的具名區域）---
    // 新增一個不調光（透明）的具名區域。挖洞屬 per-pixel alpha 語意，需能力閘控。
    //   - 空名 → Invalid；能力不可用 → Unsupported；重複名 → 忽略（冪等，回 Ok）。成功 → Ok。
    DimStatus add_cutout(const std::string& region);
    // 移除具名挖洞；回傳是否確有移除（未知名回 false，不崩潰）。
    bool remove_cutout(const std::string& region);
    bool has_cutout(const std::string& region) const;
    std::size_t cutout_count() const noexcept { return cutouts_.size(); }

    // --- 具名頂層（NFR-02）---
    // 覆蓋層固定所在的具名圖層（Topmost）。以具名圖層置頂，不以數字 z-order 表達。
    ds::kernel::SurfaceLayer layer() const noexcept { return ds::kernel::SurfaceLayer::Topmost; }

    // --- 能力閘控（NFR-03）---
    // per-pixel alpha 能力是否可用（呼叫端在動作前自檢 / 決定降級路徑的入口）。
    bool alpha_supported() const { return alpha_.supported(); }

    // 本元件是否已建立其底層 alpha surface。
    bool surface_created() const noexcept { return created_; }

    // --- 相位 1 渲染描述 ---
    // 產出覆蓋層此刻的宣告式渲染描述（不做真實繪製）。
    DimRenderModel render_model() const;

private:
    // 惰性建立置頂的半透明 alpha surface（經能力閘控）。已建立則直接回 Ok。
    DimStatus ensure_surface();
    // 把目前 intensity 反映到底層 alpha surface 的不透明度（best-effort；未建立則 no-op）。
    void sync_alpha();

    ds::kernel::AlphaSurfaceService& alpha_;
    ds::kernel::LayerStack& layers_;
    ds::kernel::SurfaceId surface_id_;

    float intensity_ = 0.5f;            // 預設半調光
    DimColor color_{};                  // 預設純黑
    bool visible_ = false;
    DimFade fade_ = DimFade::None;
    float fade_target_ = 0.0f;
    std::vector<std::string> cutouts_;  // 具名挖洞（順序即加入序，去重）
    bool created_ = false;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_30_DIM_OVERLAY_HPP
