// E3-05 音量設定致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「系統音量設定」這組副作用（設定音量 0–100、靜音 / 取消靜音、相對增減、
// 查詢目前音量 / 靜音狀態），以具名命令掛上 E6-01 命令匯流排
// （`volume.set` / `volume.get` / `volume.mute` / `volume.unmute` / `volume.adjust`）。
// 呼叫端只需 命令 id + 具名參數 即可觸發，不需相依本致動器或任何 OS 音訊 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層），消費 E6-01 契約（與 E3-02 一致）。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 音訊 API**（無 CoreAudio /
//     MPVolumeView / IAudioEndpointVolume 等）。所有音量狀態交由可抽換的 `VolumeBackend`
//     承接；預設 `NullVolumeBackend` 以純記憶體狀態模擬（音量整數 + 靜音旗標），
//     供測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實後端（win32 / cocoa）時，本致動器
//     與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實音訊 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（範圍夾限 0–100、經 E6-01 分派、結果回報）與後端解耦：夾限與參數驗證在
// 致動器層完成，後端只承接「已合法化」的意圖，因此可完全以單元測試驗證。
#ifndef DS_ACTUATORS_E3_05_VOLUME_ACTUATOR_HPP
#define DS_ACTUATORS_E3_05_VOLUME_ACTUATOR_HPP

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
const char* volume_contract_version() noexcept;

// 五個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdVolumeSet    = "volume.set";     // 必填 level（int 0–100）
inline constexpr const char* kCmdVolumeGet    = "volume.get";     // 無參數，回目前音量
inline constexpr const char* kCmdVolumeMute   = "volume.mute";    // 無參數，靜音
inline constexpr const char* kCmdVolumeUnmute = "volume.unmute";  // 無參數，取消靜音
inline constexpr const char* kCmdVolumeAdjust = "volume.adjust";  // 必填 delta（int，可負）

// 音量合法範圍（相對值域，非絕對座標）：0 = 最小，100 = 最大。
inline constexpr int kVolumeMin = 0;
inline constexpr int kVolumeMax = 100;

// 把任意整數夾限至 [kVolumeMin, kVolumeMax]。致動器層唯一的「合法化」點。
inline int clamp_volume(int level) noexcept {
    return std::max(kVolumeMin, std::min(kVolumeMax, level));
}

// ---------------------------------------------------------------------------
// VolumeState — 平台中立地描述一次音量查詢的結果。
//
// 不含任何 OS handle / 裝置語意；只承載「目前音量位階 + 是否靜音」。
// ---------------------------------------------------------------------------
struct VolumeState {
    int level = 0;        // 目前音量位階，恆在 [0,100]
    bool muted = false;   // 是否靜音（靜音不改變 level，取消靜音即恢復）

