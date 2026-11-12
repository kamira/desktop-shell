// E7-16 設定分頁登錄（核心分頁 + 每套件一頁，append-only）— 契約測試（gtest）
//
// 本測試組的**主軸是反向斷言**，不是正向斷言。
//
// 使用者於 `CHG-20260829-05` 的裁定是：「這裡的由已安裝貢獻**不是複寫**，而是**額外增加**
// 設定的 tab」。這句話有兩半，而只測得出前半是本單元最容易出現的假綠燈：
//   - 正向（append 成立）：「安裝套件後，多出了一個分頁」——這很容易過，也證明不了裁定。
//   - 反向（not-overwrite 成立）：「套件宣告了一個與核心**同名**的欄位時，核心分頁的值
//     **原封不動**」——唯有製造**衝突**才測得到。這才是裁定的實質內容。
//
// 故下方 `E7_16_AppendOnly` 一組全部是衝突情境，且刻意涵蓋數條「看起來是 append-only
// 其實不是」的路徑：
//   1. 套件 vs 核心同名欄位      → 核心值不動、核心欄位數不變。
//   2. 套件 vs 套件彼此同名欄位  → 各自一頁、互不影響，核心亦不受影響。
//   3. 套件宣告核心**沒有**的欄位 → 核心分頁**不會長出**該欄位（append 的是分頁，不是欄位）。
//   4. 卸載 → 該分頁消失，核心分頁與其他套件分頁不受影響。
//   5. 套件試圖佔用核心保留 page id → throw（不靜默覆寫核心分頁）。
//   6. 編輯互不穿透：編核心不動套件頁，編套件頁不動核心。
//   7. 列舉順序固定（核心恆首位、套件字典序）→ 衝突不會被排序遮蔽。
//
// 另涵蓋：不重造欄位模型（分頁欄位即 E7-14 `EditableField`，經 `build_editor_model` 產出、
// 經 `apply_edit` 驗證與寫回）、E7-01 文字 → 分頁的實際路徑、不靜默失敗（來源不一致 / 非 Map /
// 重複 id / 不存在的分頁與欄位 / 驗證違反）。
#include "settings_pages.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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
using ds::format::kCorePageId;
using ds::format::kCorePageTitle;
using ds::format::MemorySettingsContributions;
using ds::format::PageOrigin;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::SettingsContribution;
using ds::format::SettingsContributionSource;
using ds::format::SettingsPage;
using ds::format::SettingsPageRegistry;
using ds::format::to_string;
using ds::format::Value;

