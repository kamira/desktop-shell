// tests/e7/test_e7_06.cpp — E7-06 錯誤可見性 契約測試
//
// 驗證：把 E7-01 的解析/驗證錯誤呈現為人類可讀、定位到行/欄的診斷（NFR-04）。
//   1. 錯誤格式化含行號（單筆定位）。
//   2. 上下文片段 + 指示位置（可讀性：來源行 + `^` 插字符）。
//   3. 多錯彙整（標頭載明筆數、全部原封輸出、順序保持）。
//   4. 明確不靜默（空集合明確聲明、訊息永不遺漏、行號超範圍不硬造）。
//   5. 與 E7-01 `parse()` 真實失敗整合（吃真正的 ParseError）。

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "document.hpp"    // E7-01
#include "visibility.hpp"  // E7-06

using namespace ds::format;

namespace {

// 樣本來源文字（多行；行號 1-based）。
const std::string kSource =
    "format_version: 1.0\n"  // line 1
    "name: hello\n"          // line 2
    "name: dup\n"            // line 3（重複 key）
    "width: 800\n";          // line 4

// 子字串包含輔助。
bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. 錯誤格式化含行號
// -----------------------------------------------------------------------------

TEST(E7_06_LineNumber, SingleErrorShowsLineNumber) {
    ParseError err{3, "duplicate key 'name'"};
    const std::string out = format_error(err, kSource);

    EXPECT_TRUE(contains(out, "duplicate key 'name'"));  // 訊息保留
    EXPECT_TRUE(contains(out, "error:"));                // 嚴重度標籤
    EXPECT_TRUE(contains(out, "line 3"));                // 定位到行
}

TEST(E7_06_LineNumber, DiagnosticWithColumnShowsLineAndColumn) {
    Diagnostic d{3, 7, "duplicate key 'name'"};
    const std::string out = format_diagnostic(d, kSource);

    EXPECT_TRUE(contains(out, "line 3:7"));  // 行:欄
}

TEST(E7_06_LineNumber, ColumnUnknownShowsLineOnly) {
    Diagnostic d{2, "something"};  // column 預設 0
    const std::string out = format_diagnostic(d, kSource);

    EXPECT_TRUE(contains(out, "line 2"));
    EXPECT_FALSE(contains(out, "line 2:"));  // 無欄號後綴
}

// -----------------------------------------------------------------------------
// 2. 可讀性：上下文片段 + 指示位置
// -----------------------------------------------------------------------------

TEST(E7_06_Readability, IncludesSourceLineSnippet) {
    Diagnostic d{3, 1, "duplicate key 'name'"};
    const std::string out = format_diagnostic(d, kSource);

    // 應含第 3 行原始內容作為上下文。
    EXPECT_TRUE(contains(out, "name: dup"));
    // 側欄含行號。
    EXPECT_TRUE(contains(out, "3 | "));
}

TEST(E7_06_Readability, CaretPointsAtColumn) {
    // column = 7 → 插字符前應有 6 個空白再一個 '^'。
    Diagnostic d{3, 7, "x"};
    const std::string out = format_diagnostic(d, kSource);

    EXPECT_TRUE(contains(out, "^"));
    EXPECT_TRUE(contains(out, "|       ^"));  // "| " + 6 空白 + '^'
}

TEST(E7_06_Readability, CaretColumnOneHasNoLeadingSpaces) {
    Diagnostic d{2, 1, "x"};
    const std::string out = format_diagnostic(d, kSource);
    EXPECT_TRUE(contains(out, "| ^"));  // 第 1 欄：插字符緊接分隔
}

TEST(E7_06_Readability, NoCaretWhenColumnUnknown) {
    Diagnostic d{2, "x"};  // column 0
    const std::string out = format_diagnostic(d, kSource);
    EXPECT_TRUE(contains(out, "name: hello"));  // 仍有上下文
    EXPECT_FALSE(contains(out, "^"));            // 但無插字符
}

TEST(E7_06_Readability, ShowContextOptionSuppressesSnippet) {
    Diagnostic d{3, 7, "x"};
    FormatOptions opts;
    opts.show_context = false;
    const std::string out = format_diagnostic(d, kSource, opts);
    EXPECT_TRUE(contains(out, "line 3:7"));      // 定位仍在
    EXPECT_FALSE(contains(out, "name: dup"));    // 但無上下文片段
}

TEST(E7_06_Readability, ShowCaretOptionSuppressesCaret) {
    Diagnostic d{3, 7, "x"};
    FormatOptions opts;
    opts.show_caret = false;
    const std::string out = format_diagnostic(d, kSource, opts);
    EXPECT_TRUE(contains(out, "name: dup"));  // 上下文仍在
    EXPECT_FALSE(contains(out, "^"));          // 但無插字符
}

TEST(E7_06_Readability, HandlesCRLFSource) {
    const std::string crlf = "format_version: 1.0\r\nname: hi\r\n";
    Diagnostic d{2, 1, "x"};
    const std::string out = format_diagnostic(d, crlf);
    EXPECT_TRUE(contains(out, "name: hi"));
    EXPECT_FALSE(contains(out, "\r"));  // '\r' 應被剝除
}

// -----------------------------------------------------------------------------
// 3. 多錯彙整
// -----------------------------------------------------------------------------

TEST(E7_06_Aggregate, ReportHeaderStatesCount) {
    std::vector<Diagnostic> diags{
        Diagnostic{2, 1, "first problem"},
        Diagnostic{3, 1, "second problem"},
        Diagnostic{4, 1, "third problem"},
    };
    const std::string out = format_report(diags, kSource);
    EXPECT_TRUE(contains(out, "3 diagnostics:"));  // 筆數載明
}

TEST(E7_06_Aggregate, ReportContainsEveryError) {
    std::vector<Diagnostic> diags{
        Diagnostic{2, 1, "first problem"},
        Diagnostic{3, 1, "second problem"},
        Diagnostic{4, 1, "third problem"},
    };
    const std::string out = format_report(diags, kSource);
    // 全部原封輸出，一筆不漏（不折疊）。
    EXPECT_TRUE(contains(out, "first problem"));
    EXPECT_TRUE(contains(out, "second problem"));
    EXPECT_TRUE(contains(out, "third problem"));
}

TEST(E7_06_Aggregate, ReportPreservesOrder) {
    std::vector<Diagnostic> diags{
        Diagnostic{2, 1, "alpha"},
        Diagnostic{3, 1, "beta"},
    };
    const std::string out = format_report(diags, kSource);
    EXPECT_LT(out.find("alpha"), out.find("beta"));  // 順序保持
}

TEST(E7_06_Aggregate, SingularHeaderForOne) {
    std::vector<Diagnostic> diags{Diagnostic{2, 1, "solo"}};
    const std::string out = format_report(diags, kSource);
    EXPECT_TRUE(contains(out, "1 diagnostic:"));
    EXPECT_FALSE(contains(out, "1 diagnostics:"));  // 單數正確
}

TEST(E7_06_Aggregate, FormatErrorsBridgesParseErrors) {
    std::vector<ParseError> errs{
        ParseError{2, "bad one"},
        ParseError{3, "bad two"},
    };
    const std::string out = format_errors(errs, kSource);
    EXPECT_TRUE(contains(out, "2 diagnostics:"));
    EXPECT_TRUE(contains(out, "bad one"));
    EXPECT_TRUE(contains(out, "bad two"));
}

// -----------------------------------------------------------------------------
// 4. 明確不靜默
// -----------------------------------------------------------------------------

TEST(E7_06_NonSilent, EmptyReportIsExplicitNotBlank) {
    std::vector<Diagnostic> none;
    const std::string out = format_report(none, kSource);
    EXPECT_FALSE(out.empty());                 // 絕不回傳空字串
    EXPECT_TRUE(contains(out, "no diagnostics"));
}

TEST(E7_06_NonSilent, LineZeroStillShowsMessage) {
    // 非特定行錯誤（缺必填欄位 / 版本不相容）：無法定位到行，仍必須把訊息印出來。
    Diagnostic d{0, "missing required field format_version"};
    const std::string out = format_diagnostic(d, kSource);
    EXPECT_TRUE(contains(out, "missing required field format_version"));
    EXPECT_TRUE(contains(out, "error:"));
    EXPECT_FALSE(contains(out, "line 0"));  // 不謊報 line 0 定位
}

TEST(E7_06_NonSilent, OutOfRangeLineKeepsMessageAndLine) {
    // 行號超出來源範圍：不硬造上下文，但訊息與行號不得消失。
    Diagnostic d{999, 1, "phantom line error"};
    const std::string out = format_diagnostic(d, kSource);
    EXPECT_TRUE(contains(out, "phantom line error"));  // 訊息保留
    EXPECT_TRUE(contains(out, "line 999"));            // 行號保留
}

TEST(E7_06_NonSilent, ReportErrorCountReflectsAll) {
    DiagnosticReport report;
    report.add(Diagnostic{2, 1, "e1"});
    report.add(Diagnostic{3, 1, "e2"});
    EXPECT_EQ(report.size(), 2u);
    EXPECT_EQ(report.error_count(), 2u);
    EXPECT_FALSE(report.ok());  // 有 Error → 非 ok
}

TEST(E7_06_NonSilent, WarningsDoNotCountAsErrors) {
    DiagnosticReport report;
    Diagnostic warn{2, 1, "just a warning"};
    warn.severity = Severity::Warning;
    report.add(warn);
    EXPECT_EQ(report.size(), 1u);
    EXPECT_EQ(report.error_count(), 0u);
    EXPECT_TRUE(report.ok());  // 僅 Warning → 仍 ok
}

// -----------------------------------------------------------------------------
// 5. DiagnosticReport 收集器與 severity 標籤
// -----------------------------------------------------------------------------

TEST(E7_06_Report, RenderMatchesFormatReport) {
    DiagnosticReport report;
    report.add_parse_error(ParseError{3, "dup key"});
    const std::string via_report = report.render(kSource);
    EXPECT_TRUE(contains(via_report, "1 diagnostic:"));
    EXPECT_TRUE(contains(via_report, "dup key"));
}

TEST(E7_06_Report, SeverityLabels) {
    EXPECT_STREQ(severity_label(Severity::Error), "error");
    EXPECT_STREQ(severity_label(Severity::Warning), "warning");
    EXPECT_STREQ(severity_label(Severity::Note), "note");
}

TEST(E7_06_Report, WarningSeverityAppearsInOutput) {
    Diagnostic d{2, 1, "deprecated field"};
    d.severity = Severity::Warning;
    const std::string out = format_diagnostic(d, kSource);
    EXPECT_TRUE(contains(out, "warning: deprecated field"));
}

// -----------------------------------------------------------------------------
// 6. 與 E7-01 parse() 真實失敗整合
// -----------------------------------------------------------------------------

TEST(E7_06_Integration, FormatsRealDuplicateKeyError) {
    // E7-01 對重複 key 應失敗並帶行號。E7-06 把它變成可讀輸出。
    const std::string bad =
        "format_version: 1.0\n"
        "name: a\n"
        "name: b\n";
    ParseResult r = parse(bad);
    ASSERT_FALSE(r.ok());
    const ParseError& e = r.error();

    const std::string out = format_error(e, bad);
    EXPECT_TRUE(contains(out, "error:"));
    EXPECT_FALSE(out.empty());
    // parse() 對此類錯誤帶具體行號 → 輸出應含定位。
    if (e.line > 0) {
        EXPECT_TRUE(contains(out, "line " + std::to_string(e.line)));
    } else {
        // 即使非特定行，訊息仍不得消失（不靜默）。
        EXPECT_TRUE(contains(out, e.message));
    }
}

TEST(E7_06_Integration, FormatsRealTabIndentError) {
    // E7-01 明確拒絕 tab 縮排並定位。
    const std::string bad =
        "format_version: 1.0\n"
        "window:\n"
        "\twidth: 800\n";  // tab 縮排 → 錯誤
    ParseResult r = parse(bad);
    ASSERT_FALSE(r.ok());
    const std::string out = format_error(r.error(), bad);
    EXPECT_TRUE(contains(out, "error:"));
    EXPECT_TRUE(contains(out, r.error().message));  // 原因保留
}
