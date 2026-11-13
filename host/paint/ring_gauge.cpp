// H1-08 環形量表繪製 — 實作
#include "ring_gauge.hpp"

#include <cmath>

namespace ds::host {
namespace {

// 座標上界。刻意遠小於 INT_MAX（~2.1e9）：之後還有 `cx + radius` 這類加法，
// 貼著 INT_MAX 夾限等於把溢位推遲到下一個算式。
constexpr double kCoordLimit = 1.0e7;

// double → int 的**唯一**入口。
//
// 直接 `static_cast<int>(v)` 在 v 非有限、或超出 int 可表示範圍時是 UB，而
// CI 的 sanitizer 閘門帶 `-fsanitize=float-cast-overflow -fno-sanitize-recover=all`：
// 那不是印個訊息，是當場中止、測試 FAILED。本單元的驗收標準又正好包含
// 「值超出範圍不得崩潰」與非整數 DPI（大量 double→int），故一律先查有限性再夾限。
int to_int_clamped(double v, int fallback) noexcept {
    if (!std::isfinite(v)) return fallback;  // NaN / ±Inf：不猜，回呼叫端指定的保守值
    if (v <= -kCoordLimit) return static_cast<int>(-kCoordLimit);
    if (v >= kCoordLimit) return static_cast<int>(kCoordLimit);
    return static_cast<int>(v);
}

// 四捨五入後轉 int（遠離零）。
int round_to_int(double v, int fallback) noexcept {
    if (!std::isfinite(v)) return fallback;
    return to_int_clamped(v < 0.0 ? v - 0.5 : v + 0.5, fallback);
}

// 內縮（邏輯像素 → 裝置像素，非負整數）。
int device_inset_px(double inset_logical, double scale) noexcept {
    if (!std::isfinite(inset_logical)) return 0;
    const double d = inset_logical * scale;
    if (!(d > 0.0)) return 0;  // 正向比較：NaN 與負數一併落在這裡（K-002）
    const int px = round_to_int(d, 0);
    return px < 0 ? 0 : px;
}

// 環內可安全放讀數的方框：內緣圓的內接正方形（半邊長 = r_inner / √2）。
// 取整數裝置像素；放不下（半邊長 < 1）時回空框，由呼叫端決定不畫。
PaintBox inscribed_label_box(int cx, int cy, int inner_radius) noexcept {
    if (inner_radius <= 0) return PaintBox{};
    const int half = to_int_clamped(static_cast<double>(inner_radius) * 0.70710678118654752, 0);
    if (half < 1) return PaintBox{};
    return PaintBox{cx - half, cy - half, cx + half, cy + half};
}

// 上游沒給弧、或弧退化時的退路：退回 H1-06 內建的線性填充。
//
// 為什麼不是「什麼都不畫」：H1-06 的 `render_gauge` 已立下規矩——
// 「角度缺失不得讓數值靜默消失」。環畫不出來是幾何問題，值本身仍然是有效的讀數，
// 讓它整個從畫面上消失，使用者會以為那筆指標不存在。退化本身由 `RingGaugeStats`
// 與 `last_status()` 記下來，不靜默。
void draw_linear_fallback(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    const PaintBox track = track_strip(area, default_row_metrics().track_half_height);
    if (track.empty()) return;
    sink.fill_box(track, PaintRole::Track);
    if (!draw.available) return;  // NFR-03：軌道仍在，填充不畫
    const PaintBox filled = fill_span(track, draw.fill_ratio, draw.vertical);
    if (!filled.empty()) sink.fill_box(filled, PaintRole::Fill);
}

}  // namespace

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

const char* to_string(RingStatus status) noexcept {
    switch (status) {
        case RingStatus::Ok: return "ok";
        case RingStatus::EmptyArea: return "empty-area";
        case RingStatus::NoArc: return "no-arc";
        case RingStatus::DegenerateArc: return "degenerate-arc";
    }
    return "?";  // 不可達；不寫 default: 是為了讓日後新增狀態時 -Wswitch 提醒這裡
}

bool operator==(const ArcSpan& a, const ArcSpan& b) noexcept {
    return a.center_x == b.center_x && a.center_y == b.center_y &&
           a.outer_radius == b.outer_radius && a.inner_radius == b.inner_radius &&
           a.start_degrees == b.start_degrees && a.sweep_degrees == b.sweep_degrees;
}

bool operator!=(const ArcSpan& a, const ArcSpan& b) noexcept { return !(a == b); }

double sanitize_dpi_scale(double dpi_scale) noexcept {
    // 正向比較：NaN 落在這裡（`!(x > 0.0)`），不會一路被當成合法縮放往下送。
    if (!(dpi_scale > 0.0)) return 1.0;
    if (dpi_scale > 16.0) return 16.0;  // 1600% 已遠超任何真實螢幕；防呆而非功能
    return dpi_scale;
}

int device_stroke_px(double thickness_logical, double dpi_scale) noexcept {
    const double scale = sanitize_dpi_scale(dpi_scale);
    if (!std::isfinite(thickness_logical)) return 1;
    const double d = thickness_logical * scale;
    if (!(d > 0.0)) return 1;  // NaN / ≤0 → 最細的一條，不是「不畫」（見標頭）
    const int px = round_to_int(d, 1);
    return px < 1 ? 1 : px;
}

// ---------------------------------------------------------------------------
// 幾何（純函式）
// ---------------------------------------------------------------------------

RingGeometry compute_ring_geometry(const ElementDraw& draw, const PaintBox& area,
                                   const RingStyle& style, double dpi_scale) {
    RingGeometry out;
    out.dpi_scale = sanitize_dpi_scale(dpi_scale);

    // 置中的正方形錶盤：沿用 H1-06 的 `dial_box()`，不自己再算一次
    // （同一條幾何在兩處各寫一次，就會在兩處各錯一次）。
    const PaintBox dial = dial_box(area);
    if (dial.empty()) return out;  // status 預設即 EmptyArea

    int inset = device_inset_px(style.inset_logical, out.dpi_scale);
    int side = dial.width() - 2 * inset;
    if (side < 2) {
        // 內縮把環吃光了（格位本來就很小）→ 捨棄內縮，而不是讓環消失。
        inset = 0;
        side = dial.width();
    }
    const int radius = side / 2;
    if (radius < 1) return out;  // EmptyArea

    int stroke = device_stroke_px(style.thickness_logical, out.dpi_scale);
    if (stroke > radius) stroke = radius;  // 最厚就是實心圓（內緣半徑 0）

    // 圓心與兩個半徑**都是整數裝置像素** → 環的內 / 外緣落在像素邊界上。
    // 這就是 125% / 150% 下細線不糊的關鍵：縮放只影響筆寬該取哪個整數，
    // 不會讓邊界停在半個像素上。
    const int cx = dial.left + inset + radius;
    const int cy = dial.top + inset + radius;
    out.stroke_device_px = stroke;
    out.bounds = PaintBox{cx - radius, cy - radius, cx + radius, cy + radius};
    out.label_box = inscribed_label_box(cx, cy, radius - stroke);

    if (!draw.has_angle) {
        out.status = RingStatus::NoArc;
        return out;
    }
    if (!std::isfinite(draw.start_angle_degrees) || !std::isfinite(draw.sweep_degrees) ||
        draw.sweep_degrees == 0.0) {
        out.status = RingStatus::DegenerateArc;
        return out;
    }

    // sweep 夾限至 ±360°：超過一圈的部分只會疊畫在自己身上，而把 1e300 這種值原樣
    // 送進三角函式只會得到垃圾。夾限保留**正負號**（方向是上游的語意，不是本層的）。
    double sweep = draw.sweep_degrees;
    if (sweep > 360.0) sweep = 360.0;
    if (sweep < -360.0) sweep = -360.0;

    ArcSpan span;
    span.center_x = static_cast<double>(cx);
    span.center_y = static_cast<double>(cy);
    span.outer_radius = static_cast<double>(radius);
    span.inner_radius = static_cast<double>(radius - stroke);
    // 起始角取等價的 (-360, 360) 代表值：|start| < 360 時原封不動（E4-03 的預設 135° 即是），
    // 只有荒謬的大數才會被收斂——那是為了讓下游的三角函式還算得出東西，不是改方向。
    span.start_degrees = std::fmod(draw.start_angle_degrees, 360.0);
    span.sweep_degrees = sweep;

    out.track = span;
    out.value = span;
    // `clamp_unit_ratio` 是 H1-06 既有的、繪製層所有比例夾限的唯一來源（NaN → 0，K-002）。
    out.value.sweep_degrees = sweep * clamp_unit_ratio(draw.fill_ratio);
    out.status = RingStatus::Ok;
    return out;
}

// ---------------------------------------------------------------------------
// RingGaugePainter
// ---------------------------------------------------------------------------

RingGaugePainter::RingGaugePainter(ArcSink& arcs, RingStyle style, double dpi_scale)
    : arcs_(&arcs), style_(style), dpi_scale_(sanitize_dpi_scale(dpi_scale)) {}

void RingGaugePainter::set_dpi_scale(double dpi_scale) noexcept {
    dpi_scale_ = sanitize_dpi_scale(dpi_scale);
}

void RingGaugePainter::render(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    const RingGeometry geom = compute_ring_geometry(draw, area, style_, dpi_scale_);
    last_status_ = geom.status;

    switch (geom.status) {
        case RingStatus::EmptyArea:
            ++stats_.empty_area;
            return;  // 沒有可畫面積：連退路都沒有
        case RingStatus::NoArc:
            ++stats_.no_arc;
            draw_linear_fallback(draw, area, sink);
            return;
        case RingStatus::DegenerateArc:
            ++stats_.degenerate_arc;
            draw_linear_fallback(draw, area, sink);
            return;
        case RingStatus::Ok:
            break;
    }

    // 軌道弧一律畫：使用者得知道這個量表存在（與 H1-06 `render_gauge` 的錶盤同理）。
    if (geom.track.visible()) arcs_->draw_arc(geom.track, PaintRole::Track);

    if (draw.available) {
        // 值弧。ratio 為 0 時 sweep 為 0 → `visible()` 為 false → 不下指令
        // （畫一段零長度的弧，在筆刷型 API 上會變成一個端點小點）。
        if (geom.value.visible()) arcs_->draw_arc(geom.value, PaintRole::Fill);
        ++stats_.drawn;
    } else {
        ++stats_.degraded;  // NFR-03：只灰掉這一筆——軌道仍在，值弧不畫
    }

    // 中央讀數（E4-03 的 `show_label` / `label`，進度式的 "63%" 即走這裡）。
    if (draw.show_label && !draw.label.empty() && !geom.label_box.empty()) {
        sink.draw_text(geom.label_box, draw.label,
                       draw.available ? PaintRole::Value : PaintRole::Unavailable,
                       TextAlign::Center);
    }
}

// ---------------------------------------------------------------------------
// 接進 H1-06 的分派表
// ---------------------------------------------------------------------------

ElementRenderer make_ring_gauge_renderer(RingGaugePainter& painter) {
    // K-013：以**指標**捕獲。`std::function` 會被存進分派表而活得比這個敘述久，
    // 以 const 參考成員持有一個按值傳進來的東西正是那條懸空參考的形狀。
    RingGaugePainter* self = &painter;
    return [self](const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
        self->render(draw, area, sink);
    };
}

bool install_ring_gauge(ElementRendererTable& table, RingGaugePainter& painter) {
    const ElementRenderer renderer = make_ring_gauge_renderer(painter);
    // 覆寫內建 gauge（明示）＋ 另註冊具名鍵。兩個呼叫都要做，回傳值才能說「首次安裝成功」。
    const bool replaced = table.replace_kind(kKindGauge, renderer);
    const bool registered = table.register_kind(kKindRingGauge, renderer);
    return replaced && registered;
}

}  // namespace ds::host
