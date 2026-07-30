// E4-23 容器裁切與遮罩 — 介面（engine 層 / 子系統 render，phase 1 平台中立）
//
// 語意：**裁切（clip）與遮罩（mask）**渲染工具——子內容裁切在容器邊界內
// （矩形 / 圓角矩形 / 任意路徑），並以 alpha 遮罩控制可見區域；產出**宣告式**
// 裁切 / 遮罩描述（`render_model()`），不綁任何真實繪圖 API、不碰 OS。
//
// 建於上游 E1-03 的 surface / 容器具名指涉慣例之上（`ds::kernel::SurfaceId`）：
// 每個裁切 / 遮罩描述以**具名容器 id** 指涉，不以數字 handle / index 指涉。
//
// NFR-02（無絕對畫面座標 / 數字 z-order）：本單元所有幾何一律以**容器自身局部
// 正規化座標** [0,1]x[0,1] 表達（`NormPoint` / `NormRect`），是「相對於該容器
// 自己的比例」，不是螢幕像素座標；圓角半徑以「容器短邊的比例」表達，同樣是比例
// 而非像素。沒有任何數字 z-order：疊層順序不在本單元語意內（那是 SurfaceLayer
// 具名角色的職責，見上游 E1-24）。
//
// 相位 1 硬約束：無 `#ifdef` / win32 / cocoa / 真實繪圖 API；純記憶體宣告式狀態。
#ifndef DS_RENDER_E4_23_CLIP_MASK_HPP
#define DS_RENDER_E4_23_CLIP_MASK_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "null_backend.hpp"  // E1-03 上游（可讀不可改）：ds::kernel::SurfaceId 具名指涉慣例

namespace ds::render {

// 容器 / 區域的具名識別碼——沿用上游具名 surface 指涉慣例（NFR-02：具名，非數字）。
using ContainerId = ds::kernel::SurfaceId;

// 裁切形狀——具名列舉，決定 `ClipRegion` 中哪些欄位有意義。
enum class ClipShape {
    Rect,         // 軸對齊矩形：用 `rect`
    RoundedRect,  // 圓角矩形：用 `rect` + `corner_radius`
    Path,         // 任意路徑（隱含封閉多邊形）：用 `path`
};

// 操作結果碼——與其他子系統（如 E1-03 `AlphaStatus`）同語意：明確回報，不靜默。
enum class ClipStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件不滿足（空容器 id、無效裁切區域、無效遮罩、迴圈父代等）
};

// 容器自身**局部正規化座標**中的一點：[0,1] x [0,1]。
// 是「相對於該容器自己的比例位置」，**不是**絕對螢幕像素座標（NFR-02）。
struct NormPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// 容器局部正規化空間中的軸對齊矩形：0<=x0<x1<=1、0<=y0<y1<=1。
struct NormRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 1.0f;
    float y1 = 1.0f;
};

// 一份裁切區域描述。哪些欄位有意義取決於 `shape`：
//   Rect        -> rect
//   RoundedRect -> rect + corner_radius（容器短邊（min(width,height)）的比例，[0, 0.5]）
//   Path        -> path（>=3 個正規化點，隱含首尾相連的封閉多邊形）
struct ClipRegion {
    ClipShape shape = ClipShape::Rect;
    NormRect rect;
    float corner_radius = 0.0f;
    std::vector<NormPoint> path;
};

// 遮罩種類——具名，非數字。
enum class MaskKind {
    Uniform,       // 整個容器套用單一 alpha 乘數（`coverage`）
    NamedPattern,  // 具名圖樣（如 "fade-edge" / "vignette"）；`coverage` 為整體覆蓋率提示
};

// 一份遮罩來源描述。`kind == NamedPattern` 時 `pattern_name` 必須非空。
struct MaskSource {
    MaskKind kind = MaskKind::Uniform;
    float coverage = 1.0f;  // 正規化 [0,1] alpha 乘數（1 = 完全不透明 / 完全可見）
    std::string pattern_name;
};

