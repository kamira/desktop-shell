// content/profiles/c1_02/portrait_profile.hpp — C1-02 立繪 profile（角色立繪 / 桌寵靜態立繪）
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「立繪 profile」是角色立繪 / 桌寵靜態立繪的應用 profile：一個**具透明外形、具名圖層歸屬、
// 可自由拖曳並記憶位置、可依區域點擊觸發互動**的桌面立繪，並可切換其**具名表情 / 狀態**
// （如 idle / happy）所對應的立繪圖。其定義可由**宣告式立繪檔**載入。本單元不是新引擎邏輯，
// 而是把六個已合併的擴充點**組裝**成單一「立繪」應用 profile：
//
//   - E7-01（`ds::format::Value`）：**宣告式立繪定義** —— `load_portrait(Value)` 消費一份
//     宣告式文件的內容根（Map），把圖層 / 透明外形 / 初始位置 / 具名表情清單等欄位解讀成
//     本 profile 的期望狀態，再據以在後端**實體化**。不自造格式，完全消費 E7-01 的 `Value`
//     多型模型。
//   - E1-03（`ds::kernel::AlphaSurfaceService`）：**逐像素 alpha 透明外形** —— 立繪的視覺本體
//     是一個支援 per-pixel alpha 的 surface（不規則 / 羽化的立繪外形）。per-pixel alpha 為
//     **可選能力**（NFR-03），`load_portrait` 前一律經 `has()` 閘控；能力不可用時走降級路徑
//     （回 `Unsupported`，不建立任何 surface、不留殘留狀態）。
//   - E1-01（`ds::kernel::LayerStack`）：**具名圖層歸屬** —— 立繪指派到一個**具名圖層**
//     （桌布 / 一般 / 浮層 / 最上層…，NFR-02 具名非數字 z-order），由注入的共用 `LayerStack`
//     維護整體堆疊順序。
//   - E1-08（`ds::kernel::DraggableSurface`）：**自由拖曳 + 位置記憶** —— 拖曳狀態機
//     （begin / drag_to / end / cancel）與位置持久化（`save_position()` 序列化為 E7-01 文字、
//     `load_position()` 還原）。E1-08 建於 E1-07 之上，故 anchor 定位由本相依透傳。
//   - E4-06（`ds::render::SurfaceSwitcher`）：**具名表情 / 狀態切換** —— 立繪可有多個具名
//     表情（如 "idle" / "happy"），本物件把每個表情對應一份 E4-02 `ImageElement`（見下），
//     `switch_expression()` 純委派 E4-06 的具名狀態機決定「目前顯示哪一個」——立繪本身仍是
//     **同一個** alpha surface，切換的是要合成到該 surface 上的圖片內容。
//   - E5-14（`ds::events::RegionEventDispatcher`）：**區域點擊事件** —— 在立繪本體 surface 上
//     登記一組 E1-05 具名子區域（如頭部 / 身體），點擊不同區域時把命中的具名區域資訊帶入
//     事件參數，供訂閱者分辨「點了立繪的哪個部位」。
//
// 附帶透傳 E4-02（`ds::elements::ImageElement`，經 E4-06 標頭「借道」傳遞，可讀不可改）：
// 每個具名表情對應一份圖片渲染描述（來源參照 / 固有尺寸 / 縮放模式 / 透明度 / 目標 surface），
// `target` 一律指向本 profile 的主要 alpha surface（單一顯示目標，切表情只換內容不換 surface）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02；區域形狀之 width/height 為**元件本地
// 固有尺寸**，非畫面絕對座標，見 E1-04 / E4-02 慣例）。任何無效操作（未載入即定位 / 拖曳 /
// 切表情、重複載入、宣告式定義結構 / 型別 / 具名值不合法、能力不可用時載入）一律明確回傳
// 具名結果，不靜默；`load_portrait` 中途失敗會回滾已完成的部分實體化（全有或全無，不留殘留
// 後端狀態）。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&`：surface 宿主。本 profile 內部自持的 E1-03 / E1-08 服務皆
//     包裹**同一個**後端（由建構子確保），避免「不同服務綁不同後端」的組裝陷阱。
//   - `ds::kernel::LayerStack&`：具名圖層堆疊宿主，可與其他 surface / profile 共用（以具名
//     SurfaceId 區分，互不干擾）。
#ifndef DS_CONTENT_PROFILES_C1_02_PORTRAIT_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_02_PORTRAIT_PROFILE_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "alpha_surface.hpp"           // E1-03（上游，可讀不可改）：AlphaSurfaceService / AlphaProfile /
                                        //   AlphaMode / AlphaStatus（透傳 E1-24 KernelBackend / SurfaceId /
                                        //   SurfaceProfile / E1-21 CapabilityMatrix）
