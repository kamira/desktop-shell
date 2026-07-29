// tests/c1/test_c1_04.cpp — C1-04 OSD 浮層 profile（gtest）
//
// 涵蓋：profile 組裝正確（建構預設、E1-02 輸入策略透傳）、show / update / dismiss 行為、
// E1-14 短暫生命週期整合（逾時自動消失、多個 OSD 共用管理器互不干擾、解構安全）、
// E1-01 具名頂層整合（顯示時指派 Overlay、收起 / 逾時 / 解構時移除、能力不可用時
// show() 回滾）、E1-02 輸入策略（ClickThrough / Interactive 對映後端策略與命中結果）、
// 以及各類無效操作（重複顯示、對未顯示 OSD 更新 / 收起、ttl=0）。
#include "osd_overlay_profile.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::HitResult;
using ds::kernel::InputPolicy;
using ds::kernel::InputStrategy;
using ds::kernel::LayerStack;
using ds::kernel::SurfaceLayer;
using ds::kernel::TransientProfileManager;
using ds::profiles::DismissReason;
using ds::profiles::OsdOverlayProfile;
using ds::profiles::OsdState;

namespace {

class OsdOverlayProfileTest : public ::testing::Test {
protected:
    ds::events::TimeoutTimer timer;
    TransientProfileManager manager{timer};
    LayerStack layers{CapabilityMatrix::defaults()};
};

// -----------------------------------------------------------------------------
// 組裝正確
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, ConstructedHiddenWithDefaults) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    EXPECT_EQ(osd.state(), OsdState::Hidden);
    EXPECT_FALSE(osd.is_showing());
    EXPECT_EQ(osd.id(), "osd.volume");
    EXPECT_TRUE(osd.message().empty());
    EXPECT_EQ(osd.strategy(), InputStrategy::ClickThrough);
    EXPECT_EQ(osd.layer(), SurfaceLayer::Overlay);
    EXPECT_EQ(osd.layer_name(), "layer.overlay");
    EXPECT_FALSE(osd.assigned_to_layer_stack());
}

// -----------------------------------------------------------------------------
// show() — E1-14 短暫生命週期 + E1-01 具名頂層整合
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, ShowTransitionsToShowingAndFiresOnShow) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    bool fired = false;
    std::string seen;
    osd.on_show([&](const std::string& msg) {
        fired = true;
        seen = msg;
    });

    EXPECT_TRUE(osd.show("Volume 60%", 5));
    EXPECT_TRUE(osd.is_showing());
    EXPECT_TRUE(fired);
    EXPECT_EQ(seen, "Volume 60%");
    EXPECT_EQ(osd.message(), "Volume 60%");
    EXPECT_EQ(manager.alive_count(), 1u);

    // E1-01：顯示中應已指派到具名頂層 Overlay。
    EXPECT_TRUE(osd.assigned_to_layer_stack());
    ASSERT_TRUE(layers.contains("osd.volume"));
    ASSERT_NE(layers.layer_of("osd.volume"), nullptr);
    EXPECT_EQ(*layers.layer_of("osd.volume"), SurfaceLayer::Overlay);
}

TEST_F(OsdOverlayProfileTest, ShowWhileAlreadyShowingFails) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    ASSERT_TRUE(osd.show("Volume 60%", 5));
    EXPECT_FALSE(osd.show("Volume 70%", 5));  // 不靜默重顯。
    EXPECT_TRUE(osd.is_showing());
    EXPECT_EQ(osd.message(), "Volume 60%");  // 未被第二次呼叫覆寫。
}

TEST_F(OsdOverlayProfileTest, ShowWithZeroTtlFails) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    EXPECT_FALSE(osd.show("Volume 60%", 0));  // 委派 E1-14：ttl 必須 > 0。
    EXPECT_FALSE(osd.is_showing());
    EXPECT_EQ(manager.alive_count(), 0u);
    EXPECT_FALSE(osd.assigned_to_layer_stack());  // 未曾成功指派。
}

