// E4-11 逐字顯示 — 實作
//
// 純邏輯：以注入式 tick 推進「已顯示碼位數」，並委由 E4-01 對已顯示前綴子字串排版。
// 不含任何平台分支或真實計時器；不新增絕對座標 / 數字 z-order（NFR-02）。
#include "typewriter_element.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ds::elements {

namespace {

// text 必須已通過 ds::render::decode_utf8 驗證（合法 UTF-8）。依首位元組的高位 pattern
// 判斷每個碼位佔用的位元組數，回傳長度為 codepoint 數 + 1 的邊界表（boundaries[i] = 第 i 個
// 碼位的起始位元組偏移；boundaries[N] = text.size()）。
std::vector<std::size_t> compute_utf8_boundaries(const std::string& text,
                                                   std::size_t codepoint_count) {
    std::vector<std::size_t> boundaries;
    boundaries.reserve(codepoint_count + 1);
    std::size_t offset = 0;
    for (std::size_t i = 0; i < codepoint_count; ++i) {
        boundaries.push_back(offset);
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        std::size_t len = 1;
        if ((lead & 0x80u) == 0x00u) {
            len = 1;
        } else if ((lead & 0xE0u) == 0xC0u) {
            len = 2;
        } else if ((lead & 0xF0u) == 0xE0u) {
            len = 3;
        } else if ((lead & 0xF8u) == 0xF0u) {
            len = 4;
        }
        offset += len;
    }
    boundaries.push_back(offset);
    return boundaries;
}

}  // namespace

TypewriterElement::TypewriterElement(const ds::render::FontMetrics& metrics,
                                      ds::render::LayoutConstraints constraints,
                                      ds::kernel::SurfaceId surface)
    : layout_(metrics, std::move(surface)), constraints_(std::move(constraints)) {
    boundaries_.push_back(0);  // 空文字的邊界表：{0}
}

void TypewriterElement::set_text(const std::string& text) {
    // decode_utf8 驗證合法性並取得碼位序列；非法 UTF-8 → std::invalid_argument（不靜默）。
    std::vector<ds::render::CodePoint> decoded = ds::render::decode_utf8(text);

    text_ = text;
    codepoints_ = std::move(decoded);
    rebuild_boundaries();
    reset();  // 新文字重新從頭逐字顯示
}

void TypewriterElement::set_speed(double chars_per_tick) {
    if (!std::isfinite(chars_per_tick) || chars_per_tick <= 0.0) {
        throw std::invalid_argument(
            "TypewriterElement::set_speed: chars_per_tick must be finite and > 0");
    }
    speed_ = chars_per_tick;
}

void TypewriterElement::advance(Tick dt) {
    const double total = static_cast<double>(total_count());
    if (progress_ >= total) {
        progress_ = total;  // 已完成：安全 no-op（維持在總字數）
        return;
    }
    progress_ += speed_ * static_cast<double>(dt);
    if (progress_ > total) {
        progress_ = total;
    }
}

ds::render::AnimationId TypewriterElement::attach(ds::render::AnimationDriver& driver) {
    return driver.add([this](const ds::render::AnimationFrame& frame) { advance(frame.dt); });
}

std::size_t TypewriterElement::visible_count() const noexcept {
    if (progress_ <= 0.0) {
        return 0;
    }
    const double floored = std::floor(progress_);
    const std::size_t count = static_cast<std::size_t>(floored);
    return count > total_count() ? total_count() : count;
}

bool TypewriterElement::is_complete() const noexcept { return visible_count() >= total_count(); }

void TypewriterElement::reset() noexcept { progress_ = 0.0; }

ds::render::LayoutResult TypewriterElement::render_model() const {
    const std::size_t visible = visible_count();
    const std::string prefix = text_.substr(0, boundaries_[visible]);
    return layout_.layout(prefix, constraints_);
}

void TypewriterElement::rebuild_boundaries() {
    boundaries_ = compute_utf8_boundaries(text_, codepoints_.size());
}

}  // namespace ds::elements
