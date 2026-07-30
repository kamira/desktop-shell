// E3-11 螢幕擷取致動器 — 實作
//
// 致動器、後端與請求 / 結果型別的邏輯皆為 header-only inline（純資料操作、無平台分支、
// 無真實擷取呼叫）。本 TU 僅提供契約版本標記一個實體符號，使 STATIC 程式庫非空並集中版本字串。
#include "screen_capture_actuator.hpp"

namespace ds::actuators {

const char* screen_capture_contract_version() noexcept {
    // 螢幕擷取致動器契約版本。API 面若不相容變更，遞增主版本。
    // **前綴命名**避免與同命名空間其他致動器（E3-02 contract_version）符號衝突。
    return "e3_11/1.0.0";
}

}  // namespace ds::actuators
