// E1-08 自由拖曳與位置記憶 — 單元測試（gtest）
//
// 驗證「自由拖曳 surface 並記憶其位置」的狀態機與持久化：
//   - 拖曳週期：begin_drag → drag_to → end_drag 提交、放開後記住新位置
//   - 位置記憶經 E7-12 設定值寫回序列化 → E7-01 文字，load_positions 反向還原（round-trip）
//   - 多 surface 各自獨立拖曳 / 記憶
//   - 無效拖曳：未註冊 / 後端無此 surface / 非拖曳中操作 / 重複 begin / 無效 AnchorSpec →
//     結構化回報（Invalid / NotDragging / AlreadyDragging），不靜默、不崩潰
//   - cancel_drag 放棄 pending、還原到拖曳前
//   - live vs remembered：拖曳中 remembered 仍為上次提交、live 為 pending
//   - NFR-02：位置以九宮具名錨點 + 正規化相對偏移表達，無絕對像素座標；resolve_live 隨容器縮放；
//     序列化文字以具名 anchor + 正規化分數承載
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗 / 繪圖 API。
#include "draggable_surface.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using ds::kernel::Anchor;
using ds::kernel::anchor_from_name;
using ds::kernel::anchor_to_name;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::DraggableSurface;
using ds::kernel::DragStatus;
using ds::kernel::NullKernelBackend;
using ds::kernel::Offset;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::SurfaceId;
using ds::kernel::SurfaceProfile;

namespace {

constexpr float kEps = 1e-5f;

// 建一個帶若干具名 surface 的 null 後端（拖曳前提為後端存在該 surface）。
NullKernelBackend make_backend(const std::vector<SurfaceId>& ids) {
    NullKernelBackend backend;
    for (const auto& id : ids) {
        backend.create_surface(id, SurfaceProfile{});
    }
    return backend;
}

AnchorSpec spec_of(Anchor a, float dx = 0.0f, float dy = 0.0f) {
    AnchorSpec s;
    s.anchor = a;
    s.offset = Offset{dx, dy};
    return s;
}

// -----------------------------------------------------------------------------
// 具名錨點 ↔ 名稱（NFR-02：具名角色，非數字）
// -----------------------------------------------------------------------------

TEST(AnchorName, RoundTripAllNineAndInvalid) {
    const Anchor all[] = {
        Anchor::TopLeft,    Anchor::TopCenter,    Anchor::TopRight,
        Anchor::CenterLeft, Anchor::Center,       Anchor::CenterRight,
        Anchor::BottomLeft, Anchor::BottomCenter, Anchor::BottomRight,
    };
    for (const Anchor a : all) {
        const std::string name = anchor_to_name(a);
        EXPECT_FALSE(name.empty());
        Anchor back;
        ASSERT_TRUE(anchor_from_name(name, back));
        EXPECT_EQ(static_cast<int>(back), static_cast<int>(a));
    }
    Anchor untouched = Anchor::Center;
    EXPECT_FALSE(anchor_from_name("nope", untouched));
    EXPECT_EQ(static_cast<int>(untouched), static_cast<int>(Anchor::Center));  // out 未觸碰
    EXPECT_FALSE(anchor_from_name("", untouched));
}

// -----------------------------------------------------------------------------
// 註冊位置：後端閘控 + 無效輸入
// -----------------------------------------------------------------------------

TEST(SetPosition, RequiresRealSurfaceAndValidSpec) {
    NullKernelBackend backend = make_backend({"surface.panel"});
    DraggableSurface drag(backend);

    // 後端無此 surface → Invalid（只登錄真實存在的）。
    EXPECT_EQ(drag.set_position("surface.ghost", spec_of(Anchor::Center)), DragStatus::Invalid);
    EXPECT_FALSE(drag.is_tracked("surface.ghost"));

    // 空 id → Invalid。
    EXPECT_EQ(drag.set_position("", spec_of(Anchor::Center)), DragStatus::Invalid);

    // 越界 anchor（int static_cast）→ Invalid。
    AnchorSpec bad = spec_of(Anchor::Center);
    bad.anchor = static_cast<Anchor>(99);
    EXPECT_EQ(drag.set_position("surface.panel", bad), DragStatus::Invalid);

    // 非有限 offset → Invalid。
    EXPECT_EQ(drag.set_position("surface.panel",
                                spec_of(Anchor::Center, std::numeric_limits<float>::infinity())),
              DragStatus::Invalid);
    EXPECT_FALSE(drag.is_tracked("surface.panel"));

    // 合法 → Ok；就地更新不新增第二筆。
    EXPECT_EQ(drag.set_position("surface.panel", spec_of(Anchor::TopLeft)), DragStatus::Ok);
    EXPECT_TRUE(drag.is_tracked("surface.panel"));
    EXPECT_EQ(drag.tracked_count(), 1u);
    EXPECT_EQ(drag.set_position("surface.panel", spec_of(Anchor::Center)), DragStatus::Ok);
    EXPECT_EQ(drag.tracked_count(), 1u);
    ASSERT_NE(drag.remembered_position("surface.panel"), nullptr);
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.panel")->anchor),
              static_cast<int>(Anchor::Center));
}

