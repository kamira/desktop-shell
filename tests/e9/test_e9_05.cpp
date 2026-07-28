// E9-05 佈局存檔與還原 — 契約測試（GoogleTest）
//
// 涵蓋：存檔→還原往返一致、多具名 profile、列舉、刪除、查無佈局明確報錯、
// 序列化用 E7-12（產出為 E7-01 可再解析的宣告式格式）、覆寫既有。
#include "layout_store.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ds::format::Value;
using ds::layout::deserialize_layout;
using ds::layout::LayoutElement;
using ds::layout::LayoutState;
using ds::layout::LayoutStore;
using ds::layout::MemoryLayoutStorage;
using ds::layout::serialize_layout;

// 建構一個具代表性的佈局：兩個元件，各帶位置 / 大小 / 設定（以宣告式 properties 承載）。
LayoutState make_sample_layout() {
    LayoutState s;

    LayoutElement clock;
    clock.id = "clock.main";
    clock.type = "clock";
    clock.properties = Value::map({
        {"x", Value::integer(120)},
        {"y", Value::integer(48)},
        {"width", Value::integer(200)},
        {"height", Value::integer(64)},
        {"format24h", Value::boolean(true)},
        {"label", Value::string("主時鐘")},
    });
    s.elements.push_back(clock);

    LayoutElement gauge;
    gauge.id = "cpu.gauge";
    gauge.type = "cpu_gauge";
    gauge.properties = Value::map({
        {"x", Value::number(0.5, /*integral=*/false)},
        {"y", Value::number(0.25, /*integral=*/false)},
        {"opacity", Value::number(0.8, /*integral=*/false)},
        {"anchor", Value::string("top-right")},
    });
    s.elements.push_back(gauge);

    return s;
}

// --- 存檔→還原往返一致 --------------------------------------------------------

TEST(E9_05_RoundTrip, SaveThenLoadPreservesState) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);

    LayoutState original = make_sample_layout();
    store.save("default", original);

    LayoutState restored = store.load("default");
    EXPECT_EQ(restored, original);
    ASSERT_EQ(restored.elements.size(), 2u);
    EXPECT_EQ(restored.elements[0].id, "clock.main");
    EXPECT_EQ(restored.elements[1].type, "cpu_gauge");
}

TEST(E9_05_RoundTrip, IntegralAndFloatPropertiesSurviveRoundTrip) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    store.save("p", make_sample_layout());
    LayoutState restored = store.load("p");

    const Value& clock_props = restored.elements[0].properties;
    ASSERT_TRUE(clock_props.at("width").is_integer());
    EXPECT_EQ(clock_props.at("width").as_int(), 200);

    const Value& gauge_props = restored.elements[1].properties;
    EXPECT_FALSE(gauge_props.at("opacity").is_integer());
    EXPECT_DOUBLE_EQ(gauge_props.at("opacity").as_number(), 0.8);
    EXPECT_EQ(gauge_props.at("anchor").as_string(), "top-right");
}

TEST(E9_05_RoundTrip, EmptyLayoutRoundTrips) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    store.save("empty", LayoutState{});
    LayoutState restored = store.load("empty");
    EXPECT_TRUE(restored.elements.empty());
}

// 直接對 serialize/deserialize 純函式往返（不經後端）。
TEST(E9_05_RoundTrip, FreeFunctionRoundTrip) {
    LayoutState original = make_sample_layout();
    std::string text = serialize_layout(original);
    LayoutState restored = deserialize_layout(text);
    EXPECT_EQ(restored, original);
}

// --- 序列化用 E7-12：產出為 E7-01 可再解析的宣告式格式 --------------------------

TEST(E9_05_Format, SerializedTextIsDeclarativeWithVersionHeader) {
    std::string text = serialize_layout(make_sample_layout());
    // E7-12 serialize 於首行寫入 format_version（E7-01 宣告式格式契約）。
    EXPECT_EQ(text.rfind("format_version: 1.0", 0), 0u)
        << "序列化輸出應以 format_version 首行開頭，實得：\n" << text;
    // 內容為宣告式鍵值，含元件欄位。
    EXPECT_NE(text.find("elements"), std::string::npos);
    EXPECT_NE(text.find("clock.main"), std::string::npos);
    // E7-01 可再解析（round-trip 不變式）：deserialize 不拋錯。
    EXPECT_NO_THROW(deserialize_layout(text));
}

// --- 多具名 profile -----------------------------------------------------------

