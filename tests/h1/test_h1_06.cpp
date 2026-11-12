// H1-06 繪製層消費 E4 RenderModel（依 kind 分派）— gtest
//
// 本測試是**平台中立**的（不碰 GDI、不 include windows.h），因此 ubuntu 的 G3
// （-DDS_WERROR=ON）與 gate_sanitizers（ASan + UBSan）都真的會跑它。H1-01 那份讀真實
// 像素的測試（tests/h1/test_h1_01.cpp）仍由 gate_windows 承擔，且其斷言的幾何在本檔
// 也以數值釘住一次 —— 否則「改壞版面」只會在 Windows 上紅，ubuntu 完全看不到。
//
// 三條驗收標準各有對應的、會因實作改壞而變紅的測試：
//   1. **平台承諾**：`PlatformPromise` 一組 —— 在**本測試檔內**定義一個前所未有的 kind
//      （"sparkline"），只用公開 API（`add_projector` + `register_kind`）就被畫出來，
//      `src/` / `engine/` / `modules/` / `host/paint/` 皆一行未改。
//   2. **既有三個指標不得退步**：`ThreeMetrics` / `KindDispatch` / `GeometryParity` 三組 ——
//      CPU=gauge / GPU=bar / RAM=progress（host/app/host_main.cpp:134-141 的設定）
//      三種 kind 各自的指令序列、與 H1-01 版面幾何的數值一致性。
//   3. **NFR-03**：`Nfr03` 一組 —— 單筆不可用只灰掉該筆，軌道仍畫、填充不畫、其餘筆不受影響。
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "render_dispatch.hpp"
#include "widget_render.hpp"

using ds::host::ElementDraw;
using ds::host::ElementRendererTable;
using ds::host::DispatchResult;
using ds::host::PaintBox;
using ds::host::PaintRole;
using ds::host::PaintSink;
using ds::host::PaintSummary;
using ds::host::RowGeometry;
using ds::host::TextAlign;
using ds::host::WidgetRenderer;
using ds::host::kKindBar;
using ds::host::kKindGauge;
using ds::host::kKindProgress;
using ds::widgets::MetricRenderEntry;
using ds::widgets::SystemStatusRenderModel;

namespace {

// ---------------------------------------------------------------------------
// 記錄型假繪製介面 —— 錄下指令序列，讓「真的收到了哪些繪製指令」可被斷言
// ---------------------------------------------------------------------------
// 只斷言「沒有崩潰」等於沒有測試（知識庫 K-003 的通則），所以這裡把每一道指令的
// 矩形 / 角色 / 文字 / 角度都存下來。
struct Op {
    enum class Type { Fill, Text, Needle };

    Type type = Type::Fill;
    PaintBox box{};
    PaintRole role = PaintRole::Background;
    TextAlign align = TextAlign::Left;
    std::string text;
    double angle = 0.0;
    double length = 0.0;
    double thickness = 0.0;
};

class RecordingSink final : public PaintSink {
public:
    void fill_box(const PaintBox& box, PaintRole role) override {
        Op op;
        op.type = Op::Type::Fill;
        op.box = box;
        op.role = role;
        ops.push_back(op);
    }

    void draw_text(const PaintBox& box, const std::string& utf8, PaintRole role,
                   TextAlign align) override {
        Op op;
        op.type = Op::Type::Text;
        op.box = box;
        op.role = role;
        op.align = align;
        op.text = utf8;
        ops.push_back(op);
    }

    void draw_needle(const PaintBox& dial, double angle_degrees, double length_ratio,
                     double thickness_ratio, PaintRole role) override {
        Op op;
        op.type = Op::Type::Needle;
        op.box = dial;
        op.role = role;
        op.angle = angle_degrees;
        op.length = length_ratio;
        op.thickness = thickness_ratio;
        ops.push_back(op);
    }

    std::vector<Op> ops;

    int count(Op::Type type) const {
        return static_cast<int>(std::count_if(ops.begin(), ops.end(),
                                              [type](const Op& o) { return o.type == type; }));
    }
    int count_role(Op::Type type, PaintRole role) const {
        return static_cast<int>(
            std::count_if(ops.begin(), ops.end(), [type, role](const Op& o) {
                return o.type == type && o.role == role;
            }));
    }
    std::vector<std::string> texts() const {
        std::vector<std::string> out;
        for (const Op& o : ops)
            if (o.type == Op::Type::Text) out.push_back(o.text);
        return out;
    }
    // 第一道指定型別 + 角色的指令（找不到回 nullptr，呼叫端須自己斷言非空）。
    const Op* first(Op::Type type, PaintRole role) const {
        for (const Op& o : ops)
            if (o.type == type && o.role == role) return &o;
        return nullptr;
    }
};

PaintBox box(int l, int t, int r, int b) { return PaintBox{l, t, r, b}; }

// 與 H1-01 測試同一組邊界，方便兩邊的數值互相對照。
PaintBox widget_bounds() { return box(0, 0, 320, 132); }

// 一筆指標。`kind` 走 E4-03 的 ElementKind，與 C2-02 render() 填進去的一樣。
MetricRenderEntry entry(const char* label, ds::elements::ElementKind kind, double ratio,
                        bool available, const char* text) {
    MetricRenderEntry e;
    e.metric_id = label;
    e.label = label;
    e.available = available;
    e.display_text = text;
    e.element.kind = kind;
    e.element.fill_ratio = ratio;
    return e;
}

SystemStatusRenderModel model_of(std::vector<MetricRenderEntry> entries) {
    SystemStatusRenderModel m;
    m.entries = std::move(entries);
    return m;
}

// host/app/host_main.cpp 設定的那三個指標：CPU=gauge / GPU=bar / RAM=progress。
SystemStatusRenderModel three_metrics() {
    MetricRenderEntry cpu = entry("CPU", ds::elements::ElementKind::Gauge, 0.5, true, "50.0%");
    cpu.element.has_angle = true;
    cpu.element.start_angle_degrees = 135.0;
    cpu.element.sweep_degrees = 270.0;
    cpu.element.angle_degrees = 135.0 + 0.5 * 270.0;
    MetricRenderEntry gpu = entry("GPU", ds::elements::ElementKind::Bar, 0.25, true, "25.0%");
    MetricRenderEntry ram = entry("RAM", ds::elements::ElementKind::Progress, 0.75, true, "75.0%");
    return model_of({cpu, gpu, ram});
}

}  // namespace

