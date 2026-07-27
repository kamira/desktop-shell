// tests/e7/test_e7_10.cpp — E7-10 字串處理 契約測試
//
// 驗證：一組可在公式 / 格式脈絡中使用的字串處理函式（namespace ds::format::strings），
// 輸入 / 輸出以 E7-01 `Value` 表達、結果 / 錯誤沿用 E7-05 `EvalResult` / `EvalError`。
//   1. 各字串運算正確（concat / length / substring / upper / lower / trim / replace /
//      split / join / contains / starts_with / ends_with / pad / format）。
//   2. Unicode（UTF-8 碼位）與空字串邊界。
//   3. 型別誤用明確報錯（非字串 / 非整數引數，不靜默）。
//   4. 索引越界明確報錯（substring 起點 / 長度、format 佔位越界）。
//   5. 與 E7-01 Value 型別整合（回傳型別、整數 Number、List、format 字串化各型別）。
//   6. 函式表分派（function_table / find_function）與元數檢查。

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "document.hpp"  // E7-01：Value
#include "formula.hpp"   // E7-05：EvalResult / EvalError
#include "strings.hpp"   // E7-10

using namespace ds::format;
using ds::format::strings::StringFn;

namespace {

// 取成功結果的 Value（以值回傳：呼叫端常以 `const Value& v = ok(tmp);` 承接，
// 回傳值副本經生命週期延長綁定至 v，避免懸空參照到已解構的暫存 EvalResult）。
// 失敗則讓測試失敗並印出訊息，回傳 Null 佔位。
Value ok(const EvalResult& r) {
    EXPECT_TRUE(r.ok()) << "預期成功，卻失敗：" << r.error().message;
    return r.ok() ? r.value() : Value::null();
}

// 便捷字串 Value。
Value S(const std::string& s) { return Value::string(s); }
Value I(std::int64_t n) { return Value::integer(n); }

}  // namespace

// -----------------------------------------------------------------------------
// 1. length（含 Unicode / 空字串邊界）
// -----------------------------------------------------------------------------

TEST(E7_10_Length, Ascii) {
    EXPECT_EQ(ok(strings::length(S("hello"))).as_int(), 5);
}

TEST(E7_10_Length, Empty) {
    const Value& v = ok(strings::length(S("")));
    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.as_int(), 0);
}

TEST(E7_10_Length, UnicodeCodepoints) {
    // "héllo" = h + é(2 bytes) + l + l + o = 5 碼位 / 6 位元組。
    EXPECT_EQ(ok(strings::length(S("héllo"))).as_int(), 5);
    // 中日文（各 3 位元組）："世界" = 2 碼位。
    EXPECT_EQ(ok(strings::length(S("世界"))).as_int(), 2);
    // Emoji（4 位元組）：1 碼位。
    EXPECT_EQ(ok(strings::length(S("😀"))).as_int(), 1);
}

TEST(E7_10_Length, TypeErrorOnNonString) {
    EXPECT_FALSE(strings::length(I(3)).ok());
    EXPECT_FALSE(strings::length(Value::boolean(true)).ok());
    EXPECT_FALSE(strings::length(Value::null()).ok());
}

// -----------------------------------------------------------------------------
// 2. upper / lower / trim
// -----------------------------------------------------------------------------

TEST(E7_10_Case, UpperLowerAsciiOnly) {
    EXPECT_EQ(ok(strings::upper(S("Hello, World! 世界"))).as_string(), "HELLO, WORLD! 世界");
    EXPECT_EQ(ok(strings::lower(S("Hello, WORLD! 世界"))).as_string(), "hello, world! 世界");
}

TEST(E7_10_Case, NonAsciiUntouched) {
    // é 非 ASCII，大小寫折疊不動它（僅折疊 A-Z / a-z）。
    EXPECT_EQ(ok(strings::upper(S("café"))).as_string(), "CAFé");
}

