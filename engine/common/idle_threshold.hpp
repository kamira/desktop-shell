// E12-01 idle 記憶體與 CPU 門檻 — 契約 + 純邏輯門檻管理器（engine 層 / 平台中立）
//
// 對應 NFR-01「閒置時低資源占用」。本單元是一個**閒置資源門檻管理器**：依當前
// CPU / 記憶體用量判定系統是**閒置(Idle)**或**活躍(Active)**，據此建議採集活動等級，
// 並與 E2-02 的 `SamplingTier` 整合——閒置時把採集分級**降頻**（降級一階），活躍時
// 維持原分級，藉此在閒置期壓低資源占用（idle 門檻），活躍期又不犧牲即時性。
//
// 分層約束（engine 層）：
//   - **平台中立、純邏輯**：無 `#ifdef`、無平台分支、無真實資源 API。CPU% / 記憶體%
//     的**讀值由呼叫端注入**（未來的採集迴圈會綁真實 sysinfo），本層只做門檻判斷邏輯，
//     故可完全單元測試（注入資源值 → 驗活動等級 / 驗遲滯防抖 / 驗 tier 降級）。
//   - 相依 E2-02：`target_link_libraries(e12_01 PUBLIC e2_02)`。採集分級沿用
//     `ds::metrics::SamplingTier`（不另造分級系統），閒置降級即在其上操作。
//
// 遲滯（hysteresis）：單一門檻在臨界值附近會因讀值微幅抖動而在 Idle/Active 間反覆跳動
// （thrashing），反而拉高資源。故採**雙門檻遲滯**：活躍門檻（上緣）與閒置門檻（下緣，
// = 活躍門檻 − 遲滯帶寬）之間為「維持現態」的死區，只有明確越過某一緣才切換等級。
#ifndef DS_ENGINE_E12_01_IDLE_THRESHOLD_HPP
#define DS_ENGINE_E12_01_IDLE_THRESHOLD_HPP

#include "sampling.hpp"  // E2-02：沿用 SamplingTier 作採集分級身分

namespace ds::common {

// 系統活動等級。閒置門檻管理器的輸出：
//   Active — 活躍（CPU 或記憶體用量偏高）：維持原採集頻率，跟手。
//   Idle   — 閒置（CPU 與記憶體用量皆偏低）：降頻 / 進省電，壓低資源占用（NFR-01）。
enum class ActivityLevel {
    Active,
    Idle,
};

// 診斷用穩定字串（"active" / "idle"）。
const char* to_string(ActivityLevel level) noexcept;

// 把採集分級降一階（閒置降頻的核心）：High→Normal→Low，Low 夾底（仍週期，不完全停採），
// OnDemand 保持不變（本就非週期）。以 E2-02 的 `SamplingTier` 為輸入 / 輸出。
ds::metrics::SamplingTier lower_tier(ds::metrics::SamplingTier tier) noexcept;

// ---------------------------------------------------------------------------
// IdleThresholdPolicy：閒置資源門檻管理器
// ---------------------------------------------------------------------------
// 用法：呼叫端每輪把當前 CPU% / 記憶體% 注入 `evaluate()`，取回活動等級；再以
// `recommended_tier()`（等級 → 分級）或 `adjust_tier(base)`（把某基準分級在閒置時降頻）
// 餵給 E2-02 的採集排程。等級切換帶遲滯，避免臨界抖動。
//
// 判定語意（OR 進、AND 出——寧可誤判活躍不誤判閒置，護即時性）：
//   - 活躍：CPU ≥ 活躍門檻 **或** 記憶體 ≥ 活躍門檻。
//   - 閒置：CPU < 閒置門檻 **且** 記憶體 < 閒置門檻（閒置門檻 = 活躍門檻 − 遲滯帶寬）。
//   - 死區（介於兩緣）：維持現態（遲滯防抖）。
//
// 純邏輯、可注入：不讀真實資源、不綁時鐘，門檻與遲滯皆為百分比純數，完全可單元測試。
class IdleThresholdPolicy {
public:
    // 預設門檻：CPU 活躍門檻 30%、記憶體活躍門檻 70%；遲滯帶寬 10 個百分點；
    // 初始等級 Active（保守：先假設活躍，待證實閒置才降頻）。
    IdleThresholdPolicy();