// ===========================================================================
// 驗收標準 2 之一：版面幾何與 H1-01 一致（數值釘死）
// ===========================================================================

// H1-01 的 row_layout 在 Windows 上被 test_h1_01.cpp 斷言，而那份測試只在 gate_windows 跑。
// 改壞版面若只在 Windows 上紅，等於 ubuntu 的兩道閘門對它無效——故在此釘住同一組數值。
TEST(GeometryParity, SingleRowMatchesH101Numbers) {
    const RowGeometry g = ds::host::row_geometry(widget_bounds(), 0, 1);
    // padding=12 title_height=24 → top=36；usable=(132-12)-36=84；row_h=84
    EXPECT_EQ(g.label, box(12, 36, 56, 120));   // left+padding .. +label_width(44)
    EXPECT_EQ(g.value, box(242, 36, 308, 120));  // right-padding-value_width(66) ..
    EXPECT_EQ(g.cell, box(64, 36, 234, 120));    // label.right+gap(8) .. value.left-gap
    EXPECT_EQ(g.track, box(64, 73, 234, 83));    // 格位內垂直居中、半高 5
}

TEST(GeometryParity, RowGeometryRejectsOutOfRange) {
    for (int bad : {-1, 3, 99}) {
        const RowGeometry g = ds::host::row_geometry(widget_bounds(), bad, 3);
        EXPECT_EQ(g.track.left, 0);
        EXPECT_EQ(g.track.right, 0);
        EXPECT_EQ(g.cell.right, 0);
    }
    EXPECT_EQ(ds::host::row_geometry(widget_bounds(), 0, 0).track.right, 0);  // row_count<=0
}

TEST(GeometryParity, RowsAreOrderedTopToBottomAndDoNotOverlap) {
    const RowGeometry r0 = ds::host::row_geometry(widget_bounds(), 0, 3);
    const RowGeometry r1 = ds::host::row_geometry(widget_bounds(), 1, 3);
    const RowGeometry r2 = ds::host::row_geometry(widget_bounds(), 2, 3);
    EXPECT_LT(r0.track.top, r1.track.top);
    EXPECT_LT(r1.track.top, r2.track.top);
    EXPECT_LE(r0.label.bottom, r1.label.top);
    EXPECT_LE(r1.label.bottom, r2.label.top);
    EXPECT_LE(r0.cell.bottom, r1.cell.top);
}

TEST(GeometryParity, RowRegionsAreLeftToRightAndDisjoint) {
    const RowGeometry r = ds::host::row_geometry(widget_bounds(), 0, 3);
    EXPECT_LT(r.label.right, r.cell.left);   // 標籤在格位左邊，且有間隙
    EXPECT_LT(r.cell.right, r.value.left);   // 格位在數值左邊，且有間隙
    EXPECT_GT(r.cell.right, r.cell.left);    // 格位有實際寬度
    EXPECT_EQ(r.track.left, r.cell.left);    // 軌道橫向與格位同寬
    EXPECT_EQ(r.track.right, r.cell.right);
    EXPECT_GE(r.track.top, r.cell.top);      // 軌道在格位內
    EXPECT_LE(r.track.bottom, r.cell.bottom);
}

// fill_span 是量表長度的唯一來源（H1-01 的 fill_rect 現在就是它的轉接），逐條釘死。
TEST(GeometryParity, FillSpanIsMonotonicAndClamped) {
    const PaintBox track = box(100, 10, 200, 20);  // 寬 100
    EXPECT_EQ(ds::host::fill_span(track, 0.0).right, 100);   // 空
    EXPECT_EQ(ds::host::fill_span(track, 1.0).right, 200);   // 滿
    EXPECT_EQ(ds::host::fill_span(track, 0.5).right, 150);   // 半
    EXPECT_EQ(ds::host::fill_span(track, -5.0).right, 100);  // 負值夾到 0
    EXPECT_EQ(ds::host::fill_span(track, 42.0).right, 200);  // 超過 1 夾到滿
    int prev = 0;
    for (int i = 0; i <= 10; ++i) {
        const int w = ds::host::fill_span(track, i / 10.0).right - track.left;
        EXPECT_GE(w, prev);
        prev = w;
    }
    // 其餘三邊不得被動到。
    const PaintBox half = ds::host::fill_span(track, 0.5);
    EXPECT_EQ(half.left, track.left);
    EXPECT_EQ(half.top, track.top);
    EXPECT_EQ(half.bottom, track.bottom);
}