namespace {

// 小工具：解析一段 E7-01 文字，斷言成功並回傳 Document。
Document must_parse(const std::string& text) {
    ParseResult r = parse(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.document();
}

// 核心設定：平台自身的設定樹。刻意含一個名為 `theme` 的欄位，
// 之後讓套件宣告同名的 `theme` 來製造衝突。
Value core_settings() {
    return Value::map({
        {"theme", Value::string("dark")},
        {"opacity", Value::number(0.8)},
        {"window", Value::map({
            {"width", Value::integer(800)},
            {"height", Value::integer(600)},
        })},
    });
}

// 取某分頁某欄位的當前值（分頁 / 欄位不存在即 EXPECT 失敗並回 Null）。
Value value_at(const SettingsPageRegistry& reg, const std::string& page_id,
               const std::string& path) {
    const SettingsPage* p = reg.page(page_id);
    EXPECT_NE(p, nullptr) << "分頁不存在: " << page_id;
    if (p == nullptr) return Value::null();
    const EditableField* f = p->field(path);
    EXPECT_NE(f, nullptr) << "欄位不存在: " << page_id << " / " << path;
    if (f == nullptr) return Value::null();
    return f->current;
}

// 一個刻意「不一致」的貢獻來源：列得出 id，但 contribution() 永遠回 false。
// 用來釘住「列出了卻取不到 → 明確報錯，不靜默讓分頁無聲消失」。
class InconsistentSource : public SettingsContributionSource {
public:
    std::vector<std::string> contributing_package_ids() const override {
        return {"com.example.ghost"};
    }
    bool contribution(const std::string&, SettingsContribution&) const override { return false; }
};

// 一個會列出重複 id 的來源（記憶體替身用 map 不可能產生重複，故需手搓）。
class DuplicateIdSource : public SettingsContributionSource {
public:
    std::vector<std::string> contributing_package_ids() const override {
        return {"com.example.a", "com.example.a"};
    }
    bool contribution(const std::string& id, SettingsContribution& out) const override {
        out = SettingsContribution{};
        out.package_id = id;
        out.declared = Value::map({});
        return true;
    }
}; 

// 一個會試圖佔用核心保留 page id 的來源（記憶體替身的 declare 已擋，故需手搓，
// 以證明守門在 **registry** 這一層也成立——任何來源實作都繞不過）。
class CoreIdSquatterSource : public SettingsContributionSource {
public:
    std::vector<std::string> contributing_package_ids() const override {
        return {std::string(kCorePageId)};
    }
    bool contribution(const std::string& id, SettingsContribution& out) const override {
        out = SettingsContribution{};
        out.package_id = id;
        out.declared = Value::map({{"theme", Value::string("hijacked")}});
        return true;
    }
};

// 一個宣告根不是 Map 的來源。
class NonMapSource : public SettingsContributionSource {
public:
    std::vector<std::string> contributing_package_ids() const override {
        return {"com.example.scalar"};
    }
    bool contribution(const std::string& id, SettingsContribution& out) const override {
        out = SettingsContribution{};
        out.package_id = id;
        out.declared = Value::string("我不是 map");
        return true;
    }
};

// 一個可切換成「壞掉」的來源：正常時提供一個合法貢獻，`break_it()` 後改列出重複 id。
// 用來釘住 `refresh()` 的「全有全無」：中途失敗時登錄必須維持原狀，不得留下半套狀態。
class FlakySource : public SettingsContributionSource {
public:
    void break_it() { broken_ = true; }

    std::vector<std::string> contributing_package_ids() const override {
        if (broken_) return {"com.example.ok", "com.example.dup", "com.example.dup"};
        return {"com.example.ok"};
    }
    bool contribution(const std::string& id, SettingsContribution& out) const override {
        out = SettingsContribution{};
        out.package_id = id;
        out.declared = Value::map({{"x", Value::integer(1)}});
        return true;
    }

private:
    bool broken_ = false;
};

// 一個列出空字串 id 的來源。
class EmptyIdSource : public SettingsContributionSource {
public:
    std::vector<std::string> contributing_package_ids() const override { return {std::string()}; }
    bool contribution(const std::string& id, SettingsContribution& out) const override {
        out = SettingsContribution{};
        out.package_id = id;
        out.declared = Value::map({});
        return true;
    }
};

}  // namespace

// =============================================================================
// 基本形狀：核心分頁恆存在；每套件一頁
// =============================================================================

TEST(E7_16_Shape, CorePageAlwaysExistsEvenWithNoContributions) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);

    EXPECT_EQ(reg.size(), 1u);
    EXPECT_EQ(reg.package_page_count(), 0u);
    EXPECT_TRUE(reg.contains(kCorePageId));
    EXPECT_EQ(reg.core_page().id, std::string(kCorePageId));
    EXPECT_EQ(reg.core_page().title, std::string(kCorePageTitle));
    EXPECT_EQ(reg.core_page().origin, PageOrigin::Core);
    EXPECT_TRUE(reg.core_page().owner_package_id.empty());
}

TEST(E7_16_Shape, OnePagePerContributingPackage) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.alpha", Value::map({{"rate", Value::integer(5)}}));
    contributions.declare("com.example.beta", Value::map({{"path", Value::string("/tmp")}}));

    SettingsPageRegistry reg(core_settings(), contributions);

    EXPECT_EQ(reg.size(), 3u);  // 核心 + 兩個套件
    EXPECT_EQ(reg.package_page_count(), 2u);
    EXPECT_TRUE(reg.contains("com.example.alpha"));
    EXPECT_TRUE(reg.contains("com.example.beta"));

    const SettingsPage* alpha = reg.page("com.example.alpha");
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->origin, PageOrigin::Package);
    EXPECT_EQ(alpha->owner_package_id, "com.example.alpha");
}

TEST(E7_16_Shape, OriginToStringCoversAll) {
    EXPECT_STREQ(to_string(PageOrigin::Core), "Core");
    EXPECT_STREQ(to_string(PageOrigin::Package), "Package");
}

