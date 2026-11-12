// H1-06 E4-18 徑向線 / 指針 → 繪製層 — 實作
#include "radial_draw.hpp"

namespace ds::host {
namespace {

void render_needle(const ElementDraw& draw, const PaintBox& area, PaintSink& sink) {
    if (!draw.available) return;  // NFR-03：只少掉這一根指針，不影響其餘列
    const PaintBox dial = dial_box(area);
    if (dial.empty()) return;
    sink.draw_needle(dial, draw.angle_degrees, draw.length_ratio, draw.thickness_ratio,
                     PaintRole::Fill);
}

}  // namespace

ElementDraw project_radial(const ds::elements::RenderModel& model) {
    ElementDraw draw;
    draw.kind = kKindNeedle;
    draw.fill_ratio = model.value_ratio;
    draw.has_angle = true;
    draw.start_angle_degrees = model.start_angle_degrees;
    draw.sweep_degrees = model.sweep_degrees;
    draw.angle_degrees = model.angle_degrees;
    draw.length_ratio = model.length_ratio;
    draw.thickness_ratio = model.thickness_ratio;
    return draw;
}

bool register_radial_kind(ElementRendererTable& table) {
    return table.register_kind(kKindNeedle, &render_needle);
}

}  // namespace ds::host
