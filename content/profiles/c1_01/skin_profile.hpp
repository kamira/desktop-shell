// content/profiles/c1_01/skin_profile.hpp — C1-01 Skin profile（桌面寵物 / 角色皮膚基底 profile）
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「Skin profile」是桌面寵物 / 角色皮膚的**基底 profile**：一個**可自由拖曳、具名圖層歸屬、
// 可互動、具透明外形**的桌面角色，其定義可由**宣告式 skin 檔**載入。本單元不是新引擎邏輯，
// 而是把六個已合併的擴充點**組裝**成單一「角色皮膚」應用 profile，作為多個具體 skin 的共用
// 基底（lb=9）：
//
//   - E7-01（`ds::format::Value`）：**宣告式 skin 定義** —— `load_skin(Value)` 消費一份宣告式
//     文件的內容根（Map），把圖層 / 輸入策略 / 透明外形 / 初始位置等欄位解讀成本 profile 的
//     期望狀態，再據以在後端**實體化**。不自造格式，完全消費 E7-01 的 `Value` 多型模型。
//   - E1-03（`ds::kernel::AlphaSurfaceService`）：**逐像素 alpha 透明外形** —— skin 的視覺本體
//     是一個支援 per-pixel alpha 的 surface（不規則 / 羽化的桌寵外形）。per-pixel alpha 為
//     **可選能力**（NFR-03），`load_skin` 前一律經 `has()` 閘控；能力不可用時走降級路徑
//     （回 `Unsupported`，不建立任何 surface、不留殘留狀態）。
//   - E1-01（`ds::kernel::LayerStack`）：**具名圖層歸屬** —— skin 指派到一個**具名圖層**
//     （桌布 / 一般 / 浮層 / 最上層…，NFR-02 具名非數字 z-order），由注入的共用 `LayerStack`
//     維護整體堆疊順序。
//   - E1-02（`ds::kernel::InputStrategyController`）：**輸入策略** —— skin「可互動」的具名策略
//     （Interactive / Capture / ClickThrough / Inert），實體化時設定於後端並經
//     `to_backend_policy` 對映到 E1-24 三態。
//   - E1-07（`ds::kernel::AnchorSpec` / `resolve`）：**anchor 定位** —— 位置以九宮具名錨點 +
//     容器尺寸正規化偏移表達（NFR-02，取代絕對座標），可在給定容器 / 元件尺寸下解析為具體佈局。
//   - E1-08（`ds::kernel::DraggableSurface`）：**自由拖曳 + 位置記憶** —— 拖曳狀態機
//     （begin / drag_to / end / cancel）與位置持久化（`save_position()` 序列化為 E7-01 文字、
//     `load_position()` 還原）。E1-08 建於 E1-07 之上，故 anchor 定位由本相依透傳。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。任何無效操作（未載入即定位 / 拖曳、
// 重複載入、宣告式定義結構 / 型別 / 具名值不合法、能力不可用時載入）一律明確回傳具名結果，
// 不靜默；`load_skin` 中途失敗會回滾已完成的部分實體化（全有或全無，不留殘留後端狀態）。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&`：surface 宿主。本 profile 內部自持的 E1-03 / E1-02 / E1-08
//     服務皆包裹**同一個**後端（由建構子確保），避免「不同服務綁不同後端」的組裝陷阱。
//   - `ds::kernel::LayerStack&`：具名圖層堆疊宿主，可與其他 surface / skin 共用（以具名
//     SurfaceId 區分，互不干擾）。
#ifndef DS_CONTENT_PROFILES_C1_01_SKIN_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_01_SKIN_PROFILE_HPP

#include <string>

#include "alpha_surface.hpp"      // E1-03（上游，可讀不可改）：AlphaSurfaceService / AlphaProfile /
                                   //   AlphaMode / AlphaStatus（透傳 E1-24 KernelBackend / SurfaceId /
                                   //   SurfaceProfile / E1-21 CapabilityMatrix）
#include "anchor_model.hpp"       // E1-07（上游，可讀不可改）：Anchor / AnchorSpec / Offset / Size /
                                   //   ResolvedPlacement / AnchorStatus / resolve