// K-002：壞掉的讀數不得偽裝成一個看起來正常的值。
TEST(GeometryParity, NanRatioIsTreatedAsEmptyNotFull) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const PaintBox track = box(0, 0, 100, 10);
    EXPECT_EQ(ds::host::fill_span(track, nan).right, track.left);
    EXPECT_EQ(ds::host::clamp_unit_ratio(nan), 0.0);
    EXPECT_EQ(ds::host::clamp_unit_ratio(-1.0), 0.0);
    EXPECT_EQ(ds::host::clamp_unit_ratio(2.0), 1.0);
    EXPECT_DOUBLE_EQ(ds::host::clamp_unit_ratio(0.25), 0.25);
}

TEST(GeometryParity, VerticalFillGrowsUpwardFromTheBottom) {
    const PaintBox track = box(0, 0, 10, 100);
    const PaintBox filled = ds::host::fill_span(track, 0.25, true);
    EXPECT_EQ(filled.bottom, 100);  // 底邊不動
    EXPECT_EQ(filled.top, 75);      // 往上長 25
    EXPECT_EQ(ds::host::fill_span(track, 0.0, true).top, 100);  // 空
    EXPECT_EQ(ds::host::fill_span(track, 1.0, true).top, 0);    // 滿
}

TEST(GeometryParity, DialBoxIsCenteredAndSquare) {
    const PaintBox dial = ds::host::dial_box(box(0, 0, 200, 80));
    EXPECT_EQ(dial.width(), 80);
    EXPECT_EQ(dial.height(), 80);
    EXPECT_EQ(dial.left, 60);  // (0+200)/2 - 40
    EXPECT_TRUE(ds::host::dial_box(box(0, 0, 0, 80)).empty());
}

// ===========================================================================
// 分派表本身
// ===========================================================================

TEST(KindDispatch, BuiltinTableKnowsTheThreeE403Kinds) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    EXPECT_EQ(table.size(), 3u);
    EXPECT_TRUE(table.has_kind(kKindBar));
    EXPECT_TRUE(table.has_kind(kKindProgress));
    EXPECT_TRUE(table.has_kind(kKindGauge));
    EXPECT_EQ(table.kinds(), (std::vector<std::string>{"bar", "gauge", "progress"}));
}

TEST(KindDispatch, EmptyTableReportsUnknownKindInsteadOfDrawingNothingSilently) {
    const ElementRendererTable table;  // 空表
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindBar;
    EXPECT_EQ(table.draw(draw, box(0, 0, 100, 40), sink), DispatchResult::UnknownKind);
    EXPECT_TRUE(sink.ops.empty());
}

TEST(KindDispatch, UnknownAndEmptyKindsAreReported) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw unknown;
    unknown.kind = "no-such-kind";
    EXPECT_EQ(table.draw(unknown, box(0, 0, 100, 40), sink), DispatchResult::UnknownKind);
    ElementDraw nameless;  // kind 為空字串
    EXPECT_EQ(table.draw(nameless, box(0, 0, 100, 40), sink), DispatchResult::UnknownKind);
    EXPECT_TRUE(sink.ops.empty());
}

TEST(KindDispatch, EmptyAreaIsReportedAndNothingIsDrawn) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindBar;
    EXPECT_EQ(table.draw(draw, box(10, 10, 10, 40), sink), DispatchResult::EmptyArea);
    EXPECT_TRUE(sink.ops.empty());
}

TEST(KindDispatch, RegisterKindRefusesEmptyNullAndDuplicate) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    const ds::host::ElementRenderer noop = [](const ElementDraw&, const PaintBox&, PaintSink&) {};
    EXPECT_FALSE(table.register_kind("", noop));                      // 空鍵
    EXPECT_FALSE(table.register_kind("x", ds::host::ElementRenderer{}));  // 空 renderer
    EXPECT_FALSE(table.register_kind(kKindBar, noop));                // 已存在 → 不靜默覆寫
    EXPECT_EQ(table.size(), 3u);
    EXPECT_TRUE(table.register_kind("x", noop));
    EXPECT_EQ(table.size(), 4u);
}

TEST(KindDispatch, ReplaceKindIsTheExplicitWayToOverride) {
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    bool called = false;
    EXPECT_TRUE(table.replace_kind(kKindBar, [&called](const ElementDraw&, const PaintBox&,
                                                       PaintSink&) { called = true; }));
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindBar;
    EXPECT_EQ(table.draw(draw, box(0, 0, 100, 40), sink), DispatchResult::Drawn);
    EXPECT_TRUE(called);
    EXPECT_TRUE(sink.ops.empty());  // 換掉的畫法沒下任何指令 → 真的是新的那個被呼叫
    EXPECT_EQ(table.size(), 3u);
}

// ===========================================================================
// 驗收標準 2 之二：三種 kind 各自的畫法（bar / progress / gauge 真的不同）
// ===========================================================================

