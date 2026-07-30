// tests/c3/test_c3_03.cpp — C3-03 角色外觀組（gtest）
//
// 涵蓋：load_appearance_set（E9-01 套件式宣告：典型多外觀 / 混合 kind 內容清單 / 空組 /
// 重複載入 / 套件解析失敗 / 結構驗證失敗）、switch_look（E4-06 具名切換：成功 / 未知
// look）、互動區域（E1-05：set_regions / has_regions / regions_for / current_regions，
// 依外觀個別登記）、E9-01 套件內省（package() / has_package() / manifest / 非 look 項目
// 保留）、apply_to（C1-02：套用互動區域 + 儘力而為表情切換、以及各類前置條件不足時的
// 具名失敗：無目前外觀 / 無互動區域 / 目標 profile 未載入）。
#include "appearance_set.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using ds::content::AppearanceSet;
using ds::content::AppearanceStatus;
using ds::content::ApplyStatus;

using ds::elements::ImageDimensions;
using ds::elements::ImageElement;
using ds::elements::ImageStatus;
using ds::elements::MemoryImageSource;

using ds::events::MouseButton;
using ds::events::RegionEvent;
using ds::events::RouteStatus;

using ds::format::Value;

using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::LocalPoint;
using ds::kernel::make_rect;
using ds::kernel::NamedRegionMap;
using ds::kernel::NullKernelBackend;
using ds::kernel::RegionParams;

using ds::profiles::PortraitProfile;
using ds::profiles::PortraitStatus;

using ds::render::SwitchStatus;

namespace {

// 一份典型的外觀組套件描述：manifest + 三個具名外觀（look）。
const std::string kTypicalDefinition =
    "# miku 的三套外觀\n"
    "format_version: 1.0\n"
    "name: com.example.miku_outfits\n"
    "---\n"
    "look: casual\n"
    "look: swimsuit\n"
    "look: formal\n";

// 混合內容清單：look 與非 look（asset）項目交錯——非 look 項目本單元不解讀，僅保留於 package()。
const std::string kMixedKindDefinition =
    "format_version: 1.0\n"
    "name: com.example.miku_outfits\n"
    "---\n"
    "look: casual\n"
    "asset: icons/casual.png\n"
    "look: formal\n"
    "asset: icons/formal.png\n";

// 僅 manifest、無內容清單：合法的空外觀組。
const std::string kEmptyDefinition = "format_version: 1.0\nname: com.example.empty\n";

}  // namespace

// -----------------------------------------------------------------------------
// load_appearance_set — E9-01 套件式宣告 + E4-06 具名外觀註冊
// -----------------------------------------------------------------------------

TEST(AppearanceSet, LoadTypicalDefinitionRegistersLooks) {
    AppearanceSet set;
    EXPECT_FALSE(set.has_package());
    EXPECT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);

    EXPECT_TRUE(set.has_package());
    EXPECT_EQ(set.look_count(), 3u);
    EXPECT_TRUE(set.has_look("casual"));
    EXPECT_TRUE(set.has_look("swimsuit"));
    EXPECT_TRUE(set.has_look("formal"));
    const std::vector<std::string> names = set.looks();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "casual");
    EXPECT_EQ(names[1], "swimsuit");
    EXPECT_EQ(names[2], "formal");

    EXPECT_FALSE(set.has_current_look());
    EXPECT_TRUE(set.current_look().empty());
}

TEST(AppearanceSet, MixedKindEntriesOnlyRegisterLookKind) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kMixedKindDefinition), AppearanceStatus::Ok);

    EXPECT_EQ(set.look_count(), 2u);
    EXPECT_TRUE(set.has_look("casual"));
    EXPECT_TRUE(set.has_look("formal"));
    EXPECT_FALSE(set.has_look("icons/casual.png"));  // asset 項目不成為外觀
}

TEST(AppearanceSet, EmptyDefinitionIsLegalEmptySet) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kEmptyDefinition), AppearanceStatus::Ok);

    EXPECT_TRUE(set.has_package());
    EXPECT_EQ(set.look_count(), 0u);
    EXPECT_TRUE(set.looks().empty());
    EXPECT_FALSE(set.has_current_look());
}