#include "draggable_surface.hpp"       // E1-08（上游，可讀不可改）：DraggableSurface / DragStatus /
                                        //   anchor_to_name / anchor_from_name（其上為 E1-07 AnchorSpec /
                                        //   Size / ResolvedPlacement / AnchorStatus / resolve，及 E7-12/E7-01）
#include "layer_stack.hpp"             // E1-01（上游，可讀不可改）：LayerStack / SurfaceLayer / layer_name /
                                        //   LayerAssign
#include "document.hpp"                // E7-01（上游，可讀不可改）：Value（宣告式立繪定義的資料模型）
#include "surface_switcher.hpp"        // E4-06（上游，可讀不可改）：SurfaceSwitcher / SwitchStatus；
                                        //   並經其標頭借道傳遞 image_element.hpp（E4-02，下行）
#include "image_element.hpp"           // E4-02（上游，可讀不可改，經 E4-06 傳遞）：ImageElement /
                                        //   ImageStatus / ScaleMode / ImageDimensions / MemoryImageSource /
                                        //   ImageRenderModel（透傳 E1-03 AlphaProfile / SurfaceId）
#include "region_event_dispatcher.hpp" // E5-14（上游，可讀不可改）：RegionEventDispatcher / RegionEvent /
                                        //   RegionEventListener；經其標頭傳遞 E1-05 named_region_map.hpp
                                        //   （NamedRegionMap）與 E5-01 mouse_button_input.hpp（MouseButton /
                                        //   RouteStatus / SubscriptionId），再經其傳遞 E1-04 hit_test.hpp
                                        //   （LocalPoint / Shape / HitSurface / make_rect）

namespace ds::profiles {

// 立繪 profile 的具名生命週期狀態（NFR-02：具名，非數字）。
enum class PortraitState {
    Unloaded,  // 未載入 / 已卸載：後端無對應 surface，尚未實體化。
    Loaded,    // 已載入：後端已建立 alpha surface、已指派圖層、已登錄初始位置。
};

const char* to_string(PortraitState s) noexcept;

// 立繪載入 / 卸載的具名結果 —— 結構化、不靜默（與上游 `AlphaStatus` / `AnchorStatus` /
// `DragStatus` 同風格）。定位 / 拖曳等行為改用 E1-08 原生 `ds::kernel::DragStatus` 回報，
// 以保留其精確碼（NotDragging / AlreadyDragging）。
enum class PortraitStatus {
    Ok,             // 操作成功。
    Unsupported,    // per-pixel alpha 能力於後端不可用（E1-03 閘控，NFR-03）；不實體化，走降級路徑。
    Invalid,        // 前置條件不滿足：空 id、宣告式定義結構 / 型別 / 具名值不合法等。
    AlreadyLoaded,  // 已載入時再次 load_portrait（不靜默重載；呼叫端須先 unload()）。
};

const char* to_string(PortraitStatus s) noexcept;

// ---------------------------------------------------------------------------
// PortraitProfile —— 角色立繪 / 桌寵靜態立繪 profile：
// 組裝 E7-01 + E1-03 + E1-01 + E1-08 + E4-06（+ E4-02）+ E5-14。
//
// 每個實例代表**一個**具名立繪（如 "portrait.miku"）。內部自持四個以同一後端 / 純記憶體
// 建構的服務（E1-03 alpha surface、E1-08 拖曳 + 位置記憶、E4-06 具名表情切換、E5-14 區域
// 點擊事件），並使用注入的共用 E1-01 圖層堆疊。
// ---------------------------------------------------------------------------
class PortraitProfile {
public:
    // 建構一個具名立繪 profile。id 即其 surface 的具名 SurfaceId（NFR-02）。
    // 後端與圖層堆疊皆為注入式共用相依（不取得所有權）。
    PortraitProfile(std::string id, ds::kernel::KernelBackend& backend, ds::kernel::LayerStack& layers);