TEST(KindDispatch, BarDrawsTrackThenProportionalFill) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindBar;
    draw.fill_ratio = 0.5;
    const PaintBox area = box(0, 0, 200, 40);
    ASSERT_EQ(table.draw(draw, area, sink), DispatchResult::Drawn);
    ASSERT_EQ(sink.ops.size(), 2u);
    EXPECT_EQ(sink.ops[0].role, PaintRole::Track);
    EXPECT_EQ(sink.ops[0].box, ds::host::track_strip(area, 5));
    EXPECT_EQ(sink.ops[1].role, PaintRole::Fill);
    EXPECT_EQ(sink.ops[1].box.width(), 100);  // 200 的一半
    EXPECT_EQ(sink.count(Op::Type::Needle), 0);
}

TEST(KindDispatch, BarFillWidthIsMonotonicInRatio) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    int prev = -1;
    for (double ratio : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        RecordingSink sink;
        ElementDraw draw;
        draw.kind = kKindBar;
        draw.fill_ratio = ratio;
        ASSERT_EQ(table.draw(draw, box(0, 0, 200, 40), sink), DispatchResult::Drawn);
        const Op* fill = sink.first(Op::Type::Fill, PaintRole::Fill);
        const int width = fill == nullptr ? 0 : fill->box.width();
        EXPECT_GT(width, prev) << "ratio=" << ratio;
        prev = width;
    }
}

TEST(KindDispatch, ZeroRatioDrawsNoFillButStillDrawsTheTrack) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindBar;
    draw.fill_ratio = 0.0;
    ASSERT_EQ(table.draw(draw, box(0, 0, 200, 40), sink), DispatchResult::Drawn);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 0);
    // 軌道一定要看得見，否則使用者不知道有這條量表（H1-01 的同名斷言）。
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 1);
}

TEST(KindDispatch, ProgressAddsTheReadoutInsideTheTrack) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindProgress;
    draw.fill_ratio = 0.63;
    draw.show_label = true;
    draw.label = "63%";
    const PaintBox area = box(0, 0, 200, 40);
    ASSERT_EQ(table.draw(draw, area, sink), DispatchResult::Drawn);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 1);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 1);
    ASSERT_EQ(sink.count(Op::Type::Text), 1);
    const Op* readout = sink.first(Op::Type::Text, PaintRole::Value);
    ASSERT_NE(readout, nullptr);
    EXPECT_EQ(readout->text, "63%");
    EXPECT_EQ(readout->align, TextAlign::Center);
    EXPECT_EQ(readout->box, ds::host::track_strip(area, 5));
}

TEST(KindDispatch, ProgressWithoutLabelLooksLikeBarButIsStillItsOwnKind) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink bar_sink;
    RecordingSink progress_sink;
    ElementDraw draw;
    draw.fill_ratio = 0.4;
    draw.show_label = false;  // C2-02 目前即如此設定
    draw.kind = kKindBar;
    ASSERT_EQ(table.draw(draw, box(0, 0, 200, 40), bar_sink), DispatchResult::Drawn);
    draw.kind = kKindProgress;
    ASSERT_EQ(table.draw(draw, box(0, 0, 200, 40), progress_sink), DispatchResult::Drawn);
    EXPECT_EQ(bar_sink.ops.size(), progress_sink.ops.size());
    EXPECT_EQ(progress_sink.count(Op::Type::Text), 0);
}

TEST(KindDispatch, GaugeDrawsADialAndANeedleNotALinearFill) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindGauge;
    draw.fill_ratio = 0.5;
    draw.has_angle = true;
    draw.angle_degrees = 270.0;
    draw.length_ratio = 0.8;
    draw.thickness_ratio = 0.06;
    const PaintBox area = box(0, 0, 200, 80);
    ASSERT_EQ(table.draw(draw, area, sink), DispatchResult::Drawn);
    ASSERT_EQ(sink.ops.size(), 2u);
    EXPECT_EQ(sink.ops[0].role, PaintRole::Track);
    EXPECT_EQ(sink.ops[0].box, ds::host::dial_box(area));  // 錶盤是置中正方形，不是細長軌道
    ASSERT_EQ(sink.ops[1].type, Op::Type::Needle);
    EXPECT_DOUBLE_EQ(sink.ops[1].angle, 270.0);
    EXPECT_DOUBLE_EQ(sink.ops[1].length, 0.8);
    EXPECT_DOUBLE_EQ(sink.ops[1].thickness, 0.06);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 0);  // 沒有線性填充
}

TEST(KindDispatch, GaugeNeedleFollowsTheAngle) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    double last = -1e9;
    for (double ratio : {0.0, 0.5, 1.0}) {
        RecordingSink sink;
        ElementDraw draw;
        draw.kind = kKindGauge;
        draw.has_angle = true;
        draw.start_angle_degrees = 135.0;
        draw.sweep_degrees = 270.0;
        draw.fill_ratio = ratio;
        draw.angle_degrees = 135.0 + ratio * 270.0;
        ASSERT_EQ(table.draw(draw, box(0, 0, 200, 80), sink), DispatchResult::Drawn);
        ASSERT_EQ(sink.count(Op::Type::Needle), 1);
        const Op& needle = sink.ops.back();
        EXPECT_GT(needle.angle, last) << "ratio=" << ratio;
        last = needle.angle;
    }
}

