// E1-20 睡眠喚醒復原 — 平台中立介面
//
// 處理系統**睡眠 / 喚醒**（sleep / wake）後的**狀態復原**：
//   - 以 E5-08 系統事件（SystemSleep / SystemWake）偵測睡眠 / 喚醒。
//   - 睡眠前保存 E1-01 LayerStack 目前的具名 surface / 圖層指派，並以注入的
//     VisibilityProvider（若有）為每個已指派 surface 拍一次可見性快照。
//   - 喚醒後重建圖層指派（透過 LayerStack::assign() 逐一還原）並回呼消費者以實際
//     恢復可見性；喚醒當下判定為已失效的資源（由注入的 ResourceValidator 判斷）則
//     不重建、改以 InvalidatedCallback 通知消費者處理。
//
// 相位 1（Mac / null 期）約束：
//   - 只有平台中立介面 + 注入式來源；**絕不**接真實電源 API（無 IOKit /
//     IORegisterForSystemPower / SetThreadExecutionState / cocoa / `#ifdef`），
//     **不建 backend/win32|cocoa**。
//   - 睡眠 / 喚醒事件由 E5-08 的可注入來源（NullSystemEventSource::inject）驅動；
//     LayerStack 由呼叫端注入既有實例。本單元絕不查 OS。
//   - 狀態全存記憶體。跨平台性由 API 面約束保證，不由語言保證。
//
// 硬約束（NFR-02）：本介面**不得出現絕對座標與數字 z-order**。
//   - surface 一律以 E1-01 的具名 `SurfaceId` 指涉；圖層一律以具名 `SurfaceLayer` 指涉。
//   - 快照與復原皆不引入座標型別，可見性以布林表達（無數字層級 / index）。
// 硬約束（NFR-03）：能力閘控式狀態變更一律經 `has()` 保護、未知一律保守：
//   - 喚醒重建指派透過 LayerStack::assign()，其內部已對 `kernel.surface` 能力做
//     has() 閘控；能力不可用時該筆保守略過、不改動任何狀態、絕不崩潰。
//   - `has()` 代理查詢 / `ResourceValidator` 缺省 / `VisibilityProvider` 缺省一律
//     保守回傳安全值（有效 / 可見），本單元不擅自臆測消費者未提供之狀態。
#ifndef DS_KERNEL_E1_20_SLEEP_WAKE_RECOVERY_HPP
#define DS_KERNEL_E1_20_SLEEP_WAKE_RECOVERY_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "layer_stack.hpp"    // E1-01 LayerStack / SurfaceId / SurfaceLayer / CapabilityId
#include "system_event.hpp"   // E5-08 SystemEventSource / SystemEvent / SubscriptionId

namespace ds::kernel {

// E5-08 系統事件型別位於 ds::events；於本命名空間內以別名引用，行文簡潔且不污染全域。
using ds::events::SubscriptionId;
using ds::events::SystemEvent;
using ds::events::SystemEventSource;
using ds::events::SystemEventType;

// ---------------------------------------------------------------------------
// SleepWakeRecovery —— 睡眠喚醒後的狀態復原管理器（事件驅動、平台中立）。
//
// 訂閱 E5-08 的 SystemSleep / SystemWake：
//   - SystemSleep 到達：對目前 LayerStack 的每一已指派 surface 保存其具名圖層指派，
//     並以注入的 VisibilityProvider（若有）查一次可見性。此快照**取代**前一次快照
//     （每輪 sleep 皆是一次全新快照，供下一次 wake 使用）。
//   - SystemWake 到達：若曾有快照，逐一以 LayerStack::assign() 重建圖層指派；並對每個
//     「仍有效」的 surface 呼叫可選的 on_wake 回呼，帶回其應恢復的可見性，供消費者實際
//     呼叫 show_surface / hide_surface。若注入了 ResourceValidator 且判定某 surface 於
//     喚醒當下已失效，則**不**重建其指派，改呼叫 on_invalidated。
//   - 其餘系統事件一律忽略。
//   - 未曾經歷 SystemSleep 即收到 SystemWake：保守 no-op（無快照可還原，不崩潰）。
//
// 相位 1：來源與 LayerStack 皆為呼叫端注入的既有實例；本單元不查 OS、無平台分支、
//   無真實電源 API，狀態全存記憶體。
// ---------------------------------------------------------------------------
class SleepWakeRecovery {
public:
    // 查詢某具名 surface 目前是否可見（消費者提供，如透過 KernelBackend::is_visible）。
    // 空則保守視為可見（true）——無法得知時不擅自標記為應隱藏。
    using VisibilityProvider = std::function<bool(const SurfaceId&)>;
    // 判斷某具名 surface 於**喚醒當下**是否仍為有效資源（消費者提供）。空則保守視為
    // 仍有效（true）——本單元不擁有資源生命週期，無從得知就不擅自判定失效。
    using ResourceValidator = std::function<bool(const SurfaceId&)>;
    // 喚醒復原回呼：對每個「仍有效」的快照 surface 呼叫一次，帶入其具名圖層與應恢復的
    // 可見性；消費者於回呼中實際套用可見性 / 完成必要的重新掛載。
    using RestoreCallback = std::function<void(const SurfaceId&, SurfaceLayer, bool)>;
    // 失效資源通知：對每個快照中、但被 ResourceValidator 判定失效的 surface 呼叫一次；
    // 該 surface 不會被重建指派。消費者可於此釋放對應的殘留狀態。
    using InvalidatedCallback = std::function<void(const SurfaceId&)>;