TEST(AppearanceSet, ReloadWithoutClearRejectedAsAlreadyLoaded) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    EXPECT_EQ(set.load_appearance_set(kEmptyDefinition), AppearanceStatus::AlreadyLoaded);
    // 既有狀態不受影響。
    EXPECT_EQ(set.look_count(), 3u);
}

TEST(AppearanceSet, ClearThenReloadSucceeds) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    ASSERT_EQ(set.switch_look("casual"), SwitchStatus::Ok);
    EXPECT_TRUE(set.clear());
    EXPECT_FALSE(set.has_package());
    EXPECT_EQ(set.look_count(), 0u);
    EXPECT_FALSE(set.has_current_look());

    EXPECT_FALSE(set.clear());  // 已清空：no-op

    EXPECT_EQ(set.load_appearance_set(kEmptyDefinition), AppearanceStatus::Ok);
    EXPECT_EQ(set.look_count(), 0u);
}

TEST(AppearanceSet, MalformedPackageTextRejectedAsInvalid) {
    AppearanceSet set;
    // 無 manifest 內容（空字串）→ E9-01 parse_package 失敗（缺 manifest 區段）。
    EXPECT_EQ(set.load_appearance_set(""), AppearanceStatus::Invalid);
    EXPECT_FALSE(set.has_package());
}

TEST(AppearanceSet, StructurallyInvalidContentRejectedAsInvalid) {
    AppearanceSet set;
    // 內容清單項目缺 ':' 分隔 → E9-01 parse_package 結構失敗。
    const std::string bad =
        "format_version: 1.0\nname: com.example.bad\n---\nlook-without-colon\n";
    EXPECT_EQ(set.load_appearance_set(bad), AppearanceStatus::Invalid);
    EXPECT_FALSE(set.has_package());
}

TEST(AppearanceSet, DuplicateLogicalPathRejectedAsInvalid) {
    AppearanceSet set;
    // E9-01 全域路徑去重（不分 kind）：兩個 look 同名 → parse_package 即失敗。
    const std::string dup =
        "format_version: 1.0\nname: com.example.dup\n---\nlook: casual\nlook: casual\n";
    EXPECT_EQ(set.load_appearance_set(dup), AppearanceStatus::Invalid);
}

// -----------------------------------------------------------------------------
// switch_look — E4-06 具名外觀切換（純委派，保留其結果碼）
// -----------------------------------------------------------------------------

TEST(AppearanceSet, SwitchLookSucceedsAndTracksCurrent) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);

    EXPECT_EQ(set.switch_look("swimsuit"), SwitchStatus::Ok);
    EXPECT_TRUE(set.has_current_look());
    EXPECT_EQ(set.current_look(), "swimsuit");

    EXPECT_EQ(set.switch_look("formal"), SwitchStatus::Ok);
    EXPECT_EQ(set.current_look(), "formal");
}

TEST(AppearanceSet, SwitchLookUnknownNameReturnsNotFound) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);

    EXPECT_EQ(set.switch_look("no-such-look"), SwitchStatus::NotFound);
    EXPECT_FALSE(set.has_current_look());
    EXPECT_TRUE(set.current_look().empty());
}

// -----------------------------------------------------------------------------
// 互動區域 — E1-05 具名區域，依外觀個別登記
// -----------------------------------------------------------------------------

TEST(AppearanceSet, SetAndQueryRegionsPerLook) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);

    NamedRegionMap casual_regions;
    RegionParams body_params;
    body_params["part"] = std::string("body");
    ASSERT_TRUE(casual_regions.add_region("body", make_rect(30.0f, 40.0f), body_params));
    EXPECT_TRUE(set.set_regions("casual", std::move(casual_regions)));

    EXPECT_TRUE(set.has_regions("casual"));
    ASSERT_TRUE(set.regions_for("casual") != nullptr);
    EXPECT_TRUE(set.regions_for("casual")->has_region("body"));

    EXPECT_FALSE(set.has_regions("formal"));  // 尚未登記
    EXPECT_TRUE(set.regions_for("formal") == nullptr);

    // 未知外觀：不新增游離區域。
    NamedRegionMap orphan;
    EXPECT_FALSE(set.set_regions("no-such-look", std::move(orphan)));
    EXPECT_FALSE(set.has_regions("no-such-look"));
}

