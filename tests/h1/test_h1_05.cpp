// H1-05 邊緣吸附 — gtest
//
// 純幾何，不碰任何 Windows API——所以可以把每一條規則直接釘死，不必先有視窗。
//
// 這類「幫使用者調整位置」的功能有個特有風險：**在不該吸的時候吸**。
// widget 明明放在畫面中央卻被吸到邊上，使用者會覺得程式在跟他作對，
// 而且很難描述問題。因此負向案例（不得吸附）與正向案例同樣重要。
#include <gtest/gtest.h>

#include "position_store.hpp"

using ds::host::kDefaultSnapThreshold;
using ds::host::snap_to_work_area_edges;
using ds::host::SnapResult;
using ds::host::WorkArea;

namespace {

// 典型工作區：1920x1040，原點 (0,0)。
WorkArea typical() { return WorkArea{0, 0, 1920, 1040}; }
// 工作列在上 / 左時原點不是 (0,0)——這個情境常被忘記。
WorkArea offset() { return WorkArea{80, 40, 1840, 1000}; }

constexpr int kW = 320;
constexpr int kH = 132;
constexpr int kT = kDefaultSnapThreshold;

}  // namespace

// --- 正向：四條邊各自吸附 -----------------------------------------------------

TEST(EdgeSnap, SnapsToLeftEdge) {
    const auto r = snap_to_work_area_edges(kT - 1, 500, kW, kH, typical(), kT);
    EXPECT_TRUE(r.snapped_x);
    EXPECT_EQ(r.x, 0);
    EXPECT_FALSE(r.snapped_y) << "y 不在門檻內，不該被動到";
    EXPECT_EQ(r.y, 500);
}

TEST(EdgeSnap, SnapsToRightEdgeUsingElementWidth) {
    // 右邊緣靠近工作區右緣 → 貼齊右緣，故 x 應為 1920 - 320。
    const int x = 1920 - kW - (kT - 1);
    const auto r = snap_to_work_area_edges(x, 500, kW, kH, typical(), kT);
    EXPECT_TRUE(r.snapped_x);
    EXPECT_EQ(r.x, 1920 - kW) << "吸的是元件右緣對工作區右緣，不是左上角";
}

TEST(EdgeSnap, SnapsToTopEdge) {
    const auto r = snap_to_work_area_edges(800, kT - 1, kW, kH, typical(), kT);
    EXPECT_TRUE(r.snapped_y);
    EXPECT_EQ(r.y, 0);
    EXPECT_FALSE(r.snapped_x);
}

TEST(EdgeSnap, SnapsToBottomEdgeUsingElementHeight) {
    const int y = 1040 - kH - (kT - 1);
    const auto r = snap_to_work_area_edges(800, y, kW, kH, typical(), kT);
    EXPECT_TRUE(r.snapped_y);
    EXPECT_EQ(r.y, 1040 - kH);
}

// 角落：兩軸同時吸附。
TEST(EdgeSnap, SnapsBothAxesAtCorner) {
    const auto r = snap_to_work_area_edges(kT - 1, kT - 1, kW, kH, typical(), kT);
    EXPECT_TRUE(r.snapped_x);
    EXPECT_TRUE(r.snapped_y);
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
}

// 工作區原點非 (0,0)（工作列在上 / 左）也要正確。
TEST(EdgeSnap, RespectsNonZeroWorkAreaOrigin) {
    const auto area = offset();
    const auto r = snap_to_work_area_edges(area.x + kT - 1, area.y + kT - 1,
                                           kW, kH, area, kT);
    EXPECT_EQ(r.x, area.x) << "應貼齊工作區左緣（80），不是螢幕左緣（0）";
    EXPECT_EQ(r.y, area.y);
}

// --- 負向：不該吸的時候絕不能吸 ----------------------------------------------

TEST(EdgeSnap, DoesNotSnapInTheMiddle) {
    const auto r = snap_to_work_area_edges(800, 500, kW, kH, typical(), kT);
    EXPECT_FALSE(r.snapped_x);
    EXPECT_FALSE(r.snapped_y);
    EXPECT_EQ(r.x, 800);
    EXPECT_EQ(r.y, 500);
}

// 剛好在門檻外一格就不得吸——邊界值是這種功能最容易寫錯的地方。
TEST(EdgeSnap, DoesNotSnapJustOutsideThreshold) {
    const auto r = snap_to_work_area_edges(kT + 1, kT + 1, kW, kH, typical(), kT);
    EXPECT_FALSE(r.snapped_x);
    EXPECT_FALSE(r.snapped_y);
    EXPECT_EQ(r.x, kT + 1);
    EXPECT_EQ(r.y, kT + 1);
}

// 剛好等於門檻要吸（門檻是閉區間）。
TEST(EdgeSnap, SnapsExactlyAtThreshold) {
    const auto r = snap_to_work_area_edges(kT, 500, kW, kH, typical(), kT);
    EXPECT_TRUE(r.snapped_x);
    EXPECT_EQ(r.x, 0);
}

// 門檻 <= 0 = 停用吸附。
TEST(EdgeSnap, ZeroOrNegativeThresholdDisablesSnapping) {
    for (const int t : {0, -1, -100}) {
        const auto r = snap_to_work_area_edges(1, 1, kW, kH, typical(), t);
        EXPECT_FALSE(r.snapped_x) << "threshold=" << t;
        EXPECT_FALSE(r.snapped_y);
        EXPECT_EQ(r.x, 1);
        EXPECT_EQ(r.y, 1);
    }
}

// 退化的工作區不得產生垃圾座標。
TEST(EdgeSnap, DegenerateWorkAreaReturnsInputUnchanged) {
    for (const auto area : {WorkArea{0, 0, 0, 1040}, WorkArea{0, 0, 1920, 0},
                            WorkArea{0, 0, -5, -5}}) {
        const auto r = snap_to_work_area_edges(7, 9, kW, kH, area, kT);
        EXPECT_FALSE(r.snapped_x);
        EXPECT_EQ(r.x, 7);
        EXPECT_EQ(r.y, 9);
    }
}

// 視窗幾乎和工作區一樣寬時，兩端都在門檻內——必須取較近的一端，
// 否則會出現「明明靠左卻被吸到右邊」的跳動。
TEST(EdgeSnap, PicksNearerEdgeWhenBothWithinThreshold) {
    const WorkArea area{0, 0, kW + 10, 1040};  // 只比元件寬 10px
    // 靠左：x=2 → 距左 2、距右 8 → 應吸左
    const auto near_left = snap_to_work_area_edges(2, 500, kW, kH, area, kT);
    EXPECT_EQ(near_left.x, 0);
    // 靠右：x=8 → 距左 8、距右 2 → 應吸右
    const auto near_right = snap_to_work_area_edges(8, 500, kW, kH, area, kT);
    EXPECT_EQ(near_right.x, area.width - kW);
}

// 已經貼齊的視窗再吸一次不得移動（冪等）——否則每次拖曳結束都會抖一下。
TEST(EdgeSnap, SnappingAnAlreadySnappedPositionIsIdempotent) {
    const auto first = snap_to_work_area_edges(3, 3, kW, kH, typical(), kT);
    const auto second = snap_to_work_area_edges(first.x, first.y, kW, kH, typical(), kT);
    EXPECT_EQ(second.x, first.x);
    EXPECT_EQ(second.y, first.y);
}
