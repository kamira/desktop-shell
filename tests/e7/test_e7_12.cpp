// E7-12 設定值寫回 — 契約測試（gtest）
//
// 涵蓋：路徑解析（鍵 / 索引 / 混合 / 錯誤語法）、設定值寫回（更新既有值、新增鍵、
// 巢狀路徑、自動建中繼 map、list 索引更新 / 附加、契約違反 throw）、序列化格式正確
// （純量各型別、引號 / 轉義、整數 vs 浮點、巢狀 map / list、list-of-maps、format_version 行）、
// round-trip 一致（parse→改值→serialize→再 parse）。平台中立：不含任何平台分支。
#include "writeback.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::format::Document;
using ds::format::FormatVersion;
using ds::format::parse;
using ds::format::parse_path;
using ds::format::parse_scalar;
using ds::format::ParseResult;
using ds::format::Path;
using ds::format::PathSegment;
using ds::format::serialize;
using ds::format::serialize_value;
using ds::format::set_value;
using ds::format::Value;
using ds::format::ValueType;

namespace {

// 小工具：解析一段文字，斷言成功並回傳 Document。
Document must_parse(const std::string& text) {
    ParseResult r = parse(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.document();
}

// -----------------------------------------------------------------------------
// 路徑解析
// -----------------------------------------------------------------------------

TEST(ParsePath, SingleKey) {
    Path p;
    ASSERT_TRUE(parse_path("name", p));
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].kind, PathSegment::Kind::Key);
    EXPECT_EQ(p[0].key, "name");
}

TEST(ParsePath, DottedKeys) {
    Path p;
    ASSERT_TRUE(parse_path("window.size.width", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].key, "window");
    EXPECT_EQ(p[1].key, "size");
    EXPECT_EQ(p[2].key, "width");
}

TEST(ParsePath, KeyWithIndex) {
    Path p;
    ASSERT_TRUE(parse_path("layers[0].name", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].kind, PathSegment::Kind::Key);
    EXPECT_EQ(p[0].key, "layers");
    EXPECT_EQ(p[1].kind, PathSegment::Kind::Index);
    EXPECT_EQ(p[1].index, 0u);
    EXPECT_EQ(p[2].key, "name");
}

TEST(ParsePath, ConsecutiveIndices) {
    Path p;
    ASSERT_TRUE(parse_path("grid[2][3]", p));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].key, "grid");
    EXPECT_EQ(p[1].index, 2u);
    EXPECT_EQ(p[2].index, 3u);
}

TEST(ParsePath, MultiDigitIndex) {
    Path p;
    ASSERT_TRUE(parse_path("items[123]", p));
    ASSERT_EQ(p.size(), 2u);
    EXPECT_EQ(p[1].index, 123u);
}

TEST(ParsePath, EmptyIsEmptyPath) {
    Path p;
    ASSERT_TRUE(parse_path("", p));
    EXPECT_TRUE(p.empty());
}

TEST(ParsePath, MalformedRejected) {
    Path p;
    EXPECT_FALSE(parse_path(".name", p));  // 前導 '.'
    EXPECT_FALSE(parse_path("a..b", p));   // 連續 '.'
    EXPECT_FALSE(parse_path("a.", p));     // 懸空 '.'
    EXPECT_FALSE(parse_path("a[", p));     // 未閉合 '['
    EXPECT_FALSE(parse_path("a[]", p));    // 空括號
    EXPECT_FALSE(parse_path("a[x]", p));   // 非數字索引
    EXPECT_FALSE(parse_path("a[1", p));    // 未閉合
}

// -----------------------------------------------------------------------------
// 設定值寫回：更新既有值
// -----------------------------------------------------------------------------

TEST(SetValue, UpdateExistingScalar) {
    const Value root = Value::map({{"count", Value::integer(1)}, {"name", Value::string("a")}});
    const Value out = set_value(root, "count", Value::integer(42));

    ASSERT_TRUE(out.is_map());
    EXPECT_EQ(out.at("count").as_int(), 42);
    EXPECT_EQ(out.at("name").as_string(), "a");  // 其餘不動。
    EXPECT_EQ(out.keys().size(), 2u);            // 未新增鍵。
    // 原 root 未被就地改寫（純函式）。
    EXPECT_EQ(root.at("count").as_int(), 1);
}

