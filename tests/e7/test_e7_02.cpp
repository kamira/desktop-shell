// E7-02 變數系統 — 契約測試（gtest）
//
// 涵蓋：作用域表（定義 / 覆寫 / 查找 / 本層 vs 父鏈 / 保序 names）、引用替換
// （整串單一引用保留型別、內嵌字串化、多變數、跳脫 $$、孤立 $、容器整串引用）、
// 巢狀（巢狀作用域上溯與遮蔽、巢狀引用遞迴、循環偵測）、未定義變數明確回報、
// 各式錯誤（未終止 ${、空變數名、容器內嵌字串化）、遞迴進入 List / Map、
// Document 版本欄位保留。平台中立：不含任何平台分支。
#include "variables.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::format::Document;
using ds::format::DocumentResolveResult;
using ds::format::FormatVersion;
using ds::format::resolve;
using ds::format::ResolveResult;
using ds::format::Value;
using ds::format::ValueType;
using ds::format::VariableScope;

namespace {

// -----------------------------------------------------------------------------
// 作用域表
// -----------------------------------------------------------------------------

TEST(VariableScope, DefineAndFind) {
    VariableScope s;
    s.define("name", Value::string("hello"));
    s.define("count", Value::integer(42));

    ASSERT_NE(s.find("name"), nullptr);
    EXPECT_EQ(s.find("name")->as_string(), "hello");
    ASSERT_NE(s.find("count"), nullptr);
    EXPECT_EQ(s.find("count")->as_int(), 42);
    EXPECT_EQ(s.find("missing"), nullptr);
    EXPECT_TRUE(s.has("name"));
    EXPECT_FALSE(s.has("missing"));
    EXPECT_EQ(s.size(), 2u);
}

TEST(VariableScope, RedefineOverwritesInPlace) {
    VariableScope s;
    s.define("x", Value::integer(1));
    s.define("y", Value::integer(2));
    s.define("x", Value::integer(9));  // 覆寫，保留插入位置。

    EXPECT_EQ(s.find("x")->as_int(), 9);
    EXPECT_EQ(s.size(), 2u);
    const std::vector<std::string> names = s.names();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "x");  // 保序：x 仍在前。
    EXPECT_EQ(names[1], "y");
}

TEST(VariableScope, NestedScopeLookupAndShadowing) {
    VariableScope parent;
    parent.define("a", Value::string("parent-a"));
    parent.define("b", Value::string("parent-b"));

    VariableScope child(&parent);
    child.define("b", Value::string("child-b"));  // 遮蔽父層 b。

    EXPECT_EQ(child.find("a")->as_string(), "parent-a");  // 上溯父鏈。
    EXPECT_EQ(child.find("b")->as_string(), "child-b");   // 子層遮蔽。
    EXPECT_TRUE(child.has_local("b"));
    EXPECT_FALSE(child.has_local("a"));  // a 只在父層。
    EXPECT_TRUE(child.has("a"));         // 但可解析（含父鏈）。
    EXPECT_EQ(child.parent(), &parent);
}

// -----------------------------------------------------------------------------
// 引用替換：基本
// -----------------------------------------------------------------------------

TEST(Resolve, WholeStringReferencePreservesType) {
    VariableScope s;
    s.define("n", Value::integer(7));
    s.define("greeting", Value::string("hi"));

    const ResolveResult ri = resolve(Value::string("${n}"), s);
    ASSERT_TRUE(ri.ok());
    EXPECT_EQ(ri.value().type(), ValueType::Number);  // 保留原生型別。
    EXPECT_TRUE(ri.value().is_integer());
    EXPECT_EQ(ri.value().as_int(), 7);

    const ResolveResult rs = resolve(Value::string("${greeting}"), s);
    ASSERT_TRUE(rs.ok());
    EXPECT_EQ(rs.value().as_string(), "hi");
}

TEST(Resolve, EmbeddedReferenceStringifies) {
    VariableScope s;
    s.define("name", Value::string("world"));
    s.define("n", Value::integer(3));
    s.define("flag", Value::boolean(true));

    const ResolveResult r = resolve(Value::string("hello ${name}!"), s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().type(), ValueType::String);
    EXPECT_EQ(r.value().as_string(), "hello world!");

    // 數字 / bool 內嵌字串化。
    EXPECT_EQ(resolve(Value::string("count=${n}"), s).value().as_string(), "count=3");
    EXPECT_EQ(resolve(Value::string("on=${flag}"), s).value().as_string(), "on=true");
}

TEST(Resolve, MultipleReferencesInOneString) {
    VariableScope s;
    s.define("host", Value::string("localhost"));
    s.define("port", Value::integer(8080));

    const ResolveResult r = resolve(Value::string("${host}:${port}/api"), s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_string(), "localhost:8080/api");
}

TEST(Resolve, EscapedDollarAndLoneDollar) {
    VariableScope s;
    s.define("x", Value::string("X"));

    // $$ → 字面 $；$${ → 字面 ${（不觸發引用）。
    EXPECT_EQ(resolve(Value::string("price $$5 ${x}"), s).value().as_string(),
              "price $5 X");
    EXPECT_EQ(resolve(Value::string("$${x}"), s).value().as_string(), "${x}");
    // 孤立 $（其後非 $ 亦非 {）為字面。
    EXPECT_EQ(resolve(Value::string("a$b"), s).value().as_string(), "a$b");
    // 無引用的純字串原樣（回傳仍為 String）。
    EXPECT_EQ(resolve(Value::string("plain"), s).value().as_string(), "plain");
}

// -----------------------------------------------------------------------------
// 未定義變數：明確回報（不靜默）
// -----------------------------------------------------------------------------

