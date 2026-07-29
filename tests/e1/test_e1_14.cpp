// tests/e1/test_e1_14.cpp — E1-14 短暫 profile 生命週期（gtest）
//
// 涵蓋：建立短暫 profile、advance 達 ttl 自動過期+回呼、提早手動 expire、is_alive 狀態、
// 多個並存獨立過期、與 E1-02 輸入策略整合、無效 id / 重複過期處理、remaining、清理後可重建。
#include "transient_profile.hpp"

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ds::events::TimeoutTimer;
using ds::kernel::ExpiryReason;
using ds::kernel::InputStrategy;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceLayer;
using ds::kernel::TransientId;
using ds::kernel::TransientProfile;
using ds::kernel::TransientProfileManager;

// 建一份帶指定輸入策略的短暫 profile（其餘取預設）。
TransientProfile make_profile(InputStrategy input = ds::kernel::kDefaultStrategy) {
    TransientProfile p;
    p.input = input;
    return p;
}

// --- 建立 ---------------------------------------------------------------

TEST(TransientProfile, CreateAliveAndCount) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    EXPECT_EQ(mgr.alive_count(), 0u);

    EXPECT_TRUE(mgr.create("toast.saved", make_profile(), 5));
    EXPECT_TRUE(mgr.is_alive("toast.saved"));
    EXPECT_EQ(mgr.alive_count(), 1u);
}

TEST(TransientProfile, CreateForcesEphemeralLifecycle) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    TransientProfile p = make_profile();
    p.surface.lifecycle = SurfaceLifecycle::Persistent;  // 刻意設常駐；create 應強制改為短暫。
    p.surface.layer = SurfaceLayer::Overlay;

    ASSERT_TRUE(mgr.create("hint.drag", p, 3));
    const TransientProfile* stored = mgr.profile("hint.drag");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->surface.lifecycle, SurfaceLifecycle::Ephemeral);
    EXPECT_EQ(stored->surface.layer, SurfaceLayer::Overlay);  // 其他欄位原樣保留。
}

// --- 逾時自動過期 + 回呼 -------------------------------------------------

TEST(TransientProfile, AdvanceToTtlAutoExpiresWithCallback) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    std::vector<std::pair<TransientId, ExpiryReason>> fired;
    mgr.on_expire([&](const TransientId& id, ExpiryReason r) { fired.emplace_back(id, r); });

    ASSERT_TRUE(mgr.create("toast.a", make_profile(), 4));

    // 未達 ttl：不過期。
    EXPECT_EQ(mgr.advance(3), 0u);
    EXPECT_TRUE(mgr.is_alive("toast.a"));
    EXPECT_TRUE(fired.empty());

    // 跨過 ttl：自動過期一個。
    EXPECT_EQ(mgr.advance(1), 1u);
    EXPECT_FALSE(mgr.is_alive("toast.a"));
    EXPECT_EQ(mgr.alive_count(), 0u);
    ASSERT_EQ(fired.size(), 1u);
    EXPECT_EQ(fired[0].first, "toast.a");
    EXPECT_EQ(fired[0].second, ExpiryReason::Timeout);
}

TEST(TransientProfile, AdvancePastTtlInOneStep) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    ASSERT_TRUE(mgr.create("toast.b", make_profile(), 2));
    // 一次推進遠超 ttl 亦只過期一次（一次性）。
    EXPECT_EQ(mgr.advance(100), 1u);
    EXPECT_FALSE(mgr.is_alive("toast.b"));
}

// --- 手動提早過期 -------------------------------------------------------

TEST(TransientProfile, ManualExpireBeforeTtl) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    std::vector<ExpiryReason> reasons;
    mgr.on_expire([&](const TransientId&, ExpiryReason r) { reasons.push_back(r); });

    ASSERT_TRUE(mgr.create("popup.x", make_profile(), 10));
    EXPECT_TRUE(mgr.is_alive("popup.x"));

    EXPECT_TRUE(mgr.expire("popup.x"));
    EXPECT_FALSE(mgr.is_alive("popup.x"));
    ASSERT_EQ(reasons.size(), 1u);
    EXPECT_EQ(reasons[0], ExpiryReason::Manual);

    // 手動過期後計時器已取消：後續 advance 不再觸發、不再多一次回呼。
    EXPECT_EQ(mgr.advance(100), 0u);
    EXPECT_EQ(reasons.size(), 1u);
}

TEST(TransientProfile, CallbackSeesProfileNotAlive) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    bool alive_in_cb = true;
    mgr.on_expire([&](const TransientId& id, ExpiryReason) { alive_in_cb = mgr.is_alive(id); });

    ASSERT_TRUE(mgr.create("toast.c", make_profile(), 1));
    mgr.advance(1);
    EXPECT_FALSE(alive_in_cb);  // 過期收尾：回呼中已不存活。
}

// --- is_alive / remaining ----------------------------------------------

TEST(TransientProfile, RemainingCountsDown) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    ASSERT_TRUE(mgr.create("toast.d", make_profile(), 5));

    ASSERT_TRUE(mgr.remaining("toast.d").has_value());
    EXPECT_EQ(mgr.remaining("toast.d").value(), 5u);

    mgr.advance(2);
    EXPECT_EQ(mgr.remaining("toast.d").value(), 3u);

    // 未知 / 已過期回 nullopt。
    EXPECT_FALSE(mgr.remaining("nope").has_value());
    mgr.advance(3);
    EXPECT_FALSE(mgr.remaining("toast.d").has_value());
}

// --- 多個並存獨立過期 ---------------------------------------------------

