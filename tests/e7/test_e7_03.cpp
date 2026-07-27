// E7-03 段落變數 — 契約測試（gtest）。
//
// 覆蓋：基本替換、字串內插、未定義變數報錯且定位、循環引用報錯、巢狀引用，
// 以及：型別保留、段落移除 / 保留、父作用域鏈接、段落非映射報錯、順序無關、
// 缺段落原樣通過、Document 版本欄位保留、以 parse() 端到端。
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "document.hpp"       // E7-01
#include "section_vars.hpp"   // E7-03（透過它取得 E7-02 的型別）

namespace ds::format {
namespace {

// 便捷：以成員清單建一個 root Map。
Value make_map(std::vector<Value::Member> members) {
    return Value::map(std::move(members));
}

// -----------------------------------------------------------------------------
// 基本替換：段落定義變數，其餘內容引用之
// -----------------------------------------------------------------------------
TEST(E7_03_Basic, SingleReferenceExpanded) {
    Value root = make_map({
        {"vars", make_map({{"greeting", Value::string("hello")}})},
        {"name", Value::string("${greeting}")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    const Value& out = r.value();
    EXPECT_TRUE(out.is_map());
    // 段落預設被移除。
    EXPECT_FALSE(out.contains("vars"));
    ASSERT_TRUE(out.contains("name"));
    EXPECT_EQ(out.at("name").as_string(), std::string("hello"));
}

TEST(E7_03_Basic, TypePreservedForWholeReference) {
    // 整串恰為單一引用 → 保留變數值的原生型別（此處為整數）。
    Value root = make_map({
        {"vars", make_map({{"port", Value::integer(8080)}})},
        {"listen", Value::string("${port}")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    const Value& listen = r.value().at("listen");
    ASSERT_TRUE(listen.is_number());
    EXPECT_TRUE(listen.is_integer());
    EXPECT_EQ(listen.as_int(), static_cast<std::int64_t>(8080));
}

TEST(E7_03_Basic, ContainerValuePreservedForWholeReference) {
    Value root = make_map({
        {"vars", make_map({{"tags", Value::list({Value::string("a"), Value::string("b")})}})},
        {"labels", Value::string("${tags}")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    const Value& labels = r.value().at("labels");
    ASSERT_TRUE(labels.is_list());
    ASSERT_EQ(labels.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(labels.as_list()[0].as_string(), std::string("a"));
}

// -----------------------------------------------------------------------------
// 字串內插：引用內嵌於文字中
// -----------------------------------------------------------------------------
TEST(E7_03_Interpolation, EmbeddedReferenceStringified) {
    Value root = make_map({
        {"vars", make_map({{"who", Value::string("world")}})},
        {"msg", Value::string("hello ${who}!")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("msg").as_string(), std::string("hello world!"));
}

TEST(E7_03_Interpolation, PathComposedFromVars) {
    Value root = make_map({
        {"vars", make_map({{"base", Value::string("/opt/app")}})},
        {"bin", Value::string("${base}/bin")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("bin").as_string(), std::string("/opt/app/bin"));
}

TEST(E7_03_Interpolation, NumberStringifiedWhenEmbedded) {
    Value root = make_map({
        {"vars", make_map({{"n", Value::integer(42)}})},
        {"label", Value::string("count=${n}")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("label").as_string(), std::string("count=42"));
}

// -----------------------------------------------------------------------------
// 巢狀引用：變數值本身引用其他變數（順序無關）
// -----------------------------------------------------------------------------
TEST(E7_03_Nested, VariableReferencesAnotherVariable) {
    Value root = make_map({
        {"vars", make_map({
                    {"a", Value::string("${b}")},
                    {"b", Value::string("x")},
                })},
        {"out", Value::string("${a}")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("out").as_string(), std::string("x"));
}

TEST(E7_03_Nested, DeclarationOrderIndependent) {
    // 引用先出現、定義後出現，仍應正確（惰性求值）。
    Value root = make_map({
        {"vars", make_map({
                    {"full", Value::string("${base}/bin/${name}")},
                    {"base", Value::string("/opt")},
                    {"name", Value::string("app")},
                })},
        {"path", Value::string("${full}")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("path").as_string(), std::string("/opt/bin/app"));
}

// -----------------------------------------------------------------------------
// 未定義變數：明確報錯且定位到肇因變數名（不靜默）
// -----------------------------------------------------------------------------
TEST(E7_03_Errors, UndefinedVariableReported) {
    Value root = make_map({
        {"vars", make_map({{"known", Value::string("ok")}})},
        {"bad", Value::string("${missing}")},
    });
    ResolveResult r = expand(root);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, std::string("missing"));  // 定位到出處（變數名）。
    EXPECT_FALSE(r.error().message.empty());
}

TEST(E7_03_Errors, UndefinedVariableNotSilentlyEmptied) {
    // 確認不會靜默替成空字串：結果必為 failure，而非成功且值為空。
    Value root = make_map({
        {"x", Value::string("${nope}")},  // 無 vars 段落
    });
    ResolveResult r = expand(root);
    EXPECT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// 循環引用：明確報錯（不無限遞迴）
// -----------------------------------------------------------------------------
TEST(E7_03_Errors, DirectCycleReported) {
    Value root = make_map({
        {"vars", make_map({
                    {"a", Value::string("${b}")},
                    {"b", Value::string("${a}")},
                })},
        {"out", Value::string("${a}")},
    });
    ResolveResult r = expand(root);
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}

TEST(E7_03_Errors, SelfCycleReported) {
    Value root = make_map({
        {"vars", make_map({{"a", Value::string("${a}")}})},
        {"out", Value::string("${a}")},
    });
    ResolveResult r = expand(root);
    EXPECT_FALSE(r.ok());
}

// -----------------------------------------------------------------------------
// 段落非映射：明確報錯（不靜默）
// -----------------------------------------------------------------------------
TEST(E7_03_Errors, VarsSectionNotAMap) {
    Value root = make_map({
        {"vars", Value::string("oops-not-a-map")},
        {"x", Value::string("literal")},
    });
    ResolveResult r = expand(root);
    ASSERT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}

TEST(E7_03_Errors, BuildScopeRejectsNonMap) {
    VariableScope scope;
    ResolveError err;
    EXPECT_FALSE(build_scope(Value::integer(1), scope, err));
    EXPECT_FALSE(err.message.empty());
}

TEST(E7_03_Errors, BuildScopeAcceptsEmptyMap) {
    VariableScope scope;
    ResolveError err;
    EXPECT_TRUE(build_scope(Value::map({}), scope, err));
    EXPECT_EQ(scope.size(), static_cast<std::size_t>(0));
}

// -----------------------------------------------------------------------------
// 段落移除 / 保留
// -----------------------------------------------------------------------------
TEST(E7_03_Strip, VarsRemovedByDefault) {
    Value root = make_map({
        {"vars", make_map({{"a", Value::string("1")}})},
        {"keep", Value::string("literal")},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().contains("vars"));
    EXPECT_TRUE(r.value().contains("keep"));
}

TEST(E7_03_Strip, VarsKeptWhenRequestedAndAlsoExpanded) {
    ExpandOptions opts;
    opts.strip_vars = false;
    Value root = make_map({
        {"vars", make_map({
                    {"base", Value::string("/opt")},
                    {"bin", Value::string("${base}/bin")},
                })},
    });
    ResolveResult r = expand(root, opts);
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value().contains("vars"));
    // 保留的段落內部引用亦被展開。
    EXPECT_EQ(r.value().at("vars").at("bin").as_string(), std::string("/opt/bin"));
}

// -----------------------------------------------------------------------------
// 缺段落：內容原樣通過（無引用時）
// -----------------------------------------------------------------------------
TEST(E7_03_Missing, NoVarsSectionPassesThrough) {
    Value root = make_map({
        {"a", Value::string("plain")},
        {"n", Value::integer(7)},
    });
    ResolveResult r = expand(root);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("a").as_string(), std::string("plain"));
    EXPECT_EQ(r.value().at("n").as_int(), static_cast<std::int64_t>(7));
}

// -----------------------------------------------------------------------------
// 自訂段落鍵名
// -----------------------------------------------------------------------------
TEST(E7_03_CustomKey, AlternateVarsKey) {
    ExpandOptions opts;
    opts.vars_key = "$defs";
    Value root = make_map({
        {"$defs", make_map({{"v", Value::string("VAL")}})},
        {"use", Value::string("${v}")},
    });
    ResolveResult r = expand(root, opts);
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().contains("$defs"));
    EXPECT_EQ(r.value().at("use").as_string(), std::string("VAL"));
}

// -----------------------------------------------------------------------------
// 父作用域鏈接：段落變數遮蔽父層；未定義者上溯父層
// -----------------------------------------------------------------------------
TEST(E7_03_Parent, InheritsFromParentScope) {
    VariableScope outer;
    outer.define("env", Value::string("prod"));
    ExpandOptions opts;
    opts.parent = &outer;
    Value root = make_map({
        {"vars", make_map({{"svc", Value::string("api")}})},
        {"tag", Value::string("${svc}-${env}")},
    });
    ResolveResult r = expand(root, opts);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("tag").as_string(), std::string("api-prod"));
}

TEST(E7_03_Parent, SectionShadowsParent) {
    VariableScope outer;
    outer.define("x", Value::string("outer"));
    ExpandOptions opts;
    opts.parent = &outer;
    Value root = make_map({
        {"vars", make_map({{"x", Value::string("inner")}})},
        {"out", Value::string("${x}")},
    });
    ResolveResult r = expand(root, opts);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("out").as_string(), std::string("inner"));
}

// -----------------------------------------------------------------------------
// Document 層：format_version 原樣保留
// -----------------------------------------------------------------------------
TEST(E7_03_Document, VersionPreservedAndRootExpanded) {
    Document doc;
    doc.format_version = FormatVersion{1, 0};
    doc.root = make_map({
        {"vars", make_map({{"n", Value::string("shell")}})},
        {"title", Value::string("app: ${n}")},
    });
    DocumentResolveResult r = expand(doc);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.document().format_version, (FormatVersion{1, 0}));
    EXPECT_FALSE(r.document().root.contains("vars"));
    EXPECT_EQ(r.document().root.at("title").as_string(), std::string("app: shell"));
}

TEST(E7_03_Document, ErrorPropagatesFromRoot) {
    Document doc;
    doc.format_version = FormatVersion{1, 0};
    doc.root = make_map({{"bad", Value::string("${undef}")}});
    DocumentResolveResult r = expand(doc);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, std::string("undef"));
}

// -----------------------------------------------------------------------------
// 端到端：以 E7-01 parse() 解析文字，再以 E7-03 展開
// -----------------------------------------------------------------------------
TEST(E7_03_EndToEnd, ParseThenExpand) {
    const std::string text =
        "format_version: 1.0\n"
        "vars:\n"
        "  base: /opt/app\n"
        "  name: hello\n"
        "bin: ${base}/bin\n"
        "title: \"app: ${name}\"\n";
    ParseResult p = parse(text);
    ASSERT_TRUE(p.ok());
    DocumentResolveResult r = expand(p.document());
    ASSERT_TRUE(r.ok());
    const Document& d = r.document();
    EXPECT_FALSE(d.root.contains("vars"));
    EXPECT_EQ(d.root.at("bin").as_string(), std::string("/opt/app/bin"));
    EXPECT_EQ(d.root.at("title").as_string(), std::string("app: hello"));
}

// 非 Map root（Value 層入口）：無段落可抽，直接以父作用域展開。
TEST(E7_03_NonMap, ScalarRootResolvedAgainstParent) {
    VariableScope outer;
    outer.define("v", Value::string("Z"));
    ExpandOptions opts;
    opts.parent = &outer;
    ResolveResult r = expand(Value::string("val=${v}"), opts);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_string(), std::string("val=Z"));
}

}  // namespace
}  // namespace ds::format