TEST(Resolve, UndefinedVariableReportedNotSilent) {
    VariableScope s;
    s.define("known", Value::string("ok"));

    const ResolveResult r = resolve(Value::string("${unknown}"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "unknown");
    EXPECT_NE(r.error().message.find("undefined"), std::string::npos);
    EXPECT_NE(r.error().message.find("unknown"), std::string::npos);
}

TEST(Resolve, UndefinedInEmbeddedContextAlsoReported) {
    VariableScope s;
    const ResolveResult r = resolve(Value::string("prefix-${missing}-suffix"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "missing");
}

// -----------------------------------------------------------------------------
// 巢狀引用 + 循環偵測
// -----------------------------------------------------------------------------

TEST(Resolve, NestedReferenceResolvesRecursively) {
    VariableScope s;
    s.define("base", Value::string("/opt/app"));
    s.define("path", Value::string("${base}/bin"));  // 值本身含引用。

    const ResolveResult r = resolve(Value::string("exe=${path}"), s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_string(), "exe=/opt/app/bin");
}

TEST(Resolve, DeepNestedReferenceChain) {
    VariableScope s;
    s.define("a", Value::string("A"));
    s.define("b", Value::string("${a}B"));
    s.define("c", Value::string("${b}C"));

    const ResolveResult r = resolve(Value::string("${c}"), s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_string(), "ABC");
}

TEST(Resolve, CircularReferenceReported) {
    VariableScope s;
    s.define("a", Value::string("${b}"));
    s.define("b", Value::string("${a}"));

    const ResolveResult r = resolve(Value::string("${a}"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("circular"), std::string::npos);
}

TEST(Resolve, SelfReferenceReported) {
    VariableScope s;
    s.define("x", Value::string("${x}"));
    const ResolveResult r = resolve(Value::string("${x}"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("circular"), std::string::npos);
    EXPECT_EQ(r.error().variable, "x");
}

// -----------------------------------------------------------------------------
// 其他錯誤：未終止 / 空名 / 容器內嵌
// -----------------------------------------------------------------------------

TEST(Resolve, UnterminatedReferenceReported) {
    VariableScope s;
    const ResolveResult r = resolve(Value::string("oops ${name"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("unterminated"), std::string::npos);
}

TEST(Resolve, EmptyVariableNameReported) {
    VariableScope s;
    const ResolveResult r = resolve(Value::string("${}"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("empty"), std::string::npos);
}

TEST(Resolve, ContainerInterpolationIntoStringReported) {
    VariableScope s;
    s.define("items", Value::list({Value::integer(1), Value::integer(2)}));
    // 內嵌（非整串）引用容器 → 無法字串化 → 明確錯誤。
    const ResolveResult r = resolve(Value::string("list: ${items}"), s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("container"), std::string::npos);
}

TEST(Resolve, WholeStringContainerReferenceKeepsType) {
    VariableScope s;
    s.define("items", Value::list({Value::integer(1), Value::integer(2)}));
    // 整串單一引用容器 → 型別保留（合法）。
    const ResolveResult r = resolve(Value::string("${items}"), s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().type(), ValueType::List);
    EXPECT_EQ(r.value().size(), 2u);
}

// -----------------------------------------------------------------------------
// 遞迴進入 List / Map；非字串純量原樣
// -----------------------------------------------------------------------------

TEST(Resolve, RecursesIntoListAndMap) {
    VariableScope s;
    s.define("user", Value::string("alice"));
    s.define("port", Value::integer(9000));

    Value doc = Value::map({
        {"greeting", Value::string("hi ${user}")},
        {"port", Value::string("${port}")},  // 整串引用 → 型別保留為 Number。
        {"tags", Value::list({Value::string("u-${user}"), Value::string("plain")})},
        {"count", Value::integer(5)},  // 非字串純量：原樣。
    });

    const ResolveResult r = resolve(doc, s);
    ASSERT_TRUE(r.ok());
    const Value& out = r.value();
    EXPECT_EQ(out.at("greeting").as_string(), "hi alice");
    EXPECT_EQ(out.at("port").type(), ValueType::Number);
    EXPECT_EQ(out.at("port").as_int(), 9000);
    EXPECT_EQ(out.at("tags").as_list()[0].as_string(), "u-alice");
    EXPECT_EQ(out.at("tags").as_list()[1].as_string(), "plain");
    EXPECT_EQ(out.at("count").as_int(), 5);
}

TEST(Resolve, KeysAreNotSubstituted) {
    VariableScope s;
    s.define("k", Value::string("substituted"));
    // 鍵名含 ${k} 不應被替換——只替換值。
    Value m = Value::map({{"${k}", Value::string("v")}});
    const ResolveResult r = resolve(m, s);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value().contains("${k}"));  // 鍵原樣。
    EXPECT_FALSE(r.value().contains("substituted"));
}

// -----------------------------------------------------------------------------
// Document：版本欄位保留 + root 替換
// -----------------------------------------------------------------------------

TEST(Resolve, DocumentPreservesVersionAndSubstitutesRoot) {
    VariableScope s;
    s.define("app", Value::string("shell"));

    Document doc;
    doc.format_version = FormatVersion{1, 0};
    doc.root = Value::map({{"name", Value::string("${app}-ui")}});

    const DocumentResolveResult r = resolve(doc, s);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.document().format_version, (FormatVersion{1, 0}));  // 版本原樣。
    EXPECT_EQ(r.document().root.at("name").as_string(), "shell-ui");
}

TEST(Resolve, DocumentUndefinedVariableFails) {
    VariableScope s;
    Document doc;
    doc.format_version = FormatVersion{1, 0};
    doc.root = Value::map({{"x", Value::string("${nope}")}});

    const DocumentResolveResult r = resolve(doc, s);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "nope");
}

}  // namespace