    // 解構：若仍載入中，強制 unload()（銷毀後端 surface、移除圖層 / 位置 / 表情 / 區域登錄），
    // 確保不於共用後端 / 圖層堆疊上留下指向本已銷毀物件語意的殘留條目。
    ~PortraitProfile();

    PortraitProfile(const PortraitProfile&) = delete;
    PortraitProfile& operator=(const PortraitProfile&) = delete;

    // --- 宣告式載入（E7-01）+ 跨 E1-03 / E1-01 / E1-08 / E4-06 / E5-14 實體化 ---

    // 從宣告式立繪定義（E7-01 `Value`，須為 Map；通常為 `Document::root`）載入並實體化本立繪。
    // 解讀的欄位（皆為選填，缺者用預設；未知鍵忽略）：
    //   layer:              具名圖層字串（"wallpaper" / "below-normal" / "normal" / "overlay" /
    //                       "topmost"）
    //   alpha:              Map { mode: "opaque"|"per-pixel", opacity: [0,1] 數字 }
    //   position:           Map { anchor: 九宮具名錨點（見 E1-08 anchor_from_name），
    //                       dx / dy: 正規化偏移數字 }
    //   expressions:        List，每元素為 Map { name（非空字串，具名表情 id）、
    //                       source（非空字串，圖片來源參照）、width / height（正整數，固有尺寸）、
    //                       scale（選填，"fill"|"fit"|"stretch"|"center"|"tile"，預設 "fit"）}；
    //                       表情 id 不得重複。
    //   initial_expression: 選填字串；須匹配 expressions 內某一 name。未給且 expressions 非空
    //                       時預設為第一個表情。
    // 流程（全有或全無）：
    //   - 已載入 → AlreadyLoaded（不靜默重載）。id 為空 → Invalid。
    //   - 定義非 Map、任一已知欄位型別 / 具名值不合法、表情缺欄位 / 重複 name、
    //     initial_expression 無匹配 → Invalid，且**不改任何狀態**（不靜默）。
    //   - per-pixel alpha 能力不可用（E1-03 `supported()==false`）→ Unsupported，不建立 surface。
    //   - 建立 alpha surface（E1-03）→ 指派具名圖層（E1-01）→ 登錄初始位置（E1-08）→ 登記表情
    //     （E4-06 + E4-02）→ 切至初始表情 → 登記區域點擊命中 surface（E5-14）。任一步失敗即
    //     回滾先前步驟（不留殘留後端狀態）並回對應具名結果。
    //   - 全數成功 → Ok，state() 轉為 Loaded。
    PortraitStatus load_portrait(const ds::format::Value& definition);

    // 卸載本立繪：反向拆除後端狀態（清空區域登記 / 表情登錄 / 位置登錄 / 圖層指派、銷毀
    // alpha surface）。未載入 → false（no-op，不靜默）。成功 → true，state() 轉為 Unloaded。
    // 注意：卸載會清除 E1-08 記憶位置登錄；要跨載入保留位置，請先 save_position() 再於重載後
    // load_position()（記憶為純資料，可在 surface 建立前先回填）。
    bool unload();

    // --- 具名表情 / 狀態切換（E4-06 + E4-02，需已載入）---

    // 新增一個具名表情，對應一份已載入來源的 `ImageElement`（`image.has_source()==false` 或
    // `name` 為空 / 已存在 → false，不新增）。內部會覆寫該 image 的 `target` 為本 profile 的
    // 主要 surface（NFR-02：單一顯示目標，切表情只換內容不換 surface）。未載入 → false。
    bool add_expression(const std::string& name, ds::elements::ImageElement image);