TEST(E7_10_Case, Empty) {
    EXPECT_EQ(ok(strings::upper(S(""))).as_string(), "");
    EXPECT_EQ(ok(strings::lower(S(""))).as_string(), "");
}

TEST(E7_10_Trim, TrimsAsciiWhitespace) {
    EXPECT_EQ(ok(strings::trim(S("  \t hi \n "))).as_string(), "hi");
    EXPECT_EQ(ok(strings::trim(S("nospace"))).as_string(), "nospace");
    EXPECT_EQ(ok(strings::trim(S("   "))).as_string(), "");
    EXPECT_EQ(ok(strings::trim(S(""))).as_string(), "");
}

TEST(E7_10_Case, TypeError) {
    EXPECT_FALSE(strings::upper(I(1)).ok());
    EXPECT_FALSE(strings::lower(Value::null()).ok());
    EXPECT_FALSE(strings::trim(Value::boolean(false)).ok());
}

// -----------------------------------------------------------------------------
// 3. concat
// -----------------------------------------------------------------------------

TEST(E7_10_Concat, JoinsInOrder) {
    EXPECT_EQ(ok(strings::concat({S("foo"), S("/"), S("bar")})).as_string(), "foo/bar");
}

TEST(E7_10_Concat, EmptyListIsEmptyString) {
    EXPECT_EQ(ok(strings::concat({})).as_string(), "");
}

TEST(E7_10_Concat, TypeErrorOnNonStringPart) {
    EXPECT_FALSE(strings::concat({S("a"), I(2)}).ok());
}

// -----------------------------------------------------------------------------
// 4. substring（碼位單位；越界報錯）
// -----------------------------------------------------------------------------

TEST(E7_10_Substring, ToEnd) {
    EXPECT_EQ(ok(strings::substring(S("hello"), I(2))).as_string(), "llo");
    EXPECT_EQ(ok(strings::substring(S("hello"), I(5))).as_string(), "");  // 起點 == 長度 → 空。
    EXPECT_EQ(ok(strings::substring(S("hello"), I(0))).as_string(), "hello");
}

TEST(E7_10_Substring, WithCount) {
    EXPECT_EQ(ok(strings::substring(S("hello"), I(1), I(3))).as_string(), "ell");
    EXPECT_EQ(ok(strings::substring(S("hello"), I(0), I(0))).as_string(), "");
    EXPECT_EQ(ok(strings::substring(S("hello"), I(2), I(3))).as_string(), "llo");  // 到尾恰好。
}

TEST(E7_10_Substring, UnicodeByCodepoint) {
    // "aé世b"：碼位 0=a 1=é 2=世 3=b。取 [1,2) → "é世"。
    EXPECT_EQ(ok(strings::substring(S("aé世b"), I(1), I(2))).as_string(), "é世");
    EXPECT_EQ(ok(strings::substring(S("世界"), I(1))).as_string(), "界");
}

TEST(E7_10_Substring, StartOutOfBounds) {
    EXPECT_FALSE(strings::substring(S("hi"), I(3)).ok());       // 起點 > 長度。
    EXPECT_FALSE(strings::substring(S("hi"), I(-1)).ok());      // 負起點。
    EXPECT_FALSE(strings::substring(S("hi"), I(3), I(0)).ok());
}

TEST(E7_10_Substring, CountOutOfBounds) {
    EXPECT_FALSE(strings::substring(S("hi"), I(1), I(5)).ok());  // start+count > 長度。
    EXPECT_FALSE(strings::substring(S("hi"), I(0), I(-1)).ok()); // 負長度。
}

TEST(E7_10_Substring, TypeErrors) {
    EXPECT_FALSE(strings::substring(I(0), I(0)).ok());               // 非字串。
    EXPECT_FALSE(strings::substring(S("hi"), S("0")).ok());          // 起點非整數。
    EXPECT_FALSE(strings::substring(S("hi"), Value::number(1.5)).ok());  // 非整數 Number。
    EXPECT_FALSE(strings::substring(S("hi"), I(0), S("1")).ok());    // 長度非整數。
}

