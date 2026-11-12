// H1-07 第二案例（C2-07 媒體控制）—— 實作。見 `media_compose.hpp` 檔頭。
#include "media_compose.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace ds::host {
namespace {

// 呈現參數的預設值（**邏輯**像素；實際像素由 `placed.scale_factor` 換算）。
constexpr int kDefaultButtonLogicalPx = 24;
constexpr int kDefaultPadLogicalPx = 4;

// 把一個具名呈現參數讀成非負的實際像素；參數缺席則用 fallback（邏輯像素）。
int metric_px(const ComponentPresentation& presentation, const char* name, int fallback_logical,
              double scale) {
    const double logical = presentation.param(name, static_cast<double>(fallback_logical));
    // 負數 / NaN 的參數不得變成負尺寸：夾限於 0 之後再縮放（`scale_logical` 已處理壞的 scale）。
    const int sane_logical = (logical > 0.0) ? static_cast<int>(logical) : 0;
    return std::max(0, scale_logical(sane_logical, scale));
}

PaintBox deflate(const PaintBox& box, int pad) {
    PaintBox out = box;
    out.left += pad;
    out.top += pad;
    out.right -= pad;
    out.bottom -= pad;
    return out;
}

}  // namespace

ds::package::ComponentSlot media_control_slot(std::string slot_id) {
    ds::package::ComponentSlot slot;
    slot.id = std::move(slot_id);
    // 插槽介面 = 組件的內容清單必須同時提供這兩種 kind（E9-03 的 required_kinds 語意）。
    slot.required_kinds = {kMediaArtworkKind, kMediaTransportKind};
    return slot;
}

ds::package::ComponentSlot artwork_only_slot(std::string slot_id) {
    ds::package::ComponentSlot slot;
    slot.id = std::move(slot_id);
    slot.required_kinds = {kMediaArtworkKind};
    return slot;
}

namespace {

// 以 E9-01 的文字格式產生組件 —— **格式與驗證都由上游負責**，本層不手捏 Package 結構。
bool parse_component(const std::string& name, const std::vector<std::string>& entry_lines,
                     ds::package::Package& out, std::string& error) {
    if (name.empty()) {
        error = "組件名稱不得為空";
        return false;
    }
    std::ostringstream text;
    text << "format_version: 1.0\n";
    text << "name: " << name << "\n";
    text << "---\n";
    for (const std::string& line : entry_lines) text << line << "\n";

    const ds::package::PackageResult parsed = ds::package::parse_package(text.str());
    if (!parsed) {
        // 上游的錯誤含行號，原樣傳出，不自己改寫（錯誤可定位）。
        std::ostringstream why;
        why << "E9-01 parse_package 失敗（行 " << parsed.error().line << "）："
            << parsed.error().message;
        error = why.str();
        return false;
    }
    out = parsed.package();
    error.clear();
    return true;
}

}  // namespace

bool make_media_control_component(const std::string& name, ds::package::Package& out,
                                  std::string& error) {
    return parse_component(name,
                           {std::string(kMediaArtworkKind) + ": artwork/cover.png",
                            std::string(kMediaTransportKind) + ": controls/transport.decl"},
                           out, error);
}

bool make_artwork_only_component(const std::string& name, ds::package::Package& out,
                                 std::string& error) {
    return parse_component(name, {std::string(kMediaArtworkKind) + ": artwork/cover.png"}, out,
                           error);
}

