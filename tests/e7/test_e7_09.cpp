// E7-09 缺席模組的降級解析 — 契約測試（gtest）
//
// 涵蓋：
//   - 全部模組存在 → 原樣（value 深層相等、無決策、degraded()==false）
//   - 部分缺席 → 降級 + 缺席清單（佔位取代、missing 內容）
//   - 全缺席 → 全部降級為佔位
//   - 巢狀引用：可用外層含缺席內層（降級內層、保留外層）；缺席外層含缺席內層（皆回報）
//   - 缺席決策可見回報（DegradeNote 的 module_id / path / message；佔位節點的 disabled + 原因）
//   - 可注入模組可用性（FnAvailability 任意邏輯；available_from_manifests 由 E9-02 清單建立）
//   - 去重與保序、與 E7-01 parse() 的端到端整合、非模組結構原樣通過
// 平台中立：不含任何平台分支。
#include "fallback.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "document.hpp"
#include "manifest.hpp"

using ds::format::available_from_manifests;
using ds::format::DegradeResult;
using ds::format::FnAvailability;
using ds::format::ModuleSet;
using ds::format::parse;
using ds::format::resolve_with_fallback;
using ds::format::Value;

namespace {

// 建立一個「模組區塊」：Map 含 module: id，外加任意額外成員。
Value module_block(const std::string& id, std::vector<Value::Member> extra = {}) {
    std::vector<Value::Member> m;
    m.emplace_back(std::string("module"), Value::string(id));
    for (auto& e : extra) {
        m.push_back(std::move(e));
    }
    return Value::map(std::move(m));
}

// 於降級結果的 root map 取某鍵。
const Value& child(const DegradeResult& r, const std::string& key) {
    return r.value.at(key);
}

// 判定某 Value 是否為「停用佔位」：Map 含 disabled:true 與非空 degraded_reason。
bool is_disabled_placeholder(const Value& v) {
    if (!v.is_map()) return false;
    const Value* d = v.find("disabled");
    const Value* reason = v.find("degraded_reason");
    return d != nullptr && d->is_bool() && d->as_bool() && reason != nullptr &&
           reason->is_string() && !reason->as_string().empty();
}

// -----------------------------------------------------------------------------
// 1. 全部模組存在 → 原樣
// -----------------------------------------------------------------------------

TEST(ResolveFallback, AllPresentReturnsUnchanged) {
    Value doc = Value::map({
        {"title", Value::string("home")},
        {"clock", module_block("com.example.clock", {{"size", Value::integer(24)}})},
        {"notes", module_block("com.example.notes")},
    });
    ModuleSet avail({"com.example.clock", "com.example.notes"});

    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_FALSE(r.degraded());
    EXPECT_TRUE(r.notes.empty());
    EXPECT_TRUE(r.missing.empty());
    // 值與輸入深層相等（原樣保留）。
    EXPECT_TRUE(r.value == doc);
}

// -----------------------------------------------------------------------------
// 2. 部分缺席 → 降級 + 清單
// -----------------------------------------------------------------------------

TEST(ResolveFallback, PartialAbsentDegradesAndLists) {
    Value doc = Value::map({
        {"clock", module_block("com.example.clock")},
        {"weather", module_block("com.example.weather", {{"units", Value::string("c")}})},
    });
    ModuleSet avail({"com.example.clock"});  // weather 缺席

    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_TRUE(r.degraded());
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "com.example.weather");
    ASSERT_EQ(r.notes.size(), 1u);
    EXPECT_EQ(r.notes[0].module_id, "com.example.weather");

    // clock 原樣、weather 變停用佔位。
    EXPECT_FALSE(is_disabled_placeholder(child(r, "clock")));
    EXPECT_TRUE(is_disabled_placeholder(child(r, "weather")));
    // 佔位仍保留 module id（供上層辨識）。
    EXPECT_EQ(child(r, "weather").at("module").as_string(), "com.example.weather");
    // 缺席區塊的原內容不再出現於值中（已被佔位取代）。
    EXPECT_EQ(child(r, "weather").find("units"), nullptr);
}

// -----------------------------------------------------------------------------
// 3. 全缺席 → 全部降級
// -----------------------------------------------------------------------------