#include "draggable_surface.hpp"  // E1-08（上游，可讀不可改）：DraggableSurface / DragStatus /
                                   //   anchor_to_name / anchor_from_name（其上為 E1-07 / E7-12 / E7-01）
#include "input_strategy.hpp"     // E1-02（上游，可讀不可改）：InputStrategy / InputStrategyController /
                                   //   to_backend_policy / hit_result / to_string
#include "layer_stack.hpp"        // E1-01（上游，可讀不可改）：LayerStack / SurfaceLayer / layer_name /
                                   //   LayerAssign
#include "document.hpp"           // E7-01（上游，可讀不可改）：Value（宣告式 skin 定義的資料模型）

namespace ds::profiles {

// skin profile 的具名生命週期狀態（NFR-02：具名，非數字）。
enum class SkinState {
    Unloaded,  // 未載入 / 已卸載：後端無對應 surface，尚未實體化。
    Loaded,    // 已載入：後端已建立 alpha surface、已指派圖層 / 輸入策略 / 初始位置。
};

const char* to_string(SkinState s) noexcept;

// skin 載入 / 卸載的具名結果 —— 結構化、不靜默（與上游 `AlphaStatus` / `AnchorStatus` /
// `DragStatus` 同風格）。定位 / 拖曳等行為改用 E1-08 原生 `ds::kernel::DragStatus` 回報，
// 以保留其精確碼（NotDragging / AlreadyDragging）。
enum class SkinStatus {
    Ok,            // 操作成功。
    Unsupported,   // per-pixel alpha 能力於後端不可用（E1-03 閘控，NFR-03）；不實體化，走降級路徑。
    Invalid,       // 前置條件不滿足：空 id、宣告式定義結構 / 型別 / 具名值不合法、能力閘控拒絕等。
    AlreadyLoaded, // 已載入時再次 load_skin（不靜默重載；呼叫端須先 unload()）。
    NotLoaded,     // 尚未載入即操作（僅供查詢語意；定位 / 拖曳以 DragStatus 回報）。
};

const char* to_string(SkinStatus s) noexcept;

// ---------------------------------------------------------------------------
// SkinProfile —— 桌面角色皮膚基底 profile：組裝 E7-01 + E1-03 + E1-01 + E1-02 + E1-07 + E1-08。
//
// 每個實例代表**一個**具名桌面角色（如 "skin.cat"）。內部自持三個以同一後端建構的服務
// （E1-03 alpha surface、E1-02 輸入策略、E1-08 拖曳 + 位置記憶），並使用注入的共用 E1-01
// 圖層堆疊。作為多個具體 skin 的共用基底：具體 skin 可持有本物件並附加其自身的資產 / 行為。
// ---------------------------------------------------------------------------
class SkinProfile {
public:
    // 建構一個具名 skin profile。id 即其 surface 的具名 SurfaceId（NFR-02）。
    // 後端與圖層堆疊皆為注入式共用相依（不取得所有權）。
    SkinProfile(std::string id, ds::kernel::KernelBackend& backend, ds::kernel::LayerStack& layers);

    // 解構：若仍載入中，強制 unload()（銷毀後端 surface、移除圖層 / 輸入 / 位置登錄），
    // 確保不於共用後端 / 圖層堆疊上留下指向本已銷毀物件語意的殘留條目。
    ~SkinProfile();

    SkinProfile(const SkinProfile&) = delete;
    SkinProfile& operator=(const SkinProfile&) = delete;

    // --- 宣告式載入（E7-01）+ 跨 E1-03 / E1-01 / E1-02 / E1-07 / E1-08 實體化 ---

