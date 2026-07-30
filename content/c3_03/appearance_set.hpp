// content/c3_03/appearance_set.hpp — C3-03 角色外觀組（artifact 層 / 相位 1：純資料組合）
//
// 語意：「角色外觀組」是角色外觀 / 皮膚資源組合包——把一組**具名外觀（不同表情 / 服裝）**
// （E4-06 具名切換）與各外觀對應的**互動區域**（E1-05 具名區域）打包成一個可整組套用的
// 集合，其定義以 **E9-01 套件格式**描述（manifest + 內容清單），並可掛到一個已載入的 C1-02
// 立繪 profile 上生效。本單元不是新引擎邏輯，而是把四個已合併的擴充點**組裝**成單一
// 「外觀組」：
//
//   - E9-01（`ds::package::Package` / `parse_package` / `validate_package`）：**套件管理** ——
//     外觀組的定義即一份 E9-01 套件描述文字（manifest 區段 + 內容清單區段）。內容清單中
//     `kind == "look"` 的項目，其 `logical_path` 即一個具名外觀（look）的名稱；其餘 kind
//     （如 `asset` / `code`）本單元不解讀，原樣保留於 `package()` 供呼叫端內省（外觀組可與
//     其他資源類別共享同一份套件描述，而不需要本單元認得每一種 kind）。不重造套件格式，
//     解析 / 驗證全由 E9-01 負責（含行號的可定位錯誤，向上收斂為 `AppearanceStatus::Invalid`）。
//   - E4-06（`ds::render::SurfaceSwitcher`）：**具名外觀切換** —— 內容清單中每個 `look`
//     項目於載入時注入一個具名 `SurfaceSwitcher` 條目；`switch_look(name)` 純委派其
//     `switch_to`，保留其精確結果碼（`Ok` / `NotFound`）。
//   - E1-05（`ds::kernel::NamedRegionMap`）：**互動區域** —— 每個具名外觀可各自登記一組
//     具名子區域（呼叫端先以 `NamedRegionMap::add_region()` 建好，再以 `set_regions()`
//     指派給某個 look；未知 look → 不新增，回 false）。
//   - C1-02（`ds::profiles::PortraitProfile`）：**掛載目標** —— `apply_to(profile)` 把
//     「目前外觀」的互動區域套用到一個已載入的立繪 profile（`profile.set_regions()`），並在
//     該 profile 已註冊同名具名表情時一併切換（`profile.switch_expression()`，儘力而為——
//     表情圖片的載入屬 C1-02 自身職責，非本單元 write_scope）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組合，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02；外觀 / 區域皆以具名字串指涉，`list()`
// / `looks()` 之順序僅表示登記先後，非疊放層級）。任何無效操作（重複載入、套件解析 / 結構
// 驗證失敗、未知 look、無目前外觀、目標立繪未載入）一律明確回傳具名結果，不靜默；
// `load_appearance_set` 全有或全無（失敗時不套用任何部分結果，不動既有狀態）。
#ifndef DS_CONTENT_C3_03_APPEARANCE_SET_HPP
#define DS_CONTENT_C3_03_APPEARANCE_SET_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "named_region_map.hpp"  // E1-05（上游，可讀不可改）：NamedRegionMap
#include "package.hpp"           // E9-01（上游，可讀不可改）：Package / PackageEntry / PackageResult /
                                  //   parse_package / validate_package
#include "portrait_profile.hpp"  // C1-02（上游，可讀不可改）：PortraitProfile（本單元的掛載目標）
#include "surface_switcher.hpp"  // E4-06（上游，可讀不可改）：SurfaceSwitcher / SwitchStatus

namespace ds::content {

// 外觀組載入 / 管理的具名結果（NFR-02：具名，非數字）。
enum class AppearanceStatus {
    Ok,             // 操作成功。
    Invalid,        // 套件描述解析 / 結構驗證失敗（E9-01），或內容清單含無法註冊的 look 項目。
    AlreadyLoaded,  // 已載入時再次 load_appearance_set（不靜默重載；呼叫端須先 clear()）。
};

const char* to_string(AppearanceStatus s) noexcept;

// `apply_to(profile)` 的具名結果。
enum class ApplyStatus {
    Ok,               // 已把目前外觀的互動區域（及可能的表情）套用到目標立繪 profile。
    NoCurrentLook,    // 尚無目前外觀（未 switch_look 過，或外觀組為空組）。
    NoRegions,        // 目前外觀尚未登記互動區域（set_regions 未呼叫過）。
    ProfileNotLoaded, // 目標立繪 profile 尚未 load_portrait（`is_loaded() == false`）。
};

const char* to_string(ApplyStatus s) noexcept;

// ---------------------------------------------------------------------------
// AppearanceSet —— 角色外觀組：組裝 E9-01（套件管理）+ E4-06（具名外觀切換）+
// E1-05（互動區域），可 `apply_to` 掛上 C1-02 立繪 profile。
//
// 每個實例代表**一個**外觀組（如「miku 的私服/泳裝/正裝」）。內部自持一個 E4-06
// `SurfaceSwitcher`（以外觀名為具名 id）+ 各外觀的 E1-05 `NamedRegionMap`（以具名鍵線性
// 配對，順序即登記順序，永不以數字 index 對外暴露）+ 已驗證的 E9-01 `Package`。
// ---------------------------------------------------------------------------
class AppearanceSet {
public:
    AppearanceSet() = default;

