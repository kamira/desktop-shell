// E7-17 `parse()` 不得讓行程結束（例外外洩 + 深層巢狀爆堆疊）— 回歸測試（gtest）
//
// 守的是 `document.hpp` 檔首既有的明文契約：「任何解析失敗一律回傳帶行號 + 訊息的
// ParseError」。E7-17 之前有兩類輸入**不會**回傳任何東西，而是終結行程：
//
//   第一類 例外外洩：`numeric_token` 的 `std::stod`、`parse_version` 的 `std::stoi`
//     在數值超出可表示範圍時丟 `std::out_of_range`。`parse()` 的 try 只接自家的
//     ParseFail，該例外一路穿出。八個生產呼叫端皆無 try/catch，`host_main.cpp` 零最外層
//     攔截；且 `std::out_of_range` 繼承自 **`std::logic_error`**，所以連
//     `layout_store.hpp:131` 文件承諾要接的 `runtime_error` 都接不到它。
//     → 故本檔一律斷言 `parse()` 這一層**完全不丟**（EXPECT_NO_THROW），
//       而不是斷言「不丟某個特定型別」。
//
//   第二類 深層巢狀：遞迴下降解析無深度上限，且 `Value` 樹的**解構也是遞迴的**。
//     實測（預設 8 MiB 堆疊）深度 14000 → SIGSEGV（退出碼 139）。
//     → 現以 `kMaxNestingDepth` 明示上限；超限是帶行號的 ParseError，不是例外、不是崩潰。
//
// 平台中立：不含任何平台分支。
#include "document.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

using ds::format::kMaxNestingDepth;
using ds::format::parse;
using ds::format::parse_scalar;
using ds::format::ParseResult;
using ds::format::Value;
using ds::format::ValueType;

namespace {

// ParseResult 的預設建構子是 private（只能由兩個工廠產生），故需要一個「尚未執行」的初值。
ParseResult unset_result() { return ParseResult::failure({0, "（尚未執行 parse）"}); }

// parse() 包一層「例外即測試失敗」：直接 EXPECT_NO_THROW 無法同時取回結果。
// 任何型別的例外都在這裡被記成失敗（不挑型別——見檔首）。
::testing::AssertionResult parse_no_throw(const std::string& text, ParseResult& out) {
    try {
        out = parse(text);
    } catch (const std::exception& e) {
        return ::testing::AssertionFailure()
               << "parse() 丟出 std::exception: " << e.what() << "（契約要求一律回傳 ParseResult）";
    } catch (...) {
        return ::testing::AssertionFailure()
               << "parse() 丟出非 std::exception 的例外（契約要求一律回傳 ParseResult）";
    }
    return ::testing::AssertionSuccess();
}

// 產生巢狀深度恰為 depth 的合法文件（根 map 本身算第 1 層）。
//   depth=1 → "format_version: 1.0\nleaf: 1\n"
//   depth=N → 根層下再 N-1 層巢狀 map，最內層放 leaf。
// 每層只縮排 1 格：巢狀只要求「嚴格更深」，用 1 格可讓極深的輸入維持很小
// （深度 2000 約 2 MB，而非數百 MB）。
std::string doc_with_depth(std::size_t depth) {
    std::string s = "format_version: 1.0\n";
    for (std::size_t j = 0; j + 1 < depth; ++j) {
        s += std::string(j, ' ') + "n:\n";
    }
    s += std::string(depth - 1, ' ') + "leaf: 1\n";
    return s;
}

// doc_with_depth(depth) 之中，第 depth 層區塊的第一行行號。
// 行 1 = format_version；行 2..depth = 各層的 "n:" 鍵；行 depth+1 = leaf。
std::size_t first_line_of_depth(std::size_t depth) { return depth + 1; }

// 以 '-' 清單表達的深度 depth 巢狀（驗清單路徑同樣受上限管束）。
std::string list_doc_with_depth(std::size_t depth) {
    std::string s = "format_version: 1.0\nroot:\n";
    for (std::size_t j = 0; j + 1 < depth; ++j) {
        s += std::string(j + 1, ' ') + "-\n";
    }
    s += std::string(depth, ' ') + "- leaf\n";
    return s;
}

// =============================================================================
// 第一類：數值超出可表示範圍 —— 不得丟例外，且須帶「該值所在的行號」
// =============================================================================

// 驗收標準點名的四種輸入。每一種都刻意把壞值放在**第 4 行**（> 1），
// 只斷言「有行號」或「行號 > 0」擋不住任何東西——必須就是 4。
constexpr std::size_t kBadLine = 4;

std::string doc_with_bad_value_on_line4(const std::string& value) {
    return "# 註解（第 1 行）\n"
           "format_version: 1.0\n"
           "before: ok\n"       // 第 3 行
           "bad: " + value + "\n"  // 第 4 行 ← 壞值
           "after: ok\n";
}

TEST(E7_17_ExceptionLeak, OverflowPositiveReturnsErrorWithLine) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_bad_value_on_line4("1e400"), r));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, kBadLine);
    EXPECT_NE(r.error().message.find("1e400"), std::string::npos) << r.error().message;
}

