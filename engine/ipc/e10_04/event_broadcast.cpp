// E10-04 事件廣播型模組協定 — 契約版本標記。
//
// 協定邏輯全為 header-only inline（見 event_broadcast.hpp）；本 .cpp 只集中定義
// 契約版本字串，使 STATIC 程式庫具實體符號（與 E10-01 / E6-01 相同作法）。
#include "event_broadcast.hpp"

namespace ds::ipc {

const char* event_broadcast_contract_version() noexcept { return "e10_04/1.0.0"; }

}  // namespace ds::ipc
