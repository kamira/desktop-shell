// apps/c4_04/window_overview.cpp — C4-04 視窗總覽實作
//
// 純組裝邏輯：委派 C1-05 `SummonPanelProfile::open()` / `close()` 做總覽面板的顯隱生命
// 週期；呼叫 E4-02 `ImageElement::set_source()` 為每個視窗建立縮圖渲染描述。無平台分支、
// 無真實視窗列舉、無真實影像解碼。
#include "window_overview.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace ds::apps {

const char* to_string(LoadStatus s) noexcept {
    switch (s) {
        case LoadStatus::Ok:
            return "Ok";
        case LoadStatus::Invalid:
            return "Invalid";
    }
    return "unknown";
}

const char* to_string(SelectStatus s) noexcept {
    switch (s) {
        case SelectStatus::Selected:
            return "Selected";
        case SelectStatus::NotFound:
            return "NotFound";
        case SelectStatus::PanelClosed:
            return "PanelClosed";
    }
    return "unknown";
}

const char* to_string(ActivateStatus s) noexcept {
    switch (s) {
        case ActivateStatus::Activated:
            return "Activated";
        case ActivateStatus::NotFound:
            return "NotFound";
        case ActivateStatus::PanelClosed:
            return "PanelClosed";
    }
    return "unknown";
}

namespace {

// 平鋪版位的具名區域（NFR-02：具名，非螢幕座標）—— 每個視窗各自一個穩定區域名。
std::string tile_region(const std::string& window_id) {
    return std::string(kTileRegionPrefix) + window_id;
}

}  // namespace

WindowOverviewApp::WindowOverviewApp(ds::profiles::SummonPanelProfile& base) : base_(base) {}

int WindowOverviewApp::index_of(const std::string& window_id) const noexcept {
    for (std::size_t i = 0; i < windows_.size(); ++i) {
        if (windows_[i].window_id == window_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const WindowEntry* WindowOverviewApp::find(const std::string& window_id) const noexcept {
    const int idx = index_of(window_id);
    return idx < 0 ? nullptr : &windows_[static_cast<std::size_t>(idx)];
}

void WindowOverviewApp::relayout() {
    const std::size_t n = windows_.size();
    if (n == 0) {
        return;  // 無視窗：無平鋪版位（合法的「無視窗」狀態）。
    }

    const int columns = std::min<int>(kOverviewColumns, static_cast<int>(n));
    const int rows = static_cast<int>((n + static_cast<std::size_t>(columns) - 1) /
                                       static_cast<std::size_t>(columns));
    const double cell_w = 1.0 / static_cast<double>(columns);
    const double cell_h = 1.0 / static_cast<double>(rows);

    for (std::size_t i = 0; i < n; ++i) {
        const int row = static_cast<int>(i) / columns;
        const int col = static_cast<int>(i) % columns;
        WindowEntry& entry = windows_[i];
        entry.layout.region = tile_region(entry.window_id);
        entry.layout.x = static_cast<double>(col) * cell_w;
        entry.layout.y = static_cast<double>(row) * cell_h;
        entry.layout.width = cell_w;
        entry.layout.height = cell_h;
    }
}

LoadStatus WindowOverviewApp::load_windows(std::vector<WindowSpec> specs) {
    // 驗證：空 id 或重複 id 一律整批拒絕（不部分套用，不靜默）。
    std::unordered_set<std::string> seen;
    seen.reserve(specs.size());
    for (const WindowSpec& spec : specs) {
        if (spec.window_id.empty()) {
            return LoadStatus::Invalid;
        }
        if (!seen.insert(spec.window_id).second) {
            return LoadStatus::Invalid;  // 重複 window_id。
        }
    }

    std::vector<WindowEntry> next;
    next.reserve(specs.size());
    for (WindowSpec& spec : specs) {
        WindowEntry entry;
        entry.window_id = std::move(spec.window_id);
        entry.title = std::move(spec.title);
        if (spec.thumbnail != nullptr) {
            // 佔位圖像參照：E4-02 只複製參照 + 固有尺寸，不解碼（相位 1）。
            entry.thumbnail.set_source(*spec.thumbnail);
            entry.thumbnail.set_target("surface.overview." + entry.window_id);
        }
        next.push_back(std::move(entry));
    }

    const std::string previously_selected =
        (selected_index_ >= 0) ? windows_[static_cast<std::size_t>(selected_index_)].window_id
                                : std::string();

    windows_ = std::move(next);
    relayout();

    // 重新對齊選取狀態：舊選取的視窗若仍在新清單內，保留選取；否則清除（不留懸置選取）。
    selected_index_ = previously_selected.empty() ? -1 : index_of(previously_selected);

    return LoadStatus::Ok;
}

bool WindowOverviewApp::show_overview(ds::events::Tick ttl) { return base_.open(ttl); }

bool WindowOverviewApp::close_overview() {
    const bool closed = base_.close();
    if (closed) {
        clear_selection();
    }
    return closed;
}

SelectStatus WindowOverviewApp::select(const std::string& window_id) {
    if (!is_open()) {
        return SelectStatus::PanelClosed;  // 未開啟總覽不得選取。
    }
    const int idx = index_of(window_id);
    if (idx < 0) {
        return SelectStatus::NotFound;
    }
    selected_index_ = idx;
    return SelectStatus::Selected;
}

const WindowEntry* WindowOverviewApp::selected() const noexcept {
    if (selected_index_ < 0) {
        return nullptr;
    }
    return &windows_[static_cast<std::size_t>(selected_index_)];
}

ActivateStatus WindowOverviewApp::activate(const std::string& window_id,
                                            const WindowEntry** out) {
    if (!is_open()) {
        return ActivateStatus::PanelClosed;  // 未開啟總覽不得切換視窗。
    }
    const int idx = index_of(window_id);
    if (idx < 0) {
        return ActivateStatus::NotFound;  // 找不到，總覽保持開啟（不靜默收起）。
    }

    const WindowEntry& entry = windows_[static_cast<std::size_t>(idx)];
    last_activated_id_ = entry.window_id;
    if (out != nullptr) {
        *out = &entry;
    }

    base_.close();  // 切換視窗後總覽隨即收起（is_open() 已保證此刻為開啟，故恆成功）。
    clear_selection();
    return ActivateStatus::Activated;
}

bool WindowOverviewApp::close_window(const std::string& window_id) {
    const int idx = index_of(window_id);
    if (idx < 0) {
        return false;  // 找不到：no-op，不靜默。
    }

    windows_.erase(windows_.begin() + idx);

    if (selected_index_ == idx) {
        selected_index_ = -1;  // 被關閉的恰是目前選取的視窗：清除選取。
    } else if (selected_index_ > idx) {
        --selected_index_;  // 移除點之前的元素前移，選取索引隨之校正。
    }

    relayout();  // 重新平鋪剩餘視窗（版位不留缺口）。
    return true;
}

}  // namespace ds::apps
