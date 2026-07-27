// E7-07 熱重載 — 宣告式格式文件的熱重載生命週期（平台中立 / engine 層）
//
// 本單元建構於 E7-01（`Value` / `parse()` / `Document` / `ParseError`）與 E7-06（`Diagnostic` /
// `from_parse_error` / `format_report`）之上：把「來源內容變更 → 重新解析 → 差異套用新設定」
// 的生命週期封裝成一個狀態機。核心承諾：
//   - **偵測變更才重載**：來源未變更則不重新解析、不觸發回呼（no-op）。
//   - **失敗保舊值**（NFR-04 精神）：重新解析失敗時**保留上一個有效狀態**，絕不以壞輸入覆蓋，
//     並用 E7-06 的診斷把錯誤定位到行呈現給人看（不靜默）。
//   - **可觀測**：成功 / 失敗各有回呼；失敗診斷可事後查詢。
//
// 相位 1 平台中立（Mac / null 期）：**不接任何真實檔案系統監看**（無 inotify / FSEvents /
// `#ifdef` / `win32` / `cocoa`）。來源以**可注入的抽象** `ReloadSource` 表示：提供「目前內容」
// 與「變更通知」（單調遞增的 revision 令牌）。真實檔案監看留待相位 2，屆時只需實作一個
// 綁定平台 API 的 `ReloadSource` 具體類別即可，本狀態機一行不動。
//
// 命名空間：`ds::format`。
#ifndef DS_ENGINE_E7_07_HOT_RELOAD_HPP
#define DS_ENGINE_E7_07_HOT_RELOAD_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "document.hpp"    // E7-01：ds::format::Document / parse() / ParseError
#include "visibility.hpp"  // E7-06：ds::format::Diagnostic / format_report / FormatOptions

namespace ds::format {

// -----------------------------------------------------------------------------
// 可注入來源：目前內容 + 變更通知（平台中立）
// -----------------------------------------------------------------------------

// 熱重載的來源抽象。**注入式**、平台中立：不綁任何真實檔案系統。
//   - content()：回傳來源目前的完整宣告式文件內容。
//   - revision()：單調遞增的變更令牌。內容改變一次即遞增一次；未變則不變。
//     熱重載器據此判斷「是否需要重新解析」——這即是相位 1 的「變更通知」機制
//     （相位 2 的真實檔案監看只需在偵測到檔案變動時遞增此令牌）。
class ReloadSource {
public:
    virtual ~ReloadSource() = default;

    // 來源目前的完整內容（宣告式格式文字）。
    virtual std::string content() const = 0;

    // 單調遞增的內容修訂令牌；內容變更時嚴格遞增，未變則保持不變。
    virtual std::uint64_t revision() const = 0;
};

// 記憶體內來源——相位 1 的注入式具體實作（供狀態機驅動與測試使用），完全無檔案系統。
// set_content() 模擬「外部來源變更」：更新內容並遞增 revision（僅在內容確實不同時遞增）。
class MemorySource : public ReloadSource {
public:
    MemorySource() = default;
    explicit MemorySource(std::string initial);

    std::string content() const override;
    std::uint64_t revision() const override;

    // 設定新內容。內容與現值不同 → 更新並遞增 revision（觸發下次 poll 重載）；
    // 內容相同 → 不動 revision（無變更、不重載）。
    void set_content(std::string next);

private:
    std::string content_;
    std::uint64_t revision_ = 1;  // 建構即視為「第 1 版」，供首次載入。
};

// -----------------------------------------------------------------------------
// 重載結果
// -----------------------------------------------------------------------------

// 一次重載嘗試的結果狀態。
enum class ReloadStatus {
    Unchanged,  // 來源未變更 → 未重新解析、未觸發回呼。
    Loaded,     // 重新解析成功 → 當前有效狀態已替換為新值。
    Failed,     // 重新解析失敗 → 保留上一個有效狀態；診斷已產生。
};

// 一次重載嘗試的完整結果。失敗時 `diagnostics` / `report` 載明肇因（E7-06，定位到行）。
struct ReloadResult {
    ReloadStatus status = ReloadStatus::Unchanged;
    bool has_document = false;             // 呼叫後是否持有有效 Document（失敗保舊值時仍可能為 true）。
    std::vector<Diagnostic> diagnostics;   // 僅 Failed 時非空（沿用 E7-06 Diagnostic）。
    std::string report;                    // Failed 時為 E7-06 format_report 輸出；否則空。

