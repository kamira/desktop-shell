// apps/c4_01/shortcut_cheat_sheet.cpp — C4-01 鍵位速查 實作
#include "shortcut_cheat_sheet.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ds::apps {

namespace {

// ASCII 小寫化（相位 1：篩選僅需 ASCII 大小寫不敏感比對，不做完整 Unicode 折疊）。
std::string to_lower_ascii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// 一筆鍵位是否命中已小寫化的篩選字串（空篩選字串恆命中）。
bool matches(const ShortcutEntry& entry, const std::string& lower_query) {
    if (lower_query.empty()) {
        return true;
    }
    return to_lower_ascii(entry.keys).find(lower_query) != std::string::npos ||
           to_lower_ascii(entry.description).find(lower_query) != std::string::npos;
}

}  // namespace

const char* to_string(LoadStatus s) noexcept {
    switch (s) {
        case LoadStatus::Ok:
            return "Ok";
        case LoadStatus::Invalid:
            return "Invalid";
    }
    return "unknown";
}

ShortcutCheatSheetApp::ShortcutCheatSheetApp(ds::profiles::OsdOverlayProfile& panel,
                                             const ds::render::FontMetrics& metrics)
    : panel_(panel), layout_engine_(metrics) {}

LoadStatus ShortcutCheatSheetApp::load_shortcuts(std::vector<ShortcutEntry> entries) {
    for (const auto& e : entries) {
        if (e.keys.empty() || e.description.empty()) {
            return LoadStatus::Invalid;  // 整批拒絕：不留半份殘留清單。
        }
    }
    entries_ = std::move(entries);
    refresh_if_showing();
    return LoadStatus::Ok;
}

void ShortcutCheatSheetApp::filter(std::string query) {
    filter_query_ = std::move(query);
    refresh_if_showing();
}

std::vector<ShortcutEntry> ShortcutCheatSheetApp::visible_shortcuts() const {
    std::vector<ShortcutEntry> out;
    out.reserve(entries_.size());
    const std::string lower_query = to_lower_ascii(filter_query_);
    for (const auto& e : entries_) {
        if (matches(e, lower_query)) {
            out.push_back(e);
        }
    }
    return out;
}

std::string ShortcutCheatSheetApp::display_text() const {
    std::string text;
    bool first = true;
    for (const auto& e : visible_shortcuts()) {
        if (!first) {
            text += "\n";
        }
        first = false;
        text += e.keys;
        text += kEntrySeparator;
        text += e.description;
    }
    return text;
}

ds::render::LayoutResult ShortcutCheatSheetApp::layout(
    const ds::render::LayoutConstraints& constraints) const {
    return layout_engine_.layout(display_text(), constraints);
}

bool ShortcutCheatSheetApp::show(ds::events::Tick ttl) {
    return panel_.show(display_text(), ttl);
}

bool ShortcutCheatSheetApp::hide() {
    return panel_.dismiss();
}

bool ShortcutCheatSheetApp::is_showing() const {
    return panel_.is_showing();
}

const std::string& ShortcutCheatSheetApp::panel_id() const {
    return panel_.id();
}

void ShortcutCheatSheetApp::refresh_if_showing() {
    if (panel_.is_showing()) {
        panel_.update(display_text());
    }
}

}  // namespace ds::apps