    // 從宣告式 skin 定義（E7-01 `Value`，須為 Map；通常為 `Document::root`）載入並實體化本 skin。
    // 解讀的欄位（皆為選填，缺者用預設；未知鍵忽略）：
    //   layer:    具名圖層字串（"wallpaper" / "below-normal" / "normal" / "overlay" / "topmost"）
    //   input:    具名輸入策略字串（"interactive" / "capture" / "click-through" / "inert"）
    //   alpha:    Map { mode: "opaque"|"per-pixel", opacity: [0,1] 數字 }
    //   position: Map { anchor: 九宮具名錨點（見 E1-08 anchor_from_name），dx / dy: 正規化偏移數字 }
    // 流程（全有或全無）：
    //   - 已載入 → AlreadyLoaded（不靜默重載）。id 為空 → Invalid。
    //   - 定義非 Map、任一已知欄位型別 / 具名值不合法 → Invalid，且**不改任何狀態**（不靜默）。
    //   - per-pixel alpha 能力不可用（E1-03 `supported()==false`）→ Unsupported，不建立 surface。
    //   - 建立 alpha surface（E1-03）→ 指派具名圖層（E1-01）→ 設定輸入策略（E1-02）→ 登錄初始
    //     位置（E1-08）。任一步失敗即回滾先前步驟（不留殘留後端狀態）並回對應具名結果。
    //   - 全數成功 → Ok，state() 轉為 Loaded。
    SkinStatus load_skin(const ds::format::Value& definition);

    // 卸載本 skin：反向拆除後端狀態（移除位置登錄 / 輸入策略 / 圖層指派、銷毀 alpha surface）。
    // 未載入 → false（no-op，不靜默）。成功 → true，state() 轉為 Unloaded。
    // 注意：卸載會清除 E1-08 記憶位置登錄；要跨載入保留位置，請先 save_position() 再於重載後
    // load_position()（記憶為純資料，可在 surface 建立前先回填）。
    bool unload();

    // --- 定位（E1-07 anchor + E1-08 記憶）---

    // 設定 skin 的記憶位置（committed）——宣告式 AnchorSpec（九宮錨點 + 正規化偏移，NFR-02）。
    // 委派 E1-08 `set_position`。未載入 → Invalid；拖曳進行中 → Invalid（不可外部改位置）；
    // 無效 AnchorSpec（越界 anchor / 非有限 offset）→ Invalid；成功 → Ok。
    ds::kernel::DragStatus place(const ds::kernel::AnchorSpec& spec);

    // 把 skin 的**實時位置**（拖曳中為 pending 目標，否則為記憶位置）在給定容器 / 元件尺寸下
    // 解析為具體佈局（委派 E1-08 `resolve_live` → E1-07 `resolve`，純佈局計算）。
    // 未載入（無位置登錄）→ Invalid；非有限 / 負尺寸 → Invalid；成功 → Ok，out 填入。
    ds::kernel::AnchorStatus resolve_live(const ds::kernel::Size& container,
                                          const ds::kernel::Size& element,
                                          ds::kernel::ResolvedPlacement& out) const;

    // --- 自由拖曳狀態機（E1-08）---
    // 皆委派 E1-08，保留其精確結果碼。未載入時：begin_drag → Invalid（無位置可拖）；
    // drag_to / end_drag / cancel_drag → NotDragging（未載入必然未在拖曳）。
    ds::kernel::DragStatus begin_drag();                                  // 開始拖曳（起點 = 目前記憶位置）。
    ds::kernel::DragStatus drag_to(const ds::kernel::AnchorSpec& spec);   // 移動拖曳目標（更新 pending）。
    ds::kernel::DragStatus end_drag();                                    // 結束並提交 pending 為記憶位置。
    ds::kernel::DragStatus cancel_drag();                                 // 取消並放棄 pending（還原）。

    // 本 skin 目前是否正在拖曳。
    bool is_dragging() const;

    // --- 位置記憶持久化（E1-08 → E7-01 文字）---

    // 把本 skin 的記憶位置序列化為 E7-01 宣告式文字（首行 format_version）。委派 E1-08
    // `serialize_positions`（本物件的 E1-08 服務僅追蹤本 skin，故輸出僅含本 skin 一條）。
    // 未載入時 E1-08 無登錄 → 輸出為僅含 format_version 的空 map 文字（仍為合法 E7-01）。
    std::string save_position() const;

