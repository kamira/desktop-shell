// E10-01 本機 IPC 訊息投遞 — 契約版本標記。
//
// 通道邏輯全為 header-only inline（見 message_channel.hpp）；本 .cpp 只集中定義
// 契約版本字串，使 STATIC 程式庫具實體符號（與 E6-01 相同作法）。
#include "message_channel.hpp"

namespace ds::ipc {

const char* contract_version() noexcept { return "e10_01/1.0.0"; }

}  // namespace ds::ipc