TEST(SetValue, PreservesKeyOrderOnUpdate) {
    const Value root =
        Value::map({{"a", Value::integer(1)}, {"b", Value::integer(2)}, {"c", Value::integer(3)}});
    const Value out = set_value(root, "b", Value::integer(20));
    const auto keys = out.keys();
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "a");
    EXPECT_EQ(keys[1], "b");
    EXPECT_EQ(keys[2], "c");
    EXPECT_EQ(out.at("b").as_int(), 20);
}

// -----------------------------------------------------------------------------
// 設定值寫回：新增鍵
// -----------------------------------------------------------------------------

TEST(SetValue, AddNewKeyAppends) {
    const Value root = Value::map({{"a", Value::integer(1)}});
    const Value out = set_value(root, "b", Value::boolean(true));

    ASSERT_TRUE(out.is_map());
    ASSERT_EQ(out.keys().size(), 2u);
    EXPECT_EQ(out.keys()[0], "a");  // 既有在前。
    EXPECT_EQ(out.keys()[1], "b");  // 新鍵附加於尾端。
    EXPECT_TRUE(out.at("b").as_bool());
}

TEST(SetValue, AddNewKeyToEmptyMap) {
    const Value out = set_value(Value::map({}), "x", Value::string("hi"));
    ASSERT_TRUE(out.is_map());
    EXPECT_EQ(out.at("x").as_string(), "hi");
}

// -----------------------------------------------------------------------------
// 設定值寫回：巢狀路徑
// -----------------------------------------------------------------------------

TEST(SetValue, NestedPathUpdate) {
    const Value root = Value::map(
        {{"window",
          Value::map({{"width", Value::integer(800)}, {"height", Value::integer(600)}})}});
    const Value out = set_value(root, "window.width", Value::integer(1024));

    EXPECT_EQ(out.at("window").at("width").as_int(), 1024);
    EXPECT_EQ(out.at("window").at("height").as_int(), 600);  // 兄弟不動。
    EXPECT_EQ(root.at("window").at("width").as_int(), 800);  // 原樹不動。
}

TEST(SetValue, NestedPathCreatesIntermediateMaps) {
    const Value root = Value::map({{"format", Value::string("x")}});
    const Value out = set_value(root, "a.b.c", Value::integer(7));

    ASSERT_TRUE(out.at("a").is_map());
    ASSERT_TRUE(out.at("a").at("b").is_map());
    EXPECT_EQ(out.at("a").at("b").at("c").as_int(), 7);
    EXPECT_EQ(out.at("format").as_string(), "x");
}

TEST(SetValue, NestedAddKeyIntoExistingMap) {
    const Value root = Value::map({{"window", Value::map({{"width", Value::integer(800)}})}});
    const Value out = set_value(root, "window.title", Value::string("Hello"));
    EXPECT_EQ(out.at("window").at("width").as_int(), 800);
    EXPECT_EQ(out.at("window").at("title").as_string(), "Hello");
}

// -----------------------------------------------------------------------------
// 設定值寫回：list 索引
// -----------------------------------------------------------------------------

TEST(SetValue, ListIndexUpdate) {
    const Value root = Value::map(
        {{"tags", Value::list({Value::string("a"), Value::string("b"), Value::string("c")})}});
    const Value out = set_value(root, "tags[1]", Value::string("B"));

    ASSERT_TRUE(out.at("tags").is_list());
    EXPECT_EQ(out.at("tags").as_list()[0].as_string(), "a");
    EXPECT_EQ(out.at("tags").as_list()[1].as_string(), "B");
    EXPECT_EQ(out.at("tags").as_list()[2].as_string(), "c");
}

TEST(SetValue, ListAppendAtEnd) {
    const Value root = Value::map({{"tags", Value::list({Value::string("a")})}});
    const Value out = set_value(root, "tags[1]", Value::string("b"));  // index == size → 附加。
    ASSERT_EQ(out.at("tags").size(), 2u);
    EXPECT_EQ(out.at("tags").as_list()[1].as_string(), "b");
}

