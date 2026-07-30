// E1-09 邊緣吸附 — 拖曳中 surface 對螢幕邊 / 其他 surface 邊的自動吸附對齊（platform 相位 1 = Mac / null 期）
//
// 語意：**邊緣吸附**——當使用者拖曳（承 E1-08）一個 surface、其邊緣**靠近**螢幕（容器）邊緣或
// **其他 surface 的邊緣**到一定程度時，自動把它**吸附對齊**到那條邊。吸附後的位置一律以 E1-07
// 的**宣告式 anchor 定位**（九宮具名錨點 + 容器尺寸正規化偏移）表達，故：
//   - 吸到**螢幕邊**者以對應**具名角 / 邊錨點 + 零偏移**承載——天然「貼齊該邊、隨容器縮放恆貼齊」
//     （承 E1-07「角 / 邊錨點天然產生邊緣對齊」的洞見）；
//   - 吸到**其他 surface 邊**或某軸未吸附者，以參考錨點 + **正規化偏移**精確承載該幾何位置
//     （round-trip：吸附後的 `AnchorSpec` 經 E1-07 `resolve` 落回同一矩形）。
//
// **吸附閾值可設**：以**容器尺寸的正規化分數**（比例，非硬編像素）表達「多近算靠近」，逐軸乘以
// 對應容器邊長換算為該軸的判定距離——故解析度 / 容器尺寸無關（NFR-02）。亦可用 E1-07 具名間距
// `Spacing` 設定閾值。可分別開關「吸螢幕邊」與「吸 surface 邊」。
//
// 相位 1 硬約束：**純幾何吸附邏輯**，注入式——無真實視窗 / 繪圖 / OS API、無 `#ifdef` /
// `win32` / `cocoa`。具體像素只在 `resolve` 邊界（呼叫端提供的容器 / 元件尺寸）出現，核心宣告式
// API **不出現絕對座標 / 數字 z-order**（NFR-02）。無效輸入（越界 anchor、非有限 / 負尺寸、
// 非有限 / 負閾值、非正容器）一律結構化回報，**不靜默、不崩潰**。
//
// 上游相依（已合併，可讀不可改）：
//   - E1-07 anchor 定位模型 → `Anchor` / `AnchorSpec` / `Offset` / `Size` / `ResolvedPlacement` /
//     `resolve` / `is_valid_anchor` / `is_finite_size` / `Spacing` / `spacing_fraction`：吸附後位置的
//     宣告式表達與落地（NFR-02 正解）。
//   - E1-08 自由拖曳與位置記憶 → `DraggableSurface` / `SurfaceId`：`snap_surface` 便利層讀取拖曳中
//     surface 的實時位置（`live_position`）並對其他具名 surface 的實時位置吸附（與拖曳整合）。
#ifndef DS_KERNEL_E1_09_EDGE_SNAPPING_HPP
#define DS_KERNEL_E1_09_EDGE_SNAPPING_HPP

#include <vector>

#include "anchor_model.hpp"       // E1-07（可讀不可改）
#include "draggable_surface.hpp"  // E1-08（可讀不可改）：DraggableSurface / SurfaceId

namespace ds::kernel {

// 吸附 / 幾何操作結果碼 —— 結構化、平台中立、不靜默（與 E1-03 `AlphaStatus` /
// E1-07 `AnchorStatus` 同風格）。
enum class SnapStatus {
    Ok,       // 計算成功（可能實際未吸附——見 SnapResult 的逐軸結果）
    Invalid,  // 前置條件不滿足：越界 anchor、非有限 / 負尺寸、非有限 / 負閾值、非正容器、無效目標、
              // 拖曳中 surface 無實時位置等
};

// 具名邊 —— 以**具名角色**表達吸附涉及的邊（NFR-02：不以數字表達）。
enum class Edge {
    Left,
    Right,
    Top,
    Bottom,
};

// 單軸吸附結果 —— 該軸最終吸附到哪一類參考（具名，非數字）。
enum class AxisSnap {
    None,         // 該軸未吸附（無夠近的邊）
    ScreenStart,  // 吸到螢幕（容器）起始邊：水平 = 左、垂直 = 上
    ScreenEnd,    // 吸到螢幕（容器）末端邊：水平 = 右、垂直 = 下
    Surface,      // 吸到某其他 surface 的邊
};

// 吸附設定 —— 閾值以**容器尺寸正規化分數**表達（NFR-02：比例，非硬編像素）；可分別開關來源。
struct SnapConfig {
    // 「多近算靠近」：容器尺寸的正規化分數 [0,1]。逐軸乘以對應容器邊長換算為判定距離。
    // 預設 0.02（= E1-07 具名間距 `Snug`），相對值故解析度無關。
    float threshold = 0.02f;
    bool to_screen = true;    // 是否吸附螢幕（容器）邊
    bool to_surfaces = true;  // 是否吸附其他 surface 的邊

    // 以 E1-07 具名間距設定閾值（NFR-02：具名等級，非數字）。
    static SnapConfig from_spacing(Spacing threshold, bool to_screen = true,
                                   bool to_surfaces = true);
};

// 具名邊查詢：某軸的吸附結果對應的具名螢幕邊（`horizontal` = true 為水平軸）。
//   - `ScreenStart` → 水平 Left / 垂直 Top；`ScreenEnd` → 水平 Right / 垂直 Bottom。
//   - `None` / `Surface` 無對應具名螢幕邊 → 回 false 且不觸碰 out（不靜默）。
bool screen_edge_of(AxisSnap axis_result, bool horizontal, Edge& out);

// 純幾何吸附結果 —— `snap_rect` 的輸出。`rect` 是吸附後的具體矩形（resolve 邊界，像素只在此出現）。
struct SnapResult {
    ResolvedPlacement rect;      // 吸附後矩形（x, y = 左上角；未吸附軸維持原值）
    AxisSnap x = AxisSnap::None;  // 水平軸吸附結果
    AxisSnap y = AxisSnap::None;  // 垂直軸吸附結果