std::vector<MediaPartBox> media_part_boxes(const ds::widgets::MediaControlRenderModel& model,
                                           const ResolvedComponent& placed) {
    std::vector<MediaPartBox> parts;
    if (!placed.presentation.visible) return parts;  // 佔著插槽，但本 widget 內不顯示
    if (placed.box.empty()) return parts;            // 沒有可畫面積 → 不產出面積為零的部件框

    const double scale = placed.scale_factor;
    const int pad = metric_px(placed.presentation, "pad_px", kDefaultPadLogicalPx, scale);
    const PaintBox inner = deflate(placed.box, pad);
    if (inner.empty()) return parts;

    const bool compact = placed.presentation.variant == kMediaVariantCompact;
    const int wanted_button =
        metric_px(placed.presentation, "button_px", kDefaultButtonLogicalPx, scale);

    if (compact) {
        // --- compact：無封面、單行曲名 + 一顆依播放狀態選定的按鈕 ---
        const int button = std::min(std::min(wanted_button, inner.height()), inner.width());
        if (button >= 1) {
            PaintBox b;
            b.left = inner.right - button;
            b.right = inner.right;
            b.top = inner.top + (inner.height() - button) / 2;
            b.bottom = b.top + button;
            const bool playing = model.playback == ds::sysinfo::PlaybackState::Playing;
            parts.push_back(
                MediaPartBox{playing ? kMediaPartButtonPause : kMediaPartButtonPlay, b});
        }
        if (model.has_media) {
            PaintBox t = inner;
            if (button >= 1) t.right = inner.right - button - pad;
            if (!t.empty()) parts.push_back(MediaPartBox{kMediaPartTitle, t});
        }
        return parts;
    }

    // --- expanded（也是未知 / 空變體的歸屬）：封面 + 兩行文字 + 四顆按鈕 ---
    int text_left = inner.left;
    if (model.artwork.has_source) {
        const int side = std::min(inner.height(), inner.width());
        if (side >= 1) {
            PaintBox a;
            a.left = inner.left;
            a.top = inner.top;
            a.right = inner.left + side;
            a.bottom = inner.top + side;
            parts.push_back(MediaPartBox{kMediaPartArtwork, a});
            text_left = a.right + pad;
        }
    }
    if (text_left >= inner.right) return parts;  // 封面已佔滿，右側沒有空間

    // 四顆按鈕在右下一列；寬度不足時等比縮小，仍不足則整列不產出（不畫出框外）。
    const int avail = inner.right - text_left;
    int button = std::min(wanted_button, inner.height());
    const int widest = (avail - 3 * pad) / 4;
    button = std::min(button, widest);
    int buttons_top = inner.bottom;
    if (button >= 1) {
        buttons_top = inner.bottom - button;
        const char* const ids[4] = {kMediaPartButtonPrev, kMediaPartButtonPlay,
                                    kMediaPartButtonPause, kMediaPartButtonNext};
        for (int i = 0; i < 4; ++i) {
            PaintBox b;
            b.left = text_left + i * (button + pad);
            b.right = b.left + button;
            b.top = buttons_top;
            b.bottom = inner.bottom;
            parts.push_back(MediaPartBox{ids[i], b});
        }
    }

    if (!model.has_media) return parts;  // 無媒體 → 不產出文字框（不假造空白）

    const int text_bottom = (button >= 1) ? buttons_top - pad : inner.bottom;
    if (text_bottom <= inner.top) return parts;
    const int text_height = text_bottom - inner.top;
    const int title_height = model.artist.empty() ? text_height : std::max(1, text_height / 2);

    PaintBox title;
    title.left = text_left;
    title.right = inner.right;
    title.top = inner.top;
    title.bottom = inner.top + title_height;
    if (!title.empty()) parts.push_back(MediaPartBox{kMediaPartTitle, title});

    if (!model.artist.empty()) {
        PaintBox artist;
        artist.left = text_left;
        artist.right = inner.right;
        artist.top = title.bottom;
        artist.bottom = text_bottom;
        if (!artist.empty()) parts.push_back(MediaPartBox{kMediaPartArtist, artist});
    }
    return parts;
}

const MediaPartBox* find_part(const std::vector<MediaPartBox>& parts, const std::string& part) {
    for (const MediaPartBox& p : parts) {
        if (p.part == part) return &p;
    }
    return nullptr;
}

}  // namespace ds::host
