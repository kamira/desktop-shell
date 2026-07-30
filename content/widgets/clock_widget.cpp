// content/widgets/c2_01/clock_widget.cpp — C2-01 時鐘 widget 實作（組裝型 artifact/widgets 單元）
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。時間一律經注入式 E2-10 TimeSource 取得，不呼叫任何 wall-clock。
// 宣告式定義（E7-01）解讀後套用顯示樣式；無效輸入結構化回報，不靜默、不改動既有狀態。
#include "clock_widget.hpp"

#include <cmath>    // std::isfinite
#include <cstdio>   // std::snprintf
#include <utility>  // std::move

namespace ds::widgets {

namespace {

// 宣告式時鐘定義的欄位名。
constexpr const char* kKeyFormat = "format";
constexpr const char* kKeySeconds = "seconds";
constexpr const char* kKeyAlign = "align";
constexpr const char* kKeyWidth = "width";

// 具名時制字串 → ClockHourFormat。未知回 false（不靜默）。
bool parse_hour_format(const std::string& name, ClockHourFormat& out) {
    if (name == "24h") {
        out = ClockHourFormat::H24;
    } else if (name == "12h") {
        out = ClockHourFormat::H12;
    } else {
        return false;
    }
    return true;
}

// 具名對齊字串 → E4-01 TextAlign。未知回 false（不靜默）。
bool parse_align(const std::string& name, ds::render::TextAlign& out) {
    if (name == "left") {
        out = ds::render::TextAlign::Left;
    } else if (name == "center") {
        out = ds::render::TextAlign::Center;
    } else if (name == "right") {
        out = ds::render::TextAlign::Right;
    } else {
        return false;
    }
    return true;
}

}  // namespace

const char* to_string(ClockState s) noexcept {
    switch (s) {
        case ClockState::Unconfigured:
            return "Unconfigured";
        case ClockState::Configured:
            return "Configured";
    }
    return "unknown";
}

const char* to_string(ClockStatus s) noexcept {
    switch (s) {
        case ClockStatus::Ok:
            return "Ok";
        case ClockStatus::Invalid:
            return "Invalid";
        case ClockStatus::Unsupported:
            return "Unsupported";
        case ClockStatus::NotConfigured:
            return "NotConfigured";
    }
    return "unknown";
}

const char* to_string(ClockHourFormat f) noexcept {
    switch (f) {
        case ClockHourFormat::H24:
            return "H24";
        case ClockHourFormat::H12:
            return "H12";
    }
    return "unknown";
}

ClockWidget::ClockWidget(std::string id, ds::profiles::SkinProfile& base,
                         std::shared_ptr<ds::sysinfo::TimeSource> time_source,
                         const ds::render::FontMetrics& font_metrics)
    : id_(std::move(id)),
      base_(base),
      time_source_(std::move(time_source)),
      font_metrics_(font_metrics) {}

std::string ClockWidget::format_display(const ds::sysinfo::CivilTime& c) const {
    char buf[32];
    if (hour_format_ == ClockHourFormat::H24) {
        if (show_seconds_) {
            std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u", c.hour, c.minute, c.second);
        } else {
            std::snprintf(buf, sizeof(buf), "%02u:%02u", c.hour, c.minute);
        }
    } else {
        unsigned h12 = c.hour % 12;
        if (h12 == 0) {
            h12 = 12;
        }
        const char* ap = (c.hour < 12) ? "AM" : "PM";
        if (show_seconds_) {
            std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u %s", h12, c.minute, c.second, ap);
        } else {
            std::snprintf(buf, sizeof(buf), "%02u:%02u %s", h12, c.minute, ap);
        }
    }
    return std::string(buf);
}

void ClockWidget::relayout() {
    // 目標具名 surface 透傳掛載基底（C1-01）之 id（NFR-02：具名指涉，非數字 handle）。
    ds::render::TextLayout layout(font_metrics_, base_.id());
    ds::render::LayoutConstraints constraints;
    constraints.max_width = max_width_;
    constraints.align = align_;
    constraints.wrap = ds::render::WrapMode::None;  // 時鐘文字恆為單行，不需詞界換行。
    layout_result_ = layout.layout(display_text_, constraints);
}

ClockStatus ClockWidget::configure(const ds::format::Value& definition) {
    if (id_.empty()) {
        return ClockStatus::Invalid;
    }
    if (!definition.is_map()) {
        return ClockStatus::Invalid;  // 宣告式定義須為 Map（通常為 Document::root）。
    }

    // --- 解讀宣告式定義為期望樣式（暫存；全數驗證通過且基底就緒才提交至成員）---
    ClockHourFormat format_new = hour_format_;
    bool seconds_new = show_seconds_;
    ds::render::TextAlign align_new = align_;
    double width_new = max_width_;

    if (const ds::format::Value* v = definition.find(kKeyFormat)) {
        if (!v->is_string() || !parse_hour_format(v->as_string(), format_new)) {
            return ClockStatus::Invalid;
        }
    }
    if (const ds::format::Value* v = definition.find(kKeySeconds)) {
        if (!v->is_bool()) {
            return ClockStatus::Invalid;
        }
        seconds_new = v->as_bool();
    }
    if (const ds::format::Value* v = definition.find(kKeyAlign)) {
        if (!v->is_string() || !parse_align(v->as_string(), align_new)) {
            return ClockStatus::Invalid;
        }
    }
    if (const ds::format::Value* v = definition.find(kKeyWidth)) {
        if (!v->is_number() || !std::isfinite(v->as_number()) || v->as_number() < 0.0) {
            return ClockStatus::Invalid;
        }
        width_new = v->as_number();
    }

    // --- 掛載基底必須已就緒（有可綁定之具名 surface）才可掛載顯示 ---
    if (!base_.is_loaded()) {
        return ClockStatus::Unsupported;  // 不提交任何設定，無殘留狀態改動。
    }

    // --- 全數成功：提交顯示樣式，轉為 Configured ---
    hour_format_ = format_new;
    show_seconds_ = seconds_new;
    align_ = align_new;
    max_width_ = width_new;
    state_ = ClockState::Configured;

    // 儘力做一次初次取樣（等同內部 tick）；無時間來源時不視為 configure 失敗，僅暫不產生顯示。
    if (time_source_) {
        last_civil_ = ds::sysinfo::civil_from_epoch_seconds(time_source_->now_epoch_seconds());
        has_sampled_ = true;
        display_text_ = format_display(last_civil_);
        relayout();
    } else {
        has_sampled_ = false;
        display_text_.clear();
        layout_result_ = ds::render::LayoutResult{};
    }
    return ClockStatus::Ok;
}

ClockStatus ClockWidget::tick() {
    if (state_ != ClockState::Configured) {
        return ClockStatus::NotConfigured;  // 未 configure 即 tick：不靜默。
    }
    if (!time_source_) {
        return ClockStatus::Unsupported;  // 無注入時間來源；既有 display_text / layout 不變。
    }
    last_civil_ = ds::sysinfo::civil_from_epoch_seconds(time_source_->now_epoch_seconds());
    has_sampled_ = true;
    display_text_ = format_display(last_civil_);
    relayout();
    return ClockStatus::Ok;
}

ClockStatus ClockWidget::refresh() {
    if (state_ != ClockState::Configured) {
        return ClockStatus::NotConfigured;  // 未 configure 即 refresh：不靜默。
    }
    relayout();  // 純排版重算，不重新取樣時間。
    return ClockStatus::Ok;
}

}  // namespace ds::widgets