// ---------------------------------------------------------------------------
// ClipMaskService —— 容器裁切與遮罩的宣告式服務層。
//
// 純記憶體狀態：以具名 `ContainerId` 為鍵記錄每個容器的裁切區域、遮罩來源、
// 巢狀父容器關係。提供命中 / 可見判定（`is_visible`），與供渲染管線消費的
// 宣告式快照（`render_model`）。全部操作平台中立，不綁任何真實繪圖 API。
// ---------------------------------------------------------------------------
class ClipMaskService {
public:
    // --- 裁切（clip）---
    // 設定 / 覆寫某容器的裁切區域。容器 id 為空、或 region 依 `shape` 檢查為無效
    // （見下）一律回 `Invalid`，**不**留下半份狀態（不靜默接受無效裁切區域）。
    //   Rect / RoundedRect：rect 需 0<=x0<x1<=1、0<=y0<y1<=1（皆為有限值）；
    //                        RoundedRect 另需 corner_radius 為有限值且落在 [0, 0.5]。
    //   Path：path.size() >= 3，且每點座標為有限值並落在 [0,1] 內。
    ClipStatus set_clip(const ContainerId& container, const ClipRegion& region);
    // 清除某容器的裁切設定（清除後視為「無裁切 = 全域可見」）。未曾設定亦安全（回 Ok）。
    ClipStatus clear_clip(const ContainerId& container);
    // 該容器是否設有裁切區域。
    bool has_clip(const ContainerId& container) const;
    // 查詢某容器目前的裁切區域；未設定回 nullptr。指標於下次變更該容器狀態前有效。
    const ClipRegion* clip_region(const ContainerId& container) const;

    // --- 巢狀裁切（nested clipping）---
    // 將 `container` 的父容器設為 `parent`：`is_visible()` 會沿父代鏈同時套用祖先
    // 的裁切區域（同一 local-normalized 座標假設，見 is_visible 說明）。
    // container / parent 為空、或 container == parent（自我父代）、或會形成迴圈父代
    // 鏈，一律回 `Invalid` 且不建立關係。父容器不需事先設過裁切（可僅作邏輯分組）。
    ClipStatus set_parent(const ContainerId& container, const ContainerId& parent);
    // 清除某容器的父代關係（成為根容器）。未曾設定亦安全（回 Ok）。
    ClipStatus clear_parent(const ContainerId& container);
    // 查詢某容器目前的父容器 id；無父代回空字串。
    ContainerId parent_of(const ContainerId& container) const;

    // --- 遮罩（mask）---
    // 套用 / 覆寫某容器的 alpha 遮罩來源。容器 id 為空、coverage 非有限值、或
    // `kind == NamedPattern` 但 `pattern_name` 為空，一律回 `Invalid`。成功時
    // coverage 會 clamp 至 [0,1]。
    ClipStatus apply_mask(const ContainerId& container, const MaskSource& mask);
    // 清除某容器的遮罩（清除後視為「無遮罩 = 完全可見」）。未曾設定亦安全（回 Ok）。
    ClipStatus clear_mask(const ContainerId& container);
    // 該容器是否設有遮罩。
    bool has_mask(const ContainerId& container) const;
    // 查詢某容器目前的遮罩來源；未設定回 nullptr。
    const MaskSource* mask_source(const ContainerId& container) const;

    // --- 命中 / 可見判定 ---
    // `point` 為 `container` 自身局部正規化座標中的一點（超出 [0,1]x[0,1] 或非
    // 有限值一律視為不可見）。判定條件（全部通過才回 true）：
    //   1. point 落在 container 自身裁切區域內（未設裁切 = 視為全域可見）；
    //   2. point 亦落在沿 `set_parent` 父代鏈每一層祖先的裁切區域內——巢狀裁切採
    //      「共享具名局部座標」的簡化假設：同一 point 同時檢驗自身與祖先鏈的裁切
    //      形狀（宣告式描述服務不做真實座標系轉換，那是渲染管線 / 變形數學的職責）；
    //   3. container 遮罩 coverage > 0（未設遮罩 = 視為完全可見）。
    bool is_visible(const ContainerId& container, const NormPoint& point) const;

    // --- render model（宣告式快照，供渲染管線消費）---
    struct RenderEntry {
        ContainerId container;
        bool has_clip = false;
        ClipRegion clip;    // 僅在 has_clip 為 true 時有效
        bool has_mask = false;
        MaskSource mask;    // 僅在 has_mask 為 true 時有效
        ContainerId parent;  // 無父代則為空字串
    };
    // 回傳目前所有已知容器（曾設裁切 / 遮罩 / 父代關係其一者）的宣告式快照，
    // 依容器具名鍵字典序排序——**決定性**輸出，不依賴建立順序 / 數字 index。
    std::vector<RenderEntry> render_model() const;

private:
    std::map<ContainerId, ClipRegion> clips_;
    std::map<ContainerId, MaskSource> masks_;
    std::map<ContainerId, ContainerId> parents_;
};

// --- 純函式幾何工具（供實作與測試共用；平台中立、確定性）---

// region 依其 shape 是否為結構有效的裁切區域描述（見 set_clip 文件的檢查規則）。
bool is_valid_clip_region(const ClipRegion& region);

// point 是否落在 region 描述的裁切形狀內（point 需已知落在 [0,1]x[0,1] 內；
// 呼叫端負責該前置檢查，本函式只做形狀幾何判定）。
bool point_in_clip_region(const ClipRegion& region, const NormPoint& point);

}  // namespace ds::render

#endif  // DS_RENDER_E4_23_CLIP_MASK_HPP
