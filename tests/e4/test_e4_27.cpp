// E4-27 綁定式合成（著替） — 單元測試（gtest）
//
// 涵蓋：定義槽位（含各類無效輸入不靜默）、綁定部件（含無效 part）、compose 合成、rebind
// 切換重合成（著替）、多槽疊序（依槽定義順序，非綁定呼叫順序、非數字 z-order）、未綁定
// 槽（明確跳過）、與 E4-08 `LayerCompositor` 的整合（透過其 `SurfaceSwitcher` 定址存在性
// 檢查）、NFR-02（具名清單，不外露數字索引）。全程純記憶體邏輯，不涉及任何真實像素合成 /
// 平台後端。
#include "bound_composite.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "layer_compositor.hpp"
#include "surface_switcher.hpp"

using ds::elements::BindStatus;
using ds::elements::BoundComposite;
using ds::elements::CompositeResult;
using ds::elements::PartOption;
using ds::render::BlendMode;
using ds::render::CompositeStatus;
using ds::render::SurfaceSwitcher;
using ds::render::SwitchStatus;

namespace {

using SurfaceId = ds::kernel::SurfaceId;

// 建一顆已註冊指定具名 surface 的 switcher，供多數測試共用。
SurfaceSwitcher make_switcher_with(const std::vector<SurfaceId>& ids) {
    SurfaceSwitcher sw;
    for (const auto& id : ids) {
        [[maybe_unused]] auto status = sw.register_surface(id);
    }
    return sw;
}

}  // namespace

// --- 定義槽位 -----------------------------------------------------------------

TEST(DefineSlot, RejectsEmptySlotName) {
    SurfaceSwitcher sw = make_switcher_with({"base.default"});
    BoundComposite bc(sw);

    std::vector<PartOption> parts = {{"default", "base.default", BlendMode::Normal, 1.0f}};
    EXPECT_EQ(bc.define_slot("", parts), BindStatus::Invalid);
    EXPECT_EQ(bc.slot_count(), 0u);
}

TEST(DefineSlot, RejectsEmptyPartsList) {
    SurfaceSwitcher sw = make_switcher_with({});
    BoundComposite bc(sw);

    EXPECT_EQ(bc.define_slot("base", {}), BindStatus::Invalid);
    EXPECT_FALSE(bc.has_slot("base"));
}

TEST(DefineSlot, RejectsDuplicateSlotDefinition) {
    SurfaceSwitcher sw = make_switcher_with({"base.a", "base.b"});
    BoundComposite bc(sw);

    std::vector<PartOption> parts_a = {{"a", "base.a", BlendMode::Normal, 1.0f}};
    std::vector<PartOption> parts_b = {{"b", "base.b", BlendMode::Normal, 1.0f}};
    ASSERT_EQ(bc.define_slot("base", parts_a), BindStatus::Ok);
    EXPECT_EQ(bc.define_slot("base", parts_b), BindStatus::Invalid);  // 不覆蓋既有定義
    EXPECT_EQ(bc.slot_count(), 1u);
}

TEST(DefineSlot, RejectsPartWithEmptyPartIdOrSurfaceId) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);

    std::vector<PartOption> bad_part_id = {{"", "outfit.red", BlendMode::Normal, 1.0f}};
    EXPECT_EQ(bc.define_slot("outfit", bad_part_id), BindStatus::Invalid);

    std::vector<PartOption> bad_surface_id = {{"red", "", BlendMode::Normal, 1.0f}};
    EXPECT_EQ(bc.define_slot("outfit", bad_surface_id), BindStatus::Invalid);

    EXPECT_FALSE(bc.has_slot("outfit"));
}

TEST(DefineSlot, RejectsDuplicatePartIdWithinSlot) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red", "outfit.blue"});
    BoundComposite bc(sw);

    std::vector<PartOption> parts = {
        {"dress", "outfit.red", BlendMode::Normal, 1.0f},
        {"dress", "outfit.blue", BlendMode::Normal, 1.0f},  // part_id 重複
    };
    EXPECT_EQ(bc.define_slot("outfit", parts), BindStatus::Invalid);
    EXPECT_FALSE(bc.has_slot("outfit"));
}

