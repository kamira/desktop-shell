// H1-08 環形量表繪製 — gtest
//
// 本測試是**平台中立**的（不碰 GDI、不 include windows.h），且其 `add_test` 註冊在
// `host/paint/CMakeLists.txt` 的**頂層、不在 `if(WIN32)` 內**：ubuntu 的 G3
// （-DDS_WERROR=ON）、G3b（註冊測試數不得下降）與 `gate_sanitizers`（ASan + UBSan +
// `-fno-sanitize-recover=all`）都真的會跑它。
//
// 四條驗收標準各有對應的、會因實作改壞而變紅的測試：
//   1. **消費 E4-03 `GaugeElement` 的弧輸出**：`FromE4_03` 一組 —— 真的建一個
//      `ds::elements::GaugeElement`，經 H1-06 的 `project_entry()` 投影，再交給本層；
//      值弧終點必須等於 E4-03 自己算的 `angle()`（繪製層沒有偷偷改方向）。
//   2. **H1-06 kind 分派機制的第一個具體案例**：`Dispatch` 一組 —— 只用 H1-06 的公開
//      擴充點接上，`"gauge"` 分派出來的是弧而不是指針。
//   3. **125% / 150% 下細線不得糊成一團**：`DeviceAlignment` 一組 —— 筆寬、圓心、
//      內 / 外緣半徑在非整數縮放下仍是**精確的整數裝置像素**（逐一比對期望值，
//      不是只檢查「是整數」——那種斷言連「永遠回 0」的實作都殺不掉）。
//   4. **退化弧、值超出範圍不得崩潰**：`Degenerate` 一組 —— 退化弧 / NaN / ±Inf /
//      荒謬縮放 / 退化尺寸各有具名狀態碼與可斷言的行為，且不靜默。
//
// 另有 `SweepDirection` 一組：把 E4-03 預設弧（start 135° / sweep +270°）在畫面上的
// 實際掃向**釘住**（0% 左上 → 50% 正下方 → 100% 右上，從下方經過），並證明常規掃向
// （start 225° / sweep -270°，經上方）在本層已可用。見 `host/paint/ring_gauge.hpp` §4。
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "bar_gauge.hpp"             // E4-03（上游，可讀不可改）
#include "ring_gauge.hpp"            // 本單元
#include "system_status_widget.hpp"  // C2-02（上游）：MetricRenderEntry
#include "widget_render.hpp"         // H1-06（上游）：project_entry

using ds::host::ArcSink;
using ds::host::ArcSpan;
using ds::host::DispatchResult;
using ds::host::ElementDraw;
using ds::host::ElementRendererTable;
using ds::host::PaintBox;
using ds::host::PaintRole;
using ds::host::PaintSink;
using ds::host::RingGaugePainter;
using ds::host::RingGeometry;
using ds::host::RingStatus;
using ds::host::RingStyle;
using ds::host::TextAlign;
using ds::host::compute_ring_geometry;
using ds::host::device_stroke_px;
using ds::host::install_ring_gauge;
using ds::host::kKindGauge;
using ds::host::kKindRingGauge;
using ds::host::sanitize_dpi_scale;

namespace {

// `<cmath>` 的 M_PI 不是標準 C++（MSVC 要 _USE_MATH_DEFINES），而本測試也會被
// gate_windows 的 MSVC 建到。自備常數，不賭編譯器。
constexpr double kPi = 3.14159265358979323846;

const PaintBox kCell{0, 0, 100, 100};
const RingStyle kStyle{};  // thickness 6、inset 2（邏輯像素）

// ---------------------------------------------------------------------------
// 記錄型假繪製介面 —— 錄下指令序列（只斷言「沒有崩潰」等於沒有測試，K-003 的通則）
// ---------------------------------------------------------------------------
class RecordingArcSink final : public ArcSink {
public:
    void draw_arc(const ArcSpan& span, PaintRole role) override {
        spans.push_back(span);
        roles.push_back(role);
    }

    std::vector<ArcSpan> spans;
    std::vector<PaintRole> roles;

