// E4-01 文字排版與渲染 — 實作
//
// 純邏輯排版；經注入式 FontMetrics 取得度量，無任何真實字型引擎 / 平台分支。
#include "text_layout.hpp"

#include <cmath>      // std::isfinite
#include <stdexcept>  // std::invalid_argument
#include <utility>

namespace ds::render {

// ---------------------------------------------------------------------------
// FixedFontMetrics
// ---------------------------------------------------------------------------
FixedFontMetrics::FixedFontMetrics(double advance_per_char, double line_height,
                                   double ascent)
    : advance_(advance_per_char), line_height_(line_height), ascent_(ascent) {
    if (!std::isfinite(advance_per_char) || advance_per_char < 0.0) {
        throw std::invalid_argument("FixedFontMetrics: advance 必須為有限非負值");
    }
    if (!std::isfinite(line_height) || line_height <= 0.0) {
        throw std::invalid_argument("FixedFontMetrics: line_height 必須為有限正值");
    }
    if (ascent < 0.0) {
        ascent_ = line_height;  // 未指定 → 採用行高
    } else if (!std::isfinite(ascent)) {
        throw std::invalid_argument("FixedFontMetrics: ascent 必須為有限值");
    }
}

// ---------------------------------------------------------------------------
// UTF-8 解碼（非法即擲例外，不以替代字元靜默吞掉）
// ---------------------------------------------------------------------------
std::vector<CodePoint> decode_utf8(const std::string& text) {
    std::vector<CodePoint> out;
    out.reserve(text.size());
    const auto n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t len = 0;
        std::uint32_t min_cp = 0;  // 該長度的最小合法碼位（用以擋過長編碼）
        if (b0 < 0x80) {
            cp = b0;
            len = 1;
            min_cp = 0x0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1Fu;
            len = 2;
            min_cp = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0Fu;
            len = 3;
            min_cp = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07u;
            len = 4;
            min_cp = 0x10000;
        } else {
            throw std::invalid_argument("decode_utf8: 非法起始位元組");
        }
        if (i + len > n) {
            throw std::invalid_argument("decode_utf8: 序列被截斷");
        }
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char bk = static_cast<unsigned char>(text[i + k]);
            if ((bk & 0xC0) != 0x80) {
                throw std::invalid_argument("decode_utf8: 非法續接位元組");
            }
            cp = (cp << 6) | (bk & 0x3Fu);
        }
        if (cp < min_cp) {
            throw std::invalid_argument("decode_utf8: 過長編碼(overlong)");
        }
        if (cp > 0x10FFFFu) {
            throw std::invalid_argument("decode_utf8: 碼位越界");
        }
        if (cp >= 0xD800u && cp <= 0xDFFFu) {
            throw std::invalid_argument("decode_utf8: 代理區碼位非法");
        }
        out.push_back(static_cast<CodePoint>(cp));
        i += len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// TextLayout
// ---------------------------------------------------------------------------
TextLayout::TextLayout(const FontMetrics& metrics, ds::kernel::SurfaceId surface)
    : metrics_(metrics), surface_(std::move(surface)) {}

namespace {

constexpr CodePoint kNewline = 0x0A;  // '\n'
constexpr CodePoint kReturn = 0x0D;   // '\r'（正規化時略去）
constexpr CodePoint kSpace = 0x20;    // ' '
constexpr CodePoint kTab = 0x09;      // '\t'

bool is_space(CodePoint cp) { return cp == kSpace || cp == kTab; }

// 排版期間的中繼行（尚未定 x / y / 對齊）。
struct RawLine {
    std::vector<CodePoint> cps;      // 本行碼位
    std::vector<double> advances;    // 對應 advance
    double width = 0.0;              // 內容寬度（advance 總和）
    bool ellipsized = false;
};

// 取得並驗證度量的 advance（非有限 / 負值 → 擲例外）。
double checked_advance(const FontMetrics& m, CodePoint cp) {
    const double a = m.advance(cp);
    if (!std::isfinite(a) || a < 0.0) {
        throw std::invalid_argument("TextLayout: FontMetrics::advance 回傳非有限或負值");
    }
    return a;
}

}  // namespace

Size TextLayout::measure(const std::string& text,
                         const LayoutConstraints& constraints) const {
    return layout(text, constraints).size;
}

LayoutResult TextLayout::layout(const std::string& text,
                                const LayoutConstraints& constraints) const {
    LayoutResult result;
    result.surface = surface_;

    // --- 約束驗證（不靜默）---
    if (constraints.line_height != 0.0 && !std::isfinite(constraints.line_height)) {
        throw std::invalid_argument("TextLayout: constraints.line_height 非有限");
    }
    if (std::isnan(constraints.max_width)) {
        throw std::invalid_argument("TextLayout: constraints.max_width 為 NaN");
    }

    // 行高：覆寫 > 0 則採用，否則採度量行高（並驗證度量行高有效）。
    double line_height = constraints.line_height;
    if (line_height <= 0.0) {
        line_height = metrics_.line_height();
        if (!std::isfinite(line_height) || line_height <= 0.0) {
            throw std::invalid_argument("TextLayout: FontMetrics::line_height 非有限正值");
        }
    }
    double ascent = metrics_.ascent();
    if (!std::isfinite(ascent) || ascent < 0.0) {
        throw std::invalid_argument("TextLayout: FontMetrics::ascent 非有限或負值");
    }

    // 換行寬度是否有界（正有限）。
    const bool bounded =
        std::isfinite(constraints.max_width) && constraints.max_width > 0.0;
    const double max_width = bounded ? constraints.max_width : 0.0;
    const bool do_word_wrap = bounded && constraints.wrap == WrapMode::Word;

    // 空字串 → 空結果（0 行、size{0,0}）。
    const std::vector<CodePoint> cps = decode_utf8(text);
    if (cps.empty()) {
        return result;
    }

    // --- 1. 依硬換行 '\n' 切段（略去 '\r'）---
    std::vector<std::vector<CodePoint>> paragraphs;
    paragraphs.emplace_back();
    for (CodePoint cp : cps) {
        if (cp == kReturn) {
            continue;  // 正規化：丟棄 CR
        }
        if (cp == kNewline) {
            paragraphs.emplace_back();
        } else {
            paragraphs.back().push_back(cp);
        }
    }

    // --- 2. 逐段換行成 RawLine ---
    std::vector<RawLine> raw;
    auto append_run = [&](RawLine& line, const std::vector<CodePoint>& run,
                          const std::vector<double>& run_adv, double run_w) {
        for (std::size_t k = 0; k < run.size(); ++k) {
            line.cps.push_back(run[k]);
            line.advances.push_back(run_adv[k]);
        }
        line.width += run_w;
    };

    for (const auto& para : paragraphs) {
        if (!do_word_wrap) {
            // 不換行：整段為一行（硬換行已切段）。
            RawLine line;
            for (CodePoint cp : para) {
                const double a = checked_advance(metrics_, cp);
                line.cps.push_back(cp);
                line.advances.push_back(a);
                line.width += a;
            }
            raw.push_back(std::move(line));
            continue;
        }

        // 詞界換行：貪婪法。空白 run 只在其後接續單詞時才併入行（行尾空白丟棄）。
        RawLine current;
        std::vector<CodePoint> pending_sp;   // 尚未落地的空白 run
        std::vector<double> pending_sp_adv;
        double pending_sp_w = 0.0;

        std::size_t idx = 0;
        const std::size_t plen = para.size();
        while (idx < plen) {
            const bool space_run = is_space(para[idx]);
            std::vector<CodePoint> run;
            std::vector<double> run_adv;
            double run_w = 0.0;
            while (idx < plen && is_space(para[idx]) == space_run) {
                const double a = checked_advance(metrics_, para[idx]);
                run.push_back(para[idx]);
                run_adv.push_back(a);
                run_w += a;
                ++idx;
            }
            if (space_run) {
                // 暫存空白，待下一個單詞決定是否落地。
                for (std::size_t k = 0; k < run.size(); ++k) {
                    pending_sp.push_back(run[k]);
                    pending_sp_adv.push_back(run_adv[k]);
                }
                pending_sp_w += run_w;
                continue;
            }
            // 單詞：判斷是否需換行。
            const bool line_has_content = !current.cps.empty();
            if (line_has_content &&
                current.width + pending_sp_w + run_w > max_width) {
                // 換行：丟棄行尾待落地空白，單詞起新行。
                raw.push_back(std::move(current));
                current = RawLine{};
                pending_sp.clear();
                pending_sp_adv.clear();
                pending_sp_w = 0.0;
                append_run(current, run, run_adv, run_w);
            } else {
                // 併入：先落地待處理空白，再放單詞。
                if (!pending_sp.empty()) {
                    append_run(current, pending_sp, pending_sp_adv, pending_sp_w);
                    pending_sp.clear();
                    pending_sp_adv.clear();
                    pending_sp_w = 0.0;
                }
                append_run(current, run, run_adv, run_w);
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

    // --- 4. 省略號處理 ---
    // (a) no-wrap（或無界）下過寬的行：逐行裁切至 max_width - 省略寬 後加省略字元。
    // (b) 因 max_lines 裁掉尾段：末行加省略字元（預算為 max_width 有界時的寬度，否則無界直接加）。
    if (constraints.ellipsis) {
        const double ell_adv = checked_advance(metrics_, constraints.ellipsis_char);
        auto apply_ellipsis = [&](RawLine& line, bool have_budget, double budget) {
            // 依預算移除行尾字符，直到省略字元放得下（無預算則直接附加）。
            while (have_budget && !line.cps.empty() &&
                   line.width + ell_adv > budget) {
                line.width -= line.advances.back();
                line.cps.pop_back();
                line.advances.pop_back();
            }
            line.cps.push_back(constraints.ellipsis_char);
            line.advances.push_back(ell_adv);
            line.width += ell_adv;
            line.ellipsized = true;
        };

        if (bounded) {
            // (a) 任何過寬行都裁切加省略。
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

    // --- 6. 攤平為 LineBox + Glyph（相對佈局，NFR-02）---
    result.lines.reserve(raw.size());
    for (std::size_t li = 0; li < raw.size(); ++li) {
        const RawLine& rl = raw[li];
        double x_off = 0.0;
        switch (constraints.align) {
            case TextAlign::Left:
                x_off = 0.0;
                break;
            case TextAlign::Center:
                x_off = (align_width - rl.width) / 2.0;
                break;
            case TextAlign::Right:
                x_off = align_width - rl.width;
                break;
        }
        if (x_off < 0.0) {
            x_off = 0.0;  // 行比盒寬（過長單詞溢出）時不產生負偏移
        }

        LineBox box;
        box.begin = result.glyphs.size();
        box.count = rl.cps.size();
        box.x = x_off;
        box.y = static_cast<double>(li) * line_height;
        box.width = rl.width;
        box.baseline = box.y + ascent;
        box.ellipsized = rl.ellipsized;

        double pen = x_off;
        for (std::size_t k = 0; k < rl.cps.size(); ++k) {
            Glyph g;
            g.codepoint = rl.cps[k];
            g.x = pen;
            g.advance = rl.advances[k];
            g.line = li;
            result.glyphs.push_back(g);
            pen += rl.advances[k];
        }
        result.lines.push_back(box);
    }

    result.size.width = align_width;
    result.size.height = static_cast<double>(raw.size()) * line_height;
    return result;
}

}  // namespace ds::render
