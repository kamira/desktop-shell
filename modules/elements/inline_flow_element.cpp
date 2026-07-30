// E4-13 文字流內嵌物件 — 實作（見 inline_flow_element.hpp 規格）。
//
// 排版邏輯改寫自 E4-01 `TextLayout::layout` 的貪婪詞界換行演算法（同一套斷行/省略規則），
// 泛化到一個「文字字符 / 空白 / 硬換行 / 內嵌物件」混合 token 序列上，使內嵌物件在換行判斷
// 上等價於一個不可拆的字。純邏輯，無任何真實字型/影像引擎、無平台分支。
#include "inline_flow_element.hpp"

#include <cmath>      // std::isfinite, std::isnan
#include <stdexcept>  // std::invalid_argument
#include <utility>

namespace ds::elements {

namespace {

constexpr ds::render::CodePoint kNewline = 0x0A;  // '\n'
constexpr ds::render::CodePoint kReturn = 0x0D;   // '\r'（正規化時略去）
constexpr ds::render::CodePoint kSpace = 0x20;    // ' '
constexpr ds::render::CodePoint kTab = 0x09;      // '\t'

bool is_space_cp(ds::render::CodePoint cp) { return cp == kSpace || cp == kTab; }

// 取得並驗證度量的 advance（非有限 / 負值 → 擲例外，同 E4-01 慣例）。
double checked_advance(const ds::render::FontMetrics& m, ds::render::CodePoint cp) {
    const double a = m.advance(cp);
    if (!std::isfinite(a) || a < 0.0) {
        throw std::invalid_argument(
            "InlineFlowElement: FontMetrics::advance 回傳非有限或負值");
    }
    return a;
}

}  // namespace

// ---------------------------------------------------------------------------
// 建構 / 基本存取
// ---------------------------------------------------------------------------
InlineFlowElement::InlineFlowElement(const ds::render::FontMetrics& metrics,
                                     ds::kernel::SurfaceId surface)
    : metrics_(metrics), surface_(std::move(surface)) {}

// ---------------------------------------------------------------------------
// 附加內容
// ---------------------------------------------------------------------------
InlineFlowStatus InlineFlowElement::add_text(const std::string& text) {
    // 非法 UTF-8 → std::invalid_argument（重用 E4-01 decode_utf8，不靜默吞掉）。
    const std::vector<ds::render::CodePoint> cps = ds::render::decode_utf8(text);
    for (ds::render::CodePoint cp : cps) {
        if (cp == kReturn) {
            continue;  // 正規化：丟棄 CR（同 E4-01）
        }
        FlowToken tok;
        if (cp == kNewline) {
            tok.kind = TokenKind::NewLine;
        } else if (is_space_cp(cp)) {
            tok.kind = TokenKind::Space;
            tok.cp = cp;
            tok.advance = checked_advance(metrics_, cp);
        } else {
            tok.kind = TokenKind::Char;
            tok.cp = cp;
            tok.advance = checked_advance(metrics_, cp);
        }
        tokens_.push_back(tok);
    }
    return InlineFlowStatus::Ok;
}

InlineFlowStatus InlineFlowElement::add_inline_object(const ImageRenderModel& image,
                                                      ds::render::Size size) {
    if (!image.has_source) {
        return InlineFlowStatus::Invalid;  // 無效物件（無來源）不套用，不加入
    }
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width <= 0.0 ||
        size.height <= 0.0) {
        return InlineFlowStatus::Invalid;  // 非法尺寸（非正 / 非有限）不套用，不加入
    }

    const std::size_t idx = objects_.size();
    objects_.push_back(image);
    object_sizes_.push_back(size);

    FlowToken tok;
    tok.kind = TokenKind::Object;
    tok.advance = size.width;  // 換行判斷時，物件寬度即其排版佔用寬度
    tok.object_index = idx;
    tokens_.push_back(tok);
    ++object_count_;
    return InlineFlowStatus::Ok;
}

void InlineFlowElement::clear() noexcept {
    tokens_.clear();
    objects_.clear();
    object_sizes_.clear();
    object_count_ = 0;
}