    bool operator==(const VolumeState& o) const noexcept {
        return level == o.level && muted == o.muted;
    }
    bool operator!=(const VolumeState& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// VolumeBackend — 執行實際音量副作用的抽象後端。
//
// 相位 1 僅提供 NullVolumeBackend；相位 2 由平台後端以真實音訊 API 實作。
// 介面刻意最小：設定 / 查詢 / 靜音三組原語，致動器層以此組合出 set/get/mute/unmute/adjust。
// 契約保證：傳入 set(level) 的 level 已由致動器夾限至 [0,100]（後端可信任）。
// ---------------------------------------------------------------------------
class VolumeBackend {
public:
    virtual ~VolumeBackend() = default;

    // 設定音量位階（呼叫端保證已夾限至 [0,100]）。
    virtual void set(int level) = 0;
    // 查詢目前音量位階。
    virtual int get() const = 0;
    // 設定 / 清除靜音旗標。
    virtual void set_muted(bool muted) = 0;
    // 查詢目前靜音旗標。
    virtual bool is_muted() const = 0;
};

// ---------------------------------------------------------------------------
// NullVolumeBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體狀態模擬。
//
// 讓致動器在無真實平台後端時仍可完整跑通（設定 → 查詢一致、靜音 / 取消靜音、增減），
// 並讓測試 / 診斷驗證狀態一致性。初值可注入（預設音量 50、未靜音）。
// ---------------------------------------------------------------------------
class NullVolumeBackend : public VolumeBackend {
public:
    NullVolumeBackend() = default;
    explicit NullVolumeBackend(int initial_level, bool initial_muted = false)
        : level_(clamp_volume(initial_level)), muted_(initial_muted) {}

    void set(int level) override { level_ = clamp_volume(level); }
    int get() const override { return level_; }
    void set_muted(bool muted) override { muted_ = muted; }
    bool is_muted() const override { return muted_; }

    // 內省：完整狀態（供測試 / 診斷）。
    VolumeState state() const noexcept { return VolumeState{level_, muted_}; }

private:
    int level_ = 50;      // 記憶體模擬的音量位階（預設 50）
    bool muted_ = false;  // 記憶體模擬的靜音旗標
};

// ---------------------------------------------------------------------------
// VolumeActuator — 把五個具名命令掛上 E6-01 命令匯流排的音量致動器。
//
// 建構時綁定一個 VolumeBackend（相位 1 為 NullVolumeBackend）。register_on(bus) 將
// volume.set / volume.get / volume.mute / volume.unmute / volume.adjust 註冊到匯流排；
// 呼叫端之後只需 bus.dispatch("volume.set", args) 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/get_int 保護）：
//   - volume.set   ：必填 `level`（int）；超出 [0,100] 自動夾限（不失敗）。回夾限後音量。
//   - volume.get   ：無參數。回目前音量（int）；訊息含靜音狀態。
//   - volume.mute  ：無參數。設靜音；回目前音量。
//   - volume.unmute：無參數。清靜音；回目前音量。
//   - volume.adjust：必填 `delta`（int，可負）；套用後夾限至 [0,100]。回夾限後音量。
// 缺 / 型別錯的必填參數 → 回 CommandResult{Failed}（不崩潰、不丟例外）。
// 所有成功結果的 value 皆為夾限後的目前音量（int），供呼叫端 / 測試驗證。
// ---------------------------------------------------------------------------
class VolumeActuator {
public:
    explicit VolumeActuator(std::shared_ptr<VolumeBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullVolumeBackend（相位 1）。
    VolumeActuator() : backend_(std::make_shared<NullVolumeBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<VolumeBackend>& backend() const noexcept { return backend_; }

    // 將五個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）
    // 則回滾已註冊者並回 false（不留半掛狀態，不遮蔽既有致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_set = bus.register_command(
            kCmdVolumeSet, [self](const ds::command::CommandArgs& a) {
                return self->handle_set(a);
            });
        const bool ok_get = bus.register_command(
            kCmdVolumeGet, [self](const ds::command::CommandArgs& a) {
                return self->handle_get(a);
            });
        const bool ok_mute = bus.register_command(
            kCmdVolumeMute, [self](const ds::command::CommandArgs& a) {
                return self->handle_mute(a);
            });
        const bool ok_unmute = bus.register_command(
            kCmdVolumeUnmute, [self](const ds::command::CommandArgs& a) {
                return self->handle_unmute(a);
            });
        const bool ok_adjust = bus.register_command(
            kCmdVolumeAdjust, [self](const ds::command::CommandArgs& a) {
                return self->handle_adjust(a);
            });
        if (ok_set && ok_get && ok_mute && ok_unmute && ok_adjust) return true;
        // 回滾：只移除本次成功掛上的。
        if (ok_set) bus.unregister(kCmdVolumeSet);
        if (ok_get) bus.unregister(kCmdVolumeGet);
        if (ok_mute) bus.unregister(kCmdVolumeMute);
        if (ok_unmute) bus.unregister(kCmdVolumeUnmute);
        if (ok_adjust) bus.unregister(kCmdVolumeAdjust);
        return false;
    }

    // 從匯流排移除五個具名命令。回傳確有移除的數量（0..5）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdVolumeSet) ? 1 : 0;
        n += bus.unregister(kCmdVolumeGet) ? 1 : 0;
        n += bus.unregister(kCmdVolumeMute) ? 1 : 0;
        n += bus.unregister(kCmdVolumeUnmute) ? 1 : 0;
        n += bus.unregister(kCmdVolumeAdjust) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_set(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        if (!args.has("level")) {
            return ds::command::CommandResult::make_failed("volume.set: missing 'level'");
        }
        const auto level = args.get_int("level");
        if (!level) {
            return ds::command::CommandResult::make_failed(
                "volume.set: 'level' must be an integer");
        }
        const int clamped = clamp_volume(static_cast<int>(*level));
        backend_->set(clamped);
        return ok_with_level(clamped);
    }

    ds::command::CommandResult handle_get(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        return ok_with_level(clamp_volume(backend_->get()));
    }

    ds::command::CommandResult handle_mute(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->set_muted(true);
        return ok_with_level(clamp_volume(backend_->get()));
    }

    ds::command::CommandResult handle_unmute(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->set_muted(false);
        return ok_with_level(clamp_volume(backend_->get()));
    }

    ds::command::CommandResult handle_adjust(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        if (!args.has("delta")) {
            return ds::command::CommandResult::make_failed("volume.adjust: missing 'delta'");
        }
        const auto delta = args.get_int("delta");
        if (!delta) {
            return ds::command::CommandResult::make_failed(
                "volume.adjust: 'delta' must be an integer");
        }
        // 以 int64 相加後夾限，避免 int 溢位；結果落回 [0,100]。
        const std::int64_t next = static_cast<std::int64_t>(clamp_volume(backend_->get())) + *delta;
        const int clamped = clamp_volume(next > kVolumeMax ? kVolumeMax
                                          : next < kVolumeMin ? kVolumeMin
                                                              : static_cast<int>(next));
        backend_->set(clamped);
        return ok_with_level(clamped);
    }

    // 便捷查詢：目前完整狀態（音量 + 靜音）。不經匯流排，供呼叫端 / 測試內省。
    VolumeState current_state() const {
        if (!backend_) return VolumeState{};
        return VolumeState{clamp_volume(backend_->get()), backend_->is_muted()};
    }

private:
    static ds::command::CommandResult no_backend() {
        return ds::command::CommandResult::make_failed("volume: no backend bound");
    }

    // 成功結果統一帶「夾限後的目前音量」為回傳值，訊息帶靜音狀態供診斷。
    ds::command::CommandResult ok_with_level(int level) const {
        const bool muted = backend_ && backend_->is_muted();
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{level},
            muted ? "muted" : "unmuted");
    }

    std::shared_ptr<VolumeBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_05_VOLUME_ACTUATOR_HPP
