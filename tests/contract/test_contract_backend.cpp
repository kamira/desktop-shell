// E1-25 kernel 後端契約測試組（所有後端共用）— gtest
//
// 一組**所有 kernel 後端都必須通過**的平台中立行為契約。以 gtest parameterized test
// 對「任一符合 KernelBackend 介面的後端」跑同一批斷言：目前受測後端為契約組自帶的
// 測試本地 StubBackend；未來 E1-24 null 後端 / win32 / cocoa 各加一個 factory 即納入，
// 契約本身不動。
//
// 平台中立：本檔**不含** `sys.platform` / `#ifdef _WIN32` / `__APPLE__` / `win32` /
// `cocoa` 等平台分支（acceptance：測試僅依賴介面與能力矩陣，不得包含任何平台分支）。
// 僅依賴 kernel 後端抽象介面（kernel_backend.hpp）與 E1-21 能力矩陣（capability_matrix.hpp）。
#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "kernel_backend.hpp"
#include "stub_backend.hpp"

using ds::kernel::CapabilityMatrix;
using ds::kernel::contract::KernelBackend;
using ds::kernel::contract::kInvalidSurface;
using ds::kernel::contract::Status;
using ds::kernel::contract::StubBackend;
using ds::kernel::contract::SurfaceHandle;
using ds::kernel::contract::SurfaceProfile;

// 受測後端工廠：每個 case 產生一個全新後端實例，確保 case 間互不污染。
using BackendFactory = std::function<std::unique_ptr<KernelBackend>()>;

// ---- 契約套件：對任一符合介面的後端跑同一批斷言 ----
class KernelBackendContract : public ::testing::TestWithParam<BackendFactory> {
protected:
    void SetUp() override {
        backend = GetParam()();
        ASSERT_NE(backend, nullptr);
    }
    std::unique_ptr<KernelBackend> backend;
};

// 契約 1（能力查詢一致性）：has() 必與 capabilities().has() 完全一致；未宣告能力保守回 false。
TEST_P(KernelBackendContract, CapabilityQueryConsistentWithMatrix) {
    const CapabilityMatrix& m = backend->capabilities();
    for (const auto& decl : m.all()) {
        EXPECT_EQ(backend->has(decl.id), m.has(decl.id)) << "id=" << decl.id;
    }
    // 未宣告能力：NFR-03 保守回不可用。
    EXPECT_FALSE(backend->has("cap.totally.unknown"));
}

// 契約 2（生命週期狀態轉移）：init 前 not ready → init 後 ready → shutdown 後 not ready。
TEST_P(KernelBackendContract, LifecycleReadyStateTransitions) {
    EXPECT_FALSE(backend->is_ready());
    EXPECT_EQ(backend->initialize(), Status::Ok);
    EXPECT_TRUE(backend->is_ready());
    EXPECT_EQ(backend->shutdown(), Status::Ok);
    EXPECT_FALSE(backend->is_ready());
}

// 契約 3（生命週期冪等）：重複 initialize / shutdown 皆安全。
TEST_P(KernelBackendContract, InitializeAndShutdownAreIdempotent) {
    EXPECT_EQ(backend->initialize(), Status::Ok);
    EXPECT_EQ(backend->initialize(), Status::Ok);  // 重複 init 仍 Ok
    EXPECT_TRUE(backend->is_ready());
    EXPECT_EQ(backend->shutdown(), Status::Ok);
    EXPECT_EQ(backend->shutdown(), Status::Ok);  // 未初始化 / 重複 shutdown 安全
    EXPECT_FALSE(backend->is_ready());
}

// 契約 4（surface 前置條件）：未初始化不得建立，回 Invalid 且輸出 handle 必為無效值。
TEST_P(KernelBackendContract, CreateSurfaceRequiresInitialized) {
    SurfaceHandle handle = 0xDEAD;  // 髒值：驗證失敗時後端會覆寫為 kInvalidSurface
    EXPECT_EQ(backend->create_surface(SurfaceProfile{"surface.character"}, handle),
              Status::Invalid);
    EXPECT_EQ(handle, kInvalidSurface);  // 後置條件：失敗時輸出無效
    EXPECT_FALSE(backend->surface_alive(handle));
}