    int count_role(PaintRole role) const {
        int n = 0;
        for (const PaintRole r : roles) {
            if (r == role) ++n;
        }
        return n;
    }
};

struct PaintOp {
    enum class Type { Fill, Text, Needle };
    Type type = Type::Fill;
    PaintBox box{};
    PaintRole role = PaintRole::Background;
    std::string text;
};

class RecordingPaintSink final : public PaintSink {
public:
    void fill_box(const PaintBox& box, PaintRole role) override {
        ops.push_back(PaintOp{PaintOp::Type::Fill, box, role, std::string{}});
    }
    void draw_text(const PaintBox& box, const std::string& utf8, PaintRole role,
                   TextAlign) override {
        ops.push_back(PaintOp{PaintOp::Type::Text, box, role, utf8});
    }
    void draw_needle(const PaintBox& dial, double, double, double, PaintRole role) override {
        ops.push_back(PaintOp{PaintOp::Type::Needle, dial, role, std::string{}});
    }

    std::vector<PaintOp> ops;

    int count(PaintOp::Type type) const {
        int n = 0;
        for (const PaintOp& op : ops) {
            if (op.type == type) ++n;
        }
        return n;
    }
};

// 弧上某個比例位置的裝置座標。**與 `ArcSink` 實作該用的是同一條式子**：
// 0° 指向右、逆時針為正，y 軸朝下故 sin 前面是減號。
double arc_x(const ArcSpan& span, double ratio) {
    const double deg = span.start_degrees + span.sweep_degrees * ratio;
    return span.center_x + span.mid_radius() * std::cos(deg * kPi / 180.0);
}
double arc_y(const ArcSpan& span, double ratio) {
    const double deg = span.start_degrees + span.sweep_degrees * ratio;
    return span.center_y - span.mid_radius() * std::sin(deg * kPi / 180.0);
}

ElementDraw gauge_draw(double start, double sweep, double ratio) {
    ElementDraw draw;
    draw.kind = kKindGauge;
    draw.has_angle = true;
    draw.start_angle_degrees = start;
    draw.sweep_degrees = sweep;
    draw.fill_ratio = ratio;
    return draw;
}

bool is_integral(double v) { return std::isfinite(v) && std::floor(v) == v; }

// ===========================================================================
// 1. 消費 E4-03 GaugeElement 的弧輸出
// ===========================================================================

// E4-03 的 RenderModel 經 H1-06 的投影送進本層，弧原封不動。
TEST(FromE4_03, ConsumesGaugeArcUnchanged) {
    ds::elements::GaugeElement gauge;  // 預設 [0,1]、start 135°、sweep +270°
    gauge.set_value(0.25);

    ds::widgets::MetricRenderEntry entry;
    entry.available = true;
    entry.element = gauge.render_model();

    const ElementDraw draw = ds::host::project_entry(entry);
    ASSERT_EQ(draw.kind, std::string(kKindGauge));
    ASSERT_TRUE(draw.has_angle);

    const RingGeometry geom = compute_ring_geometry(draw, kCell, kStyle, 1.0);
    ASSERT_EQ(geom.status, RingStatus::Ok);

    EXPECT_DOUBLE_EQ(geom.track.start_degrees, gauge.start_angle());
    EXPECT_DOUBLE_EQ(geom.track.sweep_degrees, gauge.sweep());
    EXPECT_DOUBLE_EQ(geom.value.start_degrees, gauge.start_angle());
    EXPECT_DOUBLE_EQ(geom.value.sweep_degrees, gauge.sweep() * 0.25);

    // 最關鍵的一條：值弧的**終點**必須等於 E4-03 自己算出來的指針角度。
    // 繪製層若偷偷反轉方向 / 換起點，這裡立刻紅。
    EXPECT_DOUBLE_EQ(geom.track.start_degrees + geom.value.sweep_degrees, gauge.angle());
}

// 值超出 E4-03 的範圍：元件端夾限，環跟著滿，不崩潰。
TEST(FromE4_03, ValueOutOfRangeClampsToFullRing) {
    ds::elements::GaugeElement gauge(0.0, 100.0);
    gauge.set_value(1e9);  // 遠超 max

    ds::widgets::MetricRenderEntry entry;
    entry.available = true;
    entry.element = gauge.render_model();

    const ElementDraw draw = ds::host::project_entry(entry);
    const RingGeometry geom = compute_ring_geometry(draw, kCell, kStyle, 1.0);
    ASSERT_EQ(geom.status, RingStatus::Ok);
    EXPECT_DOUBLE_EQ(geom.value.sweep_degrees, geom.track.sweep_degrees);
}

// 自訂弧（含負 sweep）同樣原封照畫。
TEST(FromE4_03, CustomClockwiseArcIsHonoured) {
    ds::elements::GaugeElement gauge;
    gauge.set_arc(225.0, -270.0);  // 常規掃向：下方左側起、經上方
    gauge.set_value(0.5);

    ds::widgets::MetricRenderEntry entry;
    entry.available = true;
    entry.element = gauge.render_model();

    const RingGeometry geom =
        compute_ring_geometry(ds::host::project_entry(entry), kCell, kStyle, 1.0);
    ASSERT_EQ(geom.status, RingStatus::Ok);
    EXPECT_DOUBLE_EQ(geom.track.sweep_degrees, -270.0);
    EXPECT_DOUBLE_EQ(geom.value.sweep_degrees, -135.0);
}

// ===========================================================================
// 2. H1-06 kind 分派機制的第一個具體案例
// ===========================================================================

TEST(Dispatch, GaugeKindNowDrawsArcsInsteadOfNeedle) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(install_ring_gauge(table, painter));
    EXPECT_TRUE(table.has_kind(kKindGauge));
    EXPECT_TRUE(table.has_kind(kKindRingGauge));

