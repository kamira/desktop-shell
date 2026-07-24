// E11-03 開機自動啟動 — 契約測試（gtest）
//
// 驗證相位 1（Mac / null 期）null 後端語意：
//   - has() == false（宣告不支援）
//   - 未支援時 enable()/disable() 不宣稱成功（回 Unsupported）
//   - is_enabled() 恆為 false，且與 has() 一致
//   - 反覆操作不改變狀態（no-op）
//   - 工廠回傳可用（非空）的後端
// 相位 1：只驗介面 + null 行為，不含任何平台分支。
#include "autostart.hpp"

#include <gtest/gtest.h>

#include <memory>

using ds::host::Autostart;
using ds::host::AutostartStatus;
using ds::host::make_default_autostart;
using ds::host::NullAutostart;

namespace {

// null 後端宣告不支援：has() == false（能力閘控入口，NFR-03）。
TEST(NullAutostart, DoesNotSupportCapability) {
    NullAutostart a;
    EXPECT_FALSE(a.has());
}

// 未支援時 enable() 不得宣稱成功：回 Unsupported（絕不假裝已啟用）。
TEST(NullAutostart, EnableDoesNotClaimSuccess) {
    NullAutostart a;
    EXPECT_EQ(a.enable(), AutostartStatus::Unsupported);
    EXPECT_NE(a.enable(), AutostartStatus::Ok);
}

// 未支援時 disable() 同樣回 Unsupported。
TEST(NullAutostart, DisableReturnsUnsupported) {
    NullAutostart a;
    EXPECT_EQ(a.disable(), AutostartStatus::Unsupported);
    EXPECT_NE(a.disable(), AutostartStatus::Ok);
}

// is_enabled() 恆為 false，且與 has()==false 一致。
TEST(NullAutostart, IsEnabledIsConsistentlyFalse) {
    NullAutostart a;
    EXPECT_FALSE(a.is_enabled());
    EXPECT_FALSE(a.has());
}

// no-op：enable()/disable() 後狀態仍不變（未觸碰任何真實系統設定）。
TEST(NullAutostart, OperationsAreNoOpAndDoNotChangeState) {
    NullAutostart a;
    EXPECT_FALSE(a.is_enabled());
    a.enable();
    EXPECT_FALSE(a.is_enabled());  // 未支援 → enable 不會使其變為已啟用
    a.disable();
    EXPECT_FALSE(a.is_enabled());
}

// 呼叫端閘控樣式（NFR-03 精神）：has()==false 時不進入變更路徑。
TEST(NullAutostart, GatedCallerSkipsMutationWhenUnsupported) {
    NullAutostart a;
    bool attempted_enable = false;
    if (a.has()) {           // 閘控：唯有支援才嘗試變更
        attempted_enable = true;
        a.enable();
    }
    EXPECT_FALSE(attempted_enable);  // null 後端下永不進入變更路徑
    EXPECT_FALSE(a.is_enabled());
}

// 工廠：相位 1 回傳非空且不支援的 null 後端；經由介面呼叫語意一致。
TEST(MakeDefaultAutostart, ReturnsUsableNullBackendInPhase1) {
    std::unique_ptr<Autostart> a = make_default_autostart();
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(a->has());
    EXPECT_EQ(a->enable(), AutostartStatus::Unsupported);
    EXPECT_EQ(a->disable(), AutostartStatus::Unsupported);
    EXPECT_FALSE(a->is_enabled());
}

}  // namespace