    // 以（可注入的）系統事件來源、既有 LayerStack、可選的可見性來源建構。
    // 建構時即訂閱 SystemSleep / SystemWake（不主動查詢初始狀態——首次快照於第一次
    // SystemSleep 到達時才建立）。
    explicit SleepWakeRecovery(SystemEventSource& source, LayerStack& stack,
                                VisibilityProvider visibility = {});

    // 解構時解除訂閱（來源與 LayerStack 生命週期由外部管理，本類僅持有非擁有指標）。
    ~SleepWakeRecovery();

    // 不可複製 / 搬移：持有事件訂閱與外部指標，複製語意不明確故禁止（保守）。
    SleepWakeRecovery(const SleepWakeRecovery&) = delete;
    SleepWakeRecovery& operator=(const SleepWakeRecovery&) = delete;

    // 設定喚醒復原回呼；空回呼即停用通知（指派仍會重建，只是不通知）。
    void set_on_wake(RestoreCallback cb) { on_wake_ = std::move(cb); }
    // 設定失效資源驗證器；空即不驗證（一律視為有效）。
    void set_resource_validator(ResourceValidator validator) {
        validator_ = std::move(validator);
    }
    // 設定失效資源通知回呼；空即不通知。
    void set_on_invalidated(InvalidatedCallback cb) { on_invalidated_ = std::move(cb); }

    // --- 能力查詢（NFR-03，代理至注入的 LayerStack）---
    bool has(const CapabilityId& id) const { return stack_->has(id); }

    // --- 診斷 / 測試查詢（唯讀）---
    // 是否曾保存過快照（尚未經歷任何 SystemSleep 則為 false）。
    bool has_snapshot() const noexcept { return has_snapshot_; }
    // 目前快照中的 surface 數量。
    std::size_t snapshot_count() const noexcept { return snapshot_.size(); }
    // 快照中是否含有某具名 surface。
    bool snapshot_contains(const SurfaceId& id) const;
    // 快照中某具名 surface 的圖層；未快照或未知回 nullptr（保守）。
    const SurfaceLayer* snapshot_layer_of(const SurfaceId& id) const;
    // 快照中某具名 surface 的可見性；未快照或未知回 nullptr（保守）。
    const bool* snapshot_visible_of(const SurfaceId& id) const;
    // 累計經歷的睡眠 / 喚醒次數（供測試斷言多次循環）。
    std::size_t sleep_count() const noexcept { return sleep_count_; }
    std::size_t wake_count() const noexcept { return wake_count_; }

private:
    // 睡眠快照的單一項目：具名 surface -> 具名圖層 + 可見性。
    struct Entry {
        SurfaceId id;
        SurfaceLayer layer;
        bool visible;
    };

    void handle_event_(const SystemEvent& event);
    void handle_sleep_();
    void handle_wake_();

    const Entry* find_(const SurfaceId& id) const;

    SystemEventSource* source_;  // 非擁有；生命週期由外部管理
    LayerStack* stack_;          // 非擁有；生命週期由外部管理
    VisibilityProvider visibility_;
    ResourceValidator validator_;
    RestoreCallback on_wake_;
    InvalidatedCallback on_invalidated_;

    std::vector<Entry> snapshot_;
    bool has_snapshot_ = false;
    std::size_t sleep_count_ = 0;
    std::size_t wake_count_ = 0;
    SubscriptionId sub_ = 0;  // E5-08 訂閱代號（0 = 無效）
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_20_SLEEP_WAKE_RECOVERY_HPP