    const DispatchResult result = table.draw(gauge_draw(135.0, 270.0, 0.5), kCell, paint);
    EXPECT_EQ(result, DispatchResult::Drawn);

    // 軌道弧 + 值弧，而**沒有**任何指針 —— 內建的「圓盤 + 指針」畫法確實被換掉了。
    EXPECT_EQ(static_cast<int>(arcs.spans.size()), 2);
    EXPECT_EQ(arcs.count_role(PaintRole::Track), 1);
    EXPECT_EQ(arcs.count_role(PaintRole::Fill), 1);
    EXPECT_EQ(paint.count(PaintOp::Type::Needle), 0);
    EXPECT_EQ(painter.stats().drawn, 1);
}

TEST(Dispatch, RingGaugeKindAlsoDraws) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    ElementRendererTable table;  // 空表：只有本單元註冊進去的東西
    ASSERT_TRUE(install_ring_gauge(table, painter));

    ElementDraw draw = gauge_draw(135.0, 270.0, 1.0);
    draw.kind = kKindRingGauge;
    EXPECT_EQ(table.draw(draw, kCell, paint), DispatchResult::Drawn);
    EXPECT_EQ(static_cast<int>(arcs.spans.size()), 2);
}

// 重複安裝不得靜默覆寫（`register_kind` 的契約）。
TEST(Dispatch, SecondInstallIsRefused) {
    RecordingArcSink arcs;
    RingGaugePainter painter(arcs);
    ElementRendererTable table = ElementRendererTable::with_builtin_kinds();
    ASSERT_TRUE(install_ring_gauge(table, painter));
    EXPECT_FALSE(install_ring_gauge(table, painter));
}

// NFR-03：該筆不可用 → 軌道弧仍畫、值弧不畫，且計數說得出來。
TEST(Dispatch, UnavailableKeepsTrackDropsValue) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    ElementDraw draw = gauge_draw(135.0, 270.0, 0.75);
    draw.available = false;
    painter.render(draw, kCell, paint);

    EXPECT_EQ(static_cast<int>(arcs.spans.size()), 1);
    EXPECT_EQ(arcs.count_role(PaintRole::Track), 1);
    EXPECT_EQ(arcs.count_role(PaintRole::Fill), 0);
    EXPECT_EQ(painter.stats().degraded, 1);
    EXPECT_EQ(painter.stats().drawn, 0);
}