// -----------------------------------------------------------------------------
// update() — 更新內容 + 未顯示閘控
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, UpdateChangesMessageAndFiresOnUpdate) {
    OsdOverlayProfile osd("osd.brightness", manager, layers);
    ASSERT_TRUE(osd.show("Brightness 40%", 5));

    bool fired = false;
    std::string seen;
    osd.on_update([&](const std::string& msg) {
        fired = true;
        seen = msg;
    });

    EXPECT_TRUE(osd.update("Brightness 55%"));
    EXPECT_TRUE(fired);
    EXPECT_EQ(seen, "Brightness 55%");
    EXPECT_EQ(osd.message(), "Brightness 55%");
    EXPECT_TRUE(osd.is_showing());  // update 不改變顯示 / 生命週期狀態。
    EXPECT_EQ(manager.alive_count(), 1u);
}

TEST_F(OsdOverlayProfileTest, UpdateWhileHiddenFails) {
    OsdOverlayProfile osd("osd.brightness", manager, layers);
    EXPECT_FALSE(osd.update("Brightness 55%"));  // 未顯示，no-op，不靜默。
    EXPECT_TRUE(osd.message().empty());
}

// -----------------------------------------------------------------------------
// dismiss() — 手動收起 + 無效操作
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, DismissManualFiresOnDismissManualAndRemovesFromLayerStack) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    ASSERT_TRUE(osd.show("Volume 60%", 5));
    ASSERT_TRUE(layers.contains("osd.volume"));

    DismissReason reason = DismissReason::Timeout;
    bool fired = false;
    osd.on_dismiss([&](DismissReason r) {
        fired = true;
        reason = r;
    });

    EXPECT_TRUE(osd.dismiss());
    EXPECT_TRUE(fired);
    EXPECT_EQ(reason, DismissReason::Manual);
    EXPECT_FALSE(osd.is_showing());
    EXPECT_EQ(manager.alive_count(), 0u);
    EXPECT_FALSE(layers.contains("osd.volume"));  // E1-01：連動移除頂層歸屬。
}

TEST_F(OsdOverlayProfileTest, DismissWhileAlreadyHiddenFails) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    EXPECT_FALSE(osd.dismiss());  // no-op，不靜默。
}

// -----------------------------------------------------------------------------
// E1-14 逾時自動消失 + 多個 OSD 共用管理器 / 圖層堆疊 + 解構安全
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, TimeoutAutoDismissRemovesFromLifecycleAndLayerStack) {
    OsdOverlayProfile osd("osd.volume", manager, layers);
    ASSERT_TRUE(osd.show("Volume 60%", 3));

    int dismiss_count = 0;
    DismissReason reason = DismissReason::Manual;
    osd.on_dismiss([&](DismissReason r) {
        ++dismiss_count;
        reason = r;
    });

    EXPECT_EQ(manager.advance(2), 0u);  // 尚未到期
    EXPECT_TRUE(osd.is_showing());
    EXPECT_TRUE(layers.contains("osd.volume"));

    EXPECT_EQ(manager.advance(1), 1u);  // 跨過 ttl
    EXPECT_FALSE(osd.is_showing());
    EXPECT_EQ(dismiss_count, 1);
    EXPECT_EQ(reason, DismissReason::Timeout);
    EXPECT_FALSE(layers.contains("osd.volume"));  // 逾時亦連動移除頂層歸屬。
}