TEST(E7_16_Shape, PageTitleDefaultsToPackageIdAndCanBeOverridden) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.plain", Value::map({{"a", Value::integer(1)}}));

    SettingsContribution titled;
    titled.package_id = "com.example.titled";
    titled.title = "我的套件設定";
    titled.declared = Value::map({{"b", Value::integer(2)}});
    contributions.declare(titled);

    SettingsPageRegistry reg(core_settings(), contributions);
    EXPECT_EQ(reg.page("com.example.plain")->title, "com.example.plain");
    EXPECT_EQ(reg.page("com.example.titled")->title, "我的套件設定");
}

// =============================================================================
// 不重造欄位模型：分頁欄位就是 E7-14 的 EditableField
// =============================================================================

TEST(E7_16_FieldModel, PageFieldsAreExactlyE7_14BuildEditorModelOutput) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);

    // 對同一棵 Value 樹直接呼叫 E7-14 `build_editor_model`，結果須與分頁的 fields 逐欄位相同。
    const std::vector<EditableField> expected = build_editor_model(core_settings());
    const std::vector<EditableField>& actual = reg.core_page().fields;

    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i].path_string(), expected[i].path_string()) << "第 " << i << " 欄位";
        EXPECT_EQ(actual[i].kind, expected[i].kind) << "第 " << i << " 欄位";
        EXPECT_EQ(actual[i].current, expected[i].current) << "第 " << i << " 欄位";
        EXPECT_EQ(actual[i].label, expected[i].label) << "第 " << i << " 欄位";
    }
}

TEST(E7_16_FieldModel, InheritsE7_14KindInferenceAndNestedPaths) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);
    const SettingsPage& core = reg.core_page();

    ASSERT_NE(core.field("theme"), nullptr);
    EXPECT_EQ(core.field("theme")->kind, EditKind::Text);
    ASSERT_NE(core.field("opacity"), nullptr);
    EXPECT_EQ(core.field("opacity")->kind, EditKind::Number);
    ASSERT_NE(core.field("window.width"), nullptr);  // 巢狀路徑
    EXPECT_EQ(core.field("window.width")->kind, EditKind::Integer);
    EXPECT_EQ(core.field("window.width")->current.as_int(), 800);
}

TEST(E7_16_FieldModel, SchemaHintsAreHonouredOnBothCoreAndPackagePages) {
    EditorSchema core_schema{
        FieldHint::at("theme", EditKind::Enum)
            .with_constraints(EditConstraints::enum_of({"dark", "light"}))};

    MemorySettingsContributions contributions;
    SettingsContribution c;
    c.package_id = "com.example.ranged";
    c.declared = Value::map({{"level", Value::integer(3)}});
    c.schema = EditorSchema{
        FieldHint::at("level", EditKind::Integer).with_constraints(EditConstraints::range(1, 5))};
    contributions.declare(c);

    SettingsPageRegistry reg(core_settings(), core_schema, contributions);

    EXPECT_EQ(reg.core_page().field("theme")->kind, EditKind::Enum);
    EXPECT_EQ(reg.page("com.example.ranged")->field("level")->kind, EditKind::Integer);

    // 約束確實生效（經 E7-14 validate_edit 的範圍檢查）。
    EXPECT_THROW(reg.apply_page_edit("com.example.ranged", "level", Value::integer(9)),
                 std::runtime_error);
    EXPECT_THROW(reg.apply_page_edit(kCorePageId, "theme", Value::string("neon")),
                 std::runtime_error);
}

TEST(E7_16_FieldModel, BuildsPagesFromParsedE7_01Text) {
    // 實際路徑：E7-01 文字 → Document → 分頁（核心與套件皆然）。
    const Document core_doc = must_parse(
        "format_version: 1.0\n"
        "theme: dark\n"
        "window:\n"
        "  width: 800\n");
    const Document pkg_doc = must_parse(
        "format_version: 1.0\n"
        "theme: neon\n"
        "refresh_seconds: 30\n");

    MemorySettingsContributions contributions;
    contributions.declare("com.example.widget", pkg_doc.root);
    SettingsPageRegistry reg(core_doc.root, contributions);

    EXPECT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "dark");
    EXPECT_EQ(value_at(reg, "com.example.widget", "theme").as_string(), "neon");
    EXPECT_EQ(value_at(reg, "com.example.widget", "refresh_seconds").as_int(), 30);
}