TEST(DefineSlot, SucceedsAndSlotOrderReflectsDefinitionOrder) {
    SurfaceSwitcher sw = make_switcher_with({"base.a", "outfit.a", "accessory.a"});
    BoundComposite bc(sw);

    ASSERT_EQ(bc.define_slot("base", {{"a", "base.a", BlendMode::Normal, 1.0f}}), BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("outfit", {{"a", "outfit.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("accessory", {{"a", "accessory.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    std::vector<std::string> expected = {"base", "outfit", "accessory"};
    EXPECT_EQ(bc.slot_order(), expected);
    EXPECT_EQ(bc.slot_count(), 3u);
}

// --- 綁定部件 -----------------------------------------------------------------

TEST(Bind, RejectsUnknownSlot) {
    SurfaceSwitcher sw = make_switcher_with({});
    BoundComposite bc(sw);
    EXPECT_EQ(bc.bind("ghost", "anything"), BindStatus::NotFound);
}

TEST(Bind, RejectsInvalidPartNotInSlotCandidateSet) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    // "blue" 從未在 define_slot 的候選集內宣告過 —— 無效 part。
    EXPECT_EQ(bc.bind("outfit", "blue"), BindStatus::NotFound);
    EXPECT_FALSE(bc.is_bound("outfit"));
}

TEST(Bind, RejectsEmptyPartId) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    EXPECT_EQ(bc.bind("outfit", ""), BindStatus::Invalid);
    EXPECT_FALSE(bc.is_bound("outfit"));
}

TEST(Bind, SucceedsAndUpdatesCurrentPartAndIsBound) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    EXPECT_FALSE(bc.is_bound("outfit"));
    EXPECT_EQ(bc.current_part("outfit"), "");

    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);
    EXPECT_TRUE(bc.is_bound("outfit"));
    EXPECT_EQ(bc.current_part("outfit"), "red");
}

TEST(Bind, OverwritesPreviousBindingOnSameSlot) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red", "outfit.blue"});
    BoundComposite bc(sw);
    std::vector<PartOption> parts = {
        {"red", "outfit.red", BlendMode::Normal, 1.0f},
        {"blue", "outfit.blue", BlendMode::Normal, 1.0f},
    };
    ASSERT_EQ(bc.define_slot("outfit", parts), BindStatus::Ok);

    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "blue"), BindStatus::Ok);  // 直接改綁，同一入口
    EXPECT_EQ(bc.current_part("outfit"), "blue");
}

// --- rebind（著替：切換既有綁定 + 重新合成） ----------------------------------

TEST(Rebind, RejectsWhenSlotNeverBound) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    // 從未 bind 過，沒有舊值可換。
    EXPECT_EQ(bc.rebind("outfit", "red"), BindStatus::NotFound);
    EXPECT_FALSE(bc.is_bound("outfit"));
}

TEST(Rebind, RejectsUnknownSlot) {
    SurfaceSwitcher sw = make_switcher_with({});
    BoundComposite bc(sw);
    EXPECT_EQ(bc.rebind("ghost", "anything"), BindStatus::NotFound);
}

TEST(Rebind, RejectsInvalidPartNotInSlotCandidateSet) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);

    EXPECT_EQ(bc.rebind("outfit", "green"), BindStatus::NotFound);  // 無效 part：維持原綁定
    EXPECT_EQ(bc.current_part("outfit"), "red");
}

TEST(Rebind, SwitchesBoundPartAndRecomposesWithNewSurface) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red", "outfit.blue"});
    BoundComposite bc(sw);
    std::vector<PartOption> parts = {
        {"red", "outfit.red", BlendMode::Normal, 1.0f},
        {"blue", "outfit.blue", BlendMode::Screen, 0.8f},
    };
    ASSERT_EQ(bc.define_slot("outfit", parts), BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);

    CompositeResult before = bc.compose();
    ASSERT_EQ(before.status, CompositeStatus::Ok);
    ASSERT_EQ(before.plan.layers.size(), 1u);
    EXPECT_EQ(before.plan.layers[0].surface_id, "outfit.red");

    // 著替：換成 blue。
    ASSERT_EQ(bc.rebind("outfit", "blue"), BindStatus::Ok);
    EXPECT_EQ(bc.current_part("outfit"), "blue");

    CompositeResult after = bc.compose();
    ASSERT_EQ(after.status, CompositeStatus::Ok);
    ASSERT_EQ(after.plan.layers.size(), 1u);
    EXPECT_EQ(after.plan.layers[0].surface_id, "outfit.blue");
    EXPECT_EQ(after.plan.layers[0].blend_mode, BlendMode::Screen);
    EXPECT_FLOAT_EQ(after.plan.layers[0].opacity, 0.8f);
}

// --- compose 合成 / 多槽疊序（具名，依定義順序） / 未綁定槽 --------------------