TEST_F(OsdOverlayProfileTest, MultipleOsdShareManagerAndLayerStackWithoutInterference) {
    OsdOverlayProfile osd_volume("osd.volume", manager, layers);
    OsdOverlayProfile osd_brightness("osd.brightness", manager, layers);

    int volume_dismiss_count = 0;
    int brightness_dismiss_count = 0;
    osd_volume.on_dismiss([&](DismissReason) { ++volume_dismiss_count; });
    osd_brightness.on_dismiss([&](DismissReason) { ++brightness_dismiss_count; });

    ASSERT_TRUE(osd_volume.show("Volume 60%", 3));
    ASSERT_TRUE(osd_brightness.show("Brightness 40%", 10));

    manager.advance(3);  // 只有 osd_volume 逾時；osd_brightness 的過期事件應被過濾掉。
    EXPECT_FALSE(osd_volume.is_showing());
    EXPECT_TRUE(osd_brightness.is_showing());
    EXPECT_EQ(volume_dismiss_count, 1);
    EXPECT_EQ(brightness_dismiss_count, 0);
    EXPECT_FALSE(layers.contains("osd.volume"));
    EXPECT_TRUE(layers.contains("osd.brightness"));

    EXPECT_TRUE(osd_brightness.dismiss());
    EXPECT_EQ(brightness_dismiss_count, 1);
    EXPECT_EQ(volume_dismiss_count, 1);  // osd_volume 的計數不受 osd_brightness 收起影響。
}

TEST_F(OsdOverlayProfileTest, DestructorWhileShowingClosesLifecycleAndLayerEntry) {
    {
        OsdOverlayProfile osd("osd.transient", manager, layers);
        ASSERT_TRUE(osd.show("Copied to clipboard", 5));
        EXPECT_EQ(manager.alive_count(), 1u);
        EXPECT_TRUE(layers.contains("osd.transient"));
    }  // 解構時 OSD 仍「顯示中」；dtor 應強制 dismiss()，避免懸置計時器 / 圖層條目。
    EXPECT_EQ(manager.alive_count(), 0u);
    EXPECT_FALSE(layers.contains("osd.transient"));
    EXPECT_EQ(manager.advance(100), 0u);  // 不應再有殘留過期觸發，亦不崩潰。
}

// -----------------------------------------------------------------------------
// E1-02 輸入策略整合
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, InputStrategyClickThroughMapsToPassThroughAndTransparent) {
    OsdOverlayProfile osd("osd.volume", manager, layers);  // 預設 ClickThrough。
    EXPECT_EQ(osd.backend_input_policy(), InputPolicy::PassThrough);
    EXPECT_EQ(osd.hit_result(), HitResult::Transparent);
}

TEST_F(OsdOverlayProfileTest, InputStrategyInteractiveMapsToAcceptingAndSolid) {
    OsdOverlayProfile osd("osd.dialog_like", manager, layers, InputStrategy::Interactive);
    EXPECT_EQ(osd.strategy(), InputStrategy::Interactive);
    EXPECT_EQ(osd.backend_input_policy(), InputPolicy::Accepting);
    EXPECT_EQ(osd.hit_result(), HitResult::Solid);
}

// -----------------------------------------------------------------------------
// E1-01 NFR-03 能力閘控 — show() 於圖層能力不可用時回滾
// -----------------------------------------------------------------------------

TEST_F(OsdOverlayProfileTest, ShowRollsBackWhenLayerCapabilityUnavailable) {
    // 建構一個**未宣告** "kernel.surface" 能力的圖層堆疊：CapabilityMatrix::has() 對未知
    // 能力一律回 false（保守），故 layers.assign() 會回 RejectedNoCapability。
    LayerStack ungated_layers{CapabilityMatrix(std::vector<CapabilityDecl>{})};
    OsdOverlayProfile osd("osd.volume", manager, ungated_layers);

    EXPECT_FALSE(osd.show("Volume 60%", 5));
    EXPECT_FALSE(osd.is_showing());
    EXPECT_TRUE(osd.message().empty());
    // 回滾：E1-14 短暫 profile 條目不應殘留。
    EXPECT_EQ(manager.alive_count(), 0u);
    EXPECT_FALSE(ungated_layers.contains("osd.volume"));
}

}  // namespace
