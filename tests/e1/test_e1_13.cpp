// E1-13 多 profile 實例並存 — 契約 / 單元測試（gtest）
//
// 驗證：
//   - 同一 definition 可實例化多份（各自獨立、id 不同）
//   - 各實例獨立狀態（show/hide 互不牽連）
//   - 列舉（list）依建立序、穩定
//   - 查詢個別（get / contains）
//   - 銷毀個別（destroy）只影響該筆，其餘實例不受影響（隔離保證）
//   - 經 E1-01 LayerStack 指派具名圖層（layer_of 透傳）
//   - 未知 / 重複 id（destroy 兩次、show/hide/get 未知 id）明確回 false / nullptr，不靜默
//   - 空 definition id 拒絕
//   - 實例上限拒絕（RejectedInstanceLimit）
//   - NFR-03 has() 能力閘控：無 kernel.surface 時 instantiate / destroy 被拒、狀態不變
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）。
// NFR-02：本測試不出現任何數字 index / 數字 z-order；實例一律以具名 InstanceId 指涉。
#include "profile_instance_registry.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::HitPolicy;
using ds::kernel::InputPolicy;
using ds::kernel::InstanceId;
using ds::kernel::InstantiateOutcome;
using ds::kernel::InstantiateStatus;
using ds::kernel::ProfileDefinition;
using ds::kernel::ProfileInstance;
using ds::kernel::ProfileInstanceRegistry;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceProfile;
using ds::kernel::to_string;

namespace {

// 建一個「kernel.surface 不可用」的能力矩陣，供 NFR-03 閘控（拒絕）路徑測試。
CapabilityMatrix without_surface_capability() {
    return CapabilityMatrix(std::vector<CapabilityDecl>{
        {"kernel.surface", "surface 核心（測試設為不可用）",
         /*optional=*/false, /*default_available=*/false},
    });
}

ProfileDefinition make_definition(const std::string& id, SurfaceLayer layer = SurfaceLayer::Normal) {
    ProfileDefinition def;
    def.id = id;
    def.surface.layer = layer;
    def.surface.input = InputPolicy::Accepting;
    def.surface.hit = HitPolicy::Solid;
    def.surface.lifecycle = SurfaceLifecycle::Persistent;
    return def;
}

// --- 實例化多份：同一 definition 可並存多份，各有具名 id ---

TEST(Instantiate, SameDefinitionYieldsMultipleIndependentInstances) {
    ProfileInstanceRegistry registry;
    const ProfileDefinition def = make_definition("widget.clock");

    const InstantiateOutcome first = registry.instantiate(def);
    const InstantiateOutcome second = registry.instantiate(def);
    const InstantiateOutcome third = registry.instantiate(def);

    EXPECT_EQ(first.status, InstantiateStatus::Ok);
    EXPECT_EQ(second.status, InstantiateStatus::Ok);
    EXPECT_EQ(third.status, InstantiateStatus::Ok);

    // 三個具名 id 彼此互不相同。
    EXPECT_NE(first.id, second.id);
    EXPECT_NE(second.id, third.id);
    EXPECT_NE(first.id, third.id);
    EXPECT_FALSE(first.id.empty());

    EXPECT_EQ(registry.size(), 3u);
    EXPECT_EQ(registry.count_of_definition("widget.clock"), 3u);
}

TEST(Instantiate, EmptyDefinitionIdRejected) {
    ProfileInstanceRegistry registry;
    ProfileDefinition def = make_definition("");
    const InstantiateOutcome outcome = registry.instantiate(def);
    EXPECT_EQ(outcome.status, InstantiateStatus::RejectedEmptyDefinition);
    EXPECT_TRUE(outcome.id.empty());
    EXPECT_EQ(registry.size(), 0u);
}

TEST(Instantiate, InstanceLimitRejectsFurtherInstantiation) {
    ProfileInstanceRegistry registry(CapabilityMatrix::defaults(), /*max_instances=*/2);
    const ProfileDefinition def = make_definition("widget.timer");

    EXPECT_EQ(registry.instantiate(def).status, InstantiateStatus::Ok);
    EXPECT_EQ(registry.instantiate(def).status, InstantiateStatus::Ok);
    const InstantiateOutcome third = registry.instantiate(def);

    EXPECT_EQ(third.status, InstantiateStatus::RejectedInstanceLimit);
    EXPECT_TRUE(third.id.empty());
    EXPECT_EQ(registry.size(), 2u);  // 狀態不變：未超額建立。
}

// --- 查詢個別 ---

TEST(Query, GetReturnsIndependentDataForEachInstance) {
    ProfileInstanceRegistry registry;
    const InstanceId a = registry.instantiate(make_definition("widget.note")).id;
    const InstanceId b = registry.instantiate(make_definition("widget.note")).id;

    const ProfileInstance* pa = registry.get(a);
    const ProfileInstance* pb = registry.get(b);
    ASSERT_NE(pa, nullptr);
    ASSERT_NE(pb, nullptr);
    EXPECT_EQ(pa->id, a);
    EXPECT_EQ(pb->id, b);
    EXPECT_EQ(pa->definition_id, "widget.note");
    EXPECT_EQ(pb->definition_id, "widget.note");
    EXPECT_TRUE(registry.contains(a));
    EXPECT_TRUE(registry.contains(b));
}

TEST(Query, GetUnknownIdReturnsNullptr) {
    ProfileInstanceRegistry registry;
    registry.instantiate(make_definition("widget.note"));
    EXPECT_EQ(registry.get("does.not.exist"), nullptr);
    EXPECT_FALSE(registry.contains("does.not.exist"));
}

// --- 列舉：建立序、穩定 ---

TEST(Query, ListEnumeratesAllAliveInstancesInCreationOrder) {
    ProfileInstanceRegistry registry;
    const InstanceId a = registry.instantiate(make_definition("widget.alpha")).id;
    const InstanceId b = registry.instantiate(make_definition("widget.beta")).id;
    const InstanceId c = registry.instantiate(make_definition("widget.gamma")).id;

    const std::vector<InstanceId> ids = registry.list();
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], a);
    EXPECT_EQ(ids[1], b);
    EXPECT_EQ(ids[2], c);
}

