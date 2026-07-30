// E1-16 螢幕邊緣熱區（edge/corner hot zones）— 介面（platform 相位 1 = Mac / null 期）
//
// 語意：定義螢幕四邊、四角的觸發熱區（如滑到左邊緣觸發側欄、滑到角落觸發某動作）。用上游
// E1-04 的 HitTester 判定滑鼠本地點是否落入某已註冊熱區的矩形範圍，命中則回傳觸發之熱區
// （具名邊/角 + 呼叫端註冊的具名動作），供輸入路由層決定實際行為。
//
// 相位 1（Mac / null 期）硬約束：
//   - 純幾何 + 資料註冊邏輯，無真實視窗系統 / OS 事件迴圈；本單元只回報「觸發了誰」，
//     不執行動作本身（相位 1 無真實側欄 / 真實 UI 可觸發）。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支。
//   - NFR-02：邊/角一律具名（`EdgeHotZone`），熱區厚度一律以**螢幕比例**表達
//     （`thickness_ratio`，0 < ratio <= 1），無絕對像素座標；`ScreenExtent` 僅表達螢幕**尺寸**
//     （與 E1-04 `Shape` 的 width/height 同語意），不表達螢幕於桌面的絕對位置。
//   - 無效邊 / 無效厚度比例：`register_zone` 回結構化 `HotZoneStatus`（非 Ok），報錯不靜默。
//
// 建於上游之上（可讀不可改）：
//   - E1-04 `hit_test.hpp`：`LocalPoint`（本地點）/ `HitTester`（幾何點內判定）/ `make_rect`。
#ifndef DS_KERNEL_E1_16_EDGE_HOT_ZONE_HPP
#define DS_KERNEL_E1_16_EDGE_HOT_ZONE_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）：LocalPoint / HitTester / Shape / make_rect

namespace ds::kernel {

// 具名邊 / 角（NFR-02：無數字方位，一律具名）。
enum class EdgeHotZone {
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// 螢幕尺寸（extent，非絕對位置）——熱區厚度以此為分母換算比例。
// 與 E1-04 `Shape` 的 width/height 同語意：局部原點 (0,0) 到 (width, height)。
struct ScreenExtent {
    float width = 0.0f;
    float height = 0.0f;
};

// 具名動作標籤——呼叫端定義語意（如 "open_sidebar"）；本單元只負責判定觸發，不執行動作。
using HotZoneAction = std::string;

// `register_zone` 的結果碼——無效輸入報結構化錯誤，不靜默略過（報錯不靜默）。
enum class HotZoneStatus {
    Ok,
    InvalidZone,       // 邊/角不屬於具名集合（如未知列舉值）
    InvalidThickness,  // 厚度比例非有限、<= 0 或 > 1
};

// 觸發結果——`test()` 命中時回傳，含觸發之具名邊/角與其註冊的具名動作。
struct TriggeredHotZone {
    EdgeHotZone zone = EdgeHotZone::Left;
    HotZoneAction action;
};

// 邊/角是否屬於具名集合（供 `register_zone` 內部與外部呼叫端共用）。
bool is_named_zone(EdgeHotZone zone);

// ---------------------------------------------------------------------------
// EdgeHotZoneRegistry —— 螢幕邊緣 / 角落熱區註冊表。
//
// 同一邊/角可重複註冊；命中判定時同一類別（皆角或皆邊）內以**較晚註冊者為準**（語意同
// E1-04 topmost 的宣告順序後者為上）。`test()` 用 E1-04 `HitTester` 判定本地點是否落入
// 某已註冊熱區換算出的矩形範圍：
//   - **角落熱區優先於邊熱區**（同時落入某角與某邊時，回角落）；
//   - 同類別中，多個熱區重疊，以較晚註冊者為準；
//   - 螢幕尺寸或點非有限 / 非正 → 視為幾何前提不成立，回 `nullopt`（無熱區可觸發）；
//     這類輸入的報錯已於 `register_zone`（設定期）把關，`test()`（查詢期）維持固定的
//     `optional<觸發熱區>` 回傳形狀，不额外引入狀態碼。
// 無狀態相依，僅持有已註冊清單；非執行緒安全（呼叫端自行序列化寫入）。
// ---------------------------------------------------------------------------
class EdgeHotZoneRegistry {
public:
    // 註冊一個具名邊/角的熱區：厚度以螢幕比例表達（0 < thickness_ratio <= 1）。
    // 無效邊或無效厚度 → 回對應 `HotZoneStatus`（非 Ok），不註冊、不靜默。
    HotZoneStatus register_zone(EdgeHotZone zone, float thickness_ratio, HotZoneAction action);

    // 判定本地點（相對螢幕本地原點 (0,0) 到 (screen.width, screen.height)）是否落入任一已
    // 註冊熱區；命中回傳觸發之熱區，否則 `nullopt`。
    std::optional<TriggeredHotZone> test(const LocalPoint& point, const ScreenExtent& screen) const;

    // 目前已註冊的熱區數量（供測試 / 診斷）。
    std::size_t size() const;

private:
    struct Entry {
        EdgeHotZone zone = EdgeHotZone::Left;
        float thickness_ratio = 0.0f;
        HotZoneAction action;
    };

    std::vector<Entry> zones_;
    HitTester tester_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_16_EDGE_HOT_ZONE_HPP