// 角度缺失不得讓數值靜默消失。
TEST(KindDispatch, GaugeWithoutAngleFallsBackToALinearFill) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindGauge;
    draw.has_angle = false;
    draw.fill_ratio = 0.75;
    ASSERT_EQ(table.draw(draw, box(0, 0, 200, 80), sink), DispatchResult::Drawn);
    EXPECT_EQ(sink.count(Op::Type::Needle), 0);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 1);
}

// ===========================================================================
// 驗收標準 2 之三：既有三個指標的呈現（CPU=gauge / GPU=bar / RAM=progress）
// ===========================================================================

TEST(ThreeMetrics, BackgroundAndTitleComeFirst) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    const PaintSummary summary =
        renderer.paint(model_of({}), widget_bounds(), "desktop-shell", sink);
    EXPECT_EQ(summary.rows, 0);
    ASSERT_GE(sink.ops.size(), 2u);
    EXPECT_EQ(sink.ops[0].type, Op::Type::Fill);
    EXPECT_EQ(sink.ops[0].role, PaintRole::Background);
    EXPECT_EQ(sink.ops[0].box, widget_bounds());  // 整個 bounds 都要有底色
    EXPECT_EQ(sink.ops[1].type, Op::Type::Text);
    EXPECT_EQ(sink.ops[1].role, PaintRole::Title);
    EXPECT_EQ(sink.ops[1].text, "desktop-shell");
    EXPECT_EQ(sink.ops.size(), 2u);  // 空 model 不得畫出任何一列
}

TEST(ThreeMetrics, AllThreeAreDrawnEachByItsOwnKind) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    const PaintSummary summary = renderer.paint(three_metrics(), widget_bounds(), "t", sink);
    EXPECT_EQ(summary.rows, 3);
    EXPECT_EQ(summary.drawn, 3);
    EXPECT_EQ(summary.unknown_kind, 0);
    EXPECT_EQ(summary.degraded, 0);
    EXPECT_EQ(summary.empty_area, 0);

    // 三個標籤與三個數值都在（不得有指標在改寫後消失）。
    const std::vector<std::string> texts = sink.texts();
    for (const char* expected : {"t", "CPU", "50.0%", "GPU", "25.0%", "RAM", "75.0%"}) {
        EXPECT_NE(std::find(texts.begin(), texts.end(), std::string(expected)), texts.end())
            << expected << " 不見了";
    }
    // CPU 是量表 → 恰好一根指針，且指針角度真的是 E4-03 算出來的那個
    // （135 + 0.5*270 = 270）—— 這一條若只斷言「有一根指針」，把 angle_degrees
    // 接成 start_angle_degrees 之類的錯接就會從 paint() 這條端到端路徑溜過去。
    ASSERT_EQ(sink.count(Op::Type::Needle), 1);
    const Op* cpu_needle = sink.first(Op::Type::Needle, PaintRole::Fill);
    ASSERT_NE(cpu_needle, nullptr);
    EXPECT_DOUBLE_EQ(cpu_needle->angle, 270.0);
    EXPECT_EQ(cpu_needle->box, ds::host::dial_box(ds::host::row_geometry(widget_bounds(), 0, 3).cell));
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 2);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 3);  // 三條軌道 / 錶盤
}

TEST(ThreeMetrics, EachRowsOpsLandInsideThatRowsGeometry) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    renderer.paint(three_metrics(), widget_bounds(), "t", sink);
    for (int row = 0; row < 3; ++row) {
        const RowGeometry geo = ds::host::row_geometry(widget_bounds(), row, 3);
        const Op* label = nullptr;
        for (const Op& o : sink.ops)
            if (o.type == Op::Type::Text && o.box == geo.label) label = &o;
        ASSERT_NE(label, nullptr) << "第 " << row << " 列沒有標籤文字";
        EXPECT_EQ(label->align, TextAlign::Left);
    }
    // 數值文字右對齊（H1-01 的既有呈現）。
    EXPECT_EQ(sink.count_role(Op::Type::Text, PaintRole::Value), 3);
    for (const Op& o : sink.ops) {
        if (o.type == Op::Type::Text && o.role == PaintRole::Value) {
            EXPECT_EQ(o.align, TextAlign::Right);
        }
    }
}

TEST(ThreeMetrics, MissingLabelAndValueFallBackToPlaceholders) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    MetricRenderEntry blank;
    blank.available = true;  // 標籤與 display_text 皆空
    renderer.paint(model_of({blank}), widget_bounds(), "t", sink);
    const std::vector<std::string> texts = sink.texts();
    EXPECT_NE(std::find(texts.begin(), texts.end(), std::string("?")), texts.end());
    EXPECT_NE(std::find(texts.begin(), texts.end(), std::string("--")), texts.end());
}

TEST(ThreeMetrics, DegenerateBoundsReportEmptyAreaInsteadOfCrashing) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    const PaintSummary summary =
        renderer.paint(three_metrics(), box(0, 0, 8, 8), "t", sink);
    EXPECT_EQ(summary.rows, 3);
    EXPECT_EQ(summary.drawn, 0);
    EXPECT_EQ(summary.empty_area, 3);  // 退化尺寸被說出來，而不是靜默空白
    EXPECT_EQ(summary.unknown_kind, 0);
}