TEST(SetValue, ListOfMapsNestedField) {
    const Value root = Value::map(
        {{"layers", Value::list({Value::map({{"name", Value::string("base")}}),
                                 Value::map({{"name", Value::string("overlay")}})})}});
    const Value out = set_value(root, "layers[1].name", Value::string("top"));
    EXPECT_EQ(out.at("layers").as_list()[0].at("name").as_string(), "base");
    EXPECT_EQ(out.at("layers").as_list()[1].at("name").as_string(), "top");
}

TEST(SetValue, EmptyPathReplacesRoot) {
    const Value root = Value::map({{"a", Value::integer(1)}});
    const Value out = set_value(root, Path{}, Value::string("replaced"));
    EXPECT_TRUE(out.is_string());
    EXPECT_EQ(out.as_string(), "replaced");
}

// -----------------------------------------------------------------------------
// 設定值寫回：契約違反 → throw（不靜默）
// -----------------------------------------------------------------------------

TEST(SetValue, KeyIntoNonMapThrows) {
    const Value root = Value::map({{"n", Value::integer(1)}});
    EXPECT_THROW(set_value(root, "n.deep", Value::integer(2)), std::runtime_error);
}

TEST(SetValue, IndexIntoNonListThrows) {
    const Value root = Value::map({{"n", Value::integer(1)}});
    EXPECT_THROW(set_value(root, "n[0]", Value::integer(2)), std::runtime_error);
}

TEST(SetValue, IndexOutOfRangeThrows) {
    const Value root = Value::map({{"tags", Value::list({Value::string("a")})}});
    EXPECT_THROW(set_value(root, "tags[5]", Value::string("x")), std::runtime_error);
}

TEST(SetValue, AutoCreateListByIndexThrows) {
    const Value root = Value::map({});
    EXPECT_THROW(set_value(root, "missing[0]", Value::integer(1)), std::runtime_error);
}