    // 具名工廠（等同預設建構）。
    static IdleThresholdPolicy defaults();

    // -- 門檻設定（含驗證）-----------------------------------------------

    // 設定 CPU / 記憶體的**活躍門檻**（百分比）。兩值皆會被夾到 [0, 100]（門檻設定驗證：
    // 負值夾 0、>100 夾 100），故傳入界外值不會產生非法門檻。回傳 *this 供鏈式設定。
    IdleThresholdPolicy& set_thresholds(double cpu_pct, double mem_pct) noexcept;

    // 設定遲滯帶寬（百分點）：閒置門檻 = 活躍門檻 − 帶寬。夾到 >= 0（負帶寬無意義 → 0，
    // 退化為單門檻）。帶寬過大時閒置門檻自然被 [0,100] 夾住（見各 *_idle_threshold）。
    IdleThresholdPolicy& set_hysteresis(double band_pct) noexcept;

    // 設定等級 → 採集分級的映射（供 `recommended_tier`）。預設 Active→High、Idle→Low。
    IdleThresholdPolicy& set_tier_mapping(ds::metrics::SamplingTier active_tier,
                                          ds::metrics::SamplingTier idle_tier) noexcept;

    // -- 門檻查詢 --------------------------------------------------------

    double cpu_active_threshold() const noexcept { return cpu_active_; }
    double mem_active_threshold() const noexcept { return mem_active_; }
    double hysteresis_band() const noexcept { return band_; }

    // 閒置門檻（下緣）= 活躍門檻 − 帶寬，夾到 [0, 活躍門檻]（不低於 0、不高於上緣）。
    double cpu_idle_threshold() const noexcept;
    double mem_idle_threshold() const noexcept;

    // -- 判定 ------------------------------------------------------------

    // 注入當前 CPU% / 記憶體%，套遲滯後更新並回傳活動等級（見類別註解之判定語意）。
    // 有狀態：等級在死區維持現態，故連續呼叫才體現遲滯。
    ActivityLevel evaluate(double current_cpu_pct, double current_mem_pct) noexcept;

    // 目前活動等級（初始 Active；由最近一次 evaluate 更新）。
    ActivityLevel level() const noexcept { return level_; }

    // 重設為指定等級（預設 Active）。供測試 / 重新開機情境明確定初態。
    void reset(ActivityLevel level = ActivityLevel::Active) noexcept { level_ = level; }

    // -- 與 E2-02 整合 ---------------------------------------------------

    // 依目前等級建議採集分級：Active → active_tier、Idle → idle_tier（見 set_tier_mapping）。
    ds::metrics::SamplingTier recommended_tier() const noexcept;

    // 把某「基準分級」依目前等級調整：Active 原樣回、Idle 降一階（`lower_tier`）。
    // 這是「閒置時把 tier 降級」的整合點——呼叫端持各指標基準分級，經此得閒置調整後分級。
    ds::metrics::SamplingTier adjust_tier(ds::metrics::SamplingTier base) const noexcept;

private:
    double cpu_active_ = 30.0;   // CPU 活躍門檻（%）
    double mem_active_ = 70.0;   // 記憶體活躍門檻（%）
    double band_ = 10.0;         // 遲滯帶寬（百分點）
    ds::metrics::SamplingTier active_tier_ = ds::metrics::SamplingTier::High;
    ds::metrics::SamplingTier idle_tier_ = ds::metrics::SamplingTier::Low;
    ActivityLevel level_ = ActivityLevel::Active;
};

}  // namespace ds::common

#endif  // DS_ENGINE_E12_01_IDLE_THRESHOLD_HPP