TEST(ResolveFallback, AllAbsentDegradesEach) {
    Value doc = Value::map({
        {"a", module_block("mod.a")},
        {"b", module_block("mod.b")},
    });
    ModuleSet avail;  // 空 → 全缺席

    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_TRUE(r.degraded());
    EXPECT_EQ(r.missing.size(), 2u);
    EXPECT_EQ(r.notes.size(), 2u);
    EXPECT_TRUE(is_disabled_placeholder(child(r, "a")));
    EXPECT_TRUE(is_disabled_placeholder(child(r, "b")));
}

// -----------------------------------------------------------------------------
// 4a. 巢狀引用：可用外層含缺席內層 → 降級內層、保留外層
// -----------------------------------------------------------------------------

TEST(ResolveFallback, NestedAbsentInsidePresentOuter) {
    Value inner = module_block("mod.inner");
    Value outer = module_block("mod.outer", {{"child", inner}});
    Value doc = Value::map({{"panel", outer}});
    ModuleSet avail({"mod.outer"});  // outer 可用、inner 缺席

    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_TRUE(r.degraded());
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "mod.inner");

    // 外層保留（非佔位、module 仍在），內層降級為佔位。
    const Value& panel = child(r, "panel");
    EXPECT_FALSE(is_disabled_placeholder(panel));
    EXPECT_EQ(panel.at("module").as_string(), "mod.outer");
    EXPECT_TRUE(is_disabled_placeholder(panel.at("child")));
    // 決策 path 指向巢狀位置。
    ASSERT_EQ(r.notes.size(), 1u);
    EXPECT_EQ(r.notes[0].path, "root.panel.child");
}

// -----------------------------------------------------------------------------
// 4b. 巢狀引用：缺席外層含缺席內層 → 外層佔位，但內層缺席仍完整回報
// -----------------------------------------------------------------------------

TEST(ResolveFallback, NestedAbsentUnderAbsentOuterStillReported) {
    Value inner = module_block("mod.inner");
    Value outer = module_block("mod.outer", {{"child", inner}});
    Value doc = Value::map({{"panel", outer}});
    ModuleSet avail;  // outer 與 inner 皆缺席

    DegradeResult r = resolve_with_fallback(doc, avail);

    // 兩個缺席模組都要出現在 missing / notes（不靜默）。
    EXPECT_EQ(r.missing.size(), 2u);
    EXPECT_EQ(r.notes.size(), 2u);
    // 外層值為佔位。
    EXPECT_TRUE(is_disabled_placeholder(child(r, "panel")));

    bool saw_outer = false, saw_inner = false;
    for (const auto& id : r.missing) {
        if (id == "mod.outer") saw_outer = true;
        if (id == "mod.inner") saw_inner = true;
    }
    EXPECT_TRUE(saw_outer);
    EXPECT_TRUE(saw_inner);
}

// -----------------------------------------------------------------------------
// 4c. 巢狀引用：清單內含模組區塊，部分缺席
// -----------------------------------------------------------------------------

TEST(ResolveFallback, ListOfModuleBlocksPartialAbsent) {
    Value layers = Value::list({
        module_block("mod.base"),
        module_block("mod.overlay"),
        module_block("mod.base"),  // 重複引用可用模組
    });
    Value doc = Value::map({{"layers", layers}});
    ModuleSet avail({"mod.base"});  // overlay 缺席

    DegradeResult r = resolve_with_fallback(doc, avail);

    ASSERT_EQ(r.missing.size(), 1u);  // 去重：只有 overlay
    EXPECT_EQ(r.missing[0], "mod.overlay");
    ASSERT_EQ(r.notes.size(), 1u);
    EXPECT_EQ(r.notes[0].path, "root.layers[1]");  // 位置定位到索引

    const Value& list = child(r, "layers");
    ASSERT_TRUE(list.is_list());
    ASSERT_EQ(list.size(), 3u);
    EXPECT_FALSE(is_disabled_placeholder(list.as_list()[0]));
    EXPECT_TRUE(is_disabled_placeholder(list.as_list()[1]));
    EXPECT_FALSE(is_disabled_placeholder(list.as_list()[2]));
}

// -----------------------------------------------------------------------------
// 5. 缺席決策可見回報：note 三欄齊備 + 佔位帶原因
// -----------------------------------------------------------------------------

