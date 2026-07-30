// E5-12 全域指標手勢 — 平台中立介面
//
// 全域指標（滑鼠 / 觸控）手勢辨識：全域滑動、螢幕角落觸發、縮放等。
// 這些是**全域事件**（需 OS 後端於系統層級攔截指標），依 directory.md 置於 `src/events/`。
//
// **能力閘控項（NFR-03）**：全域手勢辨識能力（`input.gesture`）在某些平台 / 權限下
// 並不存在（例如無觸控裝置、系統未授予全域指標監聽權限）。因此本介面以 `has()` 為
// **閘控入口**——呼叫端必須先 `has()` 判定能力是否可用，`false` 時走**降級路徑**
// （不崩、不誤觸、明確回報不可用），不得無條件訂閱。可用性的單一資料來源為
// E1-21 能力矩陣（`ds::kernel::CapabilityMatrix`）。
//
// 相位 1（Mac / null 期）約束：
//   - 只有平台中立介面 + null 後端；不綁任何真實平台後端。
//   - 不得出現 `#ifdef _WIN32` / win32 / cocoa 等平台分支；跨平台性由 API 面約束保證。
//   - null 後端不連真實 OS——手勢由測試 `inject()` 手動注入以驗證辨識 / 分派 / 降級路徑。
//     相位 2 換真實後端時，介面與分派語意一行不動。
#ifndef DS_EVENTS_E5_12_GLOBAL_GESTURE_HPP
#define DS_EVENTS_E5_12_GLOBAL_GESTURE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "capability_matrix.hpp"  // E1-21（上游，可讀不可改）

namespace ds::events {

// 全域指標手勢的種類。跨平台一致的語意（各平台以自身指標機制觸發，意義相同）。
enum class GestureType {
    SwipeLeft,           // 全域向左滑動
    SwipeRight,          // 全域向右滑動
    SwipeUp,             // 全域向上滑動
    SwipeDown,           // 全域向下滑動
    CornerTopLeft,       // 觸發左上角落
    CornerTopRight,      // 觸發右上角落
    CornerBottomLeft,    // 觸發左下角落
    CornerBottomRight,   // 觸發右下角落
    PinchIn,             // 縮小（捏合）
    PinchOut,            // 放大（張開）
};

// 單一手勢事件。純資料、平台中立——不含任何 OS 原生型別或指標控制代碼。
//
// 「具名手勢 + 參數」：`type` 為具名手勢，`magnitude` 為平台中立的正規化參數
// （滑動為正規化位移、縮放為比例因子；不適用時為 0），`detail` 為人類可讀補充。
// 刻意不含絕對座標（NFR-02）——角落以具名 CornerX 表達，而非螢幕像素座標。
struct Gesture {
    GestureType type;
    double magnitude = 0.0;  // 正規化參數（平台中立），不適用時 0
    std::string detail;      // 人類可讀補充（可為空）
};

// 手勢回呼。訂閱者於對應手勢辨識並分派時被呼叫。
using GestureListener = std::function<void(const Gesture&)>;

// 訂閱代號。由 subscribe() 發出，供 unsubscribe() 使用。0 保留為無效值
//（同時是「能力不可用 / 訂閱被拒」的回傳值——見降級語意）。
using SubscriptionId = std::uint64_t;

// 本單元閘控的能力識別碼（E1-21 能力矩陣中的穩定 id）。
// 相位 1 能力矩陣預設未宣告 / 不可用此能力，故 has() 預設為 false（保守）。
inline constexpr char kGestureCapability[] = "input.gesture";

// 全域指標手勢來源的抽象介面。
//
// **能力閘控**：`has()` 為唯一閘控入口。呼叫端契約——
//   `has()==true`  → 可訂閱，手勢會被辨識並分派。
//   `has()==false` → 能力不可用；訂閱一律被拒（回 0），注入為 no-op（不誤觸）。
//                    呼叫端應據此走降級路徑（見類別註解與契約測試）。
//
// 相位 1 唯一實作為 NullGlobalGestures；相位 2 起可加入真實後端，各後端只需
// 實作本介面，並在系統層級指標事件到達、辨識為某具名手勢時完成分派。
class GlobalGestures {
public:
    virtual ~GlobalGestures() = default;

    // 能力閘控入口：全域手勢辨識能力目前是否可用。
    // 呼叫端在 subscribe() 前**必須**先查詢；false 時走降級路徑。
    virtual bool has() const = 0;

    // 訂閱特定手勢類型。
    //   能力可用（has()==true）且 cb 非空：回傳非 0 訂閱代號。
    //   能力不可用（has()==false）：**拒絕訂閱、回傳 0**（明確回報不可用）。
    //   cb 為空：回傳 0（無效訂閱）。
    virtual SubscriptionId subscribe(GestureType gesture, GestureListener cb) = 0;

    // 解除訂閱。回傳是否確實移除了一筆訂閱；未知 id（含 0）為 no-op 並回傳 false。
    virtual bool unsubscribe(SubscriptionId id) = 0;

    // 目前訂閱者數量。能力不可用時恆為 0。
    virtual std::size_t listener_count() const = 0;
};

// null 後端參考實作。
//
// 不連任何真實 OS——手勢僅能由 inject() 手動注入（供測試與相位 1 契約 / 降級驗證）。
// 可用性（has()）由建構時決定：
//   - NullGlobalGestures(bool)：直接指定（測試模擬「能力可用 / 不可用」兩態）。
//   - from_capability(matrix, id)：以 E1-21 能力矩陣為單一資料來源決定可用性。
//
// 分派 / 降級語意即為相位 2 真實後端須遵守的契約：
//   能力可用時——只分派給訂閱了「該手勢類型」的訂閱者；多訂閱者皆收；解除後不再收。
//   能力不可用時——subscribe 一律回 0、listener_count 恆 0、inject 為 no-op（不誤觸、不崩）。
class NullGlobalGestures : public GlobalGestures {
public:
    // 直接指定可用性（預設不可用，符合相位 1 保守預設）。
    explicit NullGlobalGestures(bool available = false);

    // 以 E1-21 能力矩陣決定可用性（單一資料來源）：has() == matrix.has(id)。
    // 未宣告的能力於矩陣中回 false（保守），本後端因此同樣不可用。
    static NullGlobalGestures from_capability(
        const ds::kernel::CapabilityMatrix& matrix,
        const ds::kernel::CapabilityId& id = kGestureCapability);

    bool has() const override;
    SubscriptionId subscribe(GestureType gesture, GestureListener cb) override;
    bool unsubscribe(SubscriptionId id) override;
    std::size_t listener_count() const override;

    // 手動注入一個手勢，同步分派給「訂閱了該手勢類型」的訂閱者（依訂閱順序）。
    // 這是 null 後端的手勢入口；真實後端改由系統指標辨識觸發相同的分派。
    // 能力不可用（has()==false）時為 no-op——保證降級路徑不誤觸。
    void inject(const Gesture& gesture);

private:
    struct Entry {
        GestureType gesture;
        GestureListener listener;
    };
    bool available_;
    // 以有序容器保存以保證分派順序穩定（依 SubscriptionId 遞增即訂閱順序）。
    std::map<SubscriptionId, Entry> entries_;
    SubscriptionId next_id_ = 1;  // 0 保留為無效
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_12_GLOBAL_GESTURE_HPP
