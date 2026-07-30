// E9-04 主題切換 — 實作（見 theme.hpp 檔首語意）。
//
// 平台中立、純邏輯：無任何 `#ifdef` / 系統呼叫 / 平台分支。查無主題一律明確報錯不靜默。
#include "theme.hpp"

#include <stdexcept>
#include <utility>

namespace ds::package {

ThemeData theme_from_document(std::string name, const ds::format::Document& doc) {
    ThemeData t;
    t.name = std::move(name);
    t.attributes = doc.root;  // 內容根 Map（不含 format_version 本身）作為主題屬性表。
    return t;
}

std::size_t ThemeManager::index_of(const std::string& name) const noexcept {
    for (std::size_t i = 0; i < themes_.size(); ++i) {
        if (themes_[i].name == name) {
            return i;
        }
    }
    return kNone;
}

ThemeResult ThemeManager::register_theme(const std::string& name, ThemeData data) {
    if (name.empty()) {
        return ThemeResult::failure("theme name must not be empty");
    }
    if (index_of(name) != kNone) {
        return ThemeResult::failure("theme already registered: " + name +
                                    " (use set_theme to update)");
    }
    data.name = name;  // 以註冊鍵為權威名稱。
    themes_.push_back(std::move(data));
    return ThemeResult::success();
}

ThemeResult ThemeManager::set_theme(const std::string& name, ThemeData data) {
    if (name.empty()) {
        return ThemeResult::failure("theme name must not be empty");
    }
    data.name = name;
    const std::size_t i = index_of(name);
    if (i == kNone) {
        themes_.push_back(std::move(data));
        return ThemeResult::success();
    }
    themes_[i] = std::move(data);
    // 更新的正是當前主題 → 重新套用（熱重載一份主題文件即時生效）。
    if (i == current_) {
        notify();
    }
    return ThemeResult::success();
}

ThemeResult ThemeManager::switch_to(const std::string& name) {
    const std::size_t i = index_of(name);
    if (i == kNone) {
        return ThemeResult::failure("unknown theme: " + name);
    }
    if (i == current_) {
        return ThemeResult::success();  // 已是當前主題：no-op，不重複通知。
    }
    current_ = i;
    notify();
    return ThemeResult::success();
}

bool ThemeManager::has_theme(const std::string& name) const noexcept {
    return index_of(name) != kNone;
}

const ThemeData* ThemeManager::find_theme(const std::string& name) const noexcept {
    const std::size_t i = index_of(name);
    return i == kNone ? nullptr : &themes_[i];
}

const ThemeData& ThemeManager::current() const {
    if (current_ == kNone) {
        throw std::runtime_error("ThemeManager::current(): no current theme selected");
    }
    return themes_[current_];
}

const std::string& ThemeManager::current_name() const {
    return current().name;  // 沿用 current() 的明確報錯路徑。
}

std::vector<std::string> ThemeManager::list_themes() const {
    std::vector<std::string> names;
    names.reserve(themes_.size());
    for (const auto& t : themes_) {
        names.push_back(t.name);
    }
    return names;
}

void ThemeManager::on_theme_change(ThemeChangeCallback cb) {
    callbacks_.push_back(std::move(cb));
}

void ThemeManager::notify() const {
    const ThemeData& t = themes_[current_];
    for (const auto& cb : callbacks_) {
        if (cb) {
            cb(t);
        }
    }
}

}  // namespace ds::package