TEST(E9_05_Profiles, MultipleNamedProfilesAreIndependent) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);

    LayoutState work = make_sample_layout();
    LayoutState gaming;
    LayoutElement fps;
    fps.id = "fps.counter";
    fps.type = "fps";
    fps.properties = Value::map({{"corner", Value::string("bottom-left")}});
    gaming.elements.push_back(fps);

    store.save("work", work);
    store.save("gaming", gaming);

    EXPECT_EQ(store.load("work"), work);
    EXPECT_EQ(store.load("gaming"), gaming);
    EXPECT_NE(store.load("work"), store.load("gaming"));
}

// --- 列舉 ---------------------------------------------------------------------

TEST(E9_05_List, ListEnumeratesAllProfileNames) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    EXPECT_TRUE(store.list().empty());

    store.save("beta", LayoutState{});
    store.save("alpha", LayoutState{});
    store.save("gamma", LayoutState{});

    std::vector<std::string> names = store.list();
    // MemoryLayoutStorage 以字典序列舉（穩定可測）。
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
    EXPECT_EQ(names[2], "gamma");

    EXPECT_TRUE(store.contains("alpha"));
    EXPECT_FALSE(store.contains("missing"));
}

// --- 刪除 ---------------------------------------------------------------------

TEST(E9_05_Remove, RemoveDeletesProfile) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    store.save("temp", make_sample_layout());
    ASSERT_TRUE(store.contains("temp"));

    store.remove("temp");
    EXPECT_FALSE(store.contains("temp"));
    EXPECT_TRUE(store.list().empty());
}

// --- 查無佈局明確報錯（不靜默）------------------------------------------------

TEST(E9_05_Errors, LoadUnknownThrows) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    EXPECT_THROW(store.load("nope"), std::runtime_error);
}

TEST(E9_05_Errors, RemoveUnknownThrows) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    EXPECT_THROW(store.remove("nope"), std::runtime_error);
}

TEST(E9_05_Errors, SaveEmptyNameThrows) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    EXPECT_THROW(store.save("", LayoutState{}), std::runtime_error);
}

TEST(E9_05_Errors, DeserializeMalformedThrows) {
    // 缺 format_version 的文字 → E7-01 解析失敗 → deserialize 拋錯（不靜默）。
    EXPECT_THROW(deserialize_layout("elements:\n  - id: x\n"), std::runtime_error);
}

TEST(E9_05_Errors, DeserializeMissingRequiredFieldThrows) {
    // 元件缺 type 欄位 → 反序列化明確拋錯。
    const std::string text =
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: only_id\n";
    EXPECT_THROW(deserialize_layout(text), std::runtime_error);
}

TEST(E9_05_Errors, DeserializeElementsWrongTypeThrows) {
    // elements 為純量而非清單 → 結構違反 → 拋錯。
    const std::string text =
        "format_version: 1.0\n"
        "elements: not_a_list\n";
    EXPECT_THROW(deserialize_layout(text), std::runtime_error);
}

// --- 覆寫既有 -----------------------------------------------------------------

TEST(E9_05_Overwrite, SaveSameNameOverwrites) {
    MemoryLayoutStorage storage;
    LayoutStore store(storage);

    LayoutState first = make_sample_layout();
    store.save("slot", first);
    ASSERT_EQ(store.load("slot").elements.size(), 2u);

    LayoutState second;
    LayoutElement one;
    one.id = "solo";
    one.type = "widget";
    one.properties = Value::map({{"k", Value::string("v")}});
    second.elements.push_back(one);

    store.save("slot", second);  // 覆寫。

    LayoutState restored = store.load("slot");
    EXPECT_EQ(restored, second);
    ASSERT_EQ(restored.elements.size(), 1u);
    EXPECT_EQ(restored.elements[0].id, "solo");
    // 列舉仍只有一個名稱（覆寫非新增）。
    EXPECT_EQ(store.list().size(), 1u);
}

// --- 建構契約：null 後端拒絕（防呆）------------------------------------------

TEST(E9_05_Contract, NullProperiesTreatedAsEmptyMap) {
    LayoutState s;
    LayoutElement e;
    e.id = "bare";
    e.type = "widget";
    // properties 預設為 Null → 視為空 Map，往返後仍為空 Map。
    s.elements.push_back(e);

    MemoryLayoutStorage storage;
    LayoutStore store(storage);
    store.save("bare", s);
    LayoutState restored = store.load("bare");
    ASSERT_EQ(restored.elements.size(), 1u);
    EXPECT_TRUE(restored.elements[0].properties.is_map());
    EXPECT_EQ(restored.elements[0].properties.size(), 0u);
    EXPECT_EQ(restored, s);
}

}  // namespace