TEST(TransientProfile, MultipleIndependentExpiry) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    std::vector<TransientId> expired;
    mgr.on_expire([&](const TransientId& id, ExpiryReason) { expired.push_back(id); });

    ASSERT_TRUE(mgr.create("a", make_profile(), 2));
    ASSERT_TRUE(mgr.create("b", make_profile(), 5));
    ASSERT_TRUE(mgr.create("c", make_profile(), 2));
    EXPECT_EQ(mgr.alive_count(), 3u);

    // 推進 2：a 與 c 同時逾時，b 仍存活。
    EXPECT_EQ(mgr.advance(2), 2u);
    EXPECT_FALSE(mgr.is_alive("a"));
    EXPECT_FALSE(mgr.is_alive("c"));
    EXPECT_TRUE(mgr.is_alive("b"));
    EXPECT_EQ(mgr.alive_count(), 1u);

    // 再推進 3：b 逾時。
    EXPECT_EQ(mgr.advance(3), 1u);
    EXPECT_FALSE(mgr.is_alive("b"));
    EXPECT_EQ(expired.size(), 3u);
}

TEST(TransientProfile, MixedManualAndTimeout) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    ASSERT_TRUE(mgr.create("m1", make_profile(), 3));
    ASSERT_TRUE(mgr.create("m2", make_profile(), 3));

    EXPECT_TRUE(mgr.expire("m1"));       // m1 手動先走。
    EXPECT_EQ(mgr.alive_count(), 1u);
    EXPECT_EQ(mgr.advance(3), 1u);       // m2 逾時；advance 只回逾時數 = 1。
    EXPECT_EQ(mgr.alive_count(), 0u);
}

// --- 與 E1-02 輸入策略整合 ----------------------------------------------

TEST(TransientProfile, IntegratesInputStrategy) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    // 臨時 modal 彈窗用 Capture；提示用 Inert。
    ASSERT_TRUE(mgr.create("modal.tmp", make_profile(InputStrategy::Capture), 8));
    ASSERT_TRUE(mgr.create("hint.tip", make_profile(InputStrategy::Inert), 8));

    EXPECT_EQ(mgr.input_strategy("modal.tmp"), InputStrategy::Capture);
    EXPECT_EQ(mgr.input_strategy("hint.tip"), InputStrategy::Inert);

    // 可下推回 E1-24 三態（E1-02 對映）。
    EXPECT_EQ(ds::kernel::to_backend_policy(mgr.input_strategy("modal.tmp")),
              ds::kernel::InputPolicy::Modal);
    EXPECT_EQ(ds::kernel::to_backend_policy(mgr.input_strategy("hint.tip")),
              ds::kernel::InputPolicy::Accepting);

    // 未知 id 回預設策略（保守）。
    EXPECT_EQ(mgr.input_strategy("ghost"), ds::kernel::kDefaultStrategy);
}

// --- 無效 id / 重複過期（明確不靜默）-----------------------------------

TEST(TransientProfile, RejectsInvalidCreate) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    EXPECT_FALSE(mgr.create("", make_profile(), 5));        // 空 id。
    EXPECT_FALSE(mgr.create("z", make_profile(), 0));       // ttl == 0。
    EXPECT_EQ(mgr.alive_count(), 0u);

    ASSERT_TRUE(mgr.create("dup", make_profile(), 5));
    EXPECT_FALSE(mgr.create("dup", make_profile(), 9));     // 重複 id：不覆寫。
    EXPECT_EQ(mgr.remaining("dup").value(), 5u);            // 仍為原 ttl。
    EXPECT_EQ(mgr.alive_count(), 1u);
}

TEST(TransientProfile, ExpireUnknownAndDoubleExpireReturnFalse) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);

    EXPECT_FALSE(mgr.expire("nope"));  // 未知 id。

    ASSERT_TRUE(mgr.create("once", make_profile(), 5));
    EXPECT_TRUE(mgr.expire("once"));   // 第一次成功。
    EXPECT_FALSE(mgr.expire("once"));  // 重複過期：明確回 false。
}

TEST(TransientProfile, TimeoutThenManualExpireReturnsFalse) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    ASSERT_TRUE(mgr.create("t", make_profile(), 2));
    mgr.advance(2);                    // 逾時自動過期。
    EXPECT_FALSE(mgr.expire("t"));     // 已過期再手動：回 false，不靜默。
}

// --- 清理後可重建 -------------------------------------------------------

TEST(TransientProfile, RecreateAfterExpiry) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    ASSERT_TRUE(mgr.create("reuse", make_profile(), 2));
    mgr.advance(2);
    EXPECT_FALSE(mgr.is_alive("reuse"));
    // 過期清理後同名可再次建立。
    EXPECT_TRUE(mgr.create("reuse", make_profile(), 3));
    EXPECT_TRUE(mgr.is_alive("reuse"));
    EXPECT_EQ(mgr.remaining("reuse").value(), 3u);
}

TEST(TransientProfile, AdvanceZeroIsNoop) {
    TimeoutTimer timer;
    TransientProfileManager mgr(timer);
    ASSERT_TRUE(mgr.create("s", make_profile(), 3));
    EXPECT_EQ(mgr.advance(0), 0u);
    EXPECT_TRUE(mgr.is_alive("s"));
    EXPECT_EQ(mgr.remaining("s").value(), 3u);
}

TEST(TransientProfile, ReasonToString) {
    EXPECT_STREQ(ds::kernel::to_string(ExpiryReason::Timeout), "timeout");
    EXPECT_STREQ(ds::kernel::to_string(ExpiryReason::Manual), "manual");
}

}  // namespace