TEST(ThreeMetrics, NanRatioPaintsNoFill) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    MetricRenderEntry e = entry("CPU", ds::elements::ElementKind::Bar,
                                std::numeric_limits<double>::quiet_NaN(), true, "?");
    renderer.paint(model_of({e}), widget_bounds(), "t", sink);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 0);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 1);
}

TEST(ThreeMetrics, ProjectionCarriesEveryE403FieldThroughUnchanged) {
    MetricRenderEntry e = entry("CPU", ds::elements::ElementKind::Gauge, 0.25, true, "25%");
    e.element.has_angle = true;
    e.element.start_angle_degrees = 135.0;
    e.element.sweep_degrees = 270.0;
    e.element.angle_degrees = 202.5;
    e.element.orientation = ds::elements::Orientation::Vertical;
    e.element.show_label = true;
    e.element.label = "25%";
    const ElementDraw draw = ds::host::project_entry(e);
    EXPECT_EQ(draw.kind, kKindGauge);
    EXPECT_TRUE(draw.available);
    EXPECT_TRUE(draw.vertical);
    EXPECT_DOUBLE_EQ(draw.fill_ratio, 0.25);
    EXPECT_TRUE(draw.has_angle);
    EXPECT_DOUBLE_EQ(draw.start_angle_degrees, 135.0);
    EXPECT_DOUBLE_EQ(draw.sweep_degrees, 270.0);
    EXPECT_DOUBLE_EQ(draw.angle_degrees, 202.5);
    EXPECT_TRUE(draw.show_label);
    EXPECT_EQ(draw.label, "25%");
    EXPECT_EQ(ds::host::element_kind_key(ds::elements::ElementKind::Bar), kKindBar);
    EXPECT_EQ(ds::host::element_kind_key(ds::elements::ElementKind::Progress), kKindProgress);
}

// ===========================================================================
// 驗收標準 3：NFR-03 —— 單筆指標不可用只灰掉該筆
// ===========================================================================

TEST(Nfr03, UnavailableRowKeepsItsTrackLosesItsFillAndTurnsGrey) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    RecordingSink sink;
    MetricRenderEntry down = entry("GPU", ds::elements::ElementKind::Bar, 1.0, false, "—");
    const PaintSummary summary = renderer.paint(model_of({down}), widget_bounds(), "t", sink);
    EXPECT_EQ(summary.degraded, 1);
    EXPECT_EQ(summary.drawn, 1);  // 仍然有被畫（降級不是「不畫」）
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 1);  // 軌道還在
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 0);   // 填充不畫
    // 標籤與數值都以降級色（Unavailable）呈現，而不是正常色。
    EXPECT_EQ(sink.count_role(Op::Type::Text, PaintRole::Unavailable), 2);
    EXPECT_EQ(sink.count_role(Op::Type::Text, PaintRole::Label), 0);
    EXPECT_EQ(sink.count_role(Op::Type::Text, PaintRole::Value), 0);
}

TEST(Nfr03, OneUnavailableRowDoesNotAffectTheOthers) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();

    RecordingSink both_ok;
    renderer.paint(model_of({entry("CPU", ds::elements::ElementKind::Bar, 1.0, true, "100%"),
                             entry("GPU", ds::elements::ElementKind::Bar, 1.0, true, "100%")}),
                   widget_bounds(), "t", both_ok);

    RecordingSink mixed;
    const PaintSummary summary =
        renderer.paint(model_of({entry("CPU", ds::elements::ElementKind::Bar, 1.0, true, "100%"),
                                 entry("GPU", ds::elements::ElementKind::Bar, 1.0, false, "—")}),
                       widget_bounds(), "t", mixed);

    EXPECT_EQ(summary.degraded, 1);
    EXPECT_EQ(both_ok.count_role(Op::Type::Fill, PaintRole::Fill), 2);
    EXPECT_EQ(mixed.count_role(Op::Type::Fill, PaintRole::Fill), 1);  // 只少掉壞的那筆

    // 可用的那一筆（第 0 列）的指令必須與「兩筆都好」時**完全一致** —— 逐一比對矩形。
    const RowGeometry row0 = ds::host::row_geometry(widget_bounds(), 0, 2);
    const PaintBox track0 = ds::host::track_strip(row0.cell, 5);
    for (const RecordingSink* s : {&both_ok, &mixed}) {
        int track_hits = 0;
        int fill_hits = 0;
        for (const Op& o : s->ops) {
            if (o.type != Op::Type::Fill) continue;
            if (o.box == track0 && o.role == PaintRole::Track) ++track_hits;
            if (o.box == track0 && o.role == PaintRole::Fill) ++fill_hits;
        }
        EXPECT_EQ(track_hits, 1);
        EXPECT_EQ(fill_hits, 1) << "好的那一列的填充被壞的那一列影響了";
    }
    EXPECT_EQ(mixed.count_role(Op::Type::Text, PaintRole::Label), 1);        // 好的那筆：正常色
    EXPECT_EQ(mixed.count_role(Op::Type::Text, PaintRole::Unavailable), 2);  // 壞的那筆：灰
}

TEST(Nfr03, UnavailableGaugeKeepsItsDialButLosesItsNeedle) {
    const ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    RecordingSink sink;
    ElementDraw draw;
    draw.kind = kKindGauge;
    draw.available = false;
    draw.has_angle = true;
    draw.angle_degrees = 200.0;
    ASSERT_EQ(table.draw(draw, box(0, 0, 200, 80), sink), DispatchResult::Drawn);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 1);
    EXPECT_EQ(sink.count(Op::Type::Needle), 0);
}