TEST(AppearanceSet, SetRegionsReplacesExistingForSameLook) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);

    NamedRegionMap first;
    ASSERT_TRUE(first.add_region("a", make_rect(10.0f, 10.0f)));
    ASSERT_TRUE(set.set_regions("casual", std::move(first)));
    EXPECT_EQ(set.regions_for("casual")->region_count(), 1u);

    NamedRegionMap second;
    ASSERT_TRUE(second.add_region("b", make_rect(5.0f, 5.0f)));
    ASSERT_TRUE(second.add_region("c", make_rect(6.0f, 6.0f)));
    ASSERT_TRUE(set.set_regions("casual", std::move(second)));  // 取代,非疊加
    EXPECT_EQ(set.regions_for("casual")->region_count(), 2u);
    EXPECT_FALSE(set.regions_for("casual")->has_region("a"));
}

TEST(AppearanceSet, CurrentRegionsReflectsCurrentLook) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    EXPECT_TRUE(set.current_regions() == nullptr);  // 尚無目前外觀

    NamedRegionMap regions;
    ASSERT_TRUE(regions.add_region("body", make_rect(30.0f, 40.0f)));
    ASSERT_TRUE(set.set_regions("casual", std::move(regions)));
    EXPECT_TRUE(set.current_regions() == nullptr);  // 已登記但尚未切為目前外觀

    ASSERT_EQ(set.switch_look("casual"), SwitchStatus::Ok);
    ASSERT_TRUE(set.current_regions() != nullptr);
    EXPECT_TRUE(set.current_regions()->has_region("body"));

    ASSERT_EQ(set.switch_look("formal"), SwitchStatus::Ok);
    EXPECT_TRUE(set.current_regions() == nullptr);  // formal 尚未登記區域
}

// -----------------------------------------------------------------------------
// E9-01 套件內省
// -----------------------------------------------------------------------------

TEST(AppearanceSet, PackageIntrospectionExposesManifestAndAllEntries) {
    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kMixedKindDefinition), AppearanceStatus::Ok);

    ASSERT_TRUE(set.has_package());
    const auto* pkg = set.package();
    ASSERT_TRUE(pkg != nullptr);
    EXPECT_EQ(pkg->manifest.name, "com.example.miku_outfits");
    EXPECT_EQ(pkg->manifest.format_version.major, 1);
    // 內容清單保留全部項目（含非 look 的 asset），不因本單元只解讀 look 而遺失。
    ASSERT_EQ(pkg->entries.size(), 4u);
    EXPECT_EQ(pkg->entries[1].kind, "asset");
    EXPECT_EQ(pkg->entries[1].logical_path, "icons/casual.png");
}

TEST(AppearanceSet, PackageNullptrWhenNotLoaded) {
    AppearanceSet set;
    EXPECT_TRUE(set.package() == nullptr);
}

// -----------------------------------------------------------------------------
// apply_to — 掛上 C1-02 立繪 profile
// -----------------------------------------------------------------------------

namespace {

// 建構一個已載入、已註冊一個名為 `expr_name` 的具名表情之立繪 profile。
void load_and_add_expression(PortraitProfile& portrait, const std::string& expr_name,
                              const std::string& source, int width, int height) {
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    MemoryImageSource src(source, ImageDimensions{width, height});
    ImageElement image;
    ASSERT_EQ(image.set_source(src), ImageStatus::Ok);
    ASSERT_TRUE(portrait.add_expression(expr_name, image));
}

}  // namespace

