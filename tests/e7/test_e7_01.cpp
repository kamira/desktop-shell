// E7-01 宣告式格式核心 + 版本欄位 — 契約測試（gtest）
//
// 涵蓋：資料模型（Value 多型、型別查詢、存取、深層相等）、版本欄位（解析、相容判定、
// 邊界）、解析基座（有效 / 無效文件、行號定位、巢狀 map / list、清單內含 map、
// 純量型別推斷、引號字串與轉義、tab 縮排拒絕、重複 / 未知 / 缺冒號、註解與空行）。
// 平台中立：不含任何平台分支。
#include "document.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using ds::format::Document;
using ds::format::FormatVersion;
using ds::format::is_format_compatible;
using ds::format::kSupportedFormat;
using ds::format::parse;
using ds::format::parse_scalar;
using ds::format::ParseResult;
using ds::format::Value;
using ds::format::ValueType;

namespace {

// -----------------------------------------------------------------------------
// 資料模型：Value
// -----------------------------------------------------------------------------

TEST(Value, DefaultIsNull) {
    const Value v;
    EXPECT_EQ(v.type(), ValueType::Null);
    EXPECT_TRUE(v.is_null());
    EXPECT_FALSE(v.is_bool());
}

TEST(Value, ScalarFactoriesAndAccessors) {
    EXPECT_TRUE(Value::boolean(true).as_bool());
    EXPECT_EQ(Value::string("hi").as_string(), "hi");

    const Value n = Value::number(3.5);
    EXPECT_TRUE(n.is_number());
    EXPECT_FALSE(n.is_integer());
    EXPECT_DOUBLE_EQ(n.as_number(), 3.5);

    const Value i = Value::integer(42);
    EXPECT_TRUE(i.is_number());
    EXPECT_TRUE(i.is_integer());
    EXPECT_EQ(i.as_int(), 42);
    EXPECT_DOUBLE_EQ(i.as_number(), 42.0);
}

TEST(Value, WrongTypeAccessThrows) {
    EXPECT_THROW(Value::string("x").as_bool(), std::runtime_error);
    EXPECT_THROW(Value::boolean(true).as_number(), std::runtime_error);
    EXPECT_THROW(Value::null().as_string(), std::runtime_error);
    EXPECT_THROW(Value::null().as_list(), std::runtime_error);
    EXPECT_THROW(Value::null().as_map(), std::runtime_error);
    EXPECT_THROW(Value::null().size(), std::runtime_error);
    EXPECT_THROW(Value::null().keys(), std::runtime_error);
    EXPECT_THROW(Value::null().find("k"), std::runtime_error);
}

TEST(Value, ListAccessor) {
    const Value v = Value::list({Value::integer(1), Value::integer(2), Value::integer(3)});
    ASSERT_TRUE(v.is_list());
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v.as_list()[1].as_int(), 2);
}

TEST(Value, MapAccessorsOrderedAndLookup) {
    const Value v = Value::map({
        {"a", Value::integer(1)},
        {"b", Value::string("two")},
    });
    ASSERT_TRUE(v.is_map());
    EXPECT_EQ(v.size(), 2u);
    EXPECT_TRUE(v.contains("a"));
    EXPECT_FALSE(v.contains("z"));
    ASSERT_NE(v.find("b"), nullptr);
    EXPECT_EQ(v.find("b")->as_string(), "two");
    EXPECT_EQ(v.find("z"), nullptr);
    EXPECT_EQ(v.at("a").as_int(), 1);
    EXPECT_THROW(v.at("z"), std::runtime_error);

    const std::vector<std::string> ks = v.keys();
    ASSERT_EQ(ks.size(), 2u);
    EXPECT_EQ(ks[0], "a");  // 保序。
    EXPECT_EQ(ks[1], "b");
}

