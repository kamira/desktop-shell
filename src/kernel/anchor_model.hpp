// E1-07 anchor 定位模型 — 具名錨點 + 相對偏移的宣告式定位（platform 相位 1 = Mac / null 期）
//
// 語意：以 **anchor（錨點）** 表達元件 / surface 的定位——用**九宮具名錨點**
// （top-left / center / bottom-right …）搭配**相對父容器 / 螢幕邊緣的正規化偏移**描述位置，
// **取代絕對像素座標**（NFR-02 的正解）。核心宣告式 API 內**不出現任何絕對座標 / 數字 z-order**：
//   - 位置以具名 `Anchor`（九宮）表達參考點；
//   - 偏移以**正規化分數**（容器尺寸的比例，非像素）或**具名間距** `Spacing` 表達；
//   - 具體像素數字**只在 `resolve()` 這個純佈局計算的邊界**出現——由呼叫端提供的容器 / 元件尺寸
//     推導而得（與 E1-03 以正規化 opacity 承載透明度、只在合成邊界化為具體值同理）。
//
// 建於上游 E1-03 / E1-24 的具名 surface 模型之上：surface 一律以具名 `SurfaceId` 指涉（NFR-02），
// 本單元的 `AnchorLayout` 服務把「具名 surface → 定位規格」配對起來並在給定容器尺寸下解析。
//
// 相位 1 硬約束：純佈局邏輯，無真實視窗 / 繪圖 API、無 `#ifdef` / `win32` / `cocoa`。
// 無效 anchor / 非有限尺寸一律結構化報錯（回 `Invalid`），**不靜默**。
#ifndef DS_KERNEL_E1_07_ANCHOR_MODEL_HPP
#define DS_KERNEL_E1_07_ANCHOR_MODEL_HPP

#include <cstddef>
#include <vector>

#include "null_backend.hpp"  // E1-24（經 E1-03 傳遞，可讀不可改）：具名 SurfaceId 模型

namespace ds::kernel {

// 九宮具名錨點 —— 以具名角色表達定位參考點（NFR-02：不以絕對像素座標指涉）。
//
// 同一個 anchor 同時作為**容器參考點**與**元件自身參考點**：例如 `BottomRight` 表示「把元件的
// 右下角對齊容器的右下角」，故角 / 邊錨點天然產生**邊緣對齊**（元件貼齊該邊，不溢出）。
enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// 錨點是否為合法列舉值（防禦：呼叫端可能以整數 static_cast 傳入越界值）。
bool is_valid_anchor(Anchor a);

// 錨點的**正規化分數座標** [0,1]×[0,1]（0 = 起邊 / 上緣、0.5 = 中線、1 = 末邊 / 下緣）。
// 越界 anchor 回 false 且不觸碰 out 參數（不靜默）。
bool anchor_fraction(Anchor a, float& fx, float& fy);

// 具名相對間距 —— 以**具名等級**表達偏移量（NFR-02：具名間距，非像素數字）。
// 每一等級對應一個**容器尺寸的正規化分數**（見 spacing_fraction）。
enum class Spacing {
    None,      // 0
    Tight,     // 極小
    Snug,      // 小
    Cozy,      // 中
    Roomy,     // 大
    Spacious,  // 極大
};

// 具名間距 → 正規化分數（容器尺寸的比例）。未知值保守回 0。
float spacing_fraction(Spacing s);

// 相對偏移 —— dx / dy 為**容器尺寸的正規化分數**（比例值；非像素座標、非數字 z-order）。
// 正 dx = 向右、正 dy = 向下（容器座標系）。
struct Offset {
    float dx = 0.0f;
    float dy = 0.0f;

