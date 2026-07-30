// E1-22 建置期能力閘控 lint — 平台中立靜態檢查核心
//
// 一個**建置期 / 靜態**的原始碼 lint，強制兩條 NFR：
//   - NFR-02：核心 API 不得出現絕對座標與數字 z-order（硬編碼像素座標、`z_order = 數字`）。
//   - NFR-03：能力閘控呼叫必須有 `has()` 保護（使用能力 API 卻無先行 `has(capability)` 檢查）。
//
// 本單元只交「可餵字串測試的純核心」：輸入一段原始碼文字，輸出違規清單
// （規則 id、行號、訊息）。真正「掃整個 repo」的 CI 驅動另議（見結構註記）。
//
// 相位 1（Mac / null 期）約束：純文字 / 規則分析邏輯，無 `#ifdef` / win32 / cocoa
// 平台分支，不綁任何真實後端。NFR-03 的「能力宣告」單一資料來源沿用上游
// E1-21 `ds::kernel::CapabilityMatrix`：預設閘控 API 名單由其 optional 能力導出。
#ifndef DS_KERNEL_E1_22_CAPABILITY_LINT_HPP
#define DS_KERNEL_E1_22_CAPABILITY_LINT_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "capability_matrix.hpp"  // 上游 E1-21（可讀不可改）

namespace ds::kernel {

// lint 規則種類。rule_id() 給出對應的穩定 NFR 代號字串。
enum class LintRule {
    AbsoluteCoordinate,   // NFR-02：硬編碼絕對 / 像素座標
    NumericZOrder,        // NFR-02：數字 z-order
    UnguardedCapability,  // NFR-03：能力 API 呼叫無 has() 保護
};

// 單一診斷：可定位（1-based 行號）、不靜默（帶規則代號與訊息）。
struct LintDiagnostic {
    LintRule rule;         // 規則種類
    std::string rule_id;   // 穩定代號："NFR-02" / "NFR-03"
    std::size_t line;      // 違規所在行（1-based）
    std::string message;   // 人類可讀說明

    // 便於測試比對。
    bool operator==(const LintDiagnostic& o) const {
        return rule == o.rule && rule_id == o.rule_id && line == o.line &&
               message == o.message;
    }
};

// 建置期能力閘控 lint 核心。
//
// 純函式式：`lint(source)` 不持狀態、可重入。閘控 API 名單決定 NFR-03 要盯哪些
// 「能力消費呼叫」：預設由 E1-21 能力矩陣的 optional 能力導出（id 尾段為 API 名），
// 亦可由測試 / 驅動端以自訂名單建構，以求決定性。
class CapabilityLint {
public:
    // 預設：閘控 API 名單由 CapabilityMatrix::defaults() 的 optional 能力導出。
    CapabilityLint();

    // 由自訂閘控 API 名單建構（供測試與客製驅動）。
    explicit CapabilityLint(std::vector<std::string> gated_api_names);

    // 掃描一段原始碼，回傳違規清單（依行號、同行內依規則順序遞增）。
    std::vector<LintDiagnostic> lint(const std::string& source) const;

    // 目前生效的閘控 API 名單（NFR-03 盯的能力消費呼叫名）。
    const std::vector<std::string>& gated_apis() const noexcept { return gated_apis_; }

    // 由能力矩陣的 optional 能力導出閘控 API 名單（id 最後一段，如
    // "host.tray_icon" -> "tray_icon"）。公開以便驅動端重用同一套推導。
    static std::vector<std::string> gated_apis_from_matrix(const CapabilityMatrix& m);

private:
    std::vector<std::string> gated_apis_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_22_CAPABILITY_LINT_HPP
