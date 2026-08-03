// E1-25 kernel 後端契約測試組 —— **所有後端都必須通過的行為契約**
//
// 這是 CHG-20260803-10 修復 K-003 後的樣貌。修復前的問題（記錄於此以免重蹈）：
// 契約組原本自帶一個 `ds::kernel::contract::KernelBackend` 介面與一個 `StubBackend`，
// 與真實後端實作的 `ds::kernel::KernelBackend` 是**兩個不相容的型別**
// （數字 handle vs 具名 SurfaceId、Status 三態 vs bool、有無 frame/input）。
// 契約組刻意不 link 任何真實後端以「維持單元獨立」，結果是**它從未驗證過任何真實後端**——
// PHASE-PLAN 寫的「契約測試不改一行拿去跑新後端，這才是相位 1 的投資回收點」，
// 那筆投資根本不在帳上。
//
// 現在契約直接針對**真實介面** `ds::kernel::KernelBackend`（E1-24 宣告）撰寫，
// 由各後端各自註冊一個 factory 納入：
//   - null 後端  → `tests/contract/test_contract_backend.cpp`（所有平台皆建）
//   - win32 後端 → `tests/e1/test_contract_win32.cpp`（僅 Windows 建，見下方「為何不在本目錄」）
//
// **平台中立硬約束**：本檔與同目錄任何檔案**不得**出現平台條件編譯或平台專屬標頭
// （`backend_guard` G1b 會掃前處理器指令行）。契約一旦分平台就不再是契約，是兩套測試。
//
// 為何 win32 的註冊不放在本目錄：本目錄的目標在 ubuntu CI 也要建得起來，
// 而 win32 後端只能在 Windows 建。把註冊放在該後端自己的 `if(WIN32)` 目標下，
// 契約本身（本檔）維持平台中立且一字不動——這正是「同一組契約跑不同後端」的落地方式。
#ifndef DS_CONTRACT_KERNEL_BACKEND_CONTRACT_HPP
#define DS_CONTRACT_KERNEL_BACKEND_CONTRACT_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "null_backend.hpp"  // E1-24：KernelBackend 介面 + 具名型別（上游，可讀不可改）

namespace ds::kernel::contract {

// 受測後端工廠：每個 case 產生一個全新實例，確保 case 間互不污染。
using BackendFactory = std::function<std::unique_ptr<ds::kernel::KernelBackend>()>;

// 契約套件：對任一符合 `ds::kernel::KernelBackend` 的後端跑同一批斷言。
class KernelBackendContract : public ::testing::TestWithParam<BackendFactory> {
protected:
    void SetUp() override {
        backend = GetParam()();
        ASSERT_NE(backend, nullptr);
    }
    void TearDown() override {
        if (backend) backend->shutdown();  // 真實後端會持有 OS 資源，務必收乾淨
    }

    // 契約層一律以**具名** SurfaceId 指涉，永不使用數字 handle（NFR-02）。
    static ds::kernel::SurfaceProfile panel_profile() {
        ds::kernel::SurfaceProfile p;
        p.layer = ds::kernel::SurfaceLayer::Normal;
        p.input = ds::kernel::InputPolicy::Accepting;
        p.hit = ds::kernel::HitPolicy::Solid;
        p.lifecycle = ds::kernel::SurfaceLifecycle::Persistent;
        return p;
    }

