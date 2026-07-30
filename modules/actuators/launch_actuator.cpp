// E3-02 啟動程式 / 開檔 / 網頁搜尋致動器 — 實作
//
// 致動器、後端與請求型別的邏輯皆為 header-only inline（純資料操作、無平台分支、無真實 exec）。
// 本 TU 僅提供契約版本標記一個實體符號，使 STATIC 程式庫非空並集中版本字串。
#include "launch_actuator.hpp"

namespace ds::actuators {

const char* contract_version() noexcept {
    // 啟動 / 開檔 / 網頁搜尋致動器契約版本。API 面若不相容變更，遞增主版本。
    return "e3_02/1.0.0";
}

}  // namespace ds::actuators