// 中央讀數畫在環**內緣以內**（不會壓到環本身）。
TEST(Dispatch, LabelGoesInsideTheRing) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    ElementDraw draw = gauge_draw(135.0, 270.0, 0.5);
    draw.show_label = true;
    draw.label = "63%";
    painter.render(draw, kCell, paint);

    ASSERT_EQ(paint.count(PaintOp::Type::Text), 1);
    const PaintBox& box = paint.ops.front().box;
    const RingGeometry geom = compute_ring_geometry(draw, kCell, kStyle, 1.0);
    const double inner = geom.track.inner_radius;
    // 方框的四個角都要落在內緣圓內。
    const double half = static_cast<double>(box.width()) / 2.0;
    EXPECT_GT(half, 0.0);
    EXPECT_LE(std::sqrt(2.0) * half, inner + 1e-9);
}

// ===========================================================================
// 3. 裝置像素對齊（125% / 150% 下細線不得糊成一團）
// ===========================================================================

// 筆寬永遠是整數裝置像素，且細線不會消失。
TEST(DeviceAlignment, StrokeIsAnIntegerNumberOfDevicePixels) {
    EXPECT_EQ(device_stroke_px(6.0, 1.0), 6);
    EXPECT_EQ(device_stroke_px(6.0, 1.25), 8);  // 7.5 → 8，不是 7.5 px 橫跨兩個像素
    EXPECT_EQ(device_stroke_px(6.0, 1.5), 9);
    EXPECT_EQ(device_stroke_px(6.0, 2.0), 12);

    // 細線（1 邏輯像素）在 125% 下仍是 1、150% 下是 2 —— 都是整數，都不是 0。
    EXPECT_EQ(device_stroke_px(1.0, 1.25), 1);
    EXPECT_EQ(device_stroke_px(1.0, 1.5), 2);

    // 壞輸入不得讓線消失（K-002 族：最難發現的失敗是「整條不見了」）。
    EXPECT_EQ(device_stroke_px(0.4, 1.0), 1);
    EXPECT_EQ(device_stroke_px(0.0, 1.0), 1);
    EXPECT_EQ(device_stroke_px(-5.0, 1.0), 1);
    EXPECT_EQ(device_stroke_px(std::numeric_limits<double>::quiet_NaN(), 1.0), 1);
    EXPECT_EQ(device_stroke_px(6.0, std::numeric_limits<double>::quiet_NaN()), 6);
}

// 圓心、內 / 外緣半徑在非整數縮放下仍精確落在裝置像素格上（逐一比對期望值）。
TEST(DeviceAlignment, RingEdgesLandOnPixelBoundaries) {
    struct Case {
        double scale;
        int radius;
        int stroke;
    };
    // kCell = 100x100；inset = round(2 × scale)；side = 100 − 2×inset；radius = side/2。
    const Case cases[] = {
        {1.00, 48, 6},   // inset 2 → side 96
        {1.25, 47, 8},   // inset 3 → side 94、6×1.25 = 7.5 → 8
        {1.50, 47, 9},   // inset 3 → side 94
        {2.00, 46, 12},  // inset 4 → side 92
    };

    for (const Case& c : cases) {
        const RingGeometry geom = compute_ring_geometry(gauge_draw(135.0, 270.0, 0.5), kCell,
                                                        kStyle, c.scale);
        ASSERT_EQ(geom.status, RingStatus::Ok) << "scale=" << c.scale;
        EXPECT_EQ(geom.stroke_device_px, c.stroke) << "scale=" << c.scale;
        EXPECT_DOUBLE_EQ(geom.track.outer_radius, static_cast<double>(c.radius)) << c.scale;
        EXPECT_DOUBLE_EQ(geom.track.inner_radius, static_cast<double>(c.radius - c.stroke))
            << c.scale;
        EXPECT_DOUBLE_EQ(geom.track.center_x, 50.0) << c.scale;
        EXPECT_DOUBLE_EQ(geom.track.center_y, 50.0) << c.scale;

        // 「對齊」的意思：圓心與兩條邊界都是整數裝置像素，像素要嘛整格被環覆蓋、
        // 要嘛整格沒有，不存在半格 —— 這才是 125% / 150% 下細線不糊的充分條件。
        EXPECT_TRUE(is_integral(geom.track.center_x)) << c.scale;
        EXPECT_TRUE(is_integral(geom.track.center_y)) << c.scale;
        EXPECT_TRUE(is_integral(geom.track.outer_radius)) << c.scale;
        EXPECT_TRUE(is_integral(geom.track.inner_radius)) << c.scale;
        EXPECT_DOUBLE_EQ(geom.track.stroke_width(), static_cast<double>(c.stroke)) << c.scale;
        // 值弧與軌道弧共用同一組半徑（兩者不得各自對齊到不同的格）。
        EXPECT_DOUBLE_EQ(geom.value.outer_radius, geom.track.outer_radius) << c.scale;
        EXPECT_DOUBLE_EQ(geom.value.inner_radius, geom.track.inner_radius) << c.scale;
    }
}