    // 從 E7-01 文字還原記憶位置（委派 E1-08 `load_positions`）。文字無法解析 / 條目結構不符 →
    // Invalid 且不套用任何條目（全有或全無）。還原為純資料回填，不經後端閘控——可在 load_skin
    // 前先回填記憶（對真實 surface 生效發生在後續的 place / 拖曳）。成功 → Ok。
    ds::kernel::DragStatus load_position(const std::string& text);

    // --- 查詢 ---
    SkinState state() const noexcept { return state_; }
    bool is_loaded() const noexcept { return state_ == SkinState::Loaded; }
    const std::string& id() const noexcept { return id_; }

    // 目前 / 期望的具名圖層（E1-01）。load_skin 會據宣告式 `layer` 欄位覆寫，預設 `Normal`。
    ds::kernel::SurfaceLayer layer() const noexcept { return layer_; }
    // 該具名圖層的穩定字串名（透傳 E1-01 `layer_name()`，如 "layer.overlay"）。
    std::string layer_name() const { return ds::kernel::layer_name(layer_); }
    // 本 skin 目前是否已指派於注入的圖層堆疊（載入中應為 true；卸載後應為 false）。
    bool assigned_to_layer() const { return layers_.contains(id_); }

    // 目前 / 期望的具名輸入策略（E1-02）。load_skin 會據宣告式 `input` 欄位覆寫，預設 `Interactive`。
    ds::kernel::InputStrategy input_strategy() const noexcept { return strategy_; }
    // E1-02 組裝入口：本 skin 策略對映的後端策略 / 命中結果（純函式透傳，供驗證組裝正確）。
    ds::kernel::InputPolicy backend_input_policy() const noexcept {
        return ds::kernel::to_backend_policy(strategy_);
    }
    ds::kernel::InputHitResult hit_result() const noexcept { return ds::kernel::hit_result(strategy_); }

    // 目前 / 期望的透明外形設定（E1-03）。load_skin 會據宣告式 `alpha` 欄位覆寫，預設
    // per-pixel、opacity 1.0。
    const ds::kernel::AlphaProfile& alpha() const noexcept { return alpha_; }
    // E1-03 能力閘控（NFR-03）：per-pixel alpha 於注入後端是否可用。load_skin 的前置閘門。
    bool alpha_supported() const { return alpha_svc_.supported(); }

    // 記憶位置（committed）；未登錄回 nullptr。透傳 E1-08 `remembered_position`。
    const ds::kernel::AnchorSpec* remembered_position() const {
        return drag_.remembered_position(id_);
    }
    // 實時位置（拖曳中為 pending，否則為記憶位置）；未登錄且未拖曳回 nullptr。透傳 E1-08。
    const ds::kernel::AnchorSpec* live_position() const { return drag_.live_position(id_); }

private:
    // 把後端狀態拆除乾淨（供卸載與 load_skin 中途失敗回滾）。各步對未知 id 皆安全 no-op。
    void teardown_backend_state();

    std::string id_;
    ds::kernel::KernelBackend& backend_;
    ds::kernel::LayerStack& layers_;

    // 自持服務，皆以同一注入後端建構（確保一致，見標頭「依賴注入約定」）。
    ds::kernel::AlphaSurfaceService alpha_svc_;
    ds::kernel::InputStrategyController input_ctl_;
    ds::kernel::DraggableSurface drag_;

    SkinState state_ = SkinState::Unloaded;

    // 期望狀態（預設值；load_skin 依宣告式定義覆寫，實體化時套用至各服務）。
    ds::kernel::SurfaceLayer layer_ = ds::kernel::SurfaceLayer::Normal;
    ds::kernel::InputStrategy strategy_ = ds::kernel::InputStrategy::Interactive;
    ds::kernel::AlphaProfile alpha_ = {};   // 預設 per-pixel、opacity 1.0（見 E1-03 AlphaProfile）
    ds::kernel::AnchorSpec position_ = {};  // 預設 Center、無偏移（見 E1-07 AnchorSpec）
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_01_SKIN_PROFILE_HPP