    // 移除一個具名表情；未知 name / 未載入 → false（不崩潰）。若移除的正是目前表情，切換退回
    // 「尚無目前表情」（`current_expression()` 轉空字串），不觸發任何隱式重新命中。
    bool remove_expression(const std::string& name);

    // 切換「目前顯示」的具名表情（純委派 E4-06 `switch_to`）。未知 name / 未載入 → false。
    // 成功後區域點擊命中形狀（E5-14）依新表情的固有尺寸重新整理。
    bool switch_expression(const std::string& name);

    // 目前表情 id；尚無目前表情 / 未載入回空字串。
    std::string current_expression() const;
    // 已註冊的具名表情清單（依註冊順序；透傳 E4-06 `list()`）。
    std::vector<std::string> expressions() const;
    // 該具名表情是否已註冊。
    bool has_expression(const std::string& name) const;
    // 目前表情對應的 `ImageElement`；尚無目前表情 / 未知 / 未載入回 nullptr。
    const ds::elements::ImageElement* current_image() const;

    // --- 區域點擊事件（E5-14，需已載入）---

    // 設定（或取代）立繪本體 surface 的具名子區域集合（E1-05 `NamedRegionMap`，呼叫端先以
    // `add_region()` 登記好）。未載入 → false（no-op）。
    bool set_regions(ds::kernel::NamedRegionMap regions);
    // 該立繪是否已登記子區域集合。
    bool has_regions() const;

    // 訂閱本立繪的區域點擊事件（`RegionEvent`：原始滑鼠事件 + 命中之具名子區域資訊）。
    // 純委派 E5-14 `subscribe`；`listener` 為空 → 回 0（無效訂閱）。可在載入前先訂閱
    // （載入後命中才會實際觸發）。
    ds::events::SubscriptionId on_region_click(ds::events::RegionEventListener listener);
    // 取消訂閱；回傳是否確有移除。
    bool unsubscribe_region_click(ds::events::SubscriptionId id);

    // 供測試 / 相位 1 注入：在立繪本地座標系內注入一次左鍵點擊，觸發命中判定與區域事件分派。
    // 純委派 E5-14 `inject_click`（回傳 `RouteStatus`：Hit / NoHit / Invalid）。
    ds::events::RouteStatus inject_click(const ds::kernel::LocalPoint& point);

    // --- 定位（E1-07 anchor + E1-08 記憶）---

    // 設定立繪的記憶位置（committed）——宣告式 AnchorSpec（九宮錨點 + 正規化偏移，NFR-02）。
    // 委派 E1-08 `set_position`。未載入 → Invalid；拖曳進行中 → Invalid（不可外部改位置）；
    // 無效 AnchorSpec（越界 anchor / 非有限 offset）→ Invalid；成功 → Ok。
    ds::kernel::DragStatus place(const ds::kernel::AnchorSpec& spec);

    // 把立繪的**實時位置**（拖曳中為 pending 目標，否則為記憶位置）在給定容器 / 元件尺寸下
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

    // 本立繪目前是否正在拖曳。
    bool is_dragging() const;

    // --- 位置記憶持久化（E1-08 → E7-01 文字）---

    // 把本立繪的記憶位置序列化為 E7-01 宣告式文字（首行 format_version）。委派 E1-08
    // `serialize_positions`（本物件的 E1-08 服務僅追蹤本立繪，故輸出僅含本立繪一條）。
    // 未載入時 E1-08 無登錄 → 輸出為僅含 format_version 的空 map 文字（仍為合法 E7-01）。
    std::string save_position() const;

    // 從 E7-01 文字還原記憶位置（委派 E1-08 `load_positions`）。文字無法解析 / 條目結構不符 →
    // Invalid 且不套用任何條目（全有或全無）。還原為純資料回填，不經後端閘控——可在 load_portrait
    // 前先回填記憶（對真實 surface 生效發生在後續的 place / 拖曳）。成功 → Ok。
    ds::kernel::DragStatus load_position(const std::string& text);

    // --- 查詢 ---
    PortraitState state() const noexcept { return state_; }
    bool is_loaded() const noexcept { return state_ == PortraitState::Loaded; }
    const std::string& id() const noexcept { return id_; }

