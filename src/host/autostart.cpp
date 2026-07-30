// E11-03 開機自動啟動 — 實作
//
// 相位 1（Mac / null 期）：只提供 null 後端的工廠。此檔不含任何平台分支或真實後端
// （不寫登錄檔 / LaunchAgent）。真實後端上線後在此改派，呼叫端不變。
#include "autostart.hpp"

namespace ds::host {

std::unique_ptr<Autostart> make_default_autostart() {
    // 相位 1：平台中立 null 後端。宣告不支援（has()==false），操作為 no-op。
    return std::make_unique<NullAutostart>();
}

}  // namespace ds::host