TEST(Nfr03, ProjectorCannotOverrideTheAvailabilityPolicy) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    // 一個「樂觀」的投影器：硬把 available 改成 true。繪製層不得理它。
    renderer.add_projector([](const MetricRenderEntry&, ElementDraw& draw) {
        draw.kind = kKindBar;
        draw.available = true;
        draw.fill_ratio = 1.0;
        return true;
    });
    RecordingSink sink;
    renderer.paint(model_of({entry("GPU", ds::elements::ElementKind::Bar, 1.0, false, "—")}),
                   widget_bounds(), "t", sink);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 0);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 1);
}

// ===========================================================================
// 驗收標準 1：平台承諾 —— 新增一個元件型別，不改 src/ 與 engine/ 任何一行即可被畫出
// ===========================================================================
//
// 以下這個 "sparkline" kind **完全定義在本測試檔內**：它的投影（哪一筆指標屬於它、
// 自帶哪些具名參數）與它的畫法（要下哪些繪製指令）都是測試自己的程式碼，只透過
// `add_projector()` / `register_kind()` 兩個公開 API 接上去。
// `src/` / `engine/` / `modules/` 一行未改，連 `host/paint/` 也一行未改。
namespace {

constexpr const char* kKindSparkline = "sparkline";

// 新元件型別的畫法：把格位切成 N 格，依每格自己的高度比例畫一根柱子，末端再放一根指針。
// （形狀不重要，重點是它用的是**同一組** PaintSink 原語，平台不必為它新增任何東西。）
void render_sparkline(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    const int samples = static_cast<int>(draw.param("samples", 0.0));
    if (samples <= 0 || area.empty()) return;
    const int step = area.width() / samples;
    if (step <= 0) return;
    for (int i = 0; i < samples; ++i) {
        const PaintBox column{area.left + i * step, area.top, area.left + (i + 1) * step,
                              area.bottom};
        sink.fill_box(column, PaintRole::Track);
        if (!draw.available) continue;
        sink.fill_box(ds::host::fill_span(column, draw.param("sample." + std::to_string(i), 0.0),
                                          true),
                      PaintRole::Fill);
    }
    if (draw.available && draw.has_angle)
        sink.draw_needle(ds::host::dial_box(area), draw.angle_degrees, draw.length_ratio,
                         draw.thickness_ratio, PaintRole::Fill);
}

// 新元件型別的認領：metric_id 以 "spark." 開頭的指標屬於它。
bool project_sparkline(const MetricRenderEntry& entry, ElementDraw& draw) {
    if (entry.metric_id.rfind("spark.", 0) != 0) return false;
    draw.kind = kKindSparkline;
    draw.has_angle = true;
    draw.angle_degrees = 45.0;
    draw.params["samples"] = 3.0;
    draw.params["sample.0"] = 0.25;
    draw.params["sample.1"] = 0.50;
    draw.params["sample.2"] = 1.00;
    return true;
}

MetricRenderEntry spark_entry(bool available) {
    MetricRenderEntry e;
    e.metric_id = "spark.net.rx";
    e.label = "NET";
    e.available = available;
    e.display_text = available ? "12 KB/s" : "—";
    return e;
}

}  // namespace

// 前提一：這個 kind 原本**真的不存在**（否則後面整組測試只是在測一個早就內建的東西）。
TEST(PlatformPromise, TheNewKindIsNotKnownBeforehand) {
    const WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    EXPECT_FALSE(renderer.renderer().has_kind(kKindSparkline));
    EXPECT_EQ(renderer.renderer().size(), 3u);
    EXPECT_EQ(renderer.projector_count(), 0u);
    // 還沒接上投影器時，這筆指標走內建的 E4-03 投影（element 維持預設 → bar）。
    // 這是**既有行為**，不是新 kind 生效：下一個測試才是「認領了但沒有畫法」的樣子。
    EXPECT_EQ(renderer.project(spark_entry(true)).kind, kKindBar);
}

// 前提二：認領了、但還沒註冊畫法 → 明確回報 unknown_kind（**不是**靜默畫成預設長條），
// 而該列的標籤與數值仍然畫出來 —— 指標不會因為少一個畫法就整列消失。
TEST(PlatformPromise, ClaimedButUnregisteredKindIsReportedNotSilentlyMisdrawn) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    renderer.add_projector(&project_sparkline);
    ASSERT_FALSE(renderer.renderer().has_kind(kKindSparkline));
    RecordingSink sink;
    const PaintSummary summary =
        renderer.paint(model_of({spark_entry(true)}), widget_bounds(), "t", sink);
    EXPECT_EQ(summary.unknown_kind, 1);
    EXPECT_EQ(summary.drawn, 0);
    EXPECT_EQ(sink.count(Op::Type::Text), 3);  // 標題 + 標籤 + 數值
    EXPECT_EQ(sink.count(Op::Type::Needle), 0);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 0);  // 沒有假裝畫成長條
}