TEST(AppearanceSet, ApplyToSwitchesRegionsAndMatchingExpression) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    load_and_add_expression(portrait, "casual", "res://casual", 30, 40);
    EXPECT_TRUE(portrait.current_expression().empty());  // 新增表情不自動切換

    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    ASSERT_EQ(set.switch_look("casual"), SwitchStatus::Ok);

    NamedRegionMap regions;
    RegionParams body_params;
    body_params["part"] = std::string("body");
    ASSERT_TRUE(regions.add_region("body", make_rect(30.0f, 40.0f), body_params));
    ASSERT_TRUE(set.set_regions("casual", std::move(regions)));

    EXPECT_EQ(set.apply_to(portrait), ApplyStatus::Ok);

    // 儘力而為的表情切換：profile 已有同名表情 → 切換成功。
    EXPECT_EQ(portrait.current_expression(), "casual");
    // 互動區域已套用到 profile。
    EXPECT_TRUE(portrait.has_regions());

    // 端到端驗證：注入一次落在 "body" 區域內的點擊,經 C1-02 -> E5-14 -> E1-05 命中該具名區域。
    // inject_click() 於同位置合成 Down + Up + Click 三個事件（E5-01 慣例，同 test_c1_02
    // 慣例），故取最後一筆斷言。
    std::vector<RegionEvent> received;
    portrait.on_region_click([&received](const RegionEvent& e) { received.push_back(e); });
    const RouteStatus route = portrait.inject_click(LocalPoint{10.0f, 10.0f});
    EXPECT_EQ(route, RouteStatus::Hit);
    ASSERT_FALSE(received.empty());
    EXPECT_TRUE(received.back().region_hit);
    EXPECT_EQ(received.back().region_name, "body");
}

TEST(AppearanceSet, ApplyToBestEffortSkipsExpressionSwitchWhenUnmatched) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    // profile 未註冊任何 "swimsuit" 表情。

    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    ASSERT_EQ(set.switch_look("swimsuit"), SwitchStatus::Ok);
    NamedRegionMap regions;
    ASSERT_TRUE(regions.add_region("body", make_rect(10.0f, 10.0f)));
    ASSERT_TRUE(set.set_regions("swimsuit", std::move(regions)));

    EXPECT_EQ(set.apply_to(portrait), ApplyStatus::Ok);  // 區域仍套用成功
    EXPECT_TRUE(portrait.current_expression().empty());  // 表情切換略過(不存在),不整體失敗
    EXPECT_TRUE(portrait.has_regions());
}

TEST(AppearanceSet, ApplyToNoCurrentLookRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    // 從未 switch_look。

    EXPECT_EQ(set.apply_to(portrait), ApplyStatus::NoCurrentLook);
    EXPECT_FALSE(portrait.has_regions());
}

TEST(AppearanceSet, ApplyToNoRegionsForCurrentLookRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    ASSERT_EQ(set.switch_look("casual"), SwitchStatus::Ok);  // 未 set_regions

    EXPECT_EQ(set.apply_to(portrait), ApplyStatus::NoRegions);
    EXPECT_FALSE(portrait.has_regions());
}

TEST(AppearanceSet, ApplyToProfileNotLoadedRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    // 未 load_portrait。

    AppearanceSet set;
    ASSERT_EQ(set.load_appearance_set(kTypicalDefinition), AppearanceStatus::Ok);
    ASSERT_EQ(set.switch_look("casual"), SwitchStatus::Ok);
    NamedRegionMap regions;
    ASSERT_TRUE(regions.add_region("body", make_rect(10.0f, 10.0f)));
    ASSERT_TRUE(set.set_regions("casual", std::move(regions)));

    EXPECT_EQ(set.apply_to(portrait), ApplyStatus::ProfileNotLoaded);
}

// -----------------------------------------------------------------------------
// to_string — NFR-02 具名結果可讀
// -----------------------------------------------------------------------------

TEST(AppearanceSet, ToStringCoversAllEnumerators) {
    EXPECT_STREQ(ds::content::to_string(AppearanceStatus::Ok), "Ok");
    EXPECT_STREQ(ds::content::to_string(AppearanceStatus::Invalid), "Invalid");
    EXPECT_STREQ(ds::content::to_string(AppearanceStatus::AlreadyLoaded), "AlreadyLoaded");

    EXPECT_STREQ(ds::content::to_string(ApplyStatus::Ok), "Ok");
    EXPECT_STREQ(ds::content::to_string(ApplyStatus::NoCurrentLook), "NoCurrentLook");
    EXPECT_STREQ(ds::content::to_string(ApplyStatus::NoRegions), "NoRegions");
    EXPECT_STREQ(ds::content::to_string(ApplyStatus::ProfileNotLoaded), "ProfileNotLoaded");
}