TEST(E7_17_ExceptionLeak, OverflowNegativeReturnsErrorWithLine) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_bad_value_on_line4("-1e400"), r));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, kBadLine);
    EXPECT_NE(r.error().message.find("-1e400"), std::string::npos) << r.error().message;
}

TEST(E7_17_ExceptionLeak, UnderflowReturnsErrorWithLine) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_bad_value_on_line4("1e-400"), r));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, kBadLine);
    EXPECT_NE(r.error().message.find("1e-400"), std::string::npos) << r.error().message;
}

// `parse_version` 的 std::stoi 溢位。format_version 同樣放在第 4 行。
TEST(E7_17_ExceptionLeak, FormatVersionOverflowReturnsErrorWithLine) {
    const std::string text =
        "# 註解（第 1 行）\n"
        "name: x\n"
        "other: y\n"
        "format_version: 99999999999999999999.0\n";
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(text, r));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, kBadLine);
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos) << r.error().message;
}

// 反向斷言：窮舉數值邊界，確認**零例外**穿出 parse()。
// 這一條不看解析結果對不對，只看「有沒有東西穿出來」——它守的是契約本身。
TEST(E7_17_ExceptionLeak, NoExceptionEscapesParseForAnyNumericBoundary) {
    const std::vector<std::string> tokens = {
        // 上溢。
        "1e400", "-1e400", "+1e400", "1E400", "1e309", "1.7977e308", "2e308",
        "1e99999999999999999999",
        // 下溢 / 次正規。
        "1e-400", "-1e-400", "1e-310", "1e-323", "1e-99999999999999999999",
        // 超長數字串（整數與小數兩型）。
        std::string(400, '9'),
        "-" + std::string(400, '9'),
        "0." + std::string(400, '9'),
        std::string(400, '9') + "." + std::string(400, '9'),
        std::string(5000, '1'),
        // 指數位數爆炸。
        "1e" + std::string(400, '9'),
        "1e-" + std::string(400, '9'),
        // 前後綴垃圾（語法掃描應判為非數字 → 裸字串；仍不得丟）。
        "1e400abc", "abc1e400", " 1e400", "1e400 ", "--1e400", "1e", "1e+", ".e9",
        "1.2.3", "0x1p9999", "1,000", "1e400\t", "+", "-", ".", "..", "1e400e400",
        // 其他邊界。
        "0", "-0", "0.0", "1e308", "-1e308", "4.9e-324", "2.2250738585072014e-308",
        "9223372036854775807", "9223372036854775808", "-9223372036854775809",
    };
    for (const std::string& t : tokens) {
        ParseResult r = unset_result();
        // 純量位置。
        EXPECT_TRUE(parse_no_throw("format_version: 1.0\nx: " + t + "\n", r)) << "token=" << t;
        // 清單元素位置。
        EXPECT_TRUE(parse_no_throw("format_version: 1.0\nl:\n  - " + t + "\n", r)) << "token=" << t;
        // format_version 位置（同一批 token 餵給 parse_version）。
        EXPECT_TRUE(parse_no_throw("format_version: " + t + "\n", r)) << "token=" << t;
        // 公開的型別推斷入口：同樣不得丟。
        Value v;
        EXPECT_NO_THROW({ (void)parse_scalar(t, v); }) << "token=" << t;
    }
}

// 既有合法輸入的解析結果**不得改變**。
TEST(E7_17_ExceptionLeak, LegalNumbersUnchanged) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(
        "format_version: 1.0\n"
        "a: -5\n"
        "b: -2.5\n"
        "c: 1e3\n"
        "d: 0\n"
        "e: 1.7976931348623157e308\n"   // 最大的有限 double，必須仍然可解析。
        "f: 2.2250738585072014e-308\n"  // 最小的正規 double。
        "g: 12ab\n",                    // 非數字 → 裸字串（不變）。
        r));
    ASSERT_TRUE(r.ok()) << r.error().line << ": " << r.error().message;
    const Value& root = r.document().root;
    EXPECT_EQ(root.at("a").as_number(), -5.0);
    EXPECT_TRUE(root.at("a").is_integer());
    EXPECT_EQ(root.at("b").as_number(), -2.5);
    EXPECT_FALSE(root.at("b").is_integer());
    EXPECT_EQ(root.at("c").as_number(), 1000.0);
    EXPECT_EQ(root.at("d").as_number(), 0.0);
    EXPECT_EQ(root.at("e").as_number(), 1.7976931348623157e308);
    EXPECT_EQ(root.at("f").as_number(), 2.2250738585072014e-308);
    EXPECT_EQ(root.at("g").type(), ValueType::String);
    EXPECT_EQ(root.at("g").as_string(), "12ab");
}

