// E3-08 網路 / 藍牙開關致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「切換網路介面（Wi-Fi）與藍牙 開 / 關 / 切換 / 查詢狀態」這組副作用，以具名
// 命令掛上 E6-01 命令匯流排（擴充點 3「動作」契約）：
//   `net.wifi.on` / `net.wifi.off` / `net.wifi.toggle` / `net.wifi.status`
//   `net.bluetooth.on` / `net.bluetooth.off` / `net.bluetooth.toggle` / `net.bluetooth.status`
// 呼叫端只需 命令 id 即可觸發，**不需相依本致動器或任何 OS 網路 / 藍牙 API**——與已
// 合併的 E3-05（音量設定）採同一「注入式後端 + null 樣式」範式。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層 / 子系統 actuators），消費 E6-01 契約。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 網路 / 藍牙 API**（不含 CoreWLAN /
//     IOBluetooth / `networksetup` 等）。所有無線電開關狀態交由可抽換的 `RadioBackend`
//     承接；預設 `NullRadioBackend` 以純記憶體狀態模擬（每個無線電一個布林旗標），
//     供測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實後端（win32 / cocoa）時，本致動器
//     與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（無線電列舉、經 E6-01 分派、切換 / 查詢、結果回報）與後端解耦：無線電合法性
// 驗證在致動器層完成，後端只承接「已合法化」的意圖，因此可完全以單元測試驗證。
#ifndef DS_ACTUATORS_E3_08_RADIO_ACTUATOR_HPP
#define DS_ACTUATORS_E3_08_RADIO_ACTUATOR_HPP

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// 命名加 `radio_` 前綴，避免與同命名空間其他致動器的版本函式衝突（可能同時連入同一 target）。
const char* radio_toggle_contract_version() noexcept;

// ---------------------------------------------------------------------------
// Radio — 受控的無線電種類（列舉，不使用數字 opcode 對外）。
// ---------------------------------------------------------------------------
enum class Radio { Wifi, Bluetooth };

// 有效無線電數（供內省 / 後端狀態陣列）。
inline constexpr int kRadioCount = 2;

// 無線電是否合法（防止以未定義的列舉值進入後端）。致動器層唯一的「合法化」點。
inline bool is_valid_radio(Radio radio) noexcept {
    return radio == Radio::Wifi || radio == Radio::Bluetooth;
}

// 無線電可讀名稱（穩定字串）；非法值回 nullptr（不丟例外、不 UB）。
inline const char* radio_name(Radio radio) noexcept {
    switch (radio) {
        case Radio::Wifi:      return "wifi";
        case Radio::Bluetooth: return "bluetooth";
    }
    return nullptr;  // 越界列舉值（無效 radio）
}

// ---------------------------------------------------------------------------
// RadioOp — 對一個無線電可執行的操作（開 / 關 / 切換 / 查詢）。
// ---------------------------------------------------------------------------
enum class RadioOp { On, Off, Toggle, Status };

// 八個具名命令 id（穩定、可讀字串；無線電種類與操作皆編碼於命令名）。
inline constexpr const char* kCmdWifiOn          = "net.wifi.on";
inline constexpr const char* kCmdWifiOff         = "net.wifi.off";
inline constexpr const char* kCmdWifiToggle      = "net.wifi.toggle";
inline constexpr const char* kCmdWifiStatus      = "net.wifi.status";
inline constexpr const char* kCmdBluetoothOn     = "net.bluetooth.on";
inline constexpr const char* kCmdBluetoothOff    = "net.bluetooth.off";
inline constexpr const char* kCmdBluetoothToggle = "net.bluetooth.toggle";
inline constexpr const char* kCmdBluetoothStatus = "net.bluetooth.status";

// ---------------------------------------------------------------------------
// RadioState — 平台中立地描述兩個無線電的目前開關狀態（供查詢 / 內省）。
//
// 不含任何 OS handle / 裝置語意；只承載「Wi-Fi 是否啟用 + 藍牙是否啟用」。
// ---------------------------------------------------------------------------
struct RadioState {
    bool wifi = false;       // Wi-Fi 是否啟用
    bool bluetooth = false;  // 藍牙是否啟用