    // 任一軸有吸附。
    bool snapped() const { return x != AxisSnap::None || y != AxisSnap::None; }
};

// 吸附目標（宣告式層）—— 以 E1-07 `AnchorSpec` + 元件尺寸表達某其他 surface 的位置；由 `snap`
// 在給定容器下 `resolve` 為矩形後參與吸附。
struct SnapTarget {
    AnchorSpec spec;
    Size element;
};

// E1-08 整合用具名目標 —— 以具名 `SurfaceId` + 元件尺寸指涉；`snap_surface` 讀取其實時位置。
struct SurfaceTarget {
    SurfaceId id;
    Size element;
};

// ---------------------------------------------------------------------------
// 純幾何核心（注入式；不觸碰 OS / 拖曳狀態）
// ---------------------------------------------------------------------------

// 把 `dragged` 矩形對「螢幕（容器）邊」與「targets 各矩形的邊」做最近邊吸附。
//   - 逐軸各自求「夠近（|delta| ≤ threshold·容器邊長）且最近」的對齊，套用位移；無夠近者不動該軸。
//   - 候選（水平；垂直同理）：dragged 左/右 對 螢幕左(0)/右(W)；對每個 target 的
//     左-左 / 右-右 / 左-右 / 右-左（後兩者為邊靠邊相鄰）。
//   - 非有限 threshold / threshold<0 / 非有限或負容器 / dragged 或任一 target 非有限或負尺寸 → Invalid。
// 成功回 Ok，out 填入吸附後矩形與逐軸結果（不靜默：Invalid 時不觸碰 out）。
SnapStatus snap_rect(const ResolvedPlacement& dragged,
                     const std::vector<ResolvedPlacement>& targets, const Size& container,
                     const SnapConfig& config, SnapResult& out);

// 宣告式吸附：把拖曳中 surface 的宣告式位置 `dragged`（+ 其元件尺寸）對容器邊 / 各 target 吸附，
// 並把結果**再表達為 `AnchorSpec`**（吸螢幕邊 → 具名角 / 邊錨點 + 零偏移；吸 surface 邊 / 未吸附 →
// 參考錨點 + 正規化偏移精確承載）。round-trip：`out` 經 E1-07 `resolve` 落回 `snap_rect` 的矩形。
//   - `dragged` 無效（越界 anchor / 非有限 offset）/ 容器非正（≤0）或非有限 / 元件非有限或負 /
//     任一 target 無效 / 閾值非有限或負 → Invalid（不觸碰 out）。
SnapStatus snap(const AnchorSpec& dragged, const Size& element,
                const std::vector<SnapTarget>& targets, const Size& container,
                const SnapConfig& config, AnchorSpec& out);

// ---------------------------------------------------------------------------
// EdgeSnapping —— 持有一組 `SnapConfig` 的吸附服務（可設閾值 / 來源開關）。
//
// 純值型、無平台狀態；`snap(...)` 以其設定委由自由函式 `snap` 計算。與 E1-07 `AnchorLayout` /
// E1-08 `DraggableSurface` 的服務風格一致。
// ---------------------------------------------------------------------------
class EdgeSnapping {
public:
    EdgeSnapping() = default;
    explicit EdgeSnapping(SnapConfig config) : config_(config) {}

    const SnapConfig& config() const noexcept { return config_; }
    void set_config(SnapConfig config) noexcept { config_ = config; }

    // 以本服務設定對 `dragged`（+ 元件尺寸）做邊緣吸附，結果表達為 `AnchorSpec`（見自由函式 `snap`）。
    SnapStatus snap(const AnchorSpec& dragged, const Size& element,
                    const std::vector<SnapTarget>& targets, const Size& container,
                    AnchorSpec& out) const {
        return ds::kernel::snap(dragged, element, targets, container, config_, out);
    }

private:
    SnapConfig config_;
};

// ---------------------------------------------------------------------------
// E1-08 整合便利層
// ---------------------------------------------------------------------------

// 對一個**拖曳中**（或已註冊）的具名 surface 做邊緣吸附：讀取 `surfaces` 內 `dragged` 的**實時位置**
// （E1-08 `live_position`）作為被吸附者，讀取各 `SurfaceTarget` 的實時位置作為吸附目標，回吸附後
// 的 `AnchorSpec`。典型用法：拖曳中每次 `drag_to` 後呼叫本函式取得吸附位置，再以該位置 `drag_to`
// 回寫、`end_drag` 記住吸附後位置。
//   - `dragged` 無實時位置（未註冊且未拖曳）→ Invalid。
//   - 目標 id 與 `dragged` 相同、或該目標無實時位置 → **略過**（非錯誤；不列為吸附候選）。
//   - 其餘錯誤語意同 `snap`。
SnapStatus snap_surface(const EdgeSnapping& snapping, const DraggableSurface& surfaces,
                        const SurfaceId& dragged, const Size& dragged_element,
                        const std::vector<SurfaceTarget>& targets, const Size& container,
                        AnchorSpec& out);

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_09_EDGE_SNAPPING_HPP