// 筆刷型 API 的路徑半徑：筆寬為奇數時落在半像素上，偶數時落在整數上。
TEST(DeviceAlignment, MidRadiusFollowsStrokeParity) {
    const RingGeometry odd = compute_ring_geometry(gauge_draw(135.0, 270.0, 0.5), kCell,
                                                   RingStyle{5.0, 2.0}, 1.0);
    ASSERT_EQ(odd.status, RingStatus::Ok);
    ASSERT_EQ(odd.stroke_device_px % 2, 1);
    EXPECT_DOUBLE_EQ(odd.track.mid_radius() - std::floor(odd.track.mid_radius()), 0.5);

    const RingGeometry even = compute_ring_geometry(gauge_draw(135.0, 270.0, 0.5), kCell,
                                                    RingStyle{6.0, 2.0}, 1.0);
    ASSERT_EQ(even.status, RingStatus::Ok);
    ASSERT_EQ(even.stroke_device_px % 2, 0);
    EXPECT_TRUE(is_integral(even.track.mid_radius()));
}

// ===========================================================================
// 4. 退化弧、值超出範圍、荒謬輸入 —— 不得崩潰，且不得靜默
// ===========================================================================

TEST(Degenerate, ZeroSweepIsReportedAndFallsBackToLinear) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    painter.render(gauge_draw(135.0, 0.0, 0.5), kCell, paint);

    EXPECT_EQ(painter.last_status(), RingStatus::DegenerateArc);
    EXPECT_EQ(painter.stats().degenerate_arc, 1);
    EXPECT_TRUE(arcs.spans.empty());
    // 值沒有靜默消失：退回 H1-06 的線性填充（軌道 + 填充）。
    EXPECT_EQ(paint.count(PaintOp::Type::Fill), 2);
}

TEST(Degenerate, NonFiniteAnglesAreReported) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    EXPECT_EQ(compute_ring_geometry(gauge_draw(135.0, nan, 0.5), kCell, kStyle, 1.0).status,
              RingStatus::DegenerateArc);
    EXPECT_EQ(compute_ring_geometry(gauge_draw(nan, 270.0, 0.5), kCell, kStyle, 1.0).status,
              RingStatus::DegenerateArc);
    EXPECT_EQ(compute_ring_geometry(gauge_draw(135.0, inf, 0.5), kCell, kStyle, 1.0).status,
              RingStatus::DegenerateArc);
    EXPECT_EQ(compute_ring_geometry(gauge_draw(-inf, 270.0, 0.5), kCell, kStyle, 1.0).status,
              RingStatus::DegenerateArc);
}

// NaN 的比例不得畫成「滿的」—— 壞掉的讀數偽裝成 100% 正是 K-002 的形狀。
TEST(Degenerate, NanRatioDrawsNoValueArc) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    painter.render(gauge_draw(135.0, 270.0, std::numeric_limits<double>::quiet_NaN()), kCell,
                   paint);

    EXPECT_EQ(painter.last_status(), RingStatus::Ok);
    EXPECT_EQ(static_cast<int>(arcs.spans.size()), 1);  // 只有軌道
    EXPECT_EQ(arcs.count_role(PaintRole::Fill), 0);
}