// =============================================================================
// append-only（**反向斷言**：本單元的核心契約）
// =============================================================================

// 反向斷言 1：套件宣告與核心**同名**的欄位 → 核心分頁的值原封不動、欄位數不變。
// 這是驗收標準點名的那一條，也是使用者裁定的實質內容。
TEST(E7_16_AppendOnly, PackageDeclaringFieldNameCollidingWithCoreDoesNotAlterCoreValue) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);

    // 安裝前的核心狀態（完整快照，之後逐一對帳）。
    const Value core_before = reg.core_values();
    const std::size_t core_field_count_before = reg.core_page().field_count();
    ASSERT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "dark");

    // 安裝一個套件，它宣告 `theme`（與核心同名）、`opacity`（與核心同名）
    // 以及 `window.width`（與核心同名的巢狀路徑）—— 三種衝突形狀一次到齊。
    contributions.declare("com.example.hijack", Value::map({
        {"theme", Value::string("hijacked")},
        {"opacity", Value::number(0.1)},
        {"window", Value::map({{"width", Value::integer(1)}})},
    }));
    reg.refresh();

    // 正向：分頁確實多了一個（append 成立）。
    EXPECT_EQ(reg.package_page_count(), 1u);
    EXPECT_TRUE(reg.contains("com.example.hijack"));

    // **反向：核心分頁完全沒有被改動**（not-overwrite 成立）。
    EXPECT_EQ(reg.core_values(), core_before) << "核心設定樹被套件貢獻改動了——append-only 被破壞";
    EXPECT_EQ(reg.core_page().field_count(), core_field_count_before);
    EXPECT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "dark");
    EXPECT_DOUBLE_EQ(value_at(reg, kCorePageId, "opacity").as_number(), 0.8);
    EXPECT_EQ(value_at(reg, kCorePageId, "window.width").as_int(), 800);

    // 而套件分頁持有的是**它自己的**值（兩頁各自獨立，不是共用一棵樹）。
    EXPECT_EQ(value_at(reg, "com.example.hijack", "theme").as_string(), "hijacked");
    EXPECT_EQ(value_at(reg, "com.example.hijack", "window.width").as_int(), 1);
}

// 反向斷言 2：兩個套件**彼此**宣告同名欄位 → 各自一頁、互不影響，核心亦不受影響。
// 「套件 vs 核心」過了不代表「套件 vs 套件」也過——後者是另一條獨立的覆寫路徑。
TEST(E7_16_AppendOnly, TwoPackagesDeclaringTheSameFieldNameDoNotOverwriteEachOther) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.first", Value::map({{"theme", Value::string("first")}}));
    contributions.declare("com.example.second", Value::map({{"theme", Value::string("second")}}));

    SettingsPageRegistry reg(core_settings(), contributions);

    EXPECT_EQ(reg.package_page_count(), 2u);
    EXPECT_EQ(value_at(reg, "com.example.first", "theme").as_string(), "first");
    EXPECT_EQ(value_at(reg, "com.example.second", "theme").as_string(), "second");
    // 核心的 `theme` 也還是核心的。
    EXPECT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "dark");
}

// 反向斷言 3：套件宣告核心**本來沒有**的欄位名 → 核心分頁**不會長出**該欄位。
// append-only 增加的是**分頁**，不是核心分頁上的**欄位**。這條路徑最容易被誤實作成
// 「把套件宣告併進核心設定樹」——那樣正向測試（分頁出現了）仍會綠。
TEST(E7_16_AppendOnly, PackageDeclaringBrandNewFieldDoesNotAddItToCorePage) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);
    const std::size_t core_field_count_before = reg.core_page().field_count();

    contributions.declare("com.example.extra",
                          Value::map({{"never_seen_in_core", Value::integer(42)}}));
    reg.refresh();

    // 核心分頁既沒有該欄位，欄位數也沒有變。
    EXPECT_EQ(reg.core_page().field("never_seen_in_core"), nullptr)
        << "套件宣告的新欄位被併進核心分頁了——append 的應該是分頁，不是核心欄位";
    EXPECT_EQ(reg.core_page().field_count(), core_field_count_before);
    EXPECT_FALSE(reg.core_values().contains("never_seen_in_core"));

    // 它只存在於套件自己那一頁。
    EXPECT_EQ(value_at(reg, "com.example.extra", "never_seen_in_core").as_int(), 42);
}

