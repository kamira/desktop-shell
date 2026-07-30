// content/c3_03/appearance_set.cpp — C3-03 角色外觀組實作（組裝型 artifact 單元）
//
// 相位 1：純資料 / 邏輯組合，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。套件式宣告（E9-01）解讀後跨 E4-06（具名外觀切換）/ E1-05（互動
// 區域）組裝；無效輸入結構化回報，`load_appearance_set` 中途失敗不套用任何部分結果，不靜默。
#include "appearance_set.hpp"

#include <utility>  // std::move

namespace ds::content {

namespace {

// E9-01 內容清單項目 kind：本單元唯一解讀的種類——其餘 kind（如 "asset" / "code"）原樣保留
// 於 package() 供內省，不參與外觀（look）註冊。
constexpr const char* kEntryKindLook = "look";

}  // namespace

const char* to_string(AppearanceStatus s) noexcept {
    switch (s) {
        case AppearanceStatus::Ok:
            return "Ok";
        case AppearanceStatus::Invalid:
            return "Invalid";
        case AppearanceStatus::AlreadyLoaded:
            return "AlreadyLoaded";
    }
    return "unknown";
}

const char* to_string(ApplyStatus s) noexcept {
    switch (s) {
        case ApplyStatus::Ok:
            return "Ok";
        case ApplyStatus::NoCurrentLook:
            return "NoCurrentLook";
        case ApplyStatus::NoRegions:
            return "NoRegions";
        case ApplyStatus::ProfileNotLoaded:
            return "ProfileNotLoaded";
    }
    return "unknown";
}

AppearanceStatus AppearanceSet::load_appearance_set(const std::string& definition_text) {
    if (has_package_) {
        return AppearanceStatus::AlreadyLoaded;  // 不靜默重載：呼叫端須先 clear()。
    }

    const ds::package::PackageResult parsed = ds::package::parse_package(definition_text);
    if (!parsed) {
        return AppearanceStatus::Invalid;
    }
    const ds::package::PackageResult validated = ds::package::validate_package(parsed.package());
    if (!validated) {
        return AppearanceStatus::Invalid;
    }

    const ds::package::Package& pkg = validated.package();

    // 全有或全無：先在一份暫存 SurfaceSwitcher 上逐一註冊 look 項目，任一失敗即整體放棄，
    // 不動既有狀態（looks_ / regions_ / package_ 皆未觸碰）。
    ds::render::SurfaceSwitcher new_looks;
    for (const ds::package::PackageEntry& entry : pkg.entries) {
        if (entry.kind != kEntryKindLook) {
            continue;  // 非 look 項目：本單元不解讀，原樣留在 package() 供內省。
        }
        if (new_looks.register_surface(entry.logical_path) != ds::render::SwitchStatus::Ok) {
            // 理論上不會發生（E9-01 已保證 logical_path 全域非空且不重複），此處防禦性檢查
            // 避免非 Ok 結果被靜默吞掉。
            return AppearanceStatus::Invalid;
        }
    }

    looks_ = std::move(new_looks);
    regions_.clear();
    package_ = pkg;
    has_package_ = true;
    return AppearanceStatus::Ok;
}

bool AppearanceSet::clear() {
    if (!has_package_) {
        return false;  // 尚未載入：no-op。
    }
    looks_ = ds::render::SurfaceSwitcher{};
    regions_.clear();
    package_ = ds::package::Package{};
    has_package_ = false;
    return true;
}

const ds::package::Package* AppearanceSet::package() const noexcept {
    return has_package_ ? &package_ : nullptr;
}

bool AppearanceSet::has_look(const std::string& name) const { return looks_.has(name); }

std::size_t AppearanceSet::look_count() const { return looks_.count(); }

std::vector<std::string> AppearanceSet::looks() const { return looks_.list(); }

ds::render::SwitchStatus AppearanceSet::switch_look(const std::string& name) {
    return looks_.switch_to(name);
}

bool AppearanceSet::has_current_look() const { return looks_.has_current(); }

const std::string& AppearanceSet::current_look() const { return looks_.current(); }

bool AppearanceSet::set_regions(const std::string& look, ds::kernel::NamedRegionMap regions) {
    if (!looks_.has(look)) {
        return false;  // 未知外觀：不新增游離區域。
    }
    for (auto& entry : regions_) {
        if (entry.first == look) {
            entry.second = std::move(regions);
            return true;
        }
    }
    regions_.emplace_back(look, std::move(regions));
    return true;
}

bool AppearanceSet::has_regions(const std::string& look) const {
    return find_regions(look) != nullptr;
}

const ds::kernel::NamedRegionMap* AppearanceSet::regions_for(const std::string& look) const {
    return find_regions(look);
}

const ds::kernel::NamedRegionMap* AppearanceSet::current_regions() const {
    if (!looks_.has_current()) {
        return nullptr;
    }
    return find_regions(looks_.current());
}

const ds::kernel::NamedRegionMap* AppearanceSet::find_regions(const std::string& look) const {
    for (const auto& entry : regions_) {
        if (entry.first == look) {
            return &entry.second;
        }
    }
    return nullptr;
}

ApplyStatus AppearanceSet::apply_to(ds::profiles::PortraitProfile& profile) const {
    if (!looks_.has_current()) {
        return ApplyStatus::NoCurrentLook;
    }
    if (!profile.is_loaded()) {
        return ApplyStatus::ProfileNotLoaded;
    }
    const ds::kernel::NamedRegionMap* regions = find_regions(looks_.current());
    if (regions == nullptr) {
        return ApplyStatus::NoRegions;
    }

    // 表情切換為儘力而為：僅當立繪已註冊同名具名表情才切換，不因缺表情整體失敗（表情圖片
    // 載入屬 C1-02 load_portrait / add_expression 職責，非本單元 write_scope）。
    if (profile.has_expression(looks_.current())) {
        profile.switch_expression(looks_.current());
    }
    profile.set_regions(*regions);
    return ApplyStatus::Ok;
}

}  // namespace ds::content