    static Offset none() { return {}; }
    // 以具名間距構造偏移（水平 / 垂直各自指定等級；預設正向）。
    static Offset from_spacing(Spacing sx, Spacing sy);
};

// 由**錨定邊**向容器**內側**縮排指定的具名間距：
//   - 靠左邊（fx=0）→ dx = +s；靠右邊（fx=1）→ dx = -s；水平置中（fx=0.5）→ dx = 0。
//   - 上 / 下緣同理作用於 dy。無效 anchor 回零偏移。
// 讓「距邊緣一點點」用**具名間距**表達，而非硬編像素（NFR-02）。
Offset inset_from(Anchor anchor, Spacing amount);

// 定位規格 —— **宣告式核心**：具名錨點 + 相對偏移。這是取代 (x, y) 絕對座標的表達方式。
struct AnchorSpec {
    Anchor anchor = Anchor::Center;
    Offset offset = {};
};

// 尺寸 —— `resolve()` 的**輸入**（容器 / 元件尺寸由呼叫端於解析時提供，例如螢幕 / 父容器 /
// 元件量測值）。具體數字只在此計算邊界出現，非宣告式定位 API 的一部分。
struct Size {
    float width = 0.0f;
    float height = 0.0f;
};

// 尺寸是否有效：width / height 皆為有限值且非負。
bool is_finite_size(const Size& s);

// 解析後的具體佈局 —— `resolve()` 的**輸出**：元件在容器座標系內的矩形（x, y = 左上角）。
struct ResolvedPlacement {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// 操作結果碼 —— 與 E1-03 `AlphaStatus` 同風格（平台中立、結構化、不靜默）。
enum class AnchorStatus {
    Ok,       // 解析成功
    Invalid,  // 前置條件不滿足（越界 anchor、非有限 offset / 尺寸、空 / 未知 SurfaceId 等）
};

// 純佈局計算：把宣告式 `AnchorSpec` 在給定容器 / 元件尺寸下解析為具體 `ResolvedPlacement`。
//
// 公式（fx, fy = 錨點正規化分數）：
//   x = fx * (container.w - element.w) + offset.dx * container.w
//   y = fy * (container.h - element.h) + offset.dy * container.h
// 角 / 邊錨點（無偏移）因此使元件**貼齊對應邊緣**、置中錨點使元件**置中**。
//
// 越界 anchor / 非有限 offset / 非有限或負的容器或元件尺寸 → 回 Invalid 且不寫 out（不靜默）。
// 成功 → 回 Ok，out 填入元件矩形。
AnchorStatus resolve(const AnchorSpec& spec, const Size& container, const Size& element,
                     ResolvedPlacement& out);

// 便利：把元件視為零尺寸的**點**，只解析錨點（含偏移）在容器內的位置。
// 錯誤語意同 resolve。
AnchorStatus resolve_point(const AnchorSpec& spec, const Size& container, float& x, float& y);

// ---------------------------------------------------------------------------
// AnchorLayout —— 具名 surface 的錨點定位服務層。
//
// 承 E1-03 / E1-24 的具名 surface 模型：以具名 `SurfaceId` 為鍵，維護每個 surface 的
// `AnchorSpec`（宣告式定位），並可在給定容器 / 元件尺寸下解析出具體佈局。純記憶體、純佈局，
// 不觸碰後端 / OS。與 `AlphaSurfaceService` 的記錄配對風格一致（順序即指派順序，永不以數字
// index 對外暴露）。
// ---------------------------------------------------------------------------
class AnchorLayout {
public:
    // 指派 / 更新某具名 surface 的定位規格。
    //   - id 為空、anchor 越界、offset 非有限 → Invalid（不記錄）。
    //   - 成功 → Ok（同 id 再次指派為就地更新，不新增第二筆）。
    AnchorStatus place(const SurfaceId& id, const AnchorSpec& spec);

    // 移除某具名 surface 的定位規格；未知 id 回 Invalid（不崩潰），成功回 Ok。
    AnchorStatus remove(const SurfaceId& id);

    // 該具名 surface 是否已有定位規格。
    bool has_placement(const SurfaceId& id) const { return find(id) != nullptr; }
    // 目前登錄的定位規格數量。
    std::size_t placement_count() const noexcept { return records_.size(); }

    // 查詢某具名 surface 的定位規格；未知 id 回 nullptr。指標於該筆存活期間有效。
    const AnchorSpec* spec_of(const SurfaceId& id) const;

    // 在給定容器 / 元件尺寸下解析某具名 surface 的具體佈局。
    // 未知 id → Invalid；其餘錯誤語意同 resolve()。成功 → Ok，out 填入。
    AnchorStatus resolve_for(const SurfaceId& id, const Size& container, const Size& element,
                             ResolvedPlacement& out) const;

private:
    // 以具名鍵配對記錄（順序即指派順序，永不以數字 index 對外暴露）。
    struct Record {
        SurfaceId id;
        AnchorSpec spec;
    };
    Record* find(const SurfaceId& id);
    const Record* find(const SurfaceId& id) const;

    std::vector<Record> records_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_07_ANCHOR_MODEL_HPP
