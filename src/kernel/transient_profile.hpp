// E1-14 短暫 profile 生命週期 — 平台中立介面（相位 1 = null）
//
// 某些 surface / 元件的 profile 是**短暫（transient / ephemeral）**的：提示、通知、召喚式
// 臨時彈窗等。它們不是常駐角色，而是「建立後存活一小段（邏輯）時間，時間到即自動過期銷毀，
// 或在條件達成時被提早結束」。本單元把這種**短暫 profile 的生命週期**收斂成單一模型：
//
//   建立(create) → 存活(alive) → (逾時 timeout / 手動 manual) 過期(expire) → 清理(cleanup)
//
// 兩條上游基座（皆已合併，可讀不可改）：
//   - E5-10 逾時計時器（`ds::events::TimeoutTimer`）：時間**以邏輯 tick 計**，由呼叫端經
//     `advance(dt)` 注入推進，**不綁任何真實 OS 時鐘 / wall-clock**。每個短暫 profile 於
//     建立時登記一個一次性 `set_timeout(ttl)`；`advance` 推進跨過 ttl 即自動過期。
//   - E1-02 輸入策略四態（`ds::kernel::InputStrategy`）：短暫 profile 的**輸入行為**由這四態
//     之一決定（提示多為 `Inert` / `ClickThrough`，臨時 modal 為 `Capture`）。本單元僅承載
//     該具名策略並轉交，實際路由歸 E1-02 的 `InputStrategyController`。
//
// 短暫 profile 亦組合 E1-24 的四參數 `SurfaceProfile`，其 `lifecycle` 於建立時一律強制為
// `SurfaceLifecycle::Ephemeral`（短暫語意的單一資料來源）。
//
// 相位 1（Mac / null 期）約束：純邏輯、無平台分支（無 `#ifdef` / win32 / cocoa）、不觸真實
// OS、無絕對座標 / 數字 z-order（profile 一律具名指涉，NFR-02）。時間全走 E5-10 注入式 tick。
// 無效 id / 重複過期一律**明確回 false，不靜默**。
#ifndef DS_KERNEL_E1_14_TRANSIENT_PROFILE_HPP
#define DS_KERNEL_E1_14_TRANSIENT_PROFILE_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "input_strategy.hpp"  // E1-02（上游）：InputStrategy / kDefaultStrategy / to_backend_policy
                               //   （並透傳 E1-24 SurfaceProfile / SurfaceLifecycle 等）
#include "timeout_timer.hpp"   // E5-10（上游）：TimeoutTimer / Tick / TimerId（注入式邏輯計時）

namespace ds::kernel {

// 短暫 profile 的具名識別碼（如 "toast.saved" / "hint.drag"）。
//
// 刻意用具名字串而非數字 handle（NFR-02：不以數字指涉），與 SurfaceId / E1-18 慣例一致。
using TransientId = std::string;

// 過期原因 —— 具名（NFR-02），供回呼與診斷區分「時間到」與「被提早結束」。
enum class ExpiryReason {
    Timeout,  // ttl 到期：`advance` 推進跨過存活時限，自動過期。
    Manual,   // 手動：條件達成，呼叫 `expire(id)` 提早結束。
};

// 具名字串（NFR-02：診斷 / 記錄一律具名，不用數字）。未知回 "unknown"。
const char* to_string(ExpiryReason r) noexcept;

// 短暫 profile —— 一份 lifecycle 為 Ephemeral 的四參數 `SurfaceProfile`，外加 E1-02 的
// 具名輸入策略。純資料、具名取值、無絕對座標。
struct TransientProfile {
    SurfaceProfile surface;                   // 四參數 profile；lifecycle 於 create 時強制 Ephemeral。
    InputStrategy input = kDefaultStrategy;    // E1-02 四態輸入行為（決定此短暫 profile 如何參與輸入）。
};

// 過期回呼 —— 任一短暫 profile 過期（逾時或手動）時被呼叫，攜帶其具名 id 與原因。
// 呼叫時該 profile 已不再存活（`is_alive` 回 false）：回呼觀察到的是「正在收尾」的一致視圖。
using ExpiryCallback = std::function<void(const TransientId&, ExpiryReason)>;

// ---------------------------------------------------------------------------
// TransientProfileManager —— 管理一組短暫 profile 的完整生命週期。
//
// 時間源為**注入式** E5-10 `TimeoutTimer`（不擁有其生命週期）。每個短暫 profile 於建立時
// 在該計時器上登記一次性 `set_timeout(ttl)`；管理器的 `advance` 直接推進該計時器，到期者
// 由計時器回呼觸發自動過期。手動 `expire` 則取消其計時器並立即過期。
//
// 前置條件：注入的 `TimeoutTimer` 必須比本管理器活得久（管理器在其上登記了捕捉 `this` 的
// 回呼）。本管理器解構時會主動取消其所有未觸發計時器，避免懸置回呼。
// ---------------------------------------------------------------------------
class TransientProfileManager {
public:
    // 綁定一個注入式逾時計時器（可讀寫其計時器；不擁有其生命週期）。
    explicit TransientProfileManager(ds::events::TimeoutTimer& timer) noexcept;

