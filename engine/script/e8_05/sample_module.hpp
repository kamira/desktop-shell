// E8-05 範例模組 — 用最小具體實作驗證 SDK 契約可用（header-only；示範 + 測試共用）。
//
// 展示第三方模組如何：宣告 metadata / 能力需求（ModuleInfo）、以 ModuleBase 取得
// 生命週期狀態機、在 init 以 has() 閘控存取宿主服務（NFR-03）並登記提供的能力、
// 在 start / stop 中運作、在 teardown 釋放借用的宿主服務指標。
//
// 平台中立：純邏輯，無任何系統呼叫 / 平台分支。
#ifndef DS_EXT_E8_05_SAMPLE_MODULE_HPP
#define DS_EXT_E8_05_SAMPLE_MODULE_HPP

#include <string>

#include "module_sdk.hpp"

namespace ds::ext::sdk::sample {

// 一個示範用的宿主服務介面：時脈。宿主以具體實作登記；模組向下轉型使用。
class IClockService : public IHostService {
public:
    virtual int now() const = 0;
};

// 測試 / 示範用的固定時脈實作（回傳建構時給定的刻度）。
class FixedClock : public IClockService {
public:
    explicit FixedClock(int tick) : tick_(tick) {}
    int now() const override { return tick_; }

private:
    int tick_;
};

// ---------------------------------------------------------------------------
// SampleSensorModule — 範例：宣告需要宿主 "host.clock" 服務、提供一個感測器能力
// "sample.tick"，在 start 時讀取時脈。錯誤處理示範：缺服務 / 型別不符時 init 回 false。
// ---------------------------------------------------------------------------
class SampleSensorModule : public ModuleBase {
public:
    static constexpr const char* kName = "com.example.sample_sensor";
    static constexpr const char* kClockService = "host.clock";
    static constexpr const char* kSensorId = "sample.tick";

    ModuleInfo info() const override {
        ModuleInfo i;
        i.name = kName;
        i.version = "0.1.0";
        i.description = "SDK 契約範例：requires 宿主時脈服務，提供 sample.tick 感測器";
        i.required_capabilities = {"host.time"};  // 對接 host 能力閘控（由 manifest requires 驗）
        i.permissions = {};
        return i;
    }

    // 內省：供測試觀察生命週期副作用。
    bool clock_bound() const { return clock_ != nullptr; }
    int last_reading() const { return last_reading_; }

protected:
    // NFR-03：存取宿主服務前先以 has_service() 閘控；缺服務或型別不符 → 明確失敗。
    bool on_init(ModuleContext& ctx) override {
        if (!ctx.has_service(kClockService)) return false;
        clock_ = ctx.service<IClockService>(kClockService);
        if (clock_ == nullptr) return false;  // 型別不符（dynamic_cast 失敗）
        // 登記本模組提供的能力。
        return ctx.provide_sensor(kSensorId);
    }

    bool on_start() override {
        if (clock_ == nullptr) return false;
        last_reading_ = clock_->now();  // 使用宿主服務
        return true;
    }

    void on_stop() override { last_reading_ = 0; }

    void on_teardown() override { clock_ = nullptr; }  // 釋放借用指標（不擁有，不 delete）

private:
    IClockService* clock_ = nullptr;
    int last_reading_ = 0;
};

}  // namespace ds::ext::sdk::sample

#endif  // DS_EXT_E8_05_SAMPLE_MODULE_HPP