// 反向斷言 4：卸載後該分頁消失，核心分頁與**其他**套件分頁不受影響。
TEST(E7_16_AppendOnly, UninstallRemovesOnlyThatPageAndLeavesCoreIntact) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.doomed", Value::map({
        {"theme", Value::string("doomed")},
        {"extra", Value::integer(7)},
    }));
    contributions.declare("com.example.survivor", Value::map({{"theme", Value::string("alive")}}));

    SettingsPageRegistry reg(core_settings(), contributions);
    const Value core_before = reg.core_values();
    ASSERT_EQ(reg.package_page_count(), 2u);

    // 卸載（貢獻來源不再列出該套件）。
    EXPECT_TRUE(contributions.withdraw("com.example.doomed"));
    reg.refresh();

    // 該分頁消失。
    EXPECT_FALSE(reg.contains("com.example.doomed"));
    EXPECT_EQ(reg.page("com.example.doomed"), nullptr);
    EXPECT_EQ(reg.package_values("com.example.doomed"), nullptr);
    EXPECT_EQ(reg.package_page_count(), 1u);

    // 核心分頁不受影響。
    EXPECT_EQ(reg.core_values(), core_before);
    EXPECT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "dark");
    EXPECT_TRUE(reg.contains(kCorePageId));

    // 另一個套件分頁也不受影響（卸載不得波及鄰頁）。
    EXPECT_TRUE(reg.contains("com.example.survivor"));
    EXPECT_EQ(value_at(reg, "com.example.survivor", "theme").as_string(), "alive");
}

TEST(E7_16_AppendOnly, UninstallingEveryPackageLeavesExactlyTheCorePage) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.a", Value::map({{"x", Value::integer(1)}}));
    contributions.declare("com.example.b", Value::map({{"y", Value::integer(2)}}));

    SettingsPageRegistry reg(core_settings(), contributions);
    const Value core_before = reg.core_values();
    ASSERT_EQ(reg.size(), 3u);

    contributions.clear();
    reg.refresh();

    EXPECT_EQ(reg.size(), 1u);
    EXPECT_EQ(reg.page_ids(), std::vector<std::string>{std::string(kCorePageId)});
    EXPECT_EQ(reg.core_values(), core_before);
}

// 反向斷言 5：套件試圖佔用核心保留 page id → throw，核心分頁不被取代。
// 兩條路徑都要擋：記憶體替身的 declare（fail fast），以及 registry::refresh
// （任何自訂來源實作都繞不過的那一層）。
TEST(E7_16_AppendOnly, PackageCannotSquatTheReservedCorePageId) {
    MemorySettingsContributions contributions;
    EXPECT_THROW(contributions.declare(kCorePageId, Value::map({{"theme", Value::string("x")}})),
                 std::runtime_error);
    EXPECT_EQ(contributions.size(), 0u);

    // 繞過替身的守門，直接以自訂來源餵給 registry：registry 本身也必須擋。
    CoreIdSquatterSource squatter;
    EXPECT_THROW(SettingsPageRegistry(core_settings(), squatter), std::runtime_error);
}

TEST(E7_16_AppendOnly, FailedRefreshLeavesRegistryUnchanged) {
    // refresh() 中途失敗時不得留下「一半新一半舊」的殘局——先全部建好才整批替換。
    FlakySource source;
    SettingsPageRegistry reg(core_settings(), source);
    ASSERT_EQ(reg.package_page_count(), 1u);
    ASSERT_TRUE(reg.contains("com.example.ok"));
    const Value core_before = reg.core_values();

    // 讓來源壞掉（開始列出重複 id），再 refresh：必須 throw。
    source.break_it();
    EXPECT_THROW(reg.refresh(), std::runtime_error);

    // 登錄維持原狀：既有分頁還在、沒有半建好的 `com.example.dup`、核心分頁不受影響。
    EXPECT_EQ(reg.package_page_count(), 1u) << "失敗的 refresh 留下了殘局";
    EXPECT_TRUE(reg.contains("com.example.ok"));
    EXPECT_FALSE(reg.contains("com.example.dup"));
    EXPECT_EQ(value_at(reg, "com.example.ok", "x").as_int(), 1);
    EXPECT_EQ(reg.core_values(), core_before);
    EXPECT_TRUE(reg.contains(kCorePageId));
}