    // --- 套件式宣告載入（E9-01）+ 具名外觀註冊（E4-06）---

    // 從一份 E9-01 套件描述文字載入外觀組。內容清單中 `kind == "look"` 的項目，其
    // `logical_path` 成為一個具名外觀（於內部 SurfaceSwitcher 註冊）；其餘 kind 忽略（不
    // 解讀，仍保留在 `package()` 內容清單供內省）。
    // 流程（全有或全無）：
    //   - 已載入 → AlreadyLoaded（不靜默重載）。
    //   - 套件描述解析失敗（`parse_package`）或結構驗證失敗（`validate_package`：manifest
    //     無效 / 內容清單項目不合法或路徑重複）→ Invalid，不動既有狀態。
    //   - 任一 `look` 項目無法註冊（理論上不會發生：E9-01 已保證 logical_path 全域不重複、
    //     非空；此處仍防禦性檢查，避免呼叫 SurfaceSwitcher 出現非 Ok 卻被靜默吞掉）→
    //     Invalid，不套用任何部分結果。
    //   - 全數成功 → Ok；`look_count()` 反映已註冊外觀數（可為 0，空組亦合法）。
    AppearanceStatus load_appearance_set(const std::string& definition_text);

    // 清空外觀組（已註冊外觀、各外觀之互動區域、目前外觀、已載入之套件），回到初始狀態。
    // 尚未載入 → false（no-op，不靜默）。成功 → true。
    bool clear();

    // --- E9-01 套件內省 ---
    bool has_package() const noexcept { return has_package_; }
    // 已載入之套件（manifest + 完整內容清單，含非 "look" 項目）；未載入回 nullptr。
    const ds::package::Package* package() const noexcept;

    // --- 具名外觀（E4-06）---
    bool has_look(const std::string& name) const;
    std::size_t look_count() const;
    // 依註冊順序列舉已知具名外觀（NFR-02：順序僅表示註冊先後，非層級）。
    std::vector<std::string> looks() const;

    // 切換「目前」外觀（純委派 E4-06 `switch_to`，保留其精確結果碼）。未知 name → NotFound。
    ds::render::SwitchStatus switch_look(const std::string& name);
    bool has_current_look() const;
    // 目前外觀之具名 id；`!has_current_look()` 時回空字串。
    const std::string& current_look() const;

    // --- 互動區域（E1-05），依外觀個別登記 ---

    // 設定（或取代）某具名外觀的互動區域集合（呼叫端先以 `NamedRegionMap::add_region()`
    // 登記好）。未知 look（尚未於 SurfaceSwitcher 註冊）→ false（no-op，不新增游離區域）。
    bool set_regions(const std::string& look, ds::kernel::NamedRegionMap regions);
    bool has_regions(const std::string& look) const;
    // 某具名外觀的互動區域；未知 look 或尚未登記 → nullptr。
    const ds::kernel::NamedRegionMap* regions_for(const std::string& look) const;
    // 目前外觀的互動區域；無目前外觀或尚未登記 → nullptr。
    const ds::kernel::NamedRegionMap* current_regions() const;

    // --- 掛上 C1-02 立繪 profile ---

    // 把「目前外觀」套用到一個已載入的立繪 profile：套用其互動區域（`profile.set_regions`），
    // 並在該 profile 已註冊同名具名表情時一併切換（`profile.switch_expression`，儘力而為，
    // 不因表情不存在而整體失敗——外觀組管理「有哪些外觀 + 各外觀互動區域」，表情圖片本身
    // 屬 C1-02 職責）。
    //   - 尚無目前外觀 → NoCurrentLook，不動 profile。
    //   - 目標 profile 未載入（`!profile.is_loaded()`）→ ProfileNotLoaded，不動 profile。
    //   - 目前外觀尚未登記互動區域 → NoRegions，不動 profile。
    //   - 全數成功 → Ok。
    ApplyStatus apply_to(ds::profiles::PortraitProfile& profile) const;

private:
    const ds::kernel::NamedRegionMap* find_regions(const std::string& look) const;

    ds::render::SurfaceSwitcher looks_;
    // 具名外觀 → 互動區域集合。以具名鍵線性配對，順序即登記順序（永不以數字 index 對外
    // 暴露，比照上游 E1-05 / E4-06 慣例）。
    std::vector<std::pair<std::string, ds::kernel::NamedRegionMap>> regions_;

    bool has_package_ = false;
    ds::package::Package package_;
};

}  // namespace ds::content

#endif  // DS_CONTENT_C3_03_APPEARANCE_SET_HPP