// 契約 5（surface 生命週期）：建立→alive→銷毀→not alive；重複銷毀回 Invalid。
TEST_P(KernelBackendContract, SurfaceCreateDestroyRoundTrip) {
    ASSERT_EQ(backend->initialize(), Status::Ok);
    SurfaceHandle handle = kInvalidSurface;
    ASSERT_EQ(backend->create_surface(SurfaceProfile{"surface.launcher"}, handle), Status::Ok);
    EXPECT_NE(handle, kInvalidSurface);  // 後置條件：成功回有效 handle
    EXPECT_TRUE(backend->surface_alive(handle));
    EXPECT_EQ(backend->destroy_surface(handle), Status::Ok);
    EXPECT_FALSE(backend->surface_alive(handle));
    EXPECT_EQ(backend->destroy_surface(handle), Status::Invalid);  // 重複銷毀不崩潰
}

// 契約 6（無效輸入不崩潰）：空 role 回 Invalid；銷毀無效 handle 回 Invalid。
TEST_P(KernelBackendContract, InvalidInputsRejectedNotCrash) {
    ASSERT_EQ(backend->initialize(), Status::Ok);
    SurfaceHandle handle = kInvalidSurface;
    EXPECT_EQ(backend->create_surface(SurfaceProfile{""}, handle), Status::Invalid);
    EXPECT_EQ(handle, kInvalidSurface);
    EXPECT_EQ(backend->destroy_surface(kInvalidSurface), Status::Invalid);
    EXPECT_FALSE(backend->surface_alive(kInvalidSurface));
}

// 契約 7（錯誤處理 / 能力閘控）：對能力的操作須依 has() 回報，未知能力回 Unsupported。
TEST_P(KernelBackendContract, GatedCapabilityInvocationHonoursHas) {
    ASSERT_EQ(backend->initialize(), Status::Ok);
    for (const auto& decl : backend->capabilities().all()) {
        const Status s = backend->invoke_capability(decl.id);
        if (backend->has(decl.id)) {
            EXPECT_EQ(s, Status::Ok) << "available cap must succeed: " << decl.id;
        } else {
            EXPECT_EQ(s, Status::Unsupported)
                << "unavailable cap must be Unsupported: " << decl.id;
        }
    }
    // 未宣告能力：回 Unsupported（不得崩潰、不得 Ok）。
    EXPECT_EQ(backend->invoke_capability("cap.unknown"), Status::Unsupported);
}

// 契約 8（生命週期後置條件）：shutdown 釋放所有 surface。
TEST_P(KernelBackendContract, ShutdownReleasesSurfaces) {
    ASSERT_EQ(backend->initialize(), Status::Ok);
    SurfaceHandle handle = kInvalidSurface;
    ASSERT_EQ(backend->create_surface(SurfaceProfile{"surface.overlay"}, handle), Status::Ok);
    ASSERT_TRUE(backend->surface_alive(handle));
    ASSERT_EQ(backend->shutdown(), Status::Ok);
    EXPECT_FALSE(backend->surface_alive(handle));  // shutdown 後不得殘留
}

// ---- 受測後端註冊 ----
// 目前只有契約組自帶的測試本地 stub；未來 null / win32 / cocoa 各加一行 factory 即納入，
// 契約斷言不動 —— 這正是「所有後端共用一組契約」的落地方式。
INSTANTIATE_TEST_SUITE_P(
    AllBackends, KernelBackendContract,
    ::testing::Values(BackendFactory(
        [] { return std::unique_ptr<KernelBackend>(new StubBackend()); })));

// ---- 非參數化 sanity：直接以 stub 驗證契約可套用（快速冒煙）----
TEST(KernelBackendContractStub, StubSatisfiesContractDirectly) {
    StubBackend b;
    EXPECT_FALSE(b.is_ready());
    EXPECT_EQ(b.initialize(), Status::Ok);
    SurfaceHandle handle = kInvalidSurface;
    EXPECT_EQ(b.create_surface(SurfaceProfile{"surface.character"}, handle), Status::Ok);
    EXPECT_TRUE(b.surface_alive(handle));
    EXPECT_EQ(b.shutdown(), Status::Ok);
    EXPECT_FALSE(b.surface_alive(handle));  // shutdown 釋放 surface
}
