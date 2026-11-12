// H1-06 把 E4-18（徑向線 / 指針）接進繪製層 — gtest
//
// 這份測試證明的是驗收標準裡最硬的那一句：「**新增一個元件型別**後，不改 src/ 與 engine/
// 任何一行即可被畫出」。這裡用的不是測試自己捏的假元件（那條由 test_h1_06.cpp 的
// PlatformPromise 一組負責），而是一個**真實的上游元件型別**：E4-18 的
// `RadialPointerElement`，它有自己的 `RenderModel`、分派核心原本不認得它。
// 接上它只用到兩個東西：一個投影函式（`project_radial`）+ 一次 `register_kind()`。
//
// **為什麼本測試是獨立的執行檔**：`ds::elements::RenderModel` 在 E4-03（bar_gauge.hpp）
// 與 E4-18（radial_pointer.hpp）是**同名不同型別**的兩個 struct。同一個 TU 同時 include
// 兩者會重定義；同一個程式連入兩份不同定義是 ODR 違反（UB，警告旗標與 sanitizer 都看不到
// ——K-013 那一族「綠燈不代表正確」的形狀）。故本檔**不 include** widget_render.hpp /
// system_status_widget.hpp，且 target 不與 h1_06_paint_model 連在一起。
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "radial_draw.hpp"
#include "render_dispatch.hpp"

using ds::host::DispatchResult;
using ds::host::ElementDraw;
using ds::host::ElementRendererTable;
using ds::host::PaintBox;
using ds::host::PaintRole;
using ds::host::PaintSink;
using ds::host::TextAlign;
using ds::host::kKindNeedle;

namespace {

struct Needle {
    PaintBox dial{};
    double angle = 0.0;
    double length = 0.0;
    double thickness = 0.0;
    PaintRole role = PaintRole::Fill;
};

class RecordingSink final : public PaintSink {
public:
    void fill_box(const PaintBox& b, PaintRole role) override { fills.push_back({b, role}); }
    void draw_text(const PaintBox&, const std::string&, PaintRole, TextAlign) override {
        ++texts;
    }
    void draw_needle(const PaintBox& dial, double angle_degrees, double length_ratio,
                     double thickness_ratio, PaintRole role) override {
        needles.push_back(Needle{dial, angle_degrees, length_ratio, thickness_ratio, role});
    }

    std::vector<std::pair<PaintBox, PaintRole>> fills;
    std::vector<Needle> needles;
    int texts = 0;
};

PaintBox box(int l, int t, int r, int b) { return PaintBox{l, t, r, b}; }

// E4-18 的元件，依其公開 API 設定（上游可讀不可改）。
ds::elements::RadialPointerElement pointer_at(double ratio) {
    ds::elements::RadialPointerElement p(0.0, 100.0);
    p.set_angle_span(135.0, 270.0);
    p.set_value(ratio * 100.0);
    p.set_length_ratio(0.8);
    p.set_thickness_ratio(0.04);
    p.set_surface("surface.cpu_widget");
    p.set_slot("cpu.usage");
    return p;
}

}  // namespace

// 前提：這個 kind 不是內建的（否則本組測試只是在測一個早就存在的東西）。
TEST(RadialKind, IsNotABuiltinKind) {
    const ElementRendererTable builtin = ElementRendererTable::with_builtin_kinds();
    EXPECT_FALSE(builtin.has_kind(kKindNeedle));
    EXPECT_EQ(builtin.size(), 3u);
}

TEST(RadialKind, RegisteringItIsEnoughAndIsNotSilentlyRepeatable) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    EXPECT_TRUE(ds::host::register_radial_kind(table));
    EXPECT_TRUE(table.has_kind(kKindNeedle));
    EXPECT_EQ(table.size(), 4u);
    EXPECT_FALSE(ds::host::register_radial_kind(table));  // 第二次 → 不靜默覆寫
    EXPECT_EQ(table.size(), 4u);
}

// 投影：E4-18 RenderModel 的每個欄位都原樣帶過去，不假造、不丟失。
TEST(RadialKind, ProjectionCarriesEveryE418FieldThroughUnchanged) {
    const ds::elements::RenderModel model = pointer_at(0.5).render_model();
    const ElementDraw draw = ds::host::project_radial(model);
    EXPECT_EQ(draw.kind, kKindNeedle);
    EXPECT_TRUE(draw.available);  // 可用性是繪製層的政策，不是元件描述的一部分
    EXPECT_TRUE(draw.has_angle);
    EXPECT_DOUBLE_EQ(draw.fill_ratio, model.value_ratio);
    EXPECT_DOUBLE_EQ(draw.start_angle_degrees, model.start_angle_degrees);
    EXPECT_DOUBLE_EQ(draw.sweep_degrees, model.sweep_degrees);
    EXPECT_DOUBLE_EQ(draw.angle_degrees, model.angle_degrees);
    EXPECT_DOUBLE_EQ(draw.length_ratio, model.length_ratio);
    EXPECT_DOUBLE_EQ(draw.thickness_ratio, model.thickness_ratio);
    // E4-18 的語意：angle = start + ratio*sweep。投影不得自己重算或改寫。
    EXPECT_DOUBLE_EQ(draw.angle_degrees, 135.0 + 0.5 * 270.0);
    EXPECT_DOUBLE_EQ(draw.length_ratio, 0.8);
    EXPECT_DOUBLE_EQ(draw.thickness_ratio, 0.04);
}