TEST(Degenerate, RatioOutOfRangeIsClamped) {
    const RingGeometry over =
        compute_ring_geometry(gauge_draw(135.0, 270.0, 42.0), kCell, kStyle, 1.0);
    EXPECT_DOUBLE_EQ(over.value.sweep_degrees, 270.0);

    const RingGeometry under =
        compute_ring_geometry(gauge_draw(135.0, 270.0, -42.0), kCell, kStyle, 1.0);
    EXPECT_DOUBLE_EQ(under.value.sweep_degrees, 0.0);

    const RingGeometry inf_ratio = compute_ring_geometry(
        gauge_draw(135.0, 270.0, std::numeric_limits<double>::infinity()), kCell, kStyle, 1.0);
    EXPECT_DOUBLE_EQ(inf_ratio.value.sweep_degrees, 270.0);
}

// |sweep| > 360 只會疊畫在自己身上；夾限至一圈，**方向（正負號）保留**。
TEST(Degenerate, OversizedSweepIsClampedKeepingSign) {
    EXPECT_DOUBLE_EQ(
        compute_ring_geometry(gauge_draw(0.0, 1.0e6, 1.0), kCell, kStyle, 1.0).track.sweep_degrees,
        360.0);
    EXPECT_DOUBLE_EQ(
        compute_ring_geometry(gauge_draw(0.0, -1.0e6, 1.0), kCell, kStyle, 1.0).track.sweep_degrees,
        -360.0);
}

TEST(Degenerate, NoAngleFallsBackToLinearAndIsReported) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    ElementDraw draw;
    draw.kind = kKindGauge;
    draw.has_angle = false;  // 上游沒給弧
    draw.fill_ratio = 0.5;
    painter.render(draw, kCell, paint);

    EXPECT_EQ(painter.last_status(), RingStatus::NoArc);
    EXPECT_EQ(painter.stats().no_arc, 1);
    EXPECT_TRUE(arcs.spans.empty());
    EXPECT_EQ(paint.count(PaintOp::Type::Fill), 2);  // 值仍看得見
}

TEST(Degenerate, EmptyAndTinyAreas) {
    RecordingArcSink arcs;
    RecordingPaintSink paint;
    RingGaugePainter painter(arcs);

    const ElementDraw draw = gauge_draw(135.0, 270.0, 0.5);
    painter.render(draw, PaintBox{0, 0, 0, 0}, paint);
    EXPECT_EQ(painter.last_status(), RingStatus::EmptyArea);
    EXPECT_EQ(painter.stats().empty_area, 1);
    EXPECT_TRUE(arcs.spans.empty());
    EXPECT_TRUE(paint.ops.empty());

    // 反向的矩形（right < left）同樣不得產生垃圾幾何。
    painter.render(draw, PaintBox{80, 80, 10, 10}, paint);
    EXPECT_EQ(painter.last_status(), RingStatus::EmptyArea);

    // 小到放不下內縮：捨棄內縮而不是讓環消失。
    const RingGeometry tiny = compute_ring_geometry(draw, PaintBox{0, 0, 3, 3}, kStyle, 1.0);
    ASSERT_EQ(tiny.status, RingStatus::Ok);
    EXPECT_GE(tiny.stroke_device_px, 1);
    EXPECT_GT(tiny.track.outer_radius, 0.0);
    EXPECT_GE(tiny.track.inner_radius, 0.0);
    EXPECT_TRUE(tiny.label_box.empty());  // 放不下讀數就不畫，不硬塞
}