TEST(PlatformPromise, RegisteringANewKindIsEnoughToGetItPainted) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    ASSERT_TRUE(renderer.renderer().register_kind(kKindSparkline, &render_sparkline));
    renderer.add_projector(&project_sparkline);
    EXPECT_EQ(renderer.projector_count(), 1u);
    EXPECT_TRUE(renderer.renderer().has_kind(kKindSparkline));
    EXPECT_EQ(renderer.renderer().size(), 4u);

    RecordingSink sink;
    const PaintSummary summary =
        renderer.paint(model_of({spark_entry(true)}), widget_bounds(), "t", sink);
    EXPECT_EQ(summary.rows, 1);
    EXPECT_EQ(summary.drawn, 1);
    EXPECT_EQ(summary.unknown_kind, 0);

    // 真的收到了這個 kind 自己的繪製指令：三根柱子（軌道 + 填充）+ 一根指針。
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 3);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 3);
    ASSERT_EQ(sink.count(Op::Type::Needle), 1);
    const Op* needle = sink.first(Op::Type::Needle, PaintRole::Fill);
    ASSERT_NE(needle, nullptr);
    EXPECT_DOUBLE_EQ(needle->angle, 45.0);

    // 柱子的高度真的跟著各自的具名參數走（0.25 / 0.50 / 1.00 → 遞增）。
    std::vector<int> heights;
    for (const Op& o : sink.ops)
        if (o.type == Op::Type::Fill && o.role == PaintRole::Fill) heights.push_back(o.box.height());
    ASSERT_EQ(heights.size(), 3u);
    EXPECT_LT(heights[0], heights[1]);
    EXPECT_LT(heights[1], heights[2]);

    // 柱子都落在該列的格位內（新 kind 不會畫到別人家去）。
    const RowGeometry geo = ds::host::row_geometry(widget_bounds(), 0, 1);
    for (const Op& o : sink.ops) {
        if (o.type != Op::Type::Fill || o.role == PaintRole::Background) continue;
        EXPECT_GE(o.box.left, geo.cell.left);
        EXPECT_LE(o.box.right, geo.cell.right);
    }
}

TEST(PlatformPromise, TheNewKindAlsoHonoursNfr03) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    ASSERT_TRUE(renderer.renderer().register_kind(kKindSparkline, &render_sparkline));
    renderer.add_projector(&project_sparkline);
    RecordingSink sink;
    const PaintSummary summary =
        renderer.paint(model_of({spark_entry(false)}), widget_bounds(), "t", sink);
    EXPECT_EQ(summary.degraded, 1);
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Track), 3);  // 柱子的底還在
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 0);   // 但沒有讀數
    EXPECT_EQ(sink.count(Op::Type::Needle), 0);
    EXPECT_EQ(sink.count_role(Op::Type::Text, PaintRole::Unavailable), 2);
}

// 加了新 kind 不得影響既有三個指標。
TEST(PlatformPromise, AddingANewKindDoesNotDisturbTheBuiltinOnes) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    ASSERT_TRUE(renderer.renderer().register_kind(kKindSparkline, &render_sparkline));
    renderer.add_projector(&project_sparkline);
    RecordingSink sink;
    const PaintSummary summary = renderer.paint(three_metrics(), widget_bounds(), "t", sink);
    EXPECT_EQ(summary.drawn, 3);
    EXPECT_EQ(summary.unknown_kind, 0);
    EXPECT_EQ(sink.count(Op::Type::Needle), 1);                       // 只有 CPU 那根
    EXPECT_EQ(sink.count_role(Op::Type::Fill, PaintRole::Fill), 2);   // GPU / RAM
}

// 投影器的優先順序與退回路徑。
TEST(PlatformPromise, ProjectorsAreAskedInReverseOrderAndFallBackToTheBuiltinProjection) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    renderer.add_projector([](const MetricRenderEntry&, ElementDraw& draw) {
        draw.kind = kKindBar;  // 先註冊的：會被後註冊的蓋過
        return true;
    });
    renderer.add_projector([](const MetricRenderEntry& e, ElementDraw& draw) {
        if (e.metric_id != "CPU") return false;  // 不認領 → 交給前一個
        draw.kind = kKindGauge;
        draw.has_angle = true;
        draw.angle_degrees = 10.0;
        return true;
    });
    EXPECT_EQ(renderer.projector_count(), 2u);

    const ElementDraw cpu =
        renderer.project(entry("CPU", ds::elements::ElementKind::Progress, 0.5, true, "x"));
    EXPECT_EQ(cpu.kind, kKindGauge) << "後註冊的投影器應該先認領";

    const ElementDraw other =
        renderer.project(entry("GPU", ds::elements::ElementKind::Progress, 0.5, true, "x"));
    EXPECT_EQ(other.kind, kKindBar) << "不認領時應退回前一個投影器";

    // 沒有任何投影器時走內建 E4-03 投影。
    const WidgetRenderer plain = WidgetRenderer::with_builtin_kinds();
    EXPECT_EQ(plain.project(entry("GPU", ds::elements::ElementKind::Progress, 0.5, true, "x")).kind,
              kKindProgress);
}

TEST(PlatformPromise, NullProjectorIsRefusedInsteadOfCrashingLater) {
    WidgetRenderer renderer = WidgetRenderer::with_builtin_kinds();
    renderer.add_projector(ds::host::ElementProjector{});
    EXPECT_EQ(renderer.projector_count(), 0u);
}