TEST(Value, DeepEquality) {
    const Value a = Value::map({{"x", Value::list({Value::integer(1), Value::boolean(true)})}});
    const Value b = Value::map({{"x", Value::list({Value::integer(1), Value::boolean(true)})}});
    EXPECT_EQ(a, b);

    // 整數 vs 浮點來源不同 → 不相等。
    EXPECT_NE(Value::integer(1), Value::number(1.0, /*integral=*/false));
    // 保序：鍵順序不同 → 不相等。
    EXPECT_NE(Value::map({{"a", Value::null()}, {"b", Value::null()}}),
              Value::map({{"b", Value::null()}, {"a", Value::null()}}));
}

// -----------------------------------------------------------------------------
// 版本欄位
// -----------------------------------------------------------------------------

TEST(Version, CompatibilityRule) {
    EXPECT_TRUE(is_format_compatible({1, 0}));
    EXPECT_TRUE(is_format_compatible({1, 0}, {1, 2}));   // minor 低於支援上限 → 可。
    EXPECT_FALSE(is_format_compatible({1, 3}, {1, 2}));  // minor 高於上限 → 不可。
    EXPECT_FALSE(is_format_compatible({2, 0}, {1, 9}));  // major 不同 → 不可。
    EXPECT_FALSE(is_format_compatible({0, 9}, {1, 0}));  // major 不同 → 不可。
}

TEST(Version, ParsedFromDocument) {
    const ParseResult r = parse("format_version: 1.0\nname: x\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.document().format_version, (FormatVersion{1, 0}));
    // format_version 不進 root（root 為純內容）。
    EXPECT_FALSE(r.document().root.contains("format_version"));
    EXPECT_TRUE(r.document().root.contains("name"));
}

TEST(Version, MinorParsedNotAsNumber) {
    // 關鍵回歸：1.10 若當數字會塌成 1.1；版本欄位須保留 minor=10。
    const ParseResult r = parse("format_version: 1.10\n");
    // 1.10 的 minor=10 > 支援上限 0 → 不相容（但版本本身正確解析）。
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 0u);
    EXPECT_NE(r.error().message.find("不相容"), std::string::npos);
}

// -----------------------------------------------------------------------------
// 解析：有效文件
// -----------------------------------------------------------------------------

TEST(Parse, FullDocumentAllScalarTypes) {
    const std::string text =
        "# 標頭註解\n"
        "format_version: 1.0\n"
        "name: com.example.hello\n"
        "enabled: true\n"
        "disabled: false\n"
        "count: 42\n"
        "ratio: 3.14\n"
        "nothing: null\n"
        "\n"
        "quoted: \"含: 冒號與\\n換行\"\n";

    const ParseResult r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Value& root = r.document().root;

    EXPECT_EQ(root.at("name").as_string(), "com.example.hello");
    EXPECT_TRUE(root.at("enabled").as_bool());
    EXPECT_FALSE(root.at("disabled").as_bool());
    EXPECT_TRUE(root.at("count").is_integer());
    EXPECT_EQ(root.at("count").as_int(), 42);
    EXPECT_FALSE(root.at("ratio").is_integer());
    EXPECT_DOUBLE_EQ(root.at("ratio").as_number(), 3.14);
    EXPECT_TRUE(root.at("nothing").is_null());
    EXPECT_EQ(root.at("quoted").as_string(), "含: 冒號與\n換行");
}