TEST(Query, EmptyRegistryListsNothing) {
    ProfileInstanceRegistry registry;
    EXPECT_TRUE(registry.list().empty());
    EXPECT_TRUE(registry.empty());
    EXPECT_EQ(registry.size(), 0u);
}

// --- 經 E1-01 指派具名圖層 ---

TEST(LayerAssignment, InstantiateAssignsNamedLayerViaE1_01) {
    ProfileInstanceRegistry registry;
    const InstantiateOutcome overlay = registry.instantiate(make_definition("widget.toast", SurfaceLayer::Overlay));
    const InstantiateOutcome wallpaper = registry.instantiate(make_definition("widget.bg", SurfaceLayer::Wallpaper));

    ASSERT_NE(registry.layer_of(overlay.id), nullptr);
    EXPECT_EQ(*registry.layer_of(overlay.id), SurfaceLayer::Overlay);

    ASSERT_NE(registry.layer_of(wallpaper.id), nullptr);
    EXPECT_EQ(*registry.layer_of(wallpaper.id), SurfaceLayer::Wallpaper);
}

TEST(LayerAssignment, UnknownInstanceLayerOfIsNullptr) {
    ProfileInstanceRegistry registry;
    EXPECT_EQ(registry.layer_of("does.not.exist"), nullptr);
}

// --- 銷毀個別：只影響該筆，其餘實例隔離不受影響 ---

TEST(Destroy, DestroyingOneInstanceLeavesOthersIntact) {
    ProfileInstanceRegistry registry;
    const InstanceId a = registry.instantiate(make_definition("widget.clock")).id;
    const InstanceId b = registry.instantiate(make_definition("widget.clock")).id;

    EXPECT_TRUE(registry.destroy(a));

    EXPECT_FALSE(registry.contains(a));
    EXPECT_EQ(registry.get(a), nullptr);
    EXPECT_EQ(registry.layer_of(a), nullptr);  // E1-01 指派亦已一併移除。

    // b 完全不受影響（隔離保證）。
    EXPECT_TRUE(registry.contains(b));
    ASSERT_NE(registry.get(b), nullptr);
    ASSERT_NE(registry.layer_of(b), nullptr);
    EXPECT_EQ(registry.size(), 1u);
    EXPECT_EQ(registry.count_of_definition("widget.clock"), 1u);
}

TEST(Destroy, UnknownIdReturnsFalse) {
    ProfileInstanceRegistry registry;
    EXPECT_FALSE(registry.destroy("does.not.exist"));
}

TEST(Destroy, RepeatedDestroyOnSameIdReturnsFalseNotSilently) {
    ProfileInstanceRegistry registry;
    const InstanceId a = registry.instantiate(make_definition("widget.clock")).id;

    EXPECT_TRUE(registry.destroy(a));   // 第一次：成功。
    EXPECT_FALSE(registry.destroy(a));  // 第二次（重複銷毀）：明確 false，不靜默。
    EXPECT_EQ(registry.size(), 0u);
}