TEST(RadialKind, DispatchDrawsOneNeedleAtTheProjectedAngle) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(ds::host::register_radial_kind(table));
    RecordingSink sink;
    const PaintBox area = box(0, 0, 200, 80);
    const ElementDraw draw = ds::host::project_radial(pointer_at(0.5).render_model());
    ASSERT_EQ(table.draw(draw, area, sink), DispatchResult::Drawn);
    ASSERT_EQ(sink.needles.size(), 1u);
    EXPECT_EQ(sink.needles[0].dial, ds::host::dial_box(area));  // 置中正方形錶盤
    EXPECT_DOUBLE_EQ(sink.needles[0].angle, 270.0);
    EXPECT_DOUBLE_EQ(sink.needles[0].length, 0.8);
    EXPECT_DOUBLE_EQ(sink.needles[0].thickness, 0.04);
    EXPECT_EQ(sink.needles[0].role, PaintRole::Fill);
    // E4-18 的語意是「指針本身」，錶盤底盤屬於另一個元件 —— 本 kind 不發明它。
    EXPECT_TRUE(sink.fills.empty());
    EXPECT_EQ(sink.texts, 0);
}

TEST(RadialKind, NeedleAngleFollowsTheValue) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(ds::host::register_radial_kind(table));
    double last = -1e9;
    for (double ratio : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        RecordingSink sink;
        const ElementDraw draw = ds::host::project_radial(pointer_at(ratio).render_model());
        ASSERT_EQ(table.draw(draw, box(0, 0, 200, 80), sink), DispatchResult::Drawn);
        ASSERT_EQ(sink.needles.size(), 1u);
        EXPECT_GT(sink.needles[0].angle, last) << "ratio=" << ratio;
        last = sink.needles[0].angle;
        // 夾限後的比例與角度必須一致（上游已夾限，投影不得走偏）。
        EXPECT_DOUBLE_EQ(sink.needles[0].angle, 135.0 + ratio * 270.0);
    }
}

TEST(RadialKind, OutOfRangeValueIsClampedNotWrapped) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(ds::host::register_radial_kind(table));
    RecordingSink sink;
    const ElementDraw over = ds::host::project_radial(pointer_at(5.0).render_model());
    ASSERT_EQ(table.draw(over, box(0, 0, 200, 80), sink), DispatchResult::Drawn);
    ASSERT_EQ(sink.needles.size(), 1u);
    EXPECT_DOUBLE_EQ(sink.needles[0].angle, 135.0 + 270.0);  // 滿格，不是繞回去
}

// NFR-03：該筆不可用 → 只少掉這一根指針（該列的標籤 / 數值由組版層以降級色畫出）。
TEST(RadialKind, UnavailableDrawsNoNeedle) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(ds::host::register_radial_kind(table));
    RecordingSink sink;
    ElementDraw draw = ds::host::project_radial(pointer_at(0.5).render_model());
    draw.available = false;
    ASSERT_EQ(table.draw(draw, box(0, 0, 200, 80), sink), DispatchResult::Drawn);
    EXPECT_TRUE(sink.needles.empty());
    EXPECT_TRUE(sink.fills.empty());
}

TEST(RadialKind, DegenerateAreaIsReportedAndNothingIsDrawn) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(ds::host::register_radial_kind(table));
    RecordingSink sink;
    const ElementDraw draw = ds::host::project_radial(pointer_at(0.5).render_model());
    EXPECT_EQ(table.draw(draw, box(5, 5, 5, 40), sink), DispatchResult::EmptyArea);
    EXPECT_TRUE(sink.needles.empty());
    // 面積退化到連錶盤都切不出來（寬 1）時也不崩潰、不畫。
    RecordingSink thin;
    EXPECT_EQ(table.draw(draw, box(0, 0, 1, 40), thin), DispatchResult::Drawn);
    EXPECT_EQ(thin.needles.size(), 1u);  // 1x1 的錶盤仍是合法面積
}

// 加了這個新 kind 不得影響內建的三種。
TEST(RadialKind, AddingItDoesNotDisturbTheBuiltinKinds) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(ds::host::register_radial_kind(table));
    RecordingSink sink;
    ElementDraw bar;
    bar.kind = ds::host::kKindBar;
    bar.fill_ratio = 0.5;
    ASSERT_EQ(table.draw(bar, box(0, 0, 200, 40), sink), DispatchResult::Drawn);
    EXPECT_EQ(sink.fills.size(), 2u);  // 軌道 + 填充，照舊
    EXPECT_TRUE(sink.needles.empty());
}