TEST(Parse, BareStringPreservesSpacesAndColonValue) {
    const ParseResult r = parse("format_version: 1.0\ntitle: Hello World\nurl: http://a:8080/x\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.document().root.at("title").as_string(), "Hello World");
    // 首個 ':' 分隔 key/value，value 內可再含 ':'。
    EXPECT_EQ(r.document().root.at("url").as_string(), "http://a:8080/x");
}

TEST(Parse, NegativeAndExponentNumbers) {
    const ParseResult r = parse("format_version: 1.0\na: -5\nb: -2.5\nc: 1e3\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_TRUE(r.document().root.at("a").is_integer());
    EXPECT_EQ(r.document().root.at("a").as_int(), -5);
    EXPECT_FALSE(r.document().root.at("b").is_integer());
    EXPECT_DOUBLE_EQ(r.document().root.at("b").as_number(), -2.5);
    EXPECT_FALSE(r.document().root.at("c").is_integer());
    EXPECT_DOUBLE_EQ(r.document().root.at("c").as_number(), 1000.0);
}

TEST(Parse, NestedMap) {
    const std::string text =
        "format_version: 1.0\n"
        "window:\n"
        "  width: 800\n"
        "  height: 600\n"
        "  title: Main\n";
    const ParseResult r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Value& w = r.document().root.at("window");
    ASSERT_TRUE(w.is_map());
    EXPECT_EQ(w.at("width").as_int(), 800);
    EXPECT_EQ(w.at("height").as_int(), 600);
    EXPECT_EQ(w.at("title").as_string(), "Main");
}

TEST(Parse, ScalarList) {
    const std::string text =
        "format_version: 1.0\n"
        "tags:\n"
        "  - alpha\n"
        "  - beta\n"
        "  - 3\n";
    const ParseResult r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Value& tags = r.document().root.at("tags");
    ASSERT_TRUE(tags.is_list());
    ASSERT_EQ(tags.size(), 3u);
    EXPECT_EQ(tags.as_list()[0].as_string(), "alpha");
    EXPECT_EQ(tags.as_list()[1].as_string(), "beta");
    EXPECT_EQ(tags.as_list()[2].as_int(), 3);
}

TEST(Parse, ListOfMaps) {
    const std::string text =
        "format_version: 1.0\n"
        "layers:\n"
        "  -\n"
        "    name: base\n"
        "    z: 0\n"
        "  -\n"
        "    name: overlay\n"
        "    z: 1\n";
    const ParseResult r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Value& layers = r.document().root.at("layers");
    ASSERT_TRUE(layers.is_list());
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers.as_list()[0].at("name").as_string(), "base");
    EXPECT_EQ(layers.as_list()[0].at("z").as_int(), 0);
    EXPECT_EQ(layers.as_list()[1].at("name").as_string(), "overlay");
    EXPECT_EQ(layers.as_list()[1].at("z").as_int(), 1);
}

TEST(Parse, DeepNesting) {
    const std::string text =
        "format_version: 1.0\n"
        "a:\n"
        "  b:\n"
        "    c:\n"
        "      - x\n"
        "      - y\n";
    const ParseResult r = parse(text);
    ASSERT_TRUE(r.ok()) << r.error().message;
    const Value& c = r.document().root.at("a").at("b").at("c");
    ASSERT_TRUE(c.is_list());
    ASSERT_EQ(c.size(), 2u);
    EXPECT_EQ(c.as_list()[0].as_string(), "x");
}

TEST(Parse, EmptyValueBecomesNull) {
    // key: 後無值、其下亦無更深縮排 → null。
    const ParseResult r = parse("format_version: 1.0\nempty:\nname: x\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_TRUE(r.document().root.at("empty").is_null());
    EXPECT_EQ(r.document().root.at("name").as_string(), "x");
}

TEST(Parse, OnlyVersionYieldsEmptyRoot) {
    const ParseResult r = parse("format_version: 1.0\n");
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_TRUE(r.document().root.is_map());
    EXPECT_EQ(r.document().root.size(), 0u);
}

// -----------------------------------------------------------------------------
// 解析：錯誤（皆須定位到行，不得靜默）
// -----------------------------------------------------------------------------

TEST(ParseError, MissingFormatVersion) {
    const ParseResult r = parse("name: x\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 0u);
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos);
}

TEST(ParseError, EmptyDocument) {
    const ParseResult r = parse("\n  \n# 只有註解\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 0u);
}

TEST(ParseError, IncompatibleMajor) {
    const ParseResult r = parse("format_version: 2.0\nname: x\n");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("不相容"), std::string::npos);
}

TEST(ParseError, UnparseableVersionLocatesLine) {
    const ParseResult r = parse("# c\nformat_version: abc\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos);
}

TEST(ParseError, VersionThreePartRejected) {
    const ParseResult r = parse("format_version: 1.2.3\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 1u);
}

TEST(ParseError, MissingColonLocatesLine) {
    const ParseResult r = parse("format_version: 1.0\nname x\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
    EXPECT_NE(r.error().message.find("':'"), std::string::npos);
}

TEST(ParseError, DuplicateKeyLocatesLine) {
    const ParseResult r = parse("format_version: 1.0\nname: a\nname: b\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 3u);
    EXPECT_NE(r.error().message.find("重複"), std::string::npos);
}

TEST(ParseError, DuplicateFormatVersionLocatesLine) {
    const ParseResult r = parse("format_version: 1.0\nformat_version: 1.0\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
}

TEST(ParseError, EmptyKey) {
    const ParseResult r = parse("format_version: 1.0\n: value\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
}

TEST(ParseError, TabIndentRejectedWithLine) {
    const ParseResult r = parse("format_version: 1.0\nwindow:\n\twidth: 800\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 3u);
    EXPECT_NE(r.error().message.find("tab"), std::string::npos);
}

TEST(ParseError, RootIndentedRejected) {
    const ParseResult r = parse("  format_version: 1.0\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 1u);
}

TEST(ParseError, RootAsListRejected) {
    const ParseResult r = parse("- a\n- b\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 1u);
    EXPECT_NE(r.error().message.find("map"), std::string::npos);
}

TEST(ParseError, OrphanOverIndentLocatesLine) {
    const ParseResult r = parse("format_version: 1.0\nname: x\n    stray: 1\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 3u);
}

TEST(ParseError, MalformedQuotedStringLocatesLine) {
    const ParseResult r = parse("format_version: 1.0\nk: \"unterminated\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
}

TEST(ParseError, UnknownEscapeRejected) {
    const ParseResult r = parse("format_version: 1.0\nk: \"bad\\x\"\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
}

TEST(ParseError, ListItemExpectedButMapFound) {
    // 在清單區塊中放入非 '-' 起頭的行。
    const std::string text =
        "format_version: 1.0\n"
        "items:\n"
        "  - a\n"
        "  b: 1\n";
    const ParseResult r = parse(text);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 4u);
}

// -----------------------------------------------------------------------------
// parse_scalar（型別推斷單元）
// -----------------------------------------------------------------------------

TEST(ParseScalar, InfersTypes) {
    Value v;
    ASSERT_TRUE(parse_scalar("null", v));
    EXPECT_TRUE(v.is_null());
    ASSERT_TRUE(parse_scalar("true", v));
    EXPECT_TRUE(v.as_bool());
    ASSERT_TRUE(parse_scalar("  100  ", v));
    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.as_int(), 100);
    ASSERT_TRUE(parse_scalar("2.5", v));
    EXPECT_DOUBLE_EQ(v.as_number(), 2.5);
    ASSERT_TRUE(parse_scalar("\"quoted\"", v));
    EXPECT_EQ(v.as_string(), "quoted");
    ASSERT_TRUE(parse_scalar("plain text", v));
    EXPECT_EQ(v.as_string(), "plain text");
    ASSERT_TRUE(parse_scalar("", v));
    EXPECT_TRUE(v.is_null());
}

TEST(ParseScalar, MalformedQuotedReturnsFalse) {
    Value v;
    EXPECT_FALSE(parse_scalar("\"unterminated", v));
    EXPECT_FALSE(parse_scalar("\"bad\\q\"", v));
}

TEST(ParseScalar, NumberLikeButNotNumberIsString) {
    Value v;
    ASSERT_TRUE(parse_scalar("1.2.3", v));  // 非 major.minor 這裡是純量 → 非數字 → 字串。
    EXPECT_TRUE(v.is_string());
    EXPECT_EQ(v.as_string(), "1.2.3");
    ASSERT_TRUE(parse_scalar("12ab", v));
    EXPECT_TRUE(v.is_string());
}

}  // namespace
