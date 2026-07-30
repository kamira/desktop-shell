// E11-03 開機自動啟動 — 平台中立介面
//
// 「登入時自動啟動」的設定介面。這是**能力閘控項**：`host.autostart` 可能在
// 某些平台/權限下不存在（見 E1-21 能力矩陣，該能力宣告為 optional / 預設不可用）。
//
// 相位 1（Mac / null 期）約束：
//   - 只有平台中立介面 + null 後端，不綁任何真實平台後端。
//   - 不得出現 `#ifdef _WIN32` / win32 / cocoa 分支，不得真的寫登錄檔 / LaunchAgent。
//   - null 後端宣告不支援（`has() == false`），操作為 no-op 並回傳未支援狀態。
//   - 呼叫端必須先 `has()` 閘控，才可呼叫 enable()/disable()（NFR-03 精神）。
#ifndef DS_HOST_E11_03_AUTOSTART_HPP
#define DS_HOST_E11_03_AUTOSTART_HPP

#include <memory>

namespace ds::host {

// 開機自啟操作的結果。
//
// 相位 1 的 null 後端一律回 Unsupported —— 呼叫端據此走降級路徑，
// 永不把「未支援」誤當成「已啟用/已停用」。
enum class AutostartStatus {
    Ok,           // 操作成功（真實後端上線後才可能出現）
    Unsupported,  // 平台/後端不支援此能力（has() == false）
    Failed,       // 支援但操作失敗（如權限不足；真實後端上線後才可能出現）
};

// 「登入時自動啟動」設定介面 —— 平台中立，不含任何平台分支或真實後端。
//
// 契約：
//   - has()：本後端是否支援開機自啟。能力閘控入口（NFR-03）。
//   - enable() / disable()：變更設定；呼叫端須先 has() 閘控並備妥降級路徑。
//     不支援時**不得宣稱成功** —— 回 Unsupported。
//   - is_enabled()：目前是否已啟用；不支援時一律保守回 false。
class Autostart {
public:
    virtual ~Autostart() = default;

    // 本後端是否支援開機自啟。相位 1 null 後端回 false。
    virtual bool has() const = 0;

    // 啟用登入時自動啟動。呼叫端須先 has() 閘控。
    // 不支援時回 Unsupported（絕不假裝成功）。
    virtual AutostartStatus enable() = 0;

    // 停用登入時自動啟動。不支援時回 Unsupported。
    virtual AutostartStatus disable() = 0;

    // 目前是否已啟用。不支援時一律回 false（與 has()==false 一致）。
    virtual bool is_enabled() const = 0;
};

// null 後端（相位 1 / Mac / null 期）。
//
// 宣告不支援：has() == false；所有變更操作為 no-op 並回 Unsupported；
// is_enabled() 恆為 false。不觸碰任何真實系統設定（登錄檔 / LaunchAgent / …）。
class NullAutostart final : public Autostart {
public:
    bool has() const override { return false; }
    AutostartStatus enable() override { return AutostartStatus::Unsupported; }
    AutostartStatus disable() override { return AutostartStatus::Unsupported; }
    bool is_enabled() const override { return false; }
};

// 取得目前平台的預設 Autostart 後端。
//
// 相位 1（Mac / null 期）一律回 NullAutostart。真實後端上線後由此工廠改派，
// 呼叫端程式碼不變 —— 呼叫端本就以 has() 閘控，切換後端不影響其正確性。
std::unique_ptr<Autostart> make_default_autostart();

}  // namespace ds::host

#endif  // DS_HOST_E11_03_AUTOSTART_HPP