TEST(SetValue, BadPathStringThrows) {
    const Value root = Value::map({});
    EXPECT_THROW(set_value(root, "a..b", Value::integer(1)), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 序列化：格式正確
// -----------------------------------------------------------------------------

TEST(Serialize, DocumentHasVersionLineFirst) {
    Document doc;
    doc.format_version = FormatVersion{1, 0};
    doc.root = Value::map({{"name", Value::string("hello")}});
    const std::string text = serialize(doc);
    EXPECT_EQ(text.substr(0, text.find('\n')), "format_version: 1.0");
    EXPECT_NE(text.find("name: hello"), std::string::npos);
}

TEST(Serialize, ScalarTypes) {
    EXPECT_EQ(serialize_value(Value::null()), "null\n");
    EXPECT_EQ(serialize_value(Value::boolean(true)), "true\n");
    EXPECT_EQ(serialize_value(Value::boolean(false)), "false\n");
    EXPECT_EQ(serialize_value(Value::integer(42)), "42\n");
    EXPECT_EQ(serialize_value(Value::integer(-7)), "-7\n");
}

TEST(Serialize, FloatKeepsDecimalMarker) {
    // 非整數即使值為整（如 5.0）也須帶小數點，否則再解析 integral 旗標漂移。
    const std::string s = serialize_value(Value::number(5.0, /*integral=*/false));
    EXPECT_NE(s.find('.'), std::string::npos);
    Value v;
    ASSERT_TRUE(parse_scalar(s.substr(0, s.size() - 1), v));
    EXPECT_TRUE(v.is_number());
    EXPECT_FALSE(v.is_integer());
    EXPECT_DOUBLE_EQ(v.as_number(), 5.0);
}

TEST(Serialize, StringQuotingWhenAmbiguous) {
    EXPECT_EQ(serialize_value(Value::string("true")), "\"true\"\n");
    EXPECT_EQ(serialize_value(Value::string("42")), "\"42\"\n");
    EXPECT_EQ(serialize_value(Value::string("null")), "\"null\"\n");
    EXPECT_EQ(serialize_value(Value::string("")), "\"\"\n");
    EXPECT_EQ(serialize_value(Value::string(" pad ")), "\" pad \"\n");  // 前後空白。
    EXPECT_EQ(serialize_value(Value::string("a\nb")), "\"a\\nb\"\n");   // 換行轉義。
}

TEST(Serialize, PlainStringNoQuote) {
    EXPECT_EQ(serialize_value(Value::string("com.example.hello")), "com.example.hello\n");
    EXPECT_EQ(serialize_value(Value::string("hello world")), "hello world\n");
}

TEST(Serialize, NestedMapIndentation) {
    const Value root = Value::map({{"window", Value::map({{"width", Value::integer(800)}})}});
    const std::string s = serialize_value(root);
    EXPECT_EQ(s, "window:\n  width: 800\n");
}

TEST(Serialize, ScalarList) {
    const Value root =
        Value::map({{"tags", Value::list({Value::string("alpha"), Value::string("beta")})}});
    const std::string s = serialize_value(root);
    EXPECT_EQ(s, "tags:\n  - alpha\n  - beta\n");
}

TEST(Serialize, ListOfMaps) {
    const Value root = Value::map(
        {{"layers", Value::list({Value::map({{"name", Value::string("base")}}),
                                 Value::map({{"name", Value::string("overlay")}})})}});
    const std::string s = serialize_value(root);
    EXPECT_EQ(s, "layers:\n  -\n    name: base\n  -\n    name: overlay\n");
}

// -----------------------------------------------------------------------------
// round-trip：parse → serialize → 再 parse 應一致
// -----------------------------------------------------------------------------

TEST(RoundTrip, SerializeParsedIsIdentity) {
    const std::string text =
        "format_version: 1.0\n"
        "name: com.example.app\n"
        "enabled: true\n"
        "count: 42\n"
        "ratio: 3.14\n"
        "nothing: null\n"
        "quoted: \"has: colon and\\nnewline\"\n"
        "tags:\n"
        "  - alpha\n"
        "  - beta\n"
        "window:\n"
        "  width: 800\n"
        "  height: 600\n"
        "layers:\n"
        "  -\n"
        "    name: base\n"
        "  -\n"
        "    name: overlay\n";

    const Document d1 = must_parse(text);
    const std::string out = serialize(d1);
    const Document d2 = must_parse(out);

    EXPECT_EQ(d1.format_version, d2.format_version);
    EXPECT_TRUE(d1.root == d2.root) << "序列化再解析後根內容不一致";
}

TEST(RoundTrip, SetThenSerializeThenParseValues) {
    const std::string text =
        "format_version: 1.0\n"
        "window:\n"
        "  width: 800\n"
        "  height: 600\n";

    const Document d = must_parse(text);
    Value root = set_value(d.root, "window.width", Value::integer(1280));
    root = set_value(root, "title", Value::string("My App"));
    root = set_value(root, "tags", Value::list({Value::string("x"), Value::string("y")}));

    const std::string out = serialize(root, d.format_version);
    const Document d2 = must_parse(out);

    EXPECT_EQ(d2.format_version, (FormatVersion{1, 0}));
    EXPECT_EQ(d2.root.at("window").at("width").as_int(), 1280);
    EXPECT_EQ(d2.root.at("window").at("height").as_int(), 600);
    EXPECT_EQ(d2.root.at("title").as_string(), "My App");
    ASSERT_TRUE(d2.root.at("tags").is_list());
    EXPECT_EQ(d2.root.at("tags").as_list()[1].as_string(), "y");
}

TEST(RoundTrip, TrickyStringValuesSurviveWriteBack) {
    const Value root = Value::map({{"a", Value::string("42")},        // 看似數字。
                                   {"b", Value::string("true")},      // 看似 bool。
                                   {"c", Value::string("path: /x")},  // 含冒號。
                                   {"d", Value::string("")}});        // 空字串。
    const std::string out = serialize(root, FormatVersion{1, 0});
    const Document d2 = must_parse(out);
    EXPECT_EQ(d2.root.at("a").as_string(), "42");
    EXPECT_EQ(d2.root.at("b").as_string(), "true");
    EXPECT_EQ(d2.root.at("c").as_string(), "path: /x");
    EXPECT_EQ(d2.root.at("d").as_string(), "");
}

}  // namespace