    // 目前 / 期望的具名圖層（E1-01）。load_portrait 會據宣告式 `layer` 欄位覆寫，預設 `Normal`。
    ds::kernel::SurfaceLayer layer() const noexcept { return layer_; }
    // 該具名圖層的穩定字串名（透傳 E1-01 `layer_name()`，如 "layer.overlay"）。
    std::string layer_name() const { return ds::kernel::layer_name(layer_); }
    // 本立繪目前是否已指派於注入的圖層堆疊（載入中應為 true；卸載後應為 false）。
    bool assigned_to_layer() const { return layers_.contains(id_); }

    // 目前 / 期望的透明外形設定（E1-03）。load_portrait 會據宣告式 `alpha` 欄位覆寫，預設
    // per-pixel、opacity 1.0。
    const ds::kernel::AlphaProfile& alpha() const noexcept { return alpha_; }
    // E1-03 能力閘控（NFR-03）：per-pixel alpha 於注入後端是否可用。load_portrait 的前置閘門。
    bool alpha_supported() const { return alpha_svc_.supported(); }

    // 記憶位置（committed）；未登錄回 nullptr。透傳 E1-08 `remembered_position`。
    const ds::kernel::AnchorSpec* remembered_position() const {
        return drag_.remembered_position(id_);
    }
    // 實時位置（拖曳中為 pending，否則為記憶位置）；未登錄且未拖曳回 nullptr。透傳 E1-08。
    const ds::kernel::AnchorSpec* live_position() const { return drag_.live_position(id_); }

private:
    // 把後端狀態拆除乾淨（供卸載與 load_portrait 中途失敗回滾）。各步對未知 id 皆安全 no-op。
    void teardown_backend_state();

    // 依目前表情（若有）之固有尺寸重建 E5-14 命中 surface（本立繪的唯一參與命中測試 surface）；
    // 尚無表情時退回 1x1 的最小佔位形狀（元件本地固有尺寸，非螢幕座標，NFR-02）。未載入時
    // no-op（不留殘留 surfaces_ 條目）。
    void refresh_hit_surface();

    // 依 name 於 expressions_ 尋找對應 ImageElement；找不到回 nullptr。
    ds::elements::ImageElement* find_expression(const std::string& name);
    const ds::elements::ImageElement* find_expression(const std::string& name) const;

    // 無狀態閘控（不檢查 state_）的內部實作，供 load_portrait 於實體化中段（尚未提交 state_ =
    // Loaded）與已載入後的公開 add_expression() / switch_expression() 共用。
    bool add_expression_impl(const std::string& name, ds::elements::ImageElement image);
    bool switch_expression_impl(const std::string& name);

    std::string id_;
    ds::kernel::KernelBackend& backend_;
    ds::kernel::LayerStack& layers_;

    // 自持服務，皆以同一注入後端 / 純記憶體建構（確保一致，見標頭「依賴注入約定」）。
    ds::kernel::AlphaSurfaceService alpha_svc_;
    ds::kernel::DraggableSurface drag_;
    ds::render::SurfaceSwitcher switcher_;
    ds::events::RegionEventDispatcher regions_;

    // 具名表情 → 圖片渲染描述（E4-02）。以具名鍵線性配對，順序即新增順序（永不以數字 index
    // 對外暴露，與上游各服務同慣例）。
    std::vector<std::pair<std::string, ds::elements::ImageElement>> expressions_;

    PortraitState state_ = PortraitState::Unloaded;

    // 期望狀態（預設值；load_portrait 依宣告式定義覆寫，實體化時套用至各服務）。
    ds::kernel::SurfaceLayer layer_ = ds::kernel::SurfaceLayer::Normal;
    ds::kernel::AlphaProfile alpha_ = {};   // 預設 per-pixel、opacity 1.0（見 E1-03 AlphaProfile）
    ds::kernel::AnchorSpec position_ = {};  // 預設 Center、無偏移（見 E1-07 AnchorSpec）
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_02_PORTRAIT_PROFILE_HPP