// 反向斷言 6：編輯互不穿透。
TEST(E7_16_AppendOnly, EditingCorePageDoesNotTouchAnyPackagePage) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.watcher", Value::map({{"theme", Value::string("pkg")}}));
    SettingsPageRegistry reg(core_settings(), contributions);

    const Value pkg_before = *reg.package_values("com.example.watcher");

    reg.apply_page_edit(kCorePageId, "theme", Value::string("light"));

    EXPECT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "light");  // 核心真的改了
    EXPECT_EQ(*reg.package_values("com.example.watcher"), pkg_before);     // 套件頁沒動
    EXPECT_EQ(value_at(reg, "com.example.watcher", "theme").as_string(), "pkg");
}

TEST(E7_16_AppendOnly, EditingPackagePageDoesNotTouchCorePage) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.editable", Value::map({{"theme", Value::string("pkg")}}));
    SettingsPageRegistry reg(core_settings(), contributions);

    const Value core_before = reg.core_values();

    reg.apply_page_edit("com.example.editable", "theme", Value::string("changed"));

    EXPECT_EQ(value_at(reg, "com.example.editable", "theme").as_string(), "changed");
    EXPECT_EQ(reg.core_values(), core_before) << "編輯套件分頁改動了核心設定——分頁未隔離";
    EXPECT_EQ(value_at(reg, kCorePageId, "theme").as_string(), "dark");
}

TEST(E7_16_AppendOnly, PackageRedeclaringItsOwnPageIsAllowedAndStillSpareCore) {
    // 同一套件重新宣告**自己**那一頁是合法的（版本升級 / 重新安裝）；
    // append-only 約束的是「不得改動核心」，不是「不得改自己」。
    MemorySettingsContributions contributions;
    contributions.declare("com.example.v1", Value::map({{"theme", Value::string("v1")}}));
    SettingsPageRegistry reg(core_settings(), contributions);
    const Value core_before = reg.core_values();
    ASSERT_EQ(value_at(reg, "com.example.v1", "theme").as_string(), "v1");

    contributions.declare("com.example.v1", Value::map({{"theme", Value::string("v2")}}));
    reg.refresh();

    EXPECT_EQ(reg.package_page_count(), 1u);
    EXPECT_EQ(value_at(reg, "com.example.v1", "theme").as_string(), "v2");
    EXPECT_EQ(reg.core_values(), core_before);
}

// 反向斷言 7：列舉順序固定 → 衝突無法被排序遮蔽。
TEST(E7_16_AppendOnly, EnumerationOrderIsStableWithCoreAlwaysFirst) {
    MemorySettingsContributions contributions;
    // 刻意以非字典序登錄，證明結果不受登錄順序影響。
    contributions.declare("com.example.zulu", Value::map({{"theme", Value::string("z")}}));
    contributions.declare("com.example.alpha", Value::map({{"theme", Value::string("a")}}));
    contributions.declare("com.example.mike", Value::map({{"theme", Value::string("m")}}));

    SettingsPageRegistry reg(core_settings(), contributions);

    const std::vector<std::string> expected{
        std::string(kCorePageId), "com.example.alpha", "com.example.mike", "com.example.zulu"};
    EXPECT_EQ(reg.page_ids(), expected);

    // pages() 的順序與 page_ids() 一致，且第一個恆為核心分頁。
    const std::vector<SettingsPage> pages = reg.pages();
    ASSERT_EQ(pages.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_EQ(pages[i].id, expected[i]);
    EXPECT_EQ(pages.front().origin, PageOrigin::Core);
}

// =============================================================================
// 不靜默失敗（NFR-04 精神）
// =============================================================================

TEST(E7_16_Errors, SourceListingAnIdItCannotProvideThrows) {
    InconsistentSource bad;
    EXPECT_THROW(SettingsPageRegistry(core_settings(), bad), std::runtime_error);
}

TEST(E7_16_Errors, DuplicateContributionIdThrows) {
    DuplicateIdSource dup;
    EXPECT_THROW(SettingsPageRegistry(core_settings(), dup), std::runtime_error);
}

TEST(E7_16_Errors, NonMapDeclaredSettingsThrows) {
    NonMapSource bad;
    EXPECT_THROW(SettingsPageRegistry(core_settings(), bad), std::runtime_error);

    MemorySettingsContributions contributions;
    EXPECT_THROW(contributions.declare("com.example.scalar", Value::string("not a map")),
                 std::runtime_error);
}

TEST(E7_16_Errors, EmptyContributionIdThrows) {
    EmptyIdSource bad;
    EXPECT_THROW(SettingsPageRegistry(core_settings(), bad), std::runtime_error);

    MemorySettingsContributions contributions;
    EXPECT_THROW(contributions.declare(std::string(), Value::map({})), std::runtime_error);
}

TEST(E7_16_Errors, NonMapCoreSettingsThrows) {
    MemorySettingsContributions contributions;
    EXPECT_THROW(SettingsPageRegistry(Value::string("not a map"), contributions),
                 std::runtime_error);
}

TEST(E7_16_Errors, EditOnUnknownPageOrUnknownFieldThrows) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.known", Value::map({{"x", Value::integer(1)}}));
    SettingsPageRegistry reg(core_settings(), contributions);

    EXPECT_THROW(reg.apply_page_edit("com.example.nope", "x", Value::integer(2)),
                 std::runtime_error);
    EXPECT_THROW(reg.apply_page_edit(kCorePageId, "no_such_field", Value::integer(2)),
                 std::runtime_error);
    EXPECT_THROW(reg.apply_page_edit("com.example.known", "no_such_field", Value::integer(2)),
                 std::runtime_error);
}