    bool enabled(Radio radio) const noexcept {
        return radio == Radio::Wifi ? wifi : bluetooth;
    }

    bool operator==(const RadioState& o) const noexcept {
        return wifi == o.wifi && bluetooth == o.bluetooth;
    }
    bool operator!=(const RadioState& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// RadioBackend — 執行實際無線電開關副作用的抽象後端。
//
// 相位 1 僅提供 NullRadioBackend；相位 2 由平台後端以真實網路 / 藍牙 API 實作。
// 介面刻意最小：set_enabled / is_enabled 兩組原語，致動器層以此組合出
// on / off / toggle / status。契約保證：傳入的 radio 已由致動器驗證為合法值（後端可信任）。
// ---------------------------------------------------------------------------
class RadioBackend {
public:
    virtual ~RadioBackend() = default;

    // 設定指定無線電的啟用狀態（呼叫端保證 radio 合法）。
    virtual void set_enabled(Radio radio, bool enabled) = 0;
    // 查詢指定無線電是否啟用。
    virtual bool is_enabled(Radio radio) const = 0;
};

// ---------------------------------------------------------------------------
// NullRadioBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體狀態模擬。
//
// 讓致動器在無真實平台後端時仍可完整跑通（開 → 查詢一致、切換冪等），並讓測試 / 診斷
// 驗證狀態一致性。初值可注入（預設 Wi-Fi / 藍牙皆關）。
// ---------------------------------------------------------------------------
class NullRadioBackend : public RadioBackend {
public:
    NullRadioBackend() = default;
    NullRadioBackend(bool wifi_on, bool bluetooth_on) noexcept
        : wifi_(wifi_on), bluetooth_(bluetooth_on) {}

    void set_enabled(Radio radio, bool enabled) override {
        if (radio == Radio::Wifi) {
            wifi_ = enabled;
        } else if (radio == Radio::Bluetooth) {
            bluetooth_ = enabled;
        }
        // 非法 radio：記憶體後端不改任何狀態（致動器層已先攔截，此為縱深防護）。
    }

    bool is_enabled(Radio radio) const override {
        if (radio == Radio::Wifi) return wifi_;
        if (radio == Radio::Bluetooth) return bluetooth_;
        return false;
    }

    // 內省：完整狀態（供測試 / 診斷）。
    RadioState state() const noexcept { return RadioState{wifi_, bluetooth_}; }

private:
    bool wifi_ = false;       // 記憶體模擬的 Wi-Fi 旗標（預設關）
    bool bluetooth_ = false;  // 記憶體模擬的藍牙旗標（預設關）
};

// 具名命令 → (無線電, 操作) 對照表項。供 register / unregister 共用同一份來源。
struct RadioCommand {
    const char* id;
    Radio radio;
    RadioOp op;
};

// 八個具名命令的完整對照表（Wi-Fi / 藍牙 × on / off / toggle / status）。
inline std::array<RadioCommand, 8> radio_command_table() {
    return {{
        {kCmdWifiOn,          Radio::Wifi,      RadioOp::On},
        {kCmdWifiOff,         Radio::Wifi,      RadioOp::Off},
        {kCmdWifiToggle,      Radio::Wifi,      RadioOp::Toggle},
        {kCmdWifiStatus,      Radio::Wifi,      RadioOp::Status},
        {kCmdBluetoothOn,     Radio::Bluetooth, RadioOp::On},
        {kCmdBluetoothOff,    Radio::Bluetooth, RadioOp::Off},
        {kCmdBluetoothToggle, Radio::Bluetooth, RadioOp::Toggle},
        {kCmdBluetoothStatus, Radio::Bluetooth, RadioOp::Status},
    }};
}

// ---------------------------------------------------------------------------
// RadioActuator — 把八個具名命令掛上 E6-01 命令匯流排的網路 / 藍牙開關致動器。
//
// 建構時綁定一個 RadioBackend（相位 1 為 NullRadioBackend）。register_on(bus) 將八個
// 具名命令註冊到匯流排；呼叫端之後只需 bus.dispatch("net.wifi.toggle") 即可觸發，完全
// 不需相依本型別。
//
// 命令語意（皆無必填參數；操作與無線電種類編碼於命令名）：
//   - net.<radio>.on    ：啟用該無線電（已啟用時維持啟用 → 冪等）。回啟用後狀態（bool）。
//   - net.<radio>.off   ：停用該無線電（已停用時維持停用 → 冪等）。回啟用後狀態（bool）。
//   - net.<radio>.toggle：翻轉該無線電開關。回翻轉後狀態（bool）。
//   - net.<radio>.status：查詢該無線電是否啟用（不改狀態）。回目前狀態（bool）。
// 所有成功結果的 value 皆為該無線電操作後的啟用狀態（bool），訊息帶 "<radio> on/off"。
// 無效 radio（越界列舉值）/ 無後端 → 回 CommandResult{Failed}（不崩潰、不丟例外）。
// ---------------------------------------------------------------------------
class RadioActuator {
public:
    explicit RadioActuator(std::shared_ptr<RadioBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullRadioBackend（相位 1）。
    RadioActuator() : backend_(std::make_shared<NullRadioBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<RadioBackend>& backend() const noexcept { return backend_; }

    // 將八個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）則回滾
    // 已註冊者並回 false（不留半掛狀態，不遮蔽既有致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        std::vector<ds::command::CommandId> done;
        done.reserve(8);
        for (const auto& entry : radio_command_table()) {
            auto self = this;
            const Radio radio = entry.radio;
            const RadioOp op = entry.op;
            const bool ok = bus.register_command(
                entry.id, [self, radio, op](const ds::command::CommandArgs& a) {
                    return self->handle(radio, op, a);
                });
            if (!ok) {
                for (const auto& id : done) bus.unregister(id);
                return false;
            }
            done.emplace_back(entry.id);
        }
        return true;
    }

    // 從匯流排移除八個具名命令。回傳確有移除的數量（0..8）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        for (const auto& entry : radio_command_table()) {
            n += bus.unregister(entry.id) ? 1 : 0;
        }
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----
    //
    // 對 radio 執行 op，回傳操作後啟用狀態。無後端 / 無效 radio → Failed。
    ds::command::CommandResult handle(Radio radio, RadioOp op,
                                      const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        if (!is_valid_radio(radio)) {
            return ds::command::CommandResult::make_failed("radio: unknown radio");
        }
        switch (op) {
            case RadioOp::On:
                backend_->set_enabled(radio, true);
                break;
            case RadioOp::Off:
                backend_->set_enabled(radio, false);
                break;
            case RadioOp::Toggle:
                backend_->set_enabled(radio, !backend_->is_enabled(radio));
                break;
            case RadioOp::Status:
                break;  // 純查詢，不改狀態
        }
        return ok_with_state(radio);
    }

    // 便捷查詢：目前兩個無線電的完整狀態。不經匯流排，供呼叫端 / 測試內省。
    RadioState current_state() const {
        if (!backend_) return RadioState{};
        return RadioState{backend_->is_enabled(Radio::Wifi),
                          backend_->is_enabled(Radio::Bluetooth)};
    }

private:
    static ds::command::CommandResult no_backend() {
        return ds::command::CommandResult::make_failed("radio: no backend bound");
    }

    // 成功結果統一帶「該無線電操作後的啟用狀態」為回傳值，訊息帶 "<radio> on/off" 供診斷。
    ds::command::CommandResult ok_with_state(Radio radio) const {
        const bool enabled = backend_ && backend_->is_enabled(radio);
        const char* name = radio_name(radio);
        std::string message = std::string(name ? name : "?") + (enabled ? " on" : " off");
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{enabled}, std::move(message));
    }

    std::shared_ptr<RadioBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_08_RADIO_ACTUATOR_HPP
