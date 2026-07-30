// E2-02 採集頻率分級與除頻 — 契約 + 純邏輯排程器（engine 層 / 平台中立）
//
// 本單元建立在 E2-01 統一指標介面之上（承重 20x，多個 sysinfo 提供者相依它）：
// E2-01 定義「一個指標是什麼」（名稱/值/單位/範圍/歷史/實例），本單元定義
// 「一個指標多久採集一次、以及多個消費者要同一指標時如何合併採集」。
//
// 兩件事：
//   1. **分級（tiering）**：把採集頻率分成 high / normal / low / on-demand 四級，
//      避免對每個指標都高頻採集吃資源——直接關係 NFR-01 idle 門檻（閒置時不該有
//      密集採集把 CPU 拉起來）。分級 → 間隔的映射由 `SamplingPolicy` 決定。
//   2. **除頻（de-frequency / coalescing）**：多個消費者（掛件）常要同一指標。若各採各的，
//      同一感測器被重複讀 N 次。除頻把同一指標的多個需求**合併成一次採集**，並按
//      **最高需求頻率**供給（誰要得最頻繁，就用誰的頻率；其餘消費者搭便車）。
//
// 分層約束（engine 層）：
//   - **平台中立、純邏輯**：無 `#ifdef`、無系統呼叫、無真實時鐘。時間以**邏輯 tick**
//     表達（呼叫端每次想採集時推進 tick），故可完全單元測試：注入時間、驗採集次數
//     恰符合分級、驗多消費者除頻合併。真實時鐘由呼叫端（未來的採集迴圈）綁定，本層不碰。
//   - 相依 E2-01：`target_link_libraries(e2_02 PUBLIC e2_01)`。指標身分沿用
//     `ds::metrics::MetricId`（不另造識別碼系統）。
#ifndef DS_ENGINE_E2_02_SAMPLING_HPP
#define DS_ENGINE_E2_02_SAMPLING_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "metric.hpp"  // E2-01：沿用 MetricId 作指標身分

namespace ds::metrics {

// 邏輯時間單位。非真實時鐘——呼叫端每一輪採集推進一格（或以任意單調刻度表達）。
// 平台中立的關鍵：本層只認識「第幾 tick」，不認識毫秒 / 系統時鐘。
using Tick = std::uint64_t;

// 需求票根：add_demand() 回傳，供該消費者日後 remove_demand() 撤銷自己的需求。
// 0 為無效值（永不由 add_demand 回傳）。
using DemandId = std::uint64_t;

// ---------------------------------------------------------------------------
// SamplingTier：採集頻率分級
// ---------------------------------------------------------------------------
// 四級，頻率由高到低：
//   High     — 高頻（如前景動畫用的 CPU 負載，要跟手）
//   Normal   — 常規（多數指標的預設）
//   Low      — 低頻（變動慢的指標，如磁碟容量、電池健康）
//   OnDemand — 隨需（**不週期採集**；只有呼叫端明確請求時才採一次，如展開才顯示的細項）
enum class SamplingTier {
    High,
    Normal,
    Low,
    OnDemand,
};

// 頻率位階：數字越大 = 越頻繁。供除頻時挑「最高需求頻率」。
// High=3 > Normal=2 > Low=1 > OnDemand=0（OnDemand 非週期，位階最低）。
int tier_rank(SamplingTier tier) noexcept;

// 兩級中較高頻者（除頻合併的核心）。位階相同回 a。
SamplingTier higher_tier(SamplingTier a, SamplingTier b) noexcept;

// 是否為週期性分級（OnDemand 以外皆是）。
inline bool is_periodic(SamplingTier tier) noexcept { return tier != SamplingTier::OnDemand; }

// 診斷用穩定字串（"high"/"normal"/"low"/"on-demand"）。
const char* to_string(SamplingTier tier) noexcept;

// ---------------------------------------------------------------------------
// SamplingPolicy：分級 → 採集間隔（tick 數）的映射
// ---------------------------------------------------------------------------
// 每個週期性分級對應一個「間隔」：間隔 N = 每 N 個 tick 採集一次（N==1 = 每 tick）。
// OnDemand 無週期間隔（回 nullopt）。預設值刻意拉開級距，讓 idle 門檻（NFR-01）成立：
// 低頻指標閒置時只偶爾採集，不與高頻指標同步吃資源。
//
// 保持可調（`set_interval`）：不同部署 / 電源狀態可換一組 policy（如省電模式整體放慢），
// 而排程器邏輯不變。
class SamplingPolicy {
public:
    // 預設：High=1、Normal=8、Low=64（tick），OnDemand=無週期。
    SamplingPolicy();

    // 具名工廠（等同預設建構，語意更清楚）。
    static SamplingPolicy defaults();

    // 該分級的採集間隔（tick）。OnDemand 回 nullopt（非週期）。
    std::optional<Tick> interval(SamplingTier tier) const noexcept;