TEST(ResolveFallback, DecisionsAreVisible) {
    Value doc = Value::map({{"w", module_block("mod.missing")}});
    ModuleSet avail;

    DegradeResult r = resolve_with_fallback(doc, avail);

    ASSERT_EQ(r.notes.size(), 1u);
    const auto& note = r.notes[0];
    EXPECT_EQ(note.module_id, "mod.missing");
    EXPECT_EQ(note.path, "root.w");
    EXPECT_NE(note.message.find("mod.missing"), std::string::npos);
    EXPECT_FALSE(note.message.empty());

    // 佔位節點自身即可見地說明降級。
    const Value& ph = child(r, "w");
    ASSERT_TRUE(ph.is_map());
    EXPECT_TRUE(ph.at("disabled").as_bool());
    EXPECT_NE(ph.at("degraded_reason").as_string().find("mod.missing"), std::string::npos);
}

// -----------------------------------------------------------------------------
// 6a. 可注入可用性：FnAvailability 任意邏輯
// -----------------------------------------------------------------------------

TEST(ResolveFallback, InjectableFnAvailability) {
    // 注入邏輯：只有以 "core." 開頭的模組視為存在。
    FnAvailability avail(
        [](const std::string& id) { return id.rfind("core.", 0) == 0; });

    Value doc = Value::map({
        {"a", module_block("core.clock")},   // 存在
        {"b", module_block("third.party")},  // 缺席
    });

    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_FALSE(is_disabled_placeholder(child(r, "a")));
    EXPECT_TRUE(is_disabled_placeholder(child(r, "b")));
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "third.party");
}

// -----------------------------------------------------------------------------
// 6b. 可注入可用性：由 E9-02 manifest 清單建立
// -----------------------------------------------------------------------------

TEST(ResolveFallback, AvailabilityFromManifests) {
    ds::package::Manifest m1;
    m1.name = "com.example.installed";
    ds::package::Manifest m2;
    m2.name = "com.example.other";
    ds::package::Manifest m_empty;  // 空 name → 應被略過

    ModuleSet avail = available_from_manifests({m1, m2, m_empty});
    EXPECT_EQ(avail.size(), 2u);
    EXPECT_TRUE(avail.has("com.example.installed"));
    EXPECT_FALSE(avail.has(""));

    Value doc = Value::map({
        {"x", module_block("com.example.installed")},
        {"y", module_block("com.example.absent")},
    });
    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_FALSE(is_disabled_placeholder(child(r, "x")));
    EXPECT_TRUE(is_disabled_placeholder(child(r, "y")));
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "com.example.absent");
}

// -----------------------------------------------------------------------------
// 7. 非模組結構原樣通過（只有 module 字串鍵的 Map 才算模組區塊）
// -----------------------------------------------------------------------------

TEST(ResolveFallback, NonModuleStructureUntouched) {
    Value doc = Value::map({
        {"width", Value::integer(800)},
        {"tags", Value::list({Value::string("a"), Value::string("b")})},
        // module 鍵的值非字串 → 不視為模組區塊。
        {"weird", Value::map({{"module", Value::integer(7)}})},
    });
    ModuleSet avail;  // 空，但沒有真正的模組引用

    DegradeResult r = resolve_with_fallback(doc, avail);

    EXPECT_FALSE(r.degraded());
    EXPECT_TRUE(r.missing.empty());
    EXPECT_TRUE(r.value == doc);
}

// -----------------------------------------------------------------------------
// 8. 端到端：由 E7-01 parse() 產生的文件降級
// -----------------------------------------------------------------------------

TEST(ResolveFallback, EndToEndFromParsedDocument) {
    const std::string text =
        "format_version: 1.0\n"
        "clock:\n"
        "  module: com.example.clock\n"
        "  size: 24\n"
        "weather:\n"
        "  module: com.example.weather\n"
        "  units: c\n";
    auto pr = parse(text);
    ASSERT_TRUE(pr.ok()) << pr.error().message;

    ModuleSet avail({"com.example.clock"});  // weather 缺席
    DegradeResult r = resolve_with_fallback(pr.document().root, avail);

    EXPECT_TRUE(r.degraded());
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_EQ(r.missing[0], "com.example.weather");
    EXPECT_FALSE(is_disabled_placeholder(child(r, "clock")));
    EXPECT_TRUE(is_disabled_placeholder(child(r, "weather")));
    // clock 的原內容仍在（可用區塊未被動）。
    EXPECT_EQ(child(r, "clock").at("size").as_int(), 24);
}

}  // namespace
