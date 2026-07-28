// E7-14 圖形化設定與就地編輯 — 契約測試（gtest）
//
// 涵蓋：
//   - 從 Value / Document 建可編輯欄位模型（前序、保序、巢狀路徑、空容器不產欄位）；
//   - 各控制項型別推斷（Boolean / Integer / Number / Text / Color）與 schema 覆寫（Enum / 範圍 / 標籤）；
//   - 就地編輯經 E7-12 set_value 寫回（回傳新樹、原樹不動）；
//   - 序列化 round-trip 格式保留（改一欄，其餘欄位與型別不漂移，再 parse 一致）；
//   - 型別 / 範圍 / 列舉驗證錯誤（明確 throw，不靜默）；
//   - 巢狀欄位路徑（map.list[i].key）尋址與編輯；
//   - 路徑不存在 / 便捷字串多載的錯誤處理。
#include "editor_model.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

using ds::format::apply_edit;
using ds::format::build_editor_model;
using ds::format::Document;
using ds::format::EditableField;
using ds::format::EditConstraints;
using ds::format::EditKind;
using ds::format::EditorSchema;
using ds::format::FieldHint;
using ds::format::find_field;
using ds::format::is_color_literal;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::Path;
using ds::format::serialize;
using ds::format::set_value;
using ds::format::to_string;
using ds::format::validate_edit;
using ds::format::Value;

namespace {

// 小工具：解析一段文字，斷言成功並回傳 Document。
Document must_parse(const std::string& text) {
    ParseResult r = parse(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.document();
}

// 小工具：於模型中依路徑字串取欄位（找不到即 nullptr）。
const EditableField* field_at(const std::vector<EditableField>& model, const std::string& p) {
    return find_field(model, p);
}

const char* kSample =
    "format_version: 1.0\n"
    "name: com.example.app\n"
    "enabled: true\n"
    "window:\n"
    "  width: 800\n"
    "  height: 600\n"
    "  ratio: 1.5\n"
    "theme:\n"
    "  color: \"#ff8800\"\n"
    "  mode: dark\n"
    "tags:\n"
    "  - alpha\n"
    "  - beta\n";

}  // namespace

// -----------------------------------------------------------------------------
// EditKind / 顏色字面值
// -----------------------------------------------------------------------------

TEST(E7_14_Kind, ToStringCoversAll) {
    EXPECT_STREQ("Text", to_string(EditKind::Text));
    EXPECT_STREQ("Integer", to_string(EditKind::Integer));
    EXPECT_STREQ("Number", to_string(EditKind::Number));
    EXPECT_STREQ("Boolean", to_string(EditKind::Boolean));
    EXPECT_STREQ("Enum", to_string(EditKind::Enum));
    EXPECT_STREQ("Color", to_string(EditKind::Color));
}

TEST(E7_14_Color, LiteralRecognition) {
    EXPECT_TRUE(is_color_literal("#fff"));
    EXPECT_TRUE(is_color_literal("#FF8800"));
    EXPECT_TRUE(is_color_literal("#123abc"));
    EXPECT_FALSE(is_color_literal("ff8800"));   // 缺 '#'
    EXPECT_FALSE(is_color_literal("#gg0000"));  // 非十六進位
    EXPECT_FALSE(is_color_literal("#ff88"));    // 長度不對
    EXPECT_FALSE(is_color_literal(""));
}

// -----------------------------------------------------------------------------
// 建模：欄位、型別推斷、巢狀路徑
// -----------------------------------------------------------------------------

TEST(E7_14_Model, BuildsFieldsForEveryScalarLeaf) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);

    // name, enabled, window.width, window.height, window.ratio,
    // theme.color, theme.mode, tags[0], tags[1] = 9 個純量葉。
    ASSERT_EQ(model.size(), 9u);

    // 保序（前序）：第一欄應為 name。
    EXPECT_EQ(model[0].path_string(), "name");
    EXPECT_EQ(model[0].label, "name");
}

TEST(E7_14_Model, InfersControlKinds) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);

    ASSERT_NE(field_at(model, "name"), nullptr);
    EXPECT_EQ(field_at(model, "name")->kind, EditKind::Text);
    EXPECT_EQ(field_at(model, "enabled")->kind, EditKind::Boolean);
    EXPECT_EQ(field_at(model, "window.width")->kind, EditKind::Integer);
    EXPECT_EQ(field_at(model, "window.ratio")->kind, EditKind::Number);
    EXPECT_EQ(field_at(model, "theme.color")->kind, EditKind::Color);  // "#ff8800"
    EXPECT_EQ(field_at(model, "theme.mode")->kind, EditKind::Text);
}