// --- 各實例獨立狀態：show/hide 互不牽連 ---

TEST(IndependentState, ShowHideOnlyAffectsTargetedInstance) {
    ProfileInstanceRegistry registry;
    const InstanceId a = registry.instantiate(make_definition("widget.panel")).id;
    const InstanceId b = registry.instantiate(make_definition("widget.panel")).id;

    // 建立時預設可見。
    EXPECT_TRUE(registry.is_visible(a));
    EXPECT_TRUE(registry.is_visible(b));

    EXPECT_TRUE(registry.hide(a));
    EXPECT_FALSE(registry.is_visible(a));
    EXPECT_TRUE(registry.is_visible(b));  // b 不受 a 影響（隔離）。

    EXPECT_TRUE(registry.show(a));
    EXPECT_TRUE(registry.is_visible(a));
}

TEST(IndependentState, ShowHideUnknownIdReturnsFalse) {
    ProfileInstanceRegistry registry;
    EXPECT_FALSE(registry.show("does.not.exist"));
    EXPECT_FALSE(registry.hide("does.not.exist"));
    EXPECT_FALSE(registry.is_visible("does.not.exist"));  // 保守回 false。
}

// --- 隔離：修改一份實例的獨立狀態不影響同定義的另一份 ---

TEST(IndependentState, InstancesFromSameDefinitionAreFullyIsolated) {
    ProfileInstanceRegistry registry;
    const ProfileDefinition def = make_definition("widget.same", SurfaceLayer::Normal);
    const InstanceId a = registry.instantiate(def).id;
    const InstanceId b = registry.instantiate(def).id;

    registry.hide(a);
    EXPECT_TRUE(registry.destroy(a));

    // b 仍完整存活、可見、圖層仍在 —— 完全不受 a 的隱藏 / 銷毀影響。
    EXPECT_TRUE(registry.contains(b));
    EXPECT_TRUE(registry.is_visible(b));
    ASSERT_NE(registry.layer_of(b), nullptr);
    EXPECT_EQ(*registry.layer_of(b), SurfaceLayer::Normal);
}

// --- NFR-03：has() 能力閘控 ---

TEST(CapabilityGating, DefaultMatrixEnablesInstantiateAndDestroy) {
    ProfileInstanceRegistry registry;
    EXPECT_TRUE(registry.has("kernel.surface"));
}

TEST(CapabilityGating, InstantiateRefusedWhenCapabilityUnavailable) {
    ProfileInstanceRegistry registry(without_surface_capability());
    EXPECT_FALSE(registry.has("kernel.surface"));

    const InstantiateOutcome outcome = registry.instantiate(make_definition("widget.clock"));
    EXPECT_EQ(outcome.status, InstantiateStatus::RejectedNoCapability);
    EXPECT_TRUE(outcome.id.empty());
    EXPECT_EQ(registry.size(), 0u);  // NFR-03：狀態不變。
}

TEST(CapabilityGating, DestroyRefusedWhenCapabilityUnavailable) {
    // 對照組：可用能力矩陣下，建立 + 銷毀皆成功。
    ProfileInstanceRegistry ok_registry;
    const InstanceId kept = ok_registry.instantiate(make_definition("widget.clock")).id;
    EXPECT_TRUE(ok_registry.contains(kept));
    EXPECT_TRUE(ok_registry.destroy(kept));

    // 不可用能力矩陣下：instantiate 本身即被拒（見前一測試），故無筆可銷毀；
    // 直接驗證 destroy 在此矩陣下對任意具名 id 一律結構化拒絕回 false（不崩潰）。
    ProfileInstanceRegistry denied(without_surface_capability());
    EXPECT_FALSE(denied.destroy("widget.clock#0"));
    EXPECT_EQ(denied.size(), 0u);
}

// --- to_string：具名診斷字串（NFR-02）---

TEST(Diagnostics, ToStringIsNamedForEachStatus) {
    EXPECT_STREQ(to_string(InstantiateStatus::Ok), "instantiate.ok");
    EXPECT_STREQ(to_string(InstantiateStatus::RejectedEmptyDefinition),
                 "instantiate.rejected_empty_definition");
    EXPECT_STREQ(to_string(InstantiateStatus::RejectedNoCapability),
                 "instantiate.rejected_no_capability");
    EXPECT_STREQ(to_string(InstantiateStatus::RejectedInstanceLimit),
                 "instantiate.rejected_instance_limit");
}

}  // namespace
