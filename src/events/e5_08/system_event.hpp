// E5-08 系統事件 — 平台中立介面
//
// 作業系統層級事件的訂閱介面：睡眠 / 喚醒、顯示器變更、session 鎖定 / 解鎖、
// 電源狀態變更等。這些是**全域事件**（需 OS 後端），依 docs/structure/directory.md
// 置於 `src/events/`。
//
// 相位 1（Mac / null 期）約束：
//   - 只有平台中立介面 + null 後端；不綁任何真實平台後端。
//   - 不得出現 `#ifdef _WIN32` / win32 / cocoa 等平台分支；跨平台性由 API 面約束保證。
//   - null 後端不連真實 OS——事件由測試手動注入以驗證分派路徑。相位 2 換真實後端時，
//     介面與分派語意一行不動，後端只需在 OS 事件到達時呼叫既有分派路徑。
#ifndef DS_EVENTS_E5_08_SYSTEM_EVENT_HPP
#define DS_EVENTS_E5_08_SYSTEM_EVENT_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace ds::events {

// 作業系統層級事件的種類。跨平台一致的語意（各平台以自身機制觸發，但意義相同）。
enum class SystemEventType {
    SystemSleep,         // 系統即將睡眠
    SystemWake,          // 系統自睡眠喚醒
    DisplayChanged,      // 顯示器組態變更（接上 / 拔除 / 解析度變更）
    SessionLocked,       // 使用者 session 鎖定
    SessionUnlocked,     // 使用者 session 解鎖
    PowerStatusChanged,  // 電源狀態變更（接上電源 / 改用電池 / 電量變化）
};

// 單一系統事件。純資料、平台中立——不含任何 OS 原生型別或控制代碼。
struct SystemEvent {
    SystemEventType type;
    // 人類可讀的補充說明（如 "external display attached"）。平台中立、可為空。
    std::string detail;
};

// 事件回呼。訂閱者於事件分派時被呼叫。
using SystemEventListener = std::function<void(const SystemEvent&)>;

// 訂閱代號。由 subscribe() 發出，供 unsubscribe() 使用。0 保留為無效值。
using SubscriptionId = std::uint64_t;

// 系統事件來源的抽象介面。
//
// 相位 1 唯一實作為 NullSystemEventSource；相位 2 起可加入 win32 等真實後端，
// 各後端只需實作本介面並在 OS 事件到達時完成分派。
class SystemEventSource {
public:
    virtual ~SystemEventSource() = default;

    // 訂閱系統事件。回傳非 0 的訂閱代號；listener 為空時回傳 0（無效訂閱）。
    virtual SubscriptionId subscribe(SystemEventListener listener) = 0;

    // 解除訂閱。回傳是否確實移除了一筆訂閱；未知 id 為 no-op 並回傳 false。
    virtual bool unsubscribe(SubscriptionId id) = 0;

    // 目前訂閱者數量。
    virtual std::size_t listener_count() const = 0;
};

// null 後端參考實作。
//
// 不連任何真實 OS——事件僅能由 inject() 手動注入（供測試與相位 1 契約驗證）。
// 分派語意即為相位 2 真實後端須遵守的契約：多訂閱者皆收、解除訂閱後不再收、
// 未知 id 解除為 no-op。
class NullSystemEventSource : public SystemEventSource {
public:
    SubscriptionId subscribe(SystemEventListener listener) override;
    bool unsubscribe(SubscriptionId id) override;
    std::size_t listener_count() const override;

    // 手動注入一個系統事件，同步分派給目前所有訂閱者（依訂閱順序）。
    // 這是 null 後端的事件入口；真實後端改由 OS 回呼觸發相同的分派。
    void inject(const SystemEvent& event);

private:
    // 以有序容器保存以保證分派順序穩定（依 SubscriptionId 遞增即訂閱順序）。
    std::map<SubscriptionId, SystemEventListener> listeners_;
    SubscriptionId next_id_ = 1;  // 0 保留為無效
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_08_SYSTEM_EVENT_HPP