TEST(Compose, NoSlotsBoundProducesEmptyPlan) {
    SurfaceSwitcher sw = make_switcher_with({"base.a"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("base", {{"a", "base.a", BlendMode::Normal, 1.0f}}), BindStatus::Ok);

    // 定義了槽但從未綁定 —— compose() 仍成功，只是計畫為空。
    CompositeResult result = bc.compose();
    EXPECT_EQ(result.status, CompositeStatus::Ok);
    EXPECT_TRUE(result.plan.layers.empty());
}

TEST(Compose, UnboundSlotIsSkippedAmongBoundOnes) {
    SurfaceSwitcher sw = make_switcher_with({"base.a", "outfit.a", "accessory.a"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("base", {{"a", "base.a", BlendMode::Normal, 1.0f}}), BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("outfit", {{"a", "outfit.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("accessory", {{"a", "accessory.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    // 只綁定 base 與 accessory，outfit 保持未綁定。
    ASSERT_EQ(bc.bind("base", "a"), BindStatus::Ok);
    ASSERT_EQ(bc.bind("accessory", "a"), BindStatus::Ok);

    CompositeResult result = bc.compose();
    ASSERT_EQ(result.status, CompositeStatus::Ok);
    ASSERT_EQ(result.plan.layers.size(), 2u);
    EXPECT_EQ(result.plan.layers[0].surface_id, "base.a");       // 仍依槽定義順序排在前
    EXPECT_EQ(result.plan.layers[1].surface_id, "accessory.a");  // outfit（未綁定）被跳過
}

TEST(Compose, MultipleSlotsAppearInSlotDefinitionOrderNotBindCallOrder) {
    SurfaceSwitcher sw = make_switcher_with({"base.a", "outfit.a", "accessory.a"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("base", {{"a", "base.a", BlendMode::Normal, 1.0f}}), BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("outfit", {{"a", "outfit.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("accessory", {{"a", "accessory.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    // 刻意以與定義順序相反的順序呼叫 bind。
    ASSERT_EQ(bc.bind("accessory", "a"), BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "a"), BindStatus::Ok);
    ASSERT_EQ(bc.bind("base", "a"), BindStatus::Ok);

    CompositeResult result = bc.compose();
    ASSERT_EQ(result.status, CompositeStatus::Ok);
    ASSERT_EQ(result.plan.layers.size(), 3u);
    // 疊序仍依「槽定義順序」（base -> outfit -> accessory），不受 bind 呼叫順序影響。
    EXPECT_EQ(result.plan.layers[0].surface_id, "base.a");
    EXPECT_EQ(result.plan.layers[1].surface_id, "outfit.a");
    EXPECT_EQ(result.plan.layers[2].surface_id, "accessory.a");
}

TEST(Compose, CarriesBlendModeAndOpacityFromBoundPart) {
    SurfaceSwitcher sw = make_switcher_with({"outfit.red"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Multiply, 0.6f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);

    CompositeResult result = bc.compose();
    ASSERT_EQ(result.status, CompositeStatus::Ok);
    ASSERT_EQ(result.plan.layers.size(), 1u);
    EXPECT_EQ(result.plan.layers[0].blend_mode, BlendMode::Multiply);
    EXPECT_FLOAT_EQ(result.plan.layers[0].opacity, 0.6f);
}

// --- E4-08 整合（透過其 SurfaceSwitcher 定址存在性檢查） ----------------------

TEST(E4_08Integration, UnregisteredSurfaceReturnsNotFoundFromLayerCompositor) {
    // "outfit.red" 從未在 switcher 註冊過 —— define_slot/bind 本身不檢查 surface 定址
    // 存在性（那是 compose() 時交給 E4-08 LayerCompositor::add_layer 做的），因此兩者都
    // 成功；直到 compose() 才會透過 E4-08 -> E4-06 SurfaceSwitcher::has() 發現查無此 surface。
    SurfaceSwitcher sw = make_switcher_with({});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);

    CompositeResult result = bc.compose();
    EXPECT_EQ(result.status, CompositeStatus::NotFound);
    EXPECT_TRUE(result.plan.layers.empty());  // 不回傳部分合成結果
}

TEST(E4_08Integration, RegisteringSurfaceAfterFailedComposeThenAllowsSuccess) {
    SurfaceSwitcher sw = make_switcher_with({});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("outfit", {{"red", "outfit.red", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);
    ASSERT_EQ(bc.bind("outfit", "red"), BindStatus::Ok);

    ASSERT_EQ(bc.compose().status, CompositeStatus::NotFound);

    // 事後才於 E4-06 註冊該 surface（本單元完全不重造這個定址判斷，直接沿用 E4-08 -> E4-06）。
    ASSERT_EQ(sw.register_surface("outfit.red"), SwitchStatus::Ok);

    CompositeResult result = bc.compose();
    EXPECT_EQ(result.status, CompositeStatus::Ok);
    ASSERT_EQ(result.plan.layers.size(), 1u);
    EXPECT_EQ(result.plan.layers[0].surface_id, "outfit.red");
}

// --- NFR-02：具名、無數字槽 / 部件索引外露 -------------------------------------

TEST(NamedAddressing, SlotOrderContainsOnlyNamedIdsNoNumericIndexAccess) {
    SurfaceSwitcher sw = make_switcher_with({"base.a", "outfit.a"});
    BoundComposite bc(sw);
    ASSERT_EQ(bc.define_slot("base", {{"a", "base.a", BlendMode::Normal, 1.0f}}), BindStatus::Ok);
    ASSERT_EQ(bc.define_slot("outfit", {{"a", "outfit.a", BlendMode::Normal, 1.0f}}),
              BindStatus::Ok);

    // slot_order() 元素型別即 SlotId（std::string）；順序反映定義先後，介面本身不提供任何
    // 「以數字取第 N 槽」的存取方式（無 operator[](int) / at(int) 之類 API）。
    for (const auto& id : bc.slot_order()) {
        EXPECT_FALSE(id.empty());
    }
    EXPECT_EQ(bc.slot_order().size(), bc.slot_count());
}