    bool ok() const noexcept { return status != ReloadStatus::Failed; }
    bool reloaded() const noexcept { return status == ReloadStatus::Loaded; }
    bool unchanged() const noexcept { return status == ReloadStatus::Unchanged; }
};

// 成功回呼：重新解析成功、當前值已替換時觸發，帶新的 Document。
using ReloadCallback = std::function<void(const Document&)>;
// 失敗回呼：重新解析失敗、保留舊值時觸發，帶診斷與 E7-06 報告字串。
using ReloadErrorCallback =
    std::function<void(const std::vector<Diagnostic>&, const std::string& report)>;

// -----------------------------------------------------------------------------
// 熱重載器：載入 → 解析 → 成功替換 / 失敗保舊值 的生命週期狀態機
// -----------------------------------------------------------------------------

// 管理宣告式格式文件的熱重載。持有「當前有效 Document」，並在來源變更時重新解析：
//   - 成功 → 以新 Document 替換當前值，觸發成功回呼。
//   - 失敗 → **保留當前值**，以 E7-06 產生診斷、觸發失敗回呼（不靜默、不覆寫）。
//   - 未變 → 不動作。
// 來源以參考注入；來源生命週期須長於本物件。平台中立、無 `#ifdef`。
class HotReloader {
public:
    explicit HotReloader(ReloadSource& source, FormatOptions options = {});

    // 檢查來源 revision：有變更（或從未載入）才重新解析；未變則回 Unchanged 且不觸發回呼。
    ReloadResult poll();

    // 強制重新解析（忽略 revision 比較），並同步 revision 追蹤。
    ReloadResult reload();

    // 是否持有有效 Document（曾成功載入且未被無效輸入取代——失敗一律保舊值）。
    bool has_document() const noexcept { return loaded_; }

    // 當前有效 Document / 內容根。僅在 has_document() 為 true 時有效，否則 throw std::runtime_error。
    const Document& document() const;
    const Value& root() const;  // 便捷：document().root。

    // 最近一次重載失敗的診斷（保舊值後仍可查）。無失敗或最近一次成功 → 空。
    const std::vector<Diagnostic>& last_diagnostics() const noexcept { return last_diags_; }
    // 最近一次失敗以 E7-06 format_report 產生的報告字串；無失敗 → 空。
    const std::string& last_report() const noexcept { return last_report_; }

    // 註冊回呼（各只保留最新一個；傳空 std::function 即解除）。
    void on_reload(ReloadCallback cb) { on_reload_ = std::move(cb); }
    void on_error(ReloadErrorCallback cb) { on_error_ = std::move(cb); }

private:
    // 讀取來源內容、解析、成功替換 / 失敗保舊值，並觸發對應回呼。
    ReloadResult apply_load();

    ReloadSource& source_;
    FormatOptions options_;
    bool loaded_ = false;         // 是否持有有效 Document。
    bool seen_ = false;           // 是否曾嘗試載入（用於 revision 比較的哨兵）。
    std::uint64_t last_revision_ = 0;
    Document document_;           // 當前有效 Document（loaded_ 為 true 時有效）。
    std::vector<Diagnostic> last_diags_;
    std::string last_report_;
    ReloadCallback on_reload_;
    ReloadErrorCallback on_error_;
};

}  // namespace ds::format

#endif  // DS_ENGINE_E7_07_HOT_RELOAD_HPP
