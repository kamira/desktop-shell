// E7-06 錯誤可見性 — 把 E7-01 解析/驗證錯誤呈現為人類可讀、定位到行/欄的診斷（平台中立 / engine 層）
//
// 本單元建構於 E7-01 的 `ParseError{line, message}` / `ParseResult` 之上：把「機器友善」的
// 解析錯誤，轉成「人看得懂、指得到位置」的診斷輸出。對齊 NFR-04：**描述格式錯誤須定位到行、
// 不得靜默**。屬「描述子系統」的一環——任何消費 E7-01 `parse()` 的單元（設定 / profile /
// manifest / 內容）在失敗時都可用本層把錯誤印給人看。
//
// 設計原則（延續 E7-01 / E7-02）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **不得靜默**（NFR-04）：格式化空集合會回傳明確的「無診斷」字串而非空字串；彙整 N 筆錯誤
//     必定原封輸出全部 N 筆並在標頭載明筆數——絕不吞掉、絕不折疊任何一筆。
//   - **消費上游、不重造輪子**：直接吃 E7-01 的 `ParseError`；只新增呈現層，不碰解析邏輯。
//
// 輸出風格（Rust 式診斷，極易讀）：
//     error: duplicate key 'name'
//      --> line 3:7
//       |
//     3 | name: foo
//       |       ^
//   - 首行：`<severity>: <message>`（severity ∈ error / warning / note）。
//   - 定位行：` --> line L:C`（僅有行號時為 ` --> line L`；`line == 0` 之非特定錯誤則整段省略）。
//   - 上下文：從來源文字取出第 L 行原樣呈現，帶行號側欄；`^` 插字符指向第 C 欄（1-based）。
//   - 欄位未知（`column == 0`）→ 顯示來源行但不畫插字符；行號超出範圍 → 省略上下文，仍印訊息與行號。
#ifndef DS_ENGINE_E7_06_VISIBILITY_HPP
#define DS_ENGINE_E7_06_VISIBILITY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "document.hpp"  // E7-01：ds::format::ParseError / ParseResult

namespace ds::format {

// -----------------------------------------------------------------------------
// 診斷嚴重度
// -----------------------------------------------------------------------------

// 診斷嚴重度。E7-01 的解析失敗一律為 Error；Warning / Note 供呼叫端補充非致命訊息。
enum class Severity {
    Error,
    Warning,
    Note,
};

// 嚴重度的小寫標籤（"error" / "warning" / "note"），用於首行前綴。
const char* severity_label(Severity s) noexcept;

// -----------------------------------------------------------------------------
// 診斷：ParseError + 可選欄位，呈現層的統一單位
// -----------------------------------------------------------------------------

// 一筆可呈現的診斷。是 E7-01 `ParseError` 的呈現層對應物，額外帶「欄」與「嚴重度」。
//   - line：1-based 來源行號；0 = 非特定行（如缺必填欄位、版本不相容）。沿用 ParseError 語意。
//   - column：1-based 欄號；0 = 欄位未知（ParseError 未帶欄時的預設）。
//   - message：人類可讀原因（原樣取自 ParseError.message）。
struct Diagnostic {
    Severity severity = Severity::Error;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string message;

    Diagnostic() = default;
    Diagnostic(std::size_t line_, std::string message_)
        : line(line_), message(std::move(message_)) {}
    Diagnostic(std::size_t line_, std::size_t column_, std::string message_)
        : line(line_), column(column_), message(std::move(message_)) {}

    // 由 E7-01 ParseError 建構（severity = Error，column 未知）。
    static Diagnostic from_parse_error(const ParseError& e);
    // 由 ParseError 建構並補上已知欄號（呼叫端另行得知欄位時使用）。
    static Diagnostic from_parse_error(const ParseError& e, std::size_t column);
};

// -----------------------------------------------------------------------------
// 格式化選項
// -----------------------------------------------------------------------------

// 控制呈現細節。預設全開、無顏色——輸出決定性、易於單元測試。
struct FormatOptions {
    bool show_context = true;  // 是否印出肇因來源行（帶行號側欄）。
    bool show_caret = true;    // 是否畫插字符 `^`（僅在 column > 0 且有上下文時生效）。
};

// -----------------------------------------------------------------------------
// 格式化：診斷 / 錯誤 → 人類可讀字串（定位到行/欄，不靜默）
// -----------------------------------------------------------------------------

// 格式化單筆診斷為人類可讀、定位到行/欄的多行字串（末尾無換行）。
std::string format_diagnostic(const Diagnostic& diag,
                              const std::string& source,
                              const FormatOptions& opts = {});

// 直接格式化一筆 E7-01 ParseError（最常見的橋接：parse() 失敗 → 印給人看）。
std::string format_error(const ParseError& err,
                         const std::string& source,
                         const FormatOptions& opts = {});

// 彙整多筆診斷為單一報告：標頭載明筆數，各筆以空行分隔。
// **不靜默**：空集合回傳明確的 "no diagnostics"；非空必定原封輸出全部。
std::string format_report(const std::vector<Diagnostic>& diags,
                          const std::string& source,
                          const FormatOptions& opts = {});

// 彙整多筆 E7-01 ParseError（等價於先各自 from_parse_error 再 format_report）。
std::string format_errors(const std::vector<ParseError>& errors,
                          const std::string& source,
                          const FormatOptions& opts = {});

// -----------------------------------------------------------------------------
// 診斷收集器：累積、查詢、渲染（保證不遺漏）
// -----------------------------------------------------------------------------

// 累積多筆診斷並一次渲染。用於邊解析邊蒐集（或彙整多來源），最後統一呈現。
// 保證不靜默：加入的每一筆都會被保留並於 render() 輸出；error_count() 讓呼叫端據以決定成敗。
class DiagnosticReport {
public:
    DiagnosticReport() = default;

    void add(Diagnostic d);                       // 加入一筆診斷。
    void add_parse_error(const ParseError& e);    // 由 ParseError 加入一筆（severity = Error）。

    bool empty() const noexcept { return diags_.empty(); }
    std::size_t size() const noexcept { return diags_.size(); }
    std::size_t error_count() const noexcept;     // Error 嚴重度的筆數。
    // 無任何 Error 嚴重度診斷（Warning / Note 不影響）→ true。
    bool ok() const noexcept { return error_count() == 0; }

    const std::vector<Diagnostic>& diagnostics() const noexcept { return diags_; }

    // 針對來源文字渲染全部診斷為單一報告（同 format_report 的語意，不靜默）。
    std::string render(const std::string& source, const FormatOptions& opts = {}) const;

private:
    std::vector<Diagnostic> diags_;
};

}  // namespace ds::format

#endif  // DS_ENGINE_E7_06_VISIBILITY_HPP