// -----------------------------------------------------------------------------
// 5. replace
// -----------------------------------------------------------------------------

TEST(E7_10_Replace, ReplacesAll) {
    EXPECT_EQ(ok(strings::replace(S("a.b.c"), S("."), S("/"))).as_string(), "a/b/c");
    EXPECT_EQ(ok(strings::replace(S("aaa"), S("a"), S("bb"))).as_string(), "bbbbbb");
    EXPECT_EQ(ok(strings::replace(S("hello"), S("x"), S("y"))).as_string(), "hello");  // 無匹配。
}

TEST(E7_10_Replace, ToEmptyDeletes) {
    EXPECT_EQ(ok(strings::replace(S("a-b-c"), S("-"), S(""))).as_string(), "abc");
}

TEST(E7_10_Replace, EmptyFromIsError) {
    EXPECT_FALSE(strings::replace(S("abc"), S(""), S("x")).ok());
}

TEST(E7_10_Replace, TypeError) {
    EXPECT_FALSE(strings::replace(S("a"), I(1), S("b")).ok());
}

// -----------------------------------------------------------------------------
// 6. split / join
// -----------------------------------------------------------------------------

TEST(E7_10_Split, ByDelimiter) {
    const Value& v = ok(strings::split(S("a,b,c"), S(",")));
    ASSERT_TRUE(v.is_list());
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v.as_list()[0].as_string(), "a");
    EXPECT_EQ(v.as_list()[1].as_string(), "b");
    EXPECT_EQ(v.as_list()[2].as_string(), "c");
}

TEST(E7_10_Split, KeepsEmptySegments) {
    const Value& v = ok(strings::split(S("a,,c"), S(",")));
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v.as_list()[1].as_string(), "");
    // 無分隔符 → 單一元素（原字串）。
    const Value& w = ok(strings::split(S("nodelim"), S(",")));
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(w.as_list()[0].as_string(), "nodelim");
}

TEST(E7_10_Split, EmptySepSplitsCodepoints) {
    const Value& v = ok(strings::split(S("a世b"), S("")));
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v.as_list()[0].as_string(), "a");
    EXPECT_EQ(v.as_list()[1].as_string(), "世");
    EXPECT_EQ(v.as_list()[2].as_string(), "b");
}

TEST(E7_10_Join, JoinsListOfStrings) {
    Value list = Value::list({S("a"), S("b"), S("c")});
    EXPECT_EQ(ok(strings::join(list, S("-"))).as_string(), "a-b-c");
    EXPECT_EQ(ok(strings::join(Value::list({}), S("-"))).as_string(), "");
    EXPECT_EQ(ok(strings::join(Value::list({S("solo")}), S(","))).as_string(), "solo");
}

TEST(E7_10_Join, RoundTripWithSplit) {
    const Value& parts = ok(strings::split(S("x/y/z"), S("/")));
    EXPECT_EQ(ok(strings::join(parts, S("/"))).as_string(), "x/y/z");
}

TEST(E7_10_Join, TypeErrors) {
    EXPECT_FALSE(strings::join(S("notlist"), S(",")).ok());          // 非清單。
    EXPECT_FALSE(strings::join(Value::list({S("a"), I(2)}), S(",")).ok());  // 元素非字串。
}

// -----------------------------------------------------------------------------
// 7. contains / starts_with / ends_with
// -----------------------------------------------------------------------------

TEST(E7_10_Predicates, ContainsStartsEnds) {
    EXPECT_TRUE(ok(strings::contains(S("hello world"), S("lo w"))).as_bool());
    EXPECT_FALSE(ok(strings::contains(S("hello"), S("xyz"))).as_bool());
    EXPECT_TRUE(ok(strings::starts_with(S("hello"), S("he"))).as_bool());
    EXPECT_FALSE(ok(strings::starts_with(S("hello"), S("lo"))).as_bool());
    EXPECT_TRUE(ok(strings::ends_with(S("hello"), S("lo"))).as_bool());
    EXPECT_FALSE(ok(strings::ends_with(S("hello"), S("he"))).as_bool());
}