// 超出範圍的數值不得悄悄退化成一個「看起來正常」的字串值（K-002 那一族）。
TEST(E7_17_ExceptionLeak, OutOfRangeIsNotSilentlyAString) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw("format_version: 1.0\nx: 1e400\n", r));
    ASSERT_FALSE(r.ok()) << "1e400 不得被當成字串 \"1e400\" 靜默收下";
    Value v;
    EXPECT_FALSE(parse_scalar("1e400", v));
    EXPECT_FALSE(parse_scalar("1e-400", v));
}

// =============================================================================
// 第二類：巢狀深度上限 —— 上限內正常 / 剛好超限回 ParseError / 遠超限不當機
// =============================================================================

// 上限值本身是契約的一部分。這兩道界限讓「把上限調成天文數字」這種假修法過不了關，
// 也擋住「調到比真實文件還淺」。本庫最深的真實宣告式文件是 6 層。
TEST(E7_17_NestingDepth, LimitIsAContractAndIsSane) {
    EXPECT_GE(kMaxNestingDepth, static_cast<std::size_t>(16))
        << "上限淺於真實文件所需（本庫最深 6 層，需留餘裕）";
    EXPECT_LE(kMaxNestingDepth, static_cast<std::size_t>(1024))
        << "上限大到失去意義——那等於沒有上限，堆疊仍會被打爆";
}

TEST(E7_17_NestingDepth, AtLimitParsesFine) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_depth(kMaxNestingDepth), r));
    EXPECT_TRUE(r.ok()) << r.error().line << ": " << r.error().message;
}

TEST(E7_17_NestingDepth, OneOverLimitIsParseErrorWithLine) {
    const std::size_t depth = kMaxNestingDepth + 1;
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_depth(depth), r));
    ASSERT_FALSE(r.ok()) << "剛好超限必須被擋下";
    EXPECT_EQ(r.error().line, first_line_of_depth(depth));
    EXPECT_NE(r.error().message.find("巢狀"), std::string::npos) << r.error().message;
}

TEST(E7_17_NestingDepth, ListNestingIsLimitedToo) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(list_doc_with_depth(kMaxNestingDepth + 8), r));
    EXPECT_FALSE(r.ok()) << "清單路徑（parse_list_block）同樣必須受上限管束";
}

// 遠超限：上限生效後，超限輸入不需要很大也測得到，故這裡用深度 2000（約 2 MB），
// 不是當初量到 SIGSEGV 的那種百 MB 輸入。
TEST(E7_17_NestingDepth, FarBeyondLimitDoesNotCrash) {
    const std::size_t depth = 2000;
    ASSERT_GT(depth, kMaxNestingDepth * 4);
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_depth(depth), r));
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("巢狀"), std::string::npos) << r.error().message;
}

// 解構也是遞迴的：就算 parse() 在超限時提早回傳，**已經建起來的那棵 Value 樹**
// 在解構時仍然會遞迴。這一條實際讓一棵剛好到上限的樹離開作用域、被解構，
// 然後才檢查哨兵——不是推論，是跑出來的。
TEST(E7_17_NestingDepth, DestroyingAnAtLimitTreeIsSafe) {
    bool destroyed = false;
    {
        ParseResult r = unset_result();
        ASSERT_TRUE(parse_no_throw(doc_with_depth(kMaxNestingDepth), r));
        ASSERT_TRUE(r.ok()) << r.error().line << ": " << r.error().message;
        // 另外複製一棵同樣深的樹，確保複製與解構兩條遞迴路徑都走過。
        const Value copy = r.document().root;
        EXPECT_TRUE(copy.is_map());
    }  // ← r 與 copy 於此遞迴解構。
    destroyed = true;
    EXPECT_TRUE(destroyed) << "解構深樹時爆堆疊的話，根本走不到這一行";
}

// 超限失敗那一刻，部分建好的樹要在例外展開中被解構。同樣不得爆。
TEST(E7_17_NestingDepth, UnwindingAfterOverLimitFailureIsSafe) {
    for (int rep = 0; rep < 8; ++rep) {
        ParseResult r = unset_result();
        ASSERT_TRUE(parse_no_throw(doc_with_depth(kMaxNestingDepth + 1), r));
        ASSERT_FALSE(r.ok());
    }
    SUCCEED();
}

// 真實文件的深度（本庫最深 6 層）必須毫無影響。
TEST(E7_17_NestingDepth, RealWorldDepthUnaffected) {
    ParseResult r = unset_result();
    ASSERT_TRUE(parse_no_throw(doc_with_depth(6), r));
    EXPECT_TRUE(r.ok()) << r.error().line << ": " << r.error().message;

    ASSERT_TRUE(parse_no_throw(
        "format_version: 1.0\n"
        "window:\n"
        "  size:\n"
        "    width: 800\n"
        "    height: 600\n"
        "layers:\n"
        "  -\n"
        "    name: base\n"
        "  -\n"
        "    name: overlay\n",
        r));
    ASSERT_TRUE(r.ok()) << r.error().line << ": " << r.error().message;
    EXPECT_EQ(r.document().root.at("window").at("size").at("width").as_number(), 800.0);
    EXPECT_EQ(r.document().root.at("layers").as_list().size(), 2u);
}

}  // namespace