// ---------------------------------------------------------------------------
// render_model —— 排版（重用 E4-01 換行/對齊/省略規則，泛化至混合 token 序列）
// ---------------------------------------------------------------------------
InlineFlowResult InlineFlowElement::render_model(
    const ds::render::LayoutConstraints& constraints) const {
    InlineFlowResult result;
    result.surface = surface_;

    // --- 約束驗證（不靜默，同 E4-01 規則）---
    if (constraints.line_height != 0.0 && !std::isfinite(constraints.line_height)) {
        throw std::invalid_argument("InlineFlowElement: constraints.line_height 非有限");
    }
    if (std::isnan(constraints.max_width)) {
        throw std::invalid_argument("InlineFlowElement: constraints.max_width 為 NaN");
    }

    // 行高：覆寫 > 0 則採用，否則採度量行高（並驗證度量行高有效）。
    double base_line_height = constraints.line_height;
    if (base_line_height <= 0.0) {
        base_line_height = metrics_.line_height();
        if (!std::isfinite(base_line_height) || base_line_height <= 0.0) {
            throw std::invalid_argument(
                "InlineFlowElement: FontMetrics::line_height 非有限正值");
        }
    }
    const double ascent = metrics_.ascent();
    if (!std::isfinite(ascent) || ascent < 0.0) {
        throw std::invalid_argument("InlineFlowElement: FontMetrics::ascent 非有限或負值");
    }

    // 換行寬度是否有界（正有限）。
    const bool bounded =
        std::isfinite(constraints.max_width) && constraints.max_width > 0.0;
    const double max_width = bounded ? constraints.max_width : 0.0;
    const bool do_word_wrap = bounded && constraints.wrap == ds::render::WrapMode::Word;

    // 空流 → 空結果（0 行、size{0,0}）。
    if (tokens_.empty()) {
        return result;
    }

    // --- 1. 依硬換行切段 ---
    std::vector<std::vector<FlowToken>> paragraphs;
    paragraphs.emplace_back();
    for (const FlowToken& t : tokens_) {
        if (t.kind == TokenKind::NewLine) {
            paragraphs.emplace_back();
        } else {
            paragraphs.back().push_back(t);
        }
    }

    auto is_space_tok = [](const FlowToken& t) { return t.kind == TokenKind::Space; };

    struct RawLine {
        std::vector<FlowToken> toks;
        double width = 0.0;
        bool ellipsized = false;
    };

    auto append_run = [](RawLine& line, const std::vector<FlowToken>& run, double run_w) {
        for (const FlowToken& t : run) {
            line.toks.push_back(t);
        }
        line.width += run_w;
    };

    // --- 2. 逐段換行成 RawLine（貪婪詞界換行；內嵌物件等價於一個不可拆的字）---
    std::vector<RawLine> raw;
    for (const auto& para : paragraphs) {
        if (!do_word_wrap) {
            // 不換行：整段為一行（硬換行已切段）。
            RawLine line;
            for (const FlowToken& t : para) {
                line.toks.push_back(t);
                line.width += t.advance;
            }
            raw.push_back(std::move(line));
            continue;
        }

        RawLine current;
        std::vector<FlowToken> pending_sp;  // 尚未落地的空白 run
        double pending_sp_w = 0.0;

        std::size_t idx = 0;
        const std::size_t plen = para.size();
        while (idx < plen) {
            const bool space_run = is_space_tok(para[idx]);
            std::vector<FlowToken> run;
            double run_w = 0.0;
            while (idx < plen && is_space_tok(para[idx]) == space_run) {
                run.push_back(para[idx]);
                run_w += para[idx].advance;
                ++idx;
            }
            if (space_run) {
                for (const FlowToken& t : run) {
                    pending_sp.push_back(t);
                }
                pending_sp_w += run_w;
                continue;
            }
            // 非空白 run（字符 / 內嵌物件混合的一「詞」）：判斷是否需換行。
            const bool line_has_content = !current.toks.empty();
            if (line_has_content && current.width + pending_sp_w + run_w > max_width) {
                // 換行：丟棄行尾待落地空白，本 run 起新行。
                raw.push_back(std::move(current));
                current = RawLine{};
                pending_sp.clear();
                pending_sp_w = 0.0;
                append_run(current, run, run_w);
            } else {
                // 併入：先落地待處理空白，再放本 run。
                if (!pending_sp.empty()) {
                    append_run(current, pending_sp, pending_sp_w);
                    pending_sp.clear();
                    pending_sp_w = 0.0;
                }
                append_run(current, run, run_w);
            }
        }
        raw.push_back(std::move(current));  // 段落末行（含空段落 → 空行）
    }

    // --- 3. 最大行數裁切（超出即 truncated；末行視需要加省略號）---
    bool need_ellipsis_lastline = false;
    if (constraints.max_lines > 0 && raw.size() > constraints.max_lines) {
        raw.resize(constraints.max_lines);
        result.truncated = true;
        need_ellipsis_lastline = constraints.ellipsis;
    }

    // --- 4. 省略號處理（同 E4-01：省略字元一律為文字 Char token；裁切對象可為字符或物件，
    //         皆以其 advance 一視同仁地從行尾移除，直到省略字元放得下）---
    if (constraints.ellipsis) {
        const double ell_adv = checked_advance(metrics_, constraints.ellipsis_char);
        auto apply_ellipsis = [&](RawLine& line, bool have_budget, double budget) {
            while (have_budget && !line.toks.empty() && line.width + ell_adv > budget) {
                line.width -= line.toks.back().advance;
                line.toks.pop_back();
            }
            FlowToken ell;
            ell.kind = TokenKind::Char;
            ell.cp = constraints.ellipsis_char;
            ell.advance = ell_adv;
            line.toks.push_back(ell);
            line.width += ell_adv;
            line.ellipsized = true;
        };

        if (bounded) {
            // (a) 任何過寬行（含因內嵌物件過寬）都裁切加省略。
            for (auto& line : raw) {
                if (line.width > max_width) {
                    apply_ellipsis(line, /*have_budget=*/true, max_width);
                    result.truncated = true;
                }
            }
        }
        // (b) max_lines 造成的末行省略（若尚未因過寬而 ellipsized）。
        if (need_ellipsis_lastline && !raw.empty() && !raw.back().ellipsized) {
            apply_ellipsis(raw.back(), /*have_budget=*/bounded, max_width);
        }
    }

    // --- 5. 對齊寬度 = 有界時盒寬，否則最寬行寬 ---
    double content_max = 0.0;
    for (const auto& line : raw) {
        if (line.width > content_max) {
            content_max = line.width;
        }
    }
    const double align_width = bounded ? max_width : content_max;

    // --- 6. 每行有效行高（基礎行高 vs 本行內嵌物件最大高度）---
    std::vector<double> line_heights(raw.size(), base_line_height);
    for (std::size_t li = 0; li < raw.size(); ++li) {
        for (const FlowToken& t : raw[li].toks) {
            if (t.kind == TokenKind::Object) {
                const double oh = object_sizes_[t.object_index].height;
                if (oh > line_heights[li]) {
                    line_heights[li] = oh;
                }
            }
        }
    }

    // --- 7. 攤平為 FlowLine + FlowGlyph/FlowObject（相對佈局，NFR-02）---
    double y_cursor = 0.0;
    result.lines.reserve(raw.size());
    for (std::size_t li = 0; li < raw.size(); ++li) {
        const RawLine& rl = raw[li];
        double x_off = 0.0;
        switch (constraints.align) {
            case ds::render::TextAlign::Left:
                x_off = 0.0;
                break;
            case ds::render::TextAlign::Center:
                x_off = (align_width - rl.width) / 2.0;
                break;
            case ds::render::TextAlign::Right:
                x_off = align_width - rl.width;
                break;
        }
        if (x_off < 0.0) {
            x_off = 0.0;  // 行比盒寬（過長內容溢出）時不產生負偏移
        }

        FlowLine box;
        box.x = x_off;
        box.y = y_cursor;
        box.width = rl.width;
        box.height = line_heights[li];
        box.baseline = box.y + ascent;
        box.ellipsized = rl.ellipsized;

        double pen = x_off;
        for (const FlowToken& t : rl.toks) {
            if (t.kind == TokenKind::Object) {
                FlowObject fo;
                fo.image = objects_[t.object_index];
                fo.x = pen;
                fo.width = object_sizes_[t.object_index].width;
                fo.height = object_sizes_[t.object_index].height;
                fo.line = li;
                result.objects.push_back(fo);
            } else {
                FlowGlyph g;
                g.codepoint = t.cp;
                g.x = pen;
                g.advance = t.advance;
                g.line = li;
                result.glyphs.push_back(g);
            }
            pen += t.advance;
        }
        result.lines.push_back(box);
        y_cursor += line_heights[li];
    }

    result.size.width = align_width;
    result.size.height = y_cursor;
    return result;
}

}  // namespace ds::elements