TEST(E7_10_Predicates, EmptyNeedle) {
    // 空子字串 / 空前後綴恆為真（標準子字串語意）。
    EXPECT_TRUE(ok(strings::contains(S("x"), S(""))).as_bool());
    EXPECT_TRUE(ok(strings::starts_with(S("x"), S(""))).as_bool());
    EXPECT_TRUE(ok(strings::ends_with(S("x"), S(""))).as_bool());
}

TEST(E7_10_Predicates, ResultIsBool) {
    EXPECT_TRUE(ok(strings::contains(S("a"), S("a"))).is_bool());
}

TEST(E7_10_Predicates, TypeError) {
    EXPECT_FALSE(strings::contains(I(1), S("a")).ok());
    EXPECT_FALSE(strings::starts_with(S("a"), I(1)).ok());
}

// -----------------------------------------------------------------------------
// 8. pad_left / pad_right
// -----------------------------------------------------------------------------

TEST(E7_10_Pad, PadsToWidth) {
    EXPECT_EQ(ok(strings::pad_left(S("42"), I(5), S("0"))).as_string(), "00042");
    EXPECT_EQ(ok(strings::pad_right(S("42"), I(5), S("."))).as_string(), "42...");
}

TEST(E7_10_Pad, MultiCharPadCycles) {
    EXPECT_EQ(ok(strings::pad_right(S("x"), I(5), S("ab"))).as_string(), "xabab");
    EXPECT_EQ(ok(strings::pad_left(S("x"), I(5), S("ab"))).as_string(), "ababx");
}

TEST(E7_10_Pad, WidthByCodepoint) {
    // 目標寬 4 碼位，"世" 1 碼位 → 補 3 個 '.'。
    EXPECT_EQ(ok(strings::pad_left(S("世"), I(4), S("."))).as_string(), "...世");
}

TEST(E7_10_Pad, AlreadyWideEnoughUnchanged) {
    EXPECT_EQ(ok(strings::pad_left(S("hello"), I(3), S("0"))).as_string(), "hello");
    EXPECT_EQ(ok(strings::pad_right(S("hello"), I(5), S("0"))).as_string(), "hello");
}

TEST(E7_10_Pad, Errors) {
    EXPECT_FALSE(strings::pad_left(S("x"), I(5), S("")).ok());       // 空填充。
    EXPECT_FALSE(strings::pad_left(S("x"), I(-1), S("0")).ok());     // 負寬度。
    EXPECT_FALSE(strings::pad_left(S("x"), S("5"), S("0")).ok());    // 寬度非整數。
    EXPECT_FALSE(strings::pad_left(I(1), I(5), S("0")).ok());        // 非字串。
}

// -----------------------------------------------------------------------------
// 9. format（樣板組字 + 各 Value 型別字串化整合）
// -----------------------------------------------------------------------------

TEST(E7_10_Format, IndexedPlaceholders) {
    EXPECT_EQ(ok(strings::format(S("{0}/{1}"), {S("a"), S("b")})).as_string(), "a/b");
    EXPECT_EQ(ok(strings::format(S("{1}-{0}"), {S("a"), S("b")})).as_string(), "b-a");
    EXPECT_EQ(ok(strings::format(S("{0}{0}"), {S("z")})).as_string(), "zz");  // 可重用。
}

TEST(E7_10_Format, StringifiesValueTypes) {
    // 整合 E7-01 各型別：整數 / 浮點 / 布林 / null。
    EXPECT_EQ(ok(strings::format(S("n={0} b={1} x={2}"),
                                 {I(42), Value::boolean(true), Value::null()}))
                  .as_string(),
              "n=42 b=true x=null");
    EXPECT_EQ(ok(strings::format(S("{0}"), {Value::number(3.5)})).as_string(), "3.5");
}