    std::unique_ptr<ds::kernel::KernelBackend> backend;
};

// --- 契約 1：後端具名 --------------------------------------------------------
// 每個後端必須回報一個穩定且非空的名稱，供診斷與能力矩陣對照。
TEST_P(KernelBackendContract, ReportsStableNonEmptyName) {
    const std::string a = backend->name();
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(backend->name(), a) << "name() 必須穩定，不得每次呼叫改變";
}

// --- 契約 2：能力查詢一致性（NFR-03）-----------------------------------------
// has() 必與 capabilities().has() 完全一致；未宣告能力保守回 false。
TEST_P(KernelBackendContract, CapabilityQueryConsistentWithMatrix) {
    const ds::kernel::CapabilityMatrix& m = backend->capabilities();
    for (const auto& decl : m.all()) {
        EXPECT_EQ(backend->has(decl.id), m.has(decl.id)) << "id=" << decl.id;
    }
    EXPECT_FALSE(backend->has("cap.totally.unknown"))
        << "未宣告能力必須保守回不可用（NFR-03）";
}

// 能力查詢在未初始化時也不得崩潰——呼叫端可能在 init 前先問「這個後端支援什麼」。
TEST_P(KernelBackendContract, CapabilityQueryIsSafeBeforeInit) {
    EXPECT_FALSE(backend->is_initialized());
    EXPECT_FALSE(backend->has("cap.totally.unknown"));
    EXPECT_GE(backend->capabilities().all().size(), 0u);
}

// --- 契約 3：生命週期狀態轉移 ------------------------------------------------
TEST_P(KernelBackendContract, LifecycleStateTransitions) {
    EXPECT_FALSE(backend->is_initialized());
    EXPECT_TRUE(backend->init());
    EXPECT_TRUE(backend->is_initialized());
    backend->shutdown();
    EXPECT_FALSE(backend->is_initialized());
}

// --- 契約 4：生命週期冪等 ----------------------------------------------------
TEST_P(KernelBackendContract, InitAndShutdownAreIdempotent) {
    EXPECT_TRUE(backend->init());
    EXPECT_TRUE(backend->init()) << "重複 init 仍須回 true 且狀態不變壞";
    EXPECT_TRUE(backend->is_initialized());
    backend->shutdown();
    backend->shutdown();  // 重複 shutdown 不得崩潰
    EXPECT_FALSE(backend->is_initialized());
}

TEST_P(KernelBackendContract, ShutdownBeforeInitIsSafe) {
    backend->shutdown();  // 未初始化就關，不得崩潰
    EXPECT_FALSE(backend->is_initialized());
    EXPECT_EQ(backend->surface_count(), 0u);
}

// --- 「未初始化不得建立 surface」**刻意不在共用契約裡**（見 K-007）-------------
//
// 舊契約（修復前）有這一條，但它測的是自帶 stub，所以從未被真實後端驗證過。
// 契約接上真實後端後**第一次執行就抓到分歧**：
//   - win32 後端：未 init 則 create_surface 回 false（真實後端必須先註冊視窗類別）
//   - null 後端 ：未 init 也能建立成功
//
// 這不是小事：16 個既有單元的測試都在**未初始化的 null 後端上建 surface**，
// 那些程式路徑在任何真實後端上都不可能成立。
//
// 為何不在本次一併對齊：那要改動承重單元 E1-24 的行為，並牽動 16 個單元的測試
// （實測 151 個插入點）。那是獨立的方向決策，不該夾帶在「修契約鴻溝」這張 CHG 裡。
//
// 為了讓分歧**可見且被追蹤**而非默默消失，兩邊各有一條測試釘住現況：
//   - `tests/e1/test_backend_win32.cpp` → `CreateSurfaceRequiresInit`
//   - `tests/e1/test_backend_null.cpp`  → `CreateSurfaceCurrentlyDoesNotRequireInit`
// 對齊之後，這兩條應被刪除、本段落改回一條共用契約。

// --- 契約 6：surface 建立 / 銷毀往返 -----------------------------------------
TEST_P(KernelBackendContract, SurfaceCreateDestroyRoundTrip) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.launcher", panel_profile()));
    EXPECT_TRUE(backend->has_surface("surface.launcher"));
    EXPECT_EQ(backend->surface_count(), 1u);

    EXPECT_TRUE(backend->destroy_surface("surface.launcher"));
    EXPECT_FALSE(backend->has_surface("surface.launcher"));
    EXPECT_EQ(backend->surface_count(), 0u);
    EXPECT_FALSE(backend->destroy_surface("surface.launcher"))
        << "重複銷毀須結構化回 false，不得崩潰";
}

