// E1-05 具名碰撞區域 → 事件參數 — 介面（platform 相位 1 = Mac / null 期）
//
// 語意：在 surface 上定義一組**具名碰撞區域（named hit regions / hotspots）**——例如按鈕的
// 不同部位、地圖的不同區塊——用上游 E1-04 的幾何命中測試判定某點落在哪個具名區域，並把該區域的
// **具名 id + 相關參數**帶出，供呼叫端組裝事件（把 region 名 + 參數放進事件負載）使用。
//
// 相位 1（Mac / null 期）硬約束：
//   - 純幾何邏輯，建於上游 E1-04 `HitTester` / `Shape` / `LocalPoint` 之上；無真實視窗系統 /
//     OS / 繪圖 API。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支；跨平台性由 API 面約束保證。
//   - **NFR-02**：區域一律**具名** + **本地 / 相對幾何**（沿用 E1-04 `LocalPoint` / `Shape`），
//     無畫面絕對座標、無數字 z-order——重疊區域依**加入序**決定優先（後加入者為上，呼應 E1-04
//     `topmost_hit` 同層「後宣告者在上」的慣例），不引入任何數字層級 / index。
//   - 無效形狀（`add_region` 時以 E1-04 `HitTester::is_valid` 檢驗）/ 空具名 / 重複具名 →
//     拒絕新增（報錯不靜默：`add_region` 回 `false`，呼叫端可據此得知失敗，不悄悄忽略）。
//   - 參數（`RegionParams`）為**封閉型別集合**（bool / int64 / double / string）的具名字典，
//     非任意 blob（`std::any` / `void*`）——需要複合結構時呼叫端以多個具名參數表達（NFR-02
//     「具名 / 型別安全」精神的延伸；本層自有型別，不跨層依賴 engine/command 之 `CommandArgs`，
//     platform 層不得反向依賴上層 command/engine 單元）。
//
// 建於上游之上（可讀不可改）：
//   - E1-04 `hit_test.hpp`：`LocalPoint`（本地 / 相對座標）/ `Shape`（形狀）/ `HitStatus`
//     （結果碼，不用例外）/ `HitTester`（`is_valid` / `hit_test`）。
#ifndef DS_KERNEL_E1_05_NAMED_REGION_MAP_HPP
#define DS_KERNEL_E1_05_NAMED_REGION_MAP_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "hit_test.hpp"  // E1-04（上游，可讀不可改）：LocalPoint / Shape / HitStatus / HitTester

namespace ds::kernel {

// 具名區域參數的單一具名值 —— 封閉型別集合（非任意 blob）：呼應 NFR-02「具名 / 型別安全」精神。
using RegionParamValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;

// 具名區域參數字典 —— 鍵為具名字串；供事件系統消費（如按鈕不同部位的行為代碼、地圖區塊的屬性）。
using RegionParams = std::map<std::string, RegionParamValue>;

// 一次 `NamedRegionMap::hit()` 查詢的結果 —— 狀態 + 是否命中 + 命中區域的具名 id + 該區域參數。
//
// 與 E1-04 `TopmostHit` 同慣例（結構化結果，不用例外 / `std::optional`）：
//   - `status == Invalid`：查詢點非有限值（報錯不靜默）；`hit` / `name` / `params` 皆無意義。
//   - `status == Ok` 且 `hit == false`：有效查詢、無任何區域命中（結構化空結果，`name` 為空、
//     `params` 為空字典）。
//   - `status == Ok` 且 `hit == true`：命中之具名區域見 `name` / `params`。
struct RegionHit {
    HitStatus status = HitStatus::Invalid;
    bool hit = false;
    std::string name;
    RegionParams params;
};

// ---------------------------------------------------------------------------
// NamedRegionMap —— 具名碰撞區域集合（建於 E1-04 `HitTester` 之上）。
//
// 在單一 surface 的本地座標系內登記一組具名區域（形狀 + 參數）；`hit()` 判定某本地點落在
// 哪個具名區域，回傳該區域的具名 id + 參數，供呼叫端組裝事件。重疊區域依**加入序**決定優先
// （後加入者為上）。全以具名字串指涉，不對外暴露任何數字 index / handle（NFR-02）。
// ---------------------------------------------------------------------------
class NamedRegionMap {
public:
    // 新增一個具名區域。`name` 為空、形狀無效（見 E1-04 `HitTester::is_valid`）、或 `name`
    // 已存在 → 不新增，回 `false`（報錯不靜默）。成功新增回 `true`。
    bool add_region(const std::string& name, Shape shape, RegionParams params = {});

    // 移除具名區域；未知 `name` 回 `false`，不崩潰。
    bool remove_region(const std::string& name);

    // 該具名區域是否存在。
    bool has_region(const std::string& name) const;

    // 目前登記的具名區域數量。
    std::size_t region_count() const;

    // 命中查詢：判定本地點落在哪個具名區域（重疊時取**加入序最後者**）。
    // 查詢點非有限值 → `status = Invalid`（報錯不靜默）；否則 `status = Ok`，`hit` 表示是否有
    // 任一區域命中，命中時 `name` / `params` 為該區域資料，未命中時兩者皆為空。
    RegionHit hit(const LocalPoint& point) const;

private:
    struct RegionRecord {
        Shape shape;
        RegionParams params;
    };

    // 以具名鍵配對記錄，順序即加入順序（永不以數字 index 對外暴露；比照上游 E1-24
    // `NullKernelBackend::surfaces_` 之 `vector<pair<具名鍵, 記錄>>` 慣例）。
    std::vector<std::pair<std::string, RegionRecord>> regions_;
    HitTester tester_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_05_NAMED_REGION_MAP_HPP
