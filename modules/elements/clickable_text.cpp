// E4-12 可點文字區段 — 實作
//
// 純邏輯：委派 E4-01 排版、E1-04 幾何命中，無任何真實輸入裝置 / 繪製後端 / 平台分支。
#include "clickable_text.hpp"

#include <stdexcept>
#include <utility>

namespace ds::elements {

namespace {

// 確定性字元→字符映射僅在 wrap=None 且 ellipsis=false 時成立（見標頭檔頭說明）。
void validate_constraints(const ds::render::LayoutConstraints& c) {
    if (c.wrap != ds::render::WrapMode::None) {
        throw std::invalid_argument(
            "ClickableTextElement: constraints.wrap 必須為 WrapMode::None"
            "（Word 換行會丟棄待落地空白，破壞字元→字符確定映射）");
    }
    if (c.ellipsis) {
        throw std::invalid_argument(
            "ClickableTextElement: constraints.ellipsis 必須為 false"
            "（省略號裁切會移除字元，破壞字元→字符確定映射）");
    }
}

}  // namespace

ClickableTextElement::ClickableTextElement(const ds::render::FontMetrics& metrics,
                                            ds::render::LayoutConstraints constraints,
                                            ds::kernel::SurfaceId surface)
    : layout_(metrics, std::move(surface)),
      constraints_(constraints),
      metrics_(metrics) {
    validate_constraints(constraints_);
    set_text("");  // 初始化 model_ / char_to_glyph_ 為「空文字」一致狀態（含 surface 綁定）。
}

void ClickableTextElement::set_text(const std::string& utf8_text) {
    // decode_utf8 驗證合法性（非法 UTF-8 → std::invalid_argument，不靜默）。
    const std::vector<ds::render::CodePoint> cps = ds::render::decode_utf8(utf8_text);

    // 委派 E4-01 排版；constraints_ 已於建構時驗證 wrap=None / ellipsis=false。
    ds::render::LayoutResult new_model = layout_.layout(utf8_text, constraints_);

    // 建立「原始碼位索引 → model glyph 索引」映射：'\r' 與硬換行 '\n' 本身不產生字符，
    // 其餘碼位依序恰對應排版輸出中一個字符（見標頭檔頭「確定性映射」說明）。
    std::vector<std::size_t> mapping(cps.size(), kNoGlyph);
    std::size_t gi = 0;
    for (std::size_t i = 0; i < cps.size(); ++i) {
        const ds::render::CodePoint cp = cps[i];
        if (cp == static_cast<ds::render::CodePoint>('\r') ||
            cp == static_cast<ds::render::CodePoint>('\n')) {
            continue;  // 無字符幾何。
        }
        mapping[i] = gi++;
    }
    if (gi != new_model.glyphs.size()) {
        // 不應發生（建構時已排除已知會破壞映射的約束組合）；報錯不靜默優於悄悄回錯位置。
        throw std::logic_error(
            "ClickableTextElement: 字元→字符映射與排版輸出字符數不一致");
    }

    // 全部驗證通過後才落地狀態變更（strong exception guarantee）。
    model_ = std::move(new_model);
    char_to_glyph_ = std::move(mapping);
    spans_.clear();  // 新文字下舊字元範圍語意不明，不予保留。
}

void ClickableTextElement::add_span(std::size_t start, std::size_t end, std::string id) {
    if (id.empty()) {
        throw std::invalid_argument("ClickableTextElement::add_span: id 不得為空");
    }
    if (start >= end) {
        throw std::invalid_argument(
            "ClickableTextElement::add_span: 字元範圍須滿足 start < end（不得空/ 反轉）");
    }
    if (end > char_to_glyph_.size()) {
        throw std::invalid_argument(
            "ClickableTextElement::add_span: 字元範圍越界（end 超過目前文字長度）");
    }
    for (const auto& existing : spans_) {
        const bool overlap = start < existing.end && existing.start < end;
        if (overlap) {
            throw std::invalid_argument(
                "ClickableTextElement::add_span: 字元範圍與既有區段重疊，語意不明");
        }
    }
    spans_.push_back(Span{start, end, std::move(id)});
}

void ClickableTextElement::clear_spans() noexcept { spans_.clear(); }

double ClickableTextElement::effective_line_height() const {
    if (constraints_.line_height > 0.0) {
        return constraints_.line_height;
    }
    return metrics_.line_height();
}

std::optional<std::string> ClickableTextElement::hit_span(
    const ds::kernel::LocalPoint& point) const {
    const double line_h = effective_line_height();
    for (const auto& span : spans_) {
        for (std::size_t ci = span.start; ci < span.end; ++ci) {
            const std::size_t gi = char_to_glyph_[ci];
            if (gi == kNoGlyph) {
                continue;  // 此碼位（'\r'/'\n'）無字符幾何，不參與命中判定。
            }
            const ds::render::Glyph& g = model_.glyphs[gi];
            const ds::render::LineBox& line = model_.lines[g.line];

            // 將本地點平移到「此字符矩形自身的本地座標系」（原點 = 字符左上角）。
            const ds::kernel::LocalPoint local{
                static_cast<float>(point.x - g.x),
                static_cast<float>(point.y - line.y),
            };
            const ds::kernel::Shape cell =
                ds::kernel::make_rect(static_cast<float>(g.advance), static_cast<float>(line_h));
            const ds::kernel::HitResult hr = hit_tester_.hit_test(local, cell);
            if (hr.status == ds::kernel::HitStatus::Ok && hr.inside) {
                return span.id;
            }
        }
    }
    return std::nullopt;
}

}  // namespace ds::elements
