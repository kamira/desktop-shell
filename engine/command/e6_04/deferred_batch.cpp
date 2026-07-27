// E6-04 延遲與批次執行 — 實作
//
// 排程器、批次與結果的邏輯皆為 header-only inline（純資料操作、無平台分支、無真實時間）。
// 本 TU 提供延遲/批次契約版本標記一個實體符號，使 STATIC 程式庫非空並集中版本字串。
#include "deferred_batch.hpp"

namespace ds::command {

const char* deferred_batch_contract_version() noexcept {
    // 延遲與批次執行契約版本（擴充 E6-01）。API 面若不相容變更，遞增主版本。
    return "e6_04/1.0.0";
}

}  // namespace ds::command