// -----------------------------------------------------------------------------
// 拖曳週期：begin → drag_to → end 提交，放開後記住
// -----------------------------------------------------------------------------

TEST(DragCycle, EndCommitsAndRemembersNewPosition) {
    NullKernelBackend backend = make_backend({"surface.pet"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.pet", spec_of(Anchor::Center)), DragStatus::Ok);

    ASSERT_EQ(drag.begin_drag("surface.pet"), DragStatus::Ok);
    EXPECT_TRUE(drag.is_dragging("surface.pet"));
    EXPECT_EQ(drag.dragging_count(), 1u);

    // 拖曳中 remembered 仍為提交前（Center），live 為 pending（起始 = Center）。
    ASSERT_NE(drag.remembered_position("surface.pet"), nullptr);
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.pet")->anchor),
              static_cast<int>(Anchor::Center));

    // 拖到右下角並帶內縮偏移。
    ASSERT_EQ(drag.drag_to("surface.pet", spec_of(Anchor::BottomRight, -0.05f, -0.05f)),
              DragStatus::Ok);
    // 尚未提交：remembered 仍 Center，live 為 BottomRight。
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.pet")->anchor),
              static_cast<int>(Anchor::Center));
    ASSERT_NE(drag.live_position("surface.pet"), nullptr);
    EXPECT_EQ(static_cast<int>(drag.live_position("surface.pet")->anchor),
              static_cast<int>(Anchor::BottomRight));

    // 放開 → 提交、記住新位置。
    ASSERT_EQ(drag.end_drag("surface.pet"), DragStatus::Ok);
    EXPECT_FALSE(drag.is_dragging("surface.pet"));
    EXPECT_EQ(drag.dragging_count(), 0u);
    ASSERT_NE(drag.remembered_position("surface.pet"), nullptr);
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.pet")->anchor),
              static_cast<int>(Anchor::BottomRight));
    EXPECT_NEAR(drag.remembered_position("surface.pet")->offset.dx, -0.05f, kEps);
    EXPECT_NEAR(drag.remembered_position("surface.pet")->offset.dy, -0.05f, kEps);
}

TEST(DragCycle, MultipleDragToUpdatesPending) {
    NullKernelBackend backend = make_backend({"surface.a"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.a", spec_of(Anchor::TopLeft)), DragStatus::Ok);
    ASSERT_EQ(drag.begin_drag("surface.a"), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.a", spec_of(Anchor::Center)), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.a", spec_of(Anchor::BottomLeft)), DragStatus::Ok);
    EXPECT_EQ(static_cast<int>(drag.live_position("surface.a")->anchor),
              static_cast<int>(Anchor::BottomLeft));
    ASSERT_EQ(drag.end_drag("surface.a"), DragStatus::Ok);
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.a")->anchor),
              static_cast<int>(Anchor::BottomLeft));
}

// -----------------------------------------------------------------------------
// cancel：放棄 pending、還原到拖曳前
// -----------------------------------------------------------------------------

