// E7-06 錯誤可見性 — 實作。見 visibility.hpp 檔首說明。
// 平台中立、純字串邏輯：無 `#ifdef`、無系統呼叫、無真實後端。

#include "visibility.hpp"

#include <string>
#include <vector>

namespace ds::format {

// -----------------------------------------------------------------------------
// 內部工具（無外部連結）
// -----------------------------------------------------------------------------
namespace {

// 取來源文字第 line_no 行（1-based）的內容。找不到（0 或超出範圍）→ found = false。
// 以 '\n' 切分；每行不含換行字元。'\r' 若存在則自尾端剝除（相容 CRLF）。
std::string extract_line(const std::string& source, std::size_t line_no, bool& found) {
    found = false;
    if (line_no == 0) return {};

    std::size_t current = 1;
    std::size_t start = 0;
    const std::size_t n = source.size();
    for (std::size_t i = 0; i <= n; ++i) {
        if (i == n || source[i] == '\n') {
            if (current == line_no) {
                std::string line = source.substr(start, i - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                found = true;
                return line;
            }
            ++current;
            start = i + 1;
        }
    }
    return {};
}

// 依行號寬度產生側欄縮排（等寬空白），用於對齊 " |" 與插字符行。
std::string gutter_pad(std::size_t gutter_width) { return std::string(gutter_width, ' '); }

}  // namespace

// -----------------------------------------------------------------------------
// Severity
// -----------------------------------------------------------------------------

const char* severity_label(Severity s) noexcept {
    switch (s) {
        case Severity::Error:   return "error";
        case Severity::Warning: return "warning";
        case Severity::Note:    return "note";
    }
    return "error";  // 不可達；防禦性預設。
}

// -----------------------------------------------------------------------------
// Diagnostic
// -----------------------------------------------------------------------------

Diagnostic Diagnostic::from_parse_error(const ParseError& e) {
    Diagnostic d;
    d.severity = Severity::Error;
    d.line = e.line;
    d.column = 0;  // ParseError 未帶欄。
    d.message = e.message;
    return d;
}

Diagnostic Diagnostic::from_parse_error(const ParseError& e, std::size_t column) {
    Diagnostic d = from_parse_error(e);
    d.column = column;
    return d;
}

// -----------------------------------------------------------------------------
// 格式化單筆
// -----------------------------------------------------------------------------

std::string format_diagnostic(const Diagnostic& diag,
                              const std::string& source,
                              const FormatOptions& opts) {
    std::string out;

    // 首行：`<severity>: <message>`。訊息永遠原封呈現（不靜默）。
    out += severity_label(diag.severity);
    out += ": ";
    out += diag.message;

    // line == 0：非特定行錯誤（缺必填欄位 / 版本不相容）。省略定位與上下文，僅留訊息。
    if (diag.line == 0) {
        return out;
    }

    // 定位行：` --> line L[:C]`。
    const std::string line_str = std::to_string(diag.line);
    out += "\n --> line ";
    out += line_str;
    if (diag.column > 0) {
        out += ":";
        out += std::to_string(diag.column);
    }

    if (!opts.show_context) {
        return out;
    }

    bool found = false;
    const std::string src_line = extract_line(source, diag.line, found);
    if (!found) {
        // 行號超出來源範圍：不硬造上下文，仍保留訊息與行號（不靜默）。
        return out;
    }

    // 上下文區塊（側欄寬度依行號位數對齊）。
    const std::size_t gutter = line_str.size();
    const std::string pad = gutter_pad(gutter);

    out += "\n";
    out += pad;
    out += " |";
    out += "\n";
    out += line_str;
    out += " | ";
    out += src_line;

    // 插字符行：僅在 column > 0 且允許時繪製，指向第 column 欄（1-based，字元計數）。
    if (opts.show_caret && diag.column > 0) {
        out += "\n";
        out += pad;
        out += " | ";
        // column - 1 個前導空白，接著單一 '^'。
        out += std::string(diag.column - 1, ' ');
        out += "^";
    }

    return out;
}

std::string format_error(const ParseError& err,
                         const std::string& source,
                         const FormatOptions& opts) {
    return format_diagnostic(Diagnostic::from_parse_error(err), source, opts);
}

// -----------------------------------------------------------------------------
// 彙整多筆
// -----------------------------------------------------------------------------

std::string format_report(const std::vector<Diagnostic>& diags,
                          const std::string& source,
                          const FormatOptions& opts) {
    // 不靜默：空集合明確聲明無診斷，而非回傳空字串（避免「靜默成功」的假象）。
    if (diags.empty()) {
        return "no diagnostics";
    }

    // 標頭載明筆數（單複數正確），讓呼叫端一眼看出共有幾筆——絕不折疊。
    std::string out;
    out += std::to_string(diags.size());
    out += (diags.size() == 1 ? " diagnostic:" : " diagnostics:");

    // 每筆之間以空行分隔，保證全部原封輸出。
    for (const auto& d : diags) {
        out += "\n\n";
        out += format_diagnostic(d, source, opts);
    }
    return out;
}

std::string format_errors(const std::vector<ParseError>& errors,
                          const std::string& source,
                          const FormatOptions& opts) {
    std::vector<Diagnostic> diags;
    diags.reserve(errors.size());
    for (const auto& e : errors) {
        diags.push_back(Diagnostic::from_parse_error(e));
    }
    return format_report(diags, source, opts);
}

// -----------------------------------------------------------------------------
// DiagnosticReport
// -----------------------------------------------------------------------------

void DiagnosticReport::add(Diagnostic d) { diags_.push_back(std::move(d)); }

void DiagnosticReport::add_parse_error(const ParseError& e) {
    diags_.push_back(Diagnostic::from_parse_error(e));
}

std::size_t DiagnosticReport::error_count() const noexcept {
    std::size_t n = 0;
    for (const auto& d : diags_) {
        if (d.severity == Severity::Error) ++n;
    }
    return n;
}

std::string DiagnosticReport::render(const std::string& source,
                                     const FormatOptions& opts) const {
    return format_report(diags_, source, opts);
}

}  // namespace ds::format