TEST(E7_10_Format, EscapedBraces) {
    EXPECT_EQ(ok(strings::format(S("{{{0}}}"), {S("x")})).as_string(), "{x}");
    EXPECT_EQ(ok(strings::format(S("no placeholders"), {})).as_string(), "no placeholders");
}

TEST(E7_10_Format, Errors) {
    EXPECT_FALSE(strings::format(S("{0}"), {}).ok());          // 索引越界（無引數）。
    EXPECT_FALSE(strings::format(S("{5}"), {S("a")}).ok());     // 索引越界。
    EXPECT_FALSE(strings::format(S("{a}"), {S("a")}).ok());     // 非數字索引。
    EXPECT_FALSE(strings::format(S("{}"), {S("a")}).ok());      // 缺索引。
    EXPECT_FALSE(strings::format(S("{0"), {S("a")}).ok());      // 未閉合 '{'。
    EXPECT_FALSE(strings::format(S("a}b"), {}).ok());           // 落單 '}'。
    // 容器型別無法字串化。
    EXPECT_FALSE(strings::format(S("{0}"), {Value::list({S("x")})}).ok());
    EXPECT_FALSE(strings::format(S("{0}"), {Value::map({})}).ok());
}

// -----------------------------------------------------------------------------
// 10. 函式表（分派 + 元數檢查）
// -----------------------------------------------------------------------------

TEST(E7_10_Table, FindAndDispatch) {
    const StringFn* fn = strings::find_function("upper");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(ok((*fn)({S("hi")})).as_string(), "HI");

    const StringFn* len = strings::find_function("length");
    ASSERT_NE(len, nullptr);
    EXPECT_EQ(ok((*len)({S("世界")})).as_int(), 2);
}

TEST(E7_10_Table, VariadicConcatAndFormat) {
    const StringFn* cc = strings::find_function("concat");
    ASSERT_NE(cc, nullptr);
    EXPECT_EQ(ok((*cc)({S("a"), S("b"), S("c")})).as_string(), "abc");

    const StringFn* fmt = strings::find_function("format");
    ASSERT_NE(fmt, nullptr);
    EXPECT_EQ(ok((*fmt)({S("{0}!"), S("hi")})).as_string(), "hi!");
}

TEST(E7_10_Table, SubstringAritiesBoth) {
    const StringFn* sub = strings::find_function("substring");
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(ok((*sub)({S("hello"), I(1)})).as_string(), "ello");        // 2 引數。
    EXPECT_EQ(ok((*sub)({S("hello"), I(1), I(2)})).as_string(), "el");    // 3 引數。
    EXPECT_FALSE((*sub)({S("hello")}).ok());                              // 元數不符。
}

TEST(E7_10_Table, ArityErrors) {
    const StringFn* fn = strings::find_function("length");
    ASSERT_NE(fn, nullptr);
    EXPECT_FALSE((*fn)({}).ok());                    // 需 1 個。
    EXPECT_FALSE((*fn)({S("a"), S("b")}).ok());      // 太多。

    const StringFn* fmt = strings::find_function("format");
    ASSERT_NE(fmt, nullptr);
    EXPECT_FALSE((*fmt)({}).ok());                   // format 至少 1 個。
}

TEST(E7_10_Table, UnknownIsNull) {
    EXPECT_EQ(strings::find_function("no_such_fn"), nullptr);
}

TEST(E7_10_Table, TableNonEmptyAndNamed) {
    const auto& t = strings::function_table();
    EXPECT_GE(t.size(), 15u);
    // 每個函式皆可依名反查得同一項。
    for (const auto& entry : t) {
        EXPECT_NE(strings::find_function(entry.first), nullptr) << entry.first;
    }
}

// -----------------------------------------------------------------------------
// 11. 錯誤不靜默：失敗結果帶非空訊息、且不可取 value
// -----------------------------------------------------------------------------

TEST(E7_10_Errors, FailureCarriesMessage) {
    EvalResult r = strings::length(I(1));
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}