TEST(E7_14_Model, NestedPathAndListLabels) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);

    const EditableField* w = field_at(model, "window.width");
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->label, "width");        // 末段鍵為標籤
    EXPECT_EQ(w->current.as_int(), 800);

    const EditableField* t0 = field_at(model, "tags[0]");
    ASSERT_NE(t0, nullptr);
    EXPECT_EQ(t0->label, "[0]");         // list 元素標籤
    EXPECT_EQ(t0->kind, EditKind::Text);
    EXPECT_EQ(t0->current.as_string(), "alpha");
}

TEST(E7_14_Model, EmptyContainerYieldsNoField) {
    // 空 Map root → 無欄位。
    std::vector<EditableField> model = build_editor_model(Value::map({}));
    EXPECT_TRUE(model.empty());
}

TEST(E7_14_Model, SchemaOverridesKindLabelAndConstraints) {
    Document doc = must_parse(kSample);
    EditorSchema schema;
    schema.push_back(FieldHint::at("theme.mode", EditKind::Enum)
                         .with_label("外觀模式")
                         .with_constraints(EditConstraints::enum_of({"light", "dark"})));
    schema.push_back(FieldHint::at("window.width", EditKind::Integer)
                         .with_constraints(EditConstraints::range(320, 4096)));

    std::vector<EditableField> model = build_editor_model(doc, schema);

    const EditableField* mode = field_at(model, "theme.mode");
    ASSERT_NE(mode, nullptr);
    EXPECT_EQ(mode->kind, EditKind::Enum);
    EXPECT_EQ(mode->label, "外觀模式");
    ASSERT_EQ(mode->constraints.enum_options.size(), 2u);

    const EditableField* w = field_at(model, "window.width");
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->constraints.has_min);
    EXPECT_EQ(w->constraints.min_value, 320);
    EXPECT_EQ(w->constraints.max_value, 4096);
}

// -----------------------------------------------------------------------------
// 驗證：型別 / 範圍 / 列舉（不靜默）
// -----------------------------------------------------------------------------

TEST(E7_14_Validate, TypeMismatchThrows) {
    EditableField boolean;
    boolean.kind = EditKind::Boolean;
    EXPECT_THROW(validate_edit(boolean, Value::string("yes")), std::runtime_error);
    EXPECT_NO_THROW(validate_edit(boolean, Value::boolean(false)));

    EditableField text;
    text.kind = EditKind::Text;
    EXPECT_THROW(validate_edit(text, Value::integer(3)), std::runtime_error);

    EditableField integer;
    integer.kind = EditKind::Integer;
    EXPECT_THROW(validate_edit(integer, Value::number(1.5)), std::runtime_error);  // 非整數
    EXPECT_THROW(validate_edit(integer, Value::string("x")), std::runtime_error);
    EXPECT_NO_THROW(validate_edit(integer, Value::integer(42)));
}

TEST(E7_14_Validate, RangeViolationThrows) {
    EditableField f;
    f.kind = EditKind::Integer;
    f.constraints = EditConstraints::range(0, 100);
    EXPECT_NO_THROW(validate_edit(f, Value::integer(50)));
    EXPECT_THROW(validate_edit(f, Value::integer(-1)), std::runtime_error);
    EXPECT_THROW(validate_edit(f, Value::integer(101)), std::runtime_error);

    EditableField num;
    num.kind = EditKind::Number;
    num.constraints = EditConstraints::min_of(0.0);
    EXPECT_NO_THROW(validate_edit(num, Value::number(0.25)));
    EXPECT_THROW(validate_edit(num, Value::number(-0.1)), std::runtime_error);
}

TEST(E7_14_Validate, EnumMembershipAndColor) {
    EditableField en;
    en.kind = EditKind::Enum;
    en.constraints = EditConstraints::enum_of({"light", "dark"});
    EXPECT_NO_THROW(validate_edit(en, Value::string("dark")));
    EXPECT_THROW(validate_edit(en, Value::string("sepia")), std::runtime_error);
    EXPECT_THROW(validate_edit(en, Value::integer(1)), std::runtime_error);

    EditableField col;
    col.kind = EditKind::Color;
    EXPECT_NO_THROW(validate_edit(col, Value::string("#00ff00")));
    EXPECT_THROW(validate_edit(col, Value::string("green")), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 就地套用：經 E7-12 set_value 寫回（回傳新樹、原樹不動）
// -----------------------------------------------------------------------------

TEST(E7_14_Apply, ByFieldWritesBackAndKeepsOriginalImmutable) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);
    const EditableField* w = field_at(model, "window.width");
    ASSERT_NE(w, nullptr);

    Value updated = apply_edit(doc.root, *w, Value::integer(1280));

    // 新樹已更新。
    EXPECT_EQ(updated.at("window").at("width").as_int(), 1280);
    // 原樹不被就地改寫（承 E7-01 不可變 Value）。
    EXPECT_EQ(doc.root.at("window").at("width").as_int(), 800);
}