TEST(E7_16_Errors, RejectedEditLeavesThePageUnchanged) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);
    const Value before = reg.core_values();

    // 型別不符（對 Integer 欄位給字串）→ E7-14 validate_edit 拒絕。
    EXPECT_THROW(reg.apply_page_edit(kCorePageId, "window.width", Value::string("寬")),
                 std::runtime_error);
    EXPECT_EQ(reg.core_values(), before) << "被拒絕的編輯仍改動了分頁——不得留下半套狀態";
    EXPECT_EQ(value_at(reg, kCorePageId, "window.width").as_int(), 800);
}

TEST(E7_16_Errors, UnknownPageLookupsReturnNullptrNotADefaultPage) {
    MemorySettingsContributions contributions;
    SettingsPageRegistry reg(core_settings(), contributions);

    EXPECT_EQ(reg.page("com.example.absent"), nullptr);
    EXPECT_EQ(reg.package_values("com.example.absent"), nullptr);
    EXPECT_FALSE(reg.contains("com.example.absent"));
    // 核心分頁不得經 package_values 取得（它不屬於任何套件）。
    EXPECT_EQ(reg.package_values(kCorePageId), nullptr);
}

// =============================================================================
// 貢獻來源替身本身的契約
// =============================================================================

TEST(E7_16_MemorySource, DeclareWithdrawAndLexicographicEnumeration) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.c", Value::map({{"v", Value::integer(3)}}));
    contributions.declare("com.example.a", Value::map({{"v", Value::integer(1)}}));
    contributions.declare("com.example.b", Value::map({{"v", Value::integer(2)}}));

    const std::vector<std::string> expected{"com.example.a", "com.example.b", "com.example.c"};
    EXPECT_EQ(contributions.contributing_package_ids(), expected);
    EXPECT_EQ(contributions.size(), 3u);

    EXPECT_TRUE(contributions.withdraw("com.example.b"));
    EXPECT_FALSE(contributions.withdraw("com.example.b"));  // 再卸一次 → false，不拋錯
    EXPECT_EQ(contributions.size(), 2u);

    SettingsContribution out;
    EXPECT_TRUE(contributions.contribution("com.example.a", out));
    EXPECT_EQ(out.package_id, "com.example.a");
    EXPECT_FALSE(contributions.contribution("com.example.b", out));
}

TEST(E7_16_MemorySource, EmptyDeclaredMapYieldsAPageWithNoFields) {
    MemorySettingsContributions contributions;
    contributions.declare("com.example.empty", Value::map({}));
    SettingsPageRegistry reg(core_settings(), contributions);

    const SettingsPage* p = reg.page("com.example.empty");
    ASSERT_NE(p, nullptr);          // 分頁存在（套件確實貢獻了，只是沒有欄位）
    EXPECT_EQ(p->field_count(), 0u);  // E7-14：空容器不產生欄位
}