// --- 契約 7：無效輸入一律結構化拒絕，絕不崩潰 --------------------------------
TEST_P(KernelBackendContract, InvalidInputsRejectedNotCrash) {
    ASSERT_TRUE(backend->init());
    EXPECT_FALSE(backend->create_surface("", panel_profile())) << "空 id 必須拒絕";
    EXPECT_EQ(backend->surface_count(), 0u);

    ASSERT_TRUE(backend->create_surface("surface.dup", panel_profile()));
    EXPECT_FALSE(backend->create_surface("surface.dup", panel_profile()))
        << "重複 id 必須拒絕";
    EXPECT_EQ(backend->surface_count(), 1u);
}

// 未知 id 的每一個操作都必須安全。這是「絕不崩潰」承諾的主要戰場。
TEST_P(KernelBackendContract, EveryOperationIsSafeForUnknownId) {
    ASSERT_TRUE(backend->init());
    const ds::kernel::SurfaceId unknown = "surface.does.not.exist";
    EXPECT_FALSE(backend->has_surface(unknown));
    EXPECT_FALSE(backend->show_surface(unknown));
    EXPECT_FALSE(backend->hide_surface(unknown));
    EXPECT_FALSE(backend->is_visible(unknown));
    EXPECT_FALSE(backend->destroy_surface(unknown));
    EXPECT_FALSE(backend->begin_frame(unknown));
    EXPECT_FALSE(backend->end_frame(unknown));
    EXPECT_FALSE(backend->set_input_policy(unknown, ds::kernel::InputPolicy::PassThrough));
    EXPECT_EQ(backend->surface_profile(unknown), nullptr);
}

// --- 契約 8：可見性 ----------------------------------------------------------
// 新建的 surface 預設不可見；show / hide 之後 is_visible 必須跟著改變。
TEST_P(KernelBackendContract, VisibilityFollowsShowAndHide) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.panel", panel_profile()));
    EXPECT_FALSE(backend->is_visible("surface.panel")) << "新建 surface 預設不可見";

    EXPECT_TRUE(backend->show_surface("surface.panel"));
    EXPECT_TRUE(backend->is_visible("surface.panel"));

    EXPECT_TRUE(backend->hide_surface("surface.panel"));
    EXPECT_FALSE(backend->is_visible("surface.panel"));
}

// --- 契約 9：具名 profile 往返 ----------------------------------------------
// 查詢回來的四參數 profile 必須與建立時一致（不得靜默改寫呼叫端的宣告）。
TEST_P(KernelBackendContract, SurfaceProfileRoundTrips) {
    ASSERT_TRUE(backend->init());
    ds::kernel::SurfaceProfile p = panel_profile();
    p.layer = ds::kernel::SurfaceLayer::Topmost;
    p.input = ds::kernel::InputPolicy::PassThrough;
    p.hit = ds::kernel::HitPolicy::Transparent;
    p.lifecycle = ds::kernel::SurfaceLifecycle::Ephemeral;
    ASSERT_TRUE(backend->create_surface("surface.overlay", p));

    const ds::kernel::SurfaceProfile* got = backend->surface_profile("surface.overlay");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->layer, ds::kernel::SurfaceLayer::Topmost);
    EXPECT_EQ(got->input, ds::kernel::InputPolicy::PassThrough);
    EXPECT_EQ(got->hit, ds::kernel::HitPolicy::Transparent);
    EXPECT_EQ(got->lifecycle, ds::kernel::SurfaceLifecycle::Ephemeral);
}

// --- 契約 10：繪製 frame 括號配對 --------------------------------------------
TEST_P(KernelBackendContract, FrameBracketingIsPaired) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.panel", panel_profile()));

    EXPECT_FALSE(backend->end_frame("surface.panel")) << "未 begin 就 end 必須回 false";
    EXPECT_TRUE(backend->begin_frame("surface.panel"));
    EXPECT_FALSE(backend->begin_frame("surface.panel")) << "重複 begin 必須回 false";
    EXPECT_TRUE(backend->end_frame("surface.panel"));
    EXPECT_FALSE(backend->end_frame("surface.panel")) << "重複 end 必須回 false";
}