TEST(DragCycle, CancelRevertsToPreDragPosition) {
    NullKernelBackend backend = make_backend({"surface.pet"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.pet", spec_of(Anchor::Center)), DragStatus::Ok);
    ASSERT_EQ(drag.begin_drag("surface.pet"), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.pet", spec_of(Anchor::TopRight)), DragStatus::Ok);
    ASSERT_EQ(drag.cancel_drag("surface.pet"), DragStatus::Ok);

    EXPECT_FALSE(drag.is_dragging("surface.pet"));
    // committed 位置不變（仍 Center）；live 亦回 committed。
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.pet")->anchor),
              static_cast<int>(Anchor::Center));
    EXPECT_EQ(static_cast<int>(drag.live_position("surface.pet")->anchor),
              static_cast<int>(Anchor::Center));
}

// -----------------------------------------------------------------------------
// 無效拖曳：結構化回報、不靜默
// -----------------------------------------------------------------------------

TEST(InvalidDrag, StructuredStatusesNotSilent) {
    NullKernelBackend backend = make_backend({"surface.panel"});
    DraggableSurface drag(backend);

    // 未註冊 → begin_drag Invalid。
    EXPECT_EQ(drag.begin_drag("surface.panel"), DragStatus::Invalid);
    EXPECT_EQ(drag.begin_drag(""), DragStatus::Invalid);

    ASSERT_EQ(drag.set_position("surface.panel", spec_of(Anchor::Center)), DragStatus::Ok);

    // 非拖曳中：drag_to / end_drag / cancel_drag → NotDragging。
    EXPECT_EQ(drag.drag_to("surface.panel", spec_of(Anchor::TopLeft)), DragStatus::NotDragging);
    EXPECT_EQ(drag.end_drag("surface.panel"), DragStatus::NotDragging);
    EXPECT_EQ(drag.cancel_drag("surface.panel"), DragStatus::NotDragging);

    // begin 後重複 begin → AlreadyDragging。
    ASSERT_EQ(drag.begin_drag("surface.panel"), DragStatus::Ok);
    EXPECT_EQ(drag.begin_drag("surface.panel"), DragStatus::AlreadyDragging);

    // 拖曳中 set_position 被拒（避免與 pending 競態）。
    EXPECT_EQ(drag.set_position("surface.panel", spec_of(Anchor::TopLeft)), DragStatus::Invalid);

    // 拖曳中 drag_to 無效 spec → Invalid，且 pending 不變。
    EXPECT_EQ(drag.drag_to("surface.panel",
                           spec_of(Anchor::Center, std::numeric_limits<float>::quiet_NaN())),
              DragStatus::Invalid);
    AnchorSpec bad = spec_of(Anchor::Center);
    bad.anchor = static_cast<Anchor>(-1);
    EXPECT_EQ(drag.drag_to("surface.panel", bad), DragStatus::Invalid);
    // pending 仍為起始（Center）。
    EXPECT_EQ(static_cast<int>(drag.live_position("surface.panel")->anchor),
              static_cast<int>(Anchor::Center));
}

TEST(InvalidDrag, BeginAfterBackendSurfaceGone) {
    NullKernelBackend backend = make_backend({"surface.temp"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.temp", spec_of(Anchor::Center)), DragStatus::Ok);
    // 後端銷毀該 surface 後，begin_drag 應以後端閘控回 Invalid。
    ASSERT_TRUE(backend.destroy_surface("surface.temp"));
    EXPECT_EQ(drag.begin_drag("surface.temp"), DragStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 多 surface 各自獨立
// -----------------------------------------------------------------------------

TEST(MultiSurface, IndependentDragAndMemory) {
    NullKernelBackend backend = make_backend({"surface.a", "surface.b"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.a", spec_of(Anchor::TopLeft)), DragStatus::Ok);
    ASSERT_EQ(drag.set_position("surface.b", spec_of(Anchor::TopRight)), DragStatus::Ok);
    EXPECT_EQ(drag.tracked_count(), 2u);

    ASSERT_EQ(drag.begin_drag("surface.a"), DragStatus::Ok);
    ASSERT_EQ(drag.begin_drag("surface.b"), DragStatus::Ok);
    EXPECT_EQ(drag.dragging_count(), 2u);

    ASSERT_EQ(drag.drag_to("surface.a", spec_of(Anchor::BottomLeft)), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.b", spec_of(Anchor::BottomRight)), DragStatus::Ok);

    // 只結束 a：a 提交，b 仍在拖曳、其 committed 未變。
    ASSERT_EQ(drag.end_drag("surface.a"), DragStatus::Ok);
    EXPECT_FALSE(drag.is_dragging("surface.a"));
    EXPECT_TRUE(drag.is_dragging("surface.b"));
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.a")->anchor),
              static_cast<int>(Anchor::BottomLeft));
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.b")->anchor),
              static_cast<int>(Anchor::TopRight));  // b 尚未 end，仍記舊值

    ASSERT_EQ(drag.end_drag("surface.b"), DragStatus::Ok);
    EXPECT_EQ(static_cast<int>(drag.remembered_position("surface.b")->anchor),
              static_cast<int>(Anchor::BottomRight));

    // forget 解除註冊並清拖曳。
    ASSERT_EQ(drag.forget("surface.a"), DragStatus::Ok);
    EXPECT_FALSE(drag.is_tracked("surface.a"));
    EXPECT_EQ(drag.forget("surface.a"), DragStatus::Invalid);  // 再次未知
    EXPECT_EQ(drag.tracked_count(), 1u);
}

// -----------------------------------------------------------------------------
// 位置記憶持久化：經 E7-12 序列化 → 還原（round-trip）
// -----------------------------------------------------------------------------

TEST(Persistence, SerializeThenLoadRestoresPositions) {
    NullKernelBackend backend = make_backend({"surface.panel", "surface.clock"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.panel", spec_of(Anchor::BottomRight, -0.08f, -0.08f)),
              DragStatus::Ok);
    ASSERT_EQ(drag.set_position("surface.clock", spec_of(Anchor::TopCenter, 0.0f, 0.02f)),
              DragStatus::Ok);

    const std::string text = drag.serialize_positions();
    // 序列化文字：帶 format_version、具名 anchor（NFR-02：具名角色 + 正規化分數，非像素）。
    EXPECT_NE(text.find("format_version:"), std::string::npos);
    EXPECT_NE(text.find("bottom-right"), std::string::npos);
    EXPECT_NE(text.find("top-center"), std::string::npos);
    // 含 '.' 的具名 SurfaceId 作為鍵保留（不被字串路徑誤拆）。
    EXPECT_NE(text.find("surface.panel"), std::string::npos);

    // 載入到一個全新的服務（模擬「下次啟動還原」）。
    NullKernelBackend backend2 = make_backend({"surface.panel", "surface.clock"});
    DraggableSurface restored(backend2);
    ASSERT_EQ(restored.load_positions(text), DragStatus::Ok);
    EXPECT_EQ(restored.tracked_count(), 2u);

    ASSERT_NE(restored.remembered_position("surface.panel"), nullptr);
    EXPECT_EQ(static_cast<int>(restored.remembered_position("surface.panel")->anchor),
              static_cast<int>(Anchor::BottomRight));
    EXPECT_NEAR(restored.remembered_position("surface.panel")->offset.dx, -0.08f, kEps);
    EXPECT_NEAR(restored.remembered_position("surface.panel")->offset.dy, -0.08f, kEps);
    EXPECT_EQ(static_cast<int>(restored.remembered_position("surface.clock")->anchor),
              static_cast<int>(Anchor::TopCenter));
    EXPECT_NEAR(restored.remembered_position("surface.clock")->offset.dy, 0.02f, kEps);
}

TEST(Persistence, DragThenPersistCapturesDraggedPosition) {
    NullKernelBackend backend = make_backend({"surface.pet"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.pet", spec_of(Anchor::Center)), DragStatus::Ok);
    ASSERT_EQ(drag.begin_drag("surface.pet"), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.pet", spec_of(Anchor::BottomLeft, 0.03f, -0.03f)),
              DragStatus::Ok);
    ASSERT_EQ(drag.end_drag("surface.pet"), DragStatus::Ok);

    const std::string text = drag.serialize_positions();
    NullKernelBackend backend2 = make_backend({"surface.pet"});
    DraggableSurface restored(backend2);
    ASSERT_EQ(restored.load_positions(text), DragStatus::Ok);
    EXPECT_EQ(static_cast<int>(restored.remembered_position("surface.pet")->anchor),
              static_cast<int>(Anchor::BottomLeft));
    EXPECT_NEAR(restored.remembered_position("surface.pet")->offset.dx, 0.03f, kEps);
}

TEST(Persistence, EmptySerializeRoundTrips) {
    NullKernelBackend backend = make_backend({});
    DraggableSurface drag(backend);
    const std::string text = drag.serialize_positions();
    EXPECT_EQ(drag.load_positions(text), DragStatus::Ok);
    EXPECT_EQ(drag.tracked_count(), 0u);
}

TEST(Persistence, LoadRejectsInvalidAllOrNothing) {
    NullKernelBackend backend = make_backend({"surface.a"});
    DraggableSurface drag(backend);
    ASSERT_EQ(drag.set_position("surface.a", spec_of(Anchor::Center)), DragStatus::Ok);

    // 無法解析的文字（缺 format_version）→ Invalid。
    EXPECT_EQ(drag.load_positions("not a valid document without version"), DragStatus::Invalid);

    // 合法文件但 anchor 名無效 → Invalid，且不套用（既有 surface.a 不被動）。
    const std::string bad_anchor =
        "format_version: 1.0\n"
        "surface.b:\n"
        "  anchor: diagonal\n"
        "  dx: 0.0\n"
        "  dy: 0.0\n";
    EXPECT_EQ(drag.load_positions(bad_anchor), DragStatus::Invalid);
    EXPECT_FALSE(drag.is_tracked("surface.b"));
    EXPECT_EQ(drag.tracked_count(), 1u);  // surface.a 保持

    // 缺欄位（無 dy）→ Invalid。
    const std::string missing_field =
        "format_version: 1.0\n"
        "surface.c:\n"
        "  anchor: center\n"
        "  dx: 0.0\n";
    EXPECT_EQ(drag.load_positions(missing_field), DragStatus::Invalid);
    EXPECT_FALSE(drag.is_tracked("surface.c"));

    // 型別錯（anchor 為數字）→ Invalid。
    const std::string wrong_type =
        "format_version: 1.0\n"
        "surface.d:\n"
        "  anchor: 5\n"
        "  dx: 0.0\n"
        "  dy: 0.0\n";
    EXPECT_EQ(drag.load_positions(wrong_type), DragStatus::Invalid);
    EXPECT_FALSE(drag.is_tracked("surface.d"));
}

// -----------------------------------------------------------------------------
// NFR-02：相對位置隨容器縮放；resolve_live 落地
// -----------------------------------------------------------------------------

TEST(Nfr02, LivePositionResolvesRelativeAndScales) {
    NullKernelBackend backend = make_backend({"surface.panel"});
    DraggableSurface drag(backend);
    // 置中錨點：resolve 後元件應置中，且比例隨容器縮放。
    ASSERT_EQ(drag.set_position("surface.panel", spec_of(Anchor::Center)), DragStatus::Ok);

    ResolvedPlacement small;
    ASSERT_EQ(drag.resolve_live("surface.panel", Size{1000.0f, 800.0f}, Size{100.0f, 100.0f}, small),
              AnchorStatus::Ok);
    EXPECT_NEAR(small.x, (1000.0f - 100.0f) * 0.5f, 1e-3f);
    EXPECT_NEAR(small.y, (800.0f - 100.0f) * 0.5f, 1e-3f);

    ResolvedPlacement big;
    ASSERT_EQ(drag.resolve_live("surface.panel", Size{2000.0f, 1600.0f}, Size{100.0f, 100.0f}, big),
              AnchorStatus::Ok);
    // 同一宣告式規格在更大容器下解析出不同具體像素（比例一致、非硬編座標）。
    EXPECT_NEAR(big.x, (2000.0f - 100.0f) * 0.5f, 1e-3f);
    EXPECT_GT(big.x, small.x);

    // 拖曳中 resolve_live 反映 pending 位置（右下角 → 貼右下）。
    ASSERT_EQ(drag.begin_drag("surface.panel"), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.panel", spec_of(Anchor::BottomRight)), DragStatus::Ok);
    ResolvedPlacement br;
    ASSERT_EQ(drag.resolve_live("surface.panel", Size{1000.0f, 800.0f}, Size{100.0f, 100.0f}, br),
              AnchorStatus::Ok);
    EXPECT_NEAR(br.x, 1000.0f - 100.0f, 1e-3f);
    EXPECT_NEAR(br.y, 800.0f - 100.0f, 1e-3f);

    // 未註冊且未拖曳 → Invalid。
    ResolvedPlacement none;
    EXPECT_EQ(drag.resolve_live("surface.ghost", Size{100.0f, 100.0f}, Size{10.0f, 10.0f}, none),
              AnchorStatus::Invalid);
}

}  // namespace
