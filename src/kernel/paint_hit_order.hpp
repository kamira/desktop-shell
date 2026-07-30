// E1-06 命中與繪製層序 — 介面（platform 相位 1 = Mac / null 期）
//
// 語意：**確保命中層序與繪製層序一致**——給定一組具名 surface 的**繪製順序**（painter's
// algorithm 慣例：陣列順序即繪製順序，後繪製者疊在先繪製者之上），命中測試依**繪製反序**
// 判定（最後繪製 / 視覺最上層者最先被檢查、第一個命中者即為結果），保證「視覺最上層者」與
// 「互動最上層者」恆一致——不會出現畫面上蓋在最上面的東西，點擊卻穿透命中到下面那層。
//
// 底層幾何 / alpha 判定完全交由上游 E1-04 `HitTester::hit_test_alpha`；本單元不重新實作任何
// 幾何邏輯，只決定「依什麼順序去問 E1-04」。與 E1-04 `topmost_hit`（具名圖層 rank + 宣告順序
// 為主序）並存但用途不同：`topmost_hit` 依**具名圖層語意**排序；本單元依呼叫端提供的**顯式
// 繪製順序**排序——當繪製順序本身就是依具名圖層 + 宣告順序組成時，兩者結果一致（見測試）。
//
// 相位 1（Mac / null 期）硬約束：
//   - 純幾何 / 序列邏輯，無真實視窗系統 / OS / 繪圖 API；不得出現 `#ifdef _WIN32` /
//     `win32` / `cocoa` 等平台分支。
//   - **NFR-02**：繪製順序以**陣列宣告順序**表達（具名 surface 序列），對外不暴露索引 /
//     數字 z-order；具名圖層（`SurfaceLayer`，上游 E1-24）僅為個別 surface 的屬性、透過
//     E1-04 參與 alpha / 命中判定，不是本單元的排序依據。
//   - 無效形狀 / 座標**報錯不靜默**（回結構化 `HitStatus::Invalid`，與 E1-04 語意一致）。
//
// 建於上游之上（可讀不可改）：
//   - E1-04 `hit_test.hpp`：`HitTester` / `HitSurface` / `LocalPoint` / `HitStatus` /
//     `HitResult` / `TopmostHit`。
#ifndef DS_KERNEL_E1_06_PAINT_HIT_ORDER_HPP
#define DS_KERNEL_E1_06_PAINT_HIT_ORDER_HPP

#include <cstddef>
#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）

namespace ds::kernel {

// ---------------------------------------------------------------------------
// PaintHitOrder —— 命中層序與繪製層序一致的命中測試器（相位 1：純序列邏輯 + E1-04 幾何）。
//
// 持有目前的繪製順序（一份具名 surface 序列），命中測試一律依**繪製反序**（最後繪製 /
// 視覺最上層先）逐一以 `HitTester::hit_test_alpha` 判定。改變繪製順序（`set_paint_order`）
// 會立即反映到後續命中測試——保證兩者**恆一致**，不需呼叫端自行同步。
// ---------------------------------------------------------------------------
class PaintHitOrder {
public:
    // 設定目前的繪製順序（覆蓋既有）。陣列順序即繪製順序：索引 0 最先繪製（視覺最底層），
    // 最後一個元素最後繪製（視覺最上層）。空序列合法（代表無任何 surface）。
    void set_paint_order(std::vector<HitSurface> surfaces);

    // 目前的繪製順序（唯讀，繪製序：先繪製在前、後繪製 / 最上層在後）。
    const std::vector<HitSurface>& paint_order() const noexcept { return surfaces_; }

    // 目前繪製順序內的 surface 數量。
    std::size_t size() const noexcept { return surfaces_.size(); }

    // 命中測試：依**繪製反序**（視覺最上層先）逐一以 `HitTester::hit_test_alpha` 判定，
    // 回傳第一個命中者（即視覺最上層的命中者），保證「視覺最上者即互動最上者」。
    //   - 逐一檢查全部 surface（不因已找到命中而提前跳出，語意同 E1-04 `topmost_hit`）；
    //     任一 surface 形狀 / 座標無效 → 整體回 `HitStatus::Invalid`（報錯不靜默）。
    //   - 無任何命中 → status=Ok、hit=false、id 為空。
    //   - 重疊時：依繪製反序，第一個（也就是繪製上最後、視覺最上層）命中者勝出。
    TopmostHit hit_topmost(const LocalPoint& point) const;

private:
    // 繪製順序（索引 0 = 最先繪製 / 最底層；末端 = 最後繪製 / 最上層）。
    std::vector<HitSurface> surfaces_;
    // 無狀態幾何 / alpha 判定器（複用 E1-04，不重新實作任何幾何邏輯）。
    HitTester tester_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_06_PAINT_HIT_ORDER_HPP