    // 解構：取消本管理器登記於注入計時器上的所有未觸發計時器（避免懸置回呼）。
    ~TransientProfileManager();

    TransientProfileManager(const TransientProfileManager&) = delete;
    TransientProfileManager& operator=(const TransientProfileManager&) = delete;

    // 建立一個具名短暫 profile，存活 `ttl` 個 tick 後自動過期。
    //   - id 為空、id 已存在（仍存活）、或 ttl == 0 → 回 false（保守，不靜默覆寫）。
    //   - 強制 `profile.surface.lifecycle = Ephemeral`（短暫語意）。
    //   - 於注入計時器登記一次性 `set_timeout(ttl)`；到期 → 自動過期 + on_expire(Timeout)。
    //   - 成功回 true。
    bool create(const TransientId& id, const TransientProfile& profile, ds::events::Tick ttl);

    // 推進邏輯時間 `ticks` 個 tick —— 直接委派注入計時器 `advance`，觸發所有到期的自動過期。
    // 回傳本次因**逾時**而過期的短暫 profile 數（不含手動）。ticks 為 0 不推進、不過期。
    std::size_t advance(ds::events::Tick ticks);

    // 手動提早過期一個存活中的短暫 profile。
    //   - 未知 id / 已不存活 → 回 false（**重複過期明確不靜默**）。
    //   - 取消其計時器、移除記錄、觸發 on_expire(Manual)。成功回 true。
    bool expire(const TransientId& id);

    // 登記一個過期回呼（逾時或手動皆會呼叫）。可登記多個，依登記序呼叫。
    void on_expire(ExpiryCallback cb);

    // 該具名短暫 profile 目前是否存活（已建立且尚未過期）。
    bool is_alive(const TransientId& id) const;

    // 距該 profile 自動過期還需幾個 tick；未知 / 已不存活回 std::nullopt（委派 E5-10 `remaining`）。
    std::optional<ds::events::Tick> remaining(const TransientId& id) const;

    // 取存活中短暫 profile 的資料；未知 / 已不存活回 nullptr。指標於該 profile 存活期間有效。
    const TransientProfile* profile(const TransientId& id) const;

    // 該存活 profile 的 E1-02 輸入策略（整合入口）；未知 / 已不存活回 `kDefaultStrategy`。
    InputStrategy input_strategy(const TransientId& id) const;

    // 目前存活中的短暫 profile 數。
    std::size_t alive_count() const noexcept { return entries_.size(); }

private:
    struct Entry {
        TransientId id;
        TransientProfile profile;
        ds::events::TimerId timer;  // E5-10 一次性計時器 id（逾時到期即自動移除）。
    };

    // 以具名鍵線性尋找（短暫 profile 同時存活數量小）。const 與非 const 兩版。
    Entry* find(const TransientId& id);
    const Entry* find(const TransientId& id) const;

    // 過期收尾：移除記錄後，依登記序呼叫所有 on_expire 回呼（回呼中該 id 已不存活）。
    // 回傳是否確有移除（找不到 = 已不存活，回 false）。
    bool fire_expiry(const TransientId& id, ExpiryReason reason);

    ds::events::TimeoutTimer& timer_;
    // 存活中的短暫 profile；過期即移除（清理）。順序即建立序（永不以數字 index 對外暴露）。
    std::vector<Entry> entries_;
    std::vector<ExpiryCallback> on_expire_;
    std::size_t advance_timeout_count_ = 0;  // 單次 advance 內累計的逾時過期數（advance 起始歸零）。
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_14_TRANSIENT_PROFILE_HPP