// --- 契約 11：輸入策略可切換 -------------------------------------------------
// 切換後 surface_profile 必須反映新策略（否則呼叫端無從得知目前狀態）。
TEST_P(KernelBackendContract, InputPolicyIsSwitchableAndReflected) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.panel", panel_profile()));

    EXPECT_TRUE(backend->set_input_policy("surface.panel",
                                          ds::kernel::InputPolicy::PassThrough));
    ASSERT_NE(backend->surface_profile("surface.panel"), nullptr);
    EXPECT_EQ(backend->surface_profile("surface.panel")->input,
              ds::kernel::InputPolicy::PassThrough);

    EXPECT_TRUE(backend->set_input_policy("surface.panel",
                                          ds::kernel::InputPolicy::Accepting));
    EXPECT_EQ(backend->surface_profile("surface.panel")->input,
              ds::kernel::InputPolicy::Accepting);
}

// --- 契約 12：poll_input 安全且不重複交付 ------------------------------------
// 不能對事件「數量」下斷言（真實後端可能收到環境產生的滑鼠移動），
// 但可以斷言**每個事件的目標必為已知具名 surface**——這才是有意義的部分。
TEST_P(KernelBackendContract, PolledEventsCarryKnownNamedTargets) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.panel", panel_profile()));
    for (int i = 0; i < 3; ++i) {
        for (const auto& ev : backend->poll_input()) {
            EXPECT_TRUE(ev.target.empty() || backend->has_surface(ev.target))
                << "事件目標必須是已知具名 surface：" << ev.target;
        }
    }
}

// --- 契約 13：shutdown 釋放所有 surface --------------------------------------
TEST_P(KernelBackendContract, ShutdownReleasesAllSurfaces) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.a", panel_profile()));
    ASSERT_TRUE(backend->create_surface("surface.b", panel_profile()));
    ASSERT_EQ(backend->surface_count(), 2u);

    backend->shutdown();
    EXPECT_EQ(backend->surface_count(), 0u) << "shutdown 後不得殘留 surface";
    EXPECT_FALSE(backend->has_surface("surface.a"));
    EXPECT_FALSE(backend->has_surface("surface.b"));
}

// --- 契約 14：多 surface 各自獨立 --------------------------------------------
// 對一個 surface 的操作不得影響另一個——這是最容易在真實後端寫錯的地方
// （共用狀態、錯用第一個記錄、把 HWND 表寫成單例）。
TEST_P(KernelBackendContract, SurfacesAreIndependent) {
    ASSERT_TRUE(backend->init());
    ASSERT_TRUE(backend->create_surface("surface.a", panel_profile()));
    ASSERT_TRUE(backend->create_surface("surface.b", panel_profile()));

    ASSERT_TRUE(backend->show_surface("surface.a"));
    EXPECT_TRUE(backend->is_visible("surface.a"));
    EXPECT_FALSE(backend->is_visible("surface.b")) << "顯示 a 不得順手顯示 b";

    ASSERT_TRUE(backend->begin_frame("surface.a"));
    EXPECT_TRUE(backend->begin_frame("surface.b")) << "a 在 frame 中不得阻擋 b 開 frame";
    EXPECT_TRUE(backend->end_frame("surface.a"));
    EXPECT_TRUE(backend->end_frame("surface.b"));

    ASSERT_TRUE(backend->destroy_surface("surface.a"));
    EXPECT_TRUE(backend->has_surface("surface.b")) << "銷毀 a 不得順手銷毀 b";
}

}  // namespace ds::kernel::contract

#endif  // DS_CONTRACT_KERNEL_BACKEND_CONTRACT_HPP