TEST(E7_14_Apply, ByFieldRejectsInvalidEditNoWrite) {
    Document doc = must_parse(kSample);
    EditorSchema schema;
    schema.push_back(FieldHint::at("window.width", EditKind::Integer)
                         .with_constraints(EditConstraints::range(320, 4096)));
    std::vector<EditableField> model = build_editor_model(doc, schema);
    const EditableField* w = field_at(model, "window.width");
    ASSERT_NE(w, nullptr);

    EXPECT_THROW(apply_edit(doc.root, *w, Value::integer(10)), std::runtime_error);   // 越界
    EXPECT_THROW(apply_edit(doc.root, *w, Value::string("big")), std::runtime_error); // 型別
}

TEST(E7_14_Apply, ByPathInfersKindAndTypeChecks) {
    Document doc = must_parse(kSample);

    Value ok = apply_edit(doc.root, std::string("window.height"), Value::integer(720));
    EXPECT_EQ(ok.at("window").at("height").as_int(), 720);

    // enabled 是 Boolean；給字串 → 型別錯，throw。
    EXPECT_THROW(apply_edit(doc.root, std::string("enabled"), Value::string("nope")),
                 std::runtime_error);

    // 路徑不存在 → throw（不靜默）。
    EXPECT_THROW(apply_edit(doc.root, std::string("window.zzz"), Value::integer(1)),
                 std::runtime_error);
}

TEST(E7_14_Apply, NestedListElementPath) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);
    const EditableField* t1 = field_at(model, "tags[1]");
    ASSERT_NE(t1, nullptr);

    Value updated = apply_edit(doc.root, *t1, Value::string("gamma"));
    ASSERT_EQ(updated.at("tags").size(), 2u);
    EXPECT_EQ(updated.at("tags").as_list()[1].as_string(), "gamma");
    EXPECT_EQ(updated.at("tags").as_list()[0].as_string(), "alpha");  // 兄弟不動
}

// -----------------------------------------------------------------------------
// 序列化 round-trip：格式保留（改一欄，其餘欄位 / 型別不漂移）
// -----------------------------------------------------------------------------

TEST(E7_14_RoundTrip, EditSerializeReparsePreservesEverythingElse) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);

    // 改 window.width → 1440。
    const EditableField* w = field_at(model, "window.width");
    ASSERT_NE(w, nullptr);
    Value edited = apply_edit(doc.root, *w, Value::integer(1440));

    // 經 E7-12 serialize 回文字，再 parse。
    std::string text = serialize(edited, doc.format_version);
    Document reparsed = must_parse(text);

    // 被改欄位生效。
    EXPECT_EQ(reparsed.root.at("window").at("width").as_int(), 1440);
    // 其餘欄位與型別保留（不漂移）。
    EXPECT_EQ(reparsed.root.at("name").as_string(), "com.example.app");
    EXPECT_TRUE(reparsed.root.at("enabled").as_bool());
    EXPECT_EQ(reparsed.root.at("window").at("height").as_int(), 600);
    EXPECT_TRUE(reparsed.root.at("window").at("ratio").is_number());
    EXPECT_FALSE(reparsed.root.at("window").at("ratio").is_integer());  // 1.5 不塌成整數
    EXPECT_EQ(reparsed.root.at("theme").at("color").as_string(), "#ff8800");
    EXPECT_EQ(reparsed.root.at("theme").at("mode").as_string(), "dark");
    EXPECT_EQ(reparsed.root.at("tags").size(), 2u);

    // round-trip 後重新建模，欄位數與型別不變（模型穩定）。
    std::vector<EditableField> model2 = build_editor_model(reparsed);
    EXPECT_EQ(model2.size(), model.size());
    EXPECT_EQ(field_at(model2, "window.width")->current.as_int(), 1440);
}

TEST(E7_14_RoundTrip, ColorEditPreservedThroughSerialization) {
    Document doc = must_parse(kSample);
    std::vector<EditableField> model = build_editor_model(doc);
    const EditableField* c = field_at(model, "theme.color");
    ASSERT_NE(c, nullptr);
    ASSERT_EQ(c->kind, EditKind::Color);

    Value edited = apply_edit(doc.root, *c, Value::string("#00aaff"));
    std::string text = serialize(edited, doc.format_version);
    Document reparsed = must_parse(text);

    EXPECT_EQ(reparsed.root.at("theme").at("color").as_string(), "#00aaff");
    EXPECT_EQ(build_editor_model(reparsed).size(), model.size());
}