    // 設定某週期性分級的間隔。ticks 會被夾到 >=1（間隔 0 無意義）。
    // 對 OnDemand 呼叫為 no-op（它恆為非週期）。回傳 *this 供鏈式設定。
    SamplingPolicy& set_interval(SamplingTier tier, Tick ticks) noexcept;

private:
    // 索引 = tier_rank 的週期三級：[Low..High] 存於 0..2；OnDemand 不佔位。
    Tick high_ = 1;
    Tick normal_ = 8;
    Tick low_ = 64;
};

// ---------------------------------------------------------------------------
// SamplingScheduler：分級排程 + 除頻合併
// ---------------------------------------------------------------------------
// 消費者對「想要哪個指標、要多頻繁」下需求（add_demand）；排程器把同一指標的多個需求
// **除頻合併**：有效分級 = 所有需求中最高頻者，一次採集供所有消費者。呼叫端每輪以
// advance(now) 推進邏輯時間，取回「此刻該採集的指標清單」（決定性順序），據此去讀一次
// 感測器並更新 E2-01 的 Metric。
//
// 純邏輯、可注入時間：排程只看邏輯 tick 與各指標的間隔，完全可單元測試
// （驗某分級在 T 個 tick 內恰採集 T/N 次、驗多消費者合併只採一份、驗 on-demand 只在
// request_now 後採一次、驗撤銷需求後降頻或停採）。
class SamplingScheduler {
public:
    explicit SamplingScheduler(SamplingPolicy policy = SamplingPolicy::defaults());

    // 目前的邏輯時間（初始 0）。
    Tick now() const noexcept { return now_; }

    // 生效中的分級 policy。
    const SamplingPolicy& policy() const noexcept { return policy_; }

    // -- 需求登記（除頻的入口）--------------------------------------------

    // 登記一筆「某消費者要 id 這個指標、頻率為 tier」的需求。回傳票根供日後撤銷。
    // 同一指標的多筆需求會被除頻合併（有效分級 = 最高頻者）。新指標的首採排在「下一次
    // advance 到達目前 tick」時；若新需求把有效頻率拉高，會把下次採集時機相應提前。
    DemandId add_demand(const MetricId& id, SamplingTier tier);

    // 撤銷一筆需求。當某指標最後一筆需求被撤銷，該指標即停止被排程。
    // 撤銷後有效頻率可能下降（少了最高頻的消費者），排程器據新的有效分級續行。
    // 找不到票根回 false。
    bool remove_demand(DemandId demand);

    // -- 除頻 / 排程查詢 --------------------------------------------------

    // 某指標目前的有效（除頻後）分級 = 所有存活需求中最高頻者。無需求回 nullopt。
    std::optional<SamplingTier> effective_tier(const MetricId& id) const;

    // 某指標目前的有效採集間隔（tick）。無需求、或所有需求皆 OnDemand 時回 nullopt
    // （代表「無週期採集」，只靠 request_now 觸發）。
    std::optional<Tick> effective_interval(const MetricId& id) const;

    // 某指標目前登記的需求筆數。
    std::size_t demand_count(const MetricId& id) const;

    // 是否正在追蹤該指標（至少一筆存活需求）。
    bool tracks(const MetricId& id) const;

    // 目前被追蹤的指標數（有 >=1 筆需求者）。
    std::size_t metric_count() const noexcept { return metrics_.size(); }

    // 某指標的下次週期採集時機（tick）。未追蹤、或非週期（全 OnDemand）時回 nullopt。
    std::optional<Tick> next_due(const MetricId& id) const;

    // -- 觸發 -------------------------------------------------------------

    // 請求某指標在下一次 advance 時採集一份，**無視間隔**。這是驅動 OnDemand 指標的手段
    // （它們不週期採集），對週期指標亦可用於「立即刷新一次」。未追蹤的 id 為 no-op。
    void request_now(const MetricId& id);

    // 把邏輯時間推進到 now（須 >= 目前 now()，否則視為不推進、回空清單），回傳此刻
    // **該採集**的指標清單（決定性順序 = 指標首次登記順序），並將其標記為已於 now 採集
    // （下次時機 = now + 有效間隔）。跨越多個間隔時，一次 advance 對同一指標仍只採一份
    // （不補採、不爆量——直接關係 idle 門檻：閒置久了醒來不該一次灌一堆採集）。
    std::vector<MetricId> advance(Tick now);

private:
    struct MetricState {
        std::size_t order = 0;   // 首次登記序（決定性輸出用）
        std::vector<std::pair<DemandId, SamplingTier>> demands;  // 存活需求
        Tick next_due = 0;       // 下次週期採集 tick（僅週期指標有意義）
        bool pending_now = false;  // request_now 旗標：下次 advance 強制採一份
    };

    std::optional<SamplingTier> effective_tier_of(const MetricState& st) const;
    std::optional<Tick> interval_of(const MetricState& st) const;

    SamplingPolicy policy_;
    Tick now_ = 0;
    DemandId next_demand_id_ = 1;
    std::size_t next_order_ = 0;
    std::unordered_map<MetricId, MetricState> metrics_;
    std::unordered_map<DemandId, MetricId> demand_index_;  // 票根 → 所屬指標
};

}  // namespace ds::metrics

#endif  // DS_ENGINE_E2_02_SAMPLING_HPP