TEST(Degenerate, AbsurdDpiScaleNeverCrashes) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_DOUBLE_EQ(sanitize_dpi_scale(nan), 1.0);
    EXPECT_DOUBLE_EQ(sanitize_dpi_scale(0.0), 1.0);
    EXPECT_DOUBLE_EQ(sanitize_dpi_scale(-2.0), 1.0);
    EXPECT_DOUBLE_EQ(sanitize_dpi_scale(std::numeric_limits<double>::infinity()), 16.0);
    EXPECT_DOUBLE_EQ(sanitize_dpi_scale(1.25), 1.25);

    const ElementDraw draw = gauge_draw(135.0, 270.0, 0.5);
    for (const double scale : {nan, 0.0, -3.0, 1e308, 1e-300}) {
        const RingGeometry geom = compute_ring_geometry(draw, kCell, kStyle, scale);
        ASSERT_EQ(geom.status, RingStatus::Ok) << "scale=" << scale;
        EXPECT_GE(geom.stroke_device_px, 1) << "scale=" << scale;
        EXPECT_TRUE(is_integral(geom.track.outer_radius)) << "scale=" << scale;
        EXPECT_TRUE(is_integral(geom.track.inner_radius)) << "scale=" << scale;
        EXPECT_GE(geom.track.inner_radius, 0.0) << "scale=" << scale;
    }
}

// NaN 必須讓 `visible()` 回 false（fail-closed），不得一路被送進繪製 API。
TEST(Degenerate, ArcSpanWithNanIsNotVisible) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    ArcSpan span;
    span.outer_radius = 10.0;
    span.inner_radius = 4.0;
    span.sweep_degrees = 90.0;
    EXPECT_TRUE(span.visible());

    ArcSpan nan_sweep = span;
    nan_sweep.sweep_degrees = nan;
    EXPECT_FALSE(nan_sweep.visible());

    ArcSpan nan_radius = span;
    nan_radius.outer_radius = nan;
    EXPECT_FALSE(nan_radius.visible());

    ArcSpan inverted = span;
    inverted.inner_radius = 20.0;  // 內緣比外緣大 → 筆寬為負
    EXPECT_FALSE(inverted.visible());
}

// ===========================================================================
// 5. 掃向（H1-06 驗收時裁定「另案」的那一案）
// ===========================================================================

// E4-03 的預設弧在畫面上的實際位置，逐一釘住：0% 左上 → 50% **正下方** → 100% 右上。
// 本測試不是在說這個方向「對」，而是不讓它再悄悄漂走（見 ring_gauge.hpp §4）。
TEST(SweepDirection, E4_03DefaultArcSweepsThroughTheBottom) {
    const RingGeometry geom =
        compute_ring_geometry(gauge_draw(135.0, 270.0, 1.0), kCell, kStyle, 1.0);
    ASSERT_EQ(geom.status, RingStatus::Ok);
    const ArcSpan& arc = geom.track;
    const double cx = arc.center_x;
    const double cy = arc.center_y;

    EXPECT_LT(arc_x(arc, 0.0), cx);  // 0%：左
    EXPECT_LT(arc_y(arc, 0.0), cy);  //      上
    EXPECT_NEAR(arc_x(arc, 0.5), cx, 1e-9);  // 50%：正中央的 x
    EXPECT_GT(arc_y(arc, 0.5), cy);          //       下方
    EXPECT_GT(arc_x(arc, 1.0), cx);  // 100%：右
    EXPECT_LT(arc_y(arc, 1.0), cy);  //        上
}

// 常規掃向（下方左側起、經**上方**、到下方右側）在本層已可用：
// 只要給 start 225° / sweep −270°，一行程式碼都不必改。
TEST(SweepDirection, NegativeSweepGoesOverTheTop) {
    const RingGeometry geom =
        compute_ring_geometry(gauge_draw(225.0, -270.0, 1.0), kCell, kStyle, 1.0);
    ASSERT_EQ(geom.status, RingStatus::Ok);
    const ArcSpan& arc = geom.track;
    const double cx = arc.center_x;
    const double cy = arc.center_y;

    EXPECT_LT(arc_x(arc, 0.0), cx);  // 0%：左下
    EXPECT_GT(arc_y(arc, 0.0), cy);
    EXPECT_NEAR(arc_x(arc, 0.5), cx, 1e-9);  // 50%：正上方
    EXPECT_LT(arc_y(arc, 0.5), cy);
    EXPECT_GT(arc_x(arc, 1.0), cx);  // 100%：右下
    EXPECT_GT(arc_y(arc, 1.0), cy);
}

}  // namespace
