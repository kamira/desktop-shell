// E7-16 設定分頁登錄（核心分頁 + 每套件一頁，append-only）— 實作
//
// 契約與 append-only 的結構保證見 `settings_pages.hpp` 檔首。
// 本檔的實作要點只有一句：**沒有任何函式會以套件貢獻去寫核心分頁**。
// `refresh()` 只碰 `package_pages_`，`core_` 連重算都不做；這不是紀律，是程式路徑上的事實。
#include "settings_pages.hpp"

#include <set>
#include <stdexcept>
#include <utility>

namespace ds::format {

namespace {

// 保留 page id / 空 id 的統一守門（貢獻來源與記憶體替身共用，訊息一致）。
void check_contribution_id(const std::string& package_id, const char* where) {
    if (package_id.empty()) {
        throw std::runtime_error(std::string(where) + ": 貢獻的 package_id 不得為空");
    }
    if (package_id == kCorePageId) {
        throw std::runtime_error(std::string(where) + ": package_id '" + package_id +
                                 "' 是核心分頁的保留 page id，套件不得佔用"
                                 "（套件貢獻為 append-only，不得取代核心分頁）");
    }
}

// 宣告的設定根必須是 Map（分頁的欄位由 Map 走訪而來）。
void check_declared_is_map(const std::string& package_id, const Value& declared,
                           const char* where) {
    if (!declared.is_map()) {
        throw std::runtime_error(std::string(where) + ": 套件 '" + package_id +
                                 "' 宣告的設定根必須是 Map 型別");
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// PageOrigin
// -----------------------------------------------------------------------------

const char* to_string(PageOrigin origin) noexcept {
    switch (origin) {
        case PageOrigin::Core:    return "Core";
        case PageOrigin::Package: return "Package";
    }
    return "Core";  // 不可達；保守回傳（不回 nullptr，呼叫端可直接串接）。
}

// -----------------------------------------------------------------------------
// SettingsPage
// -----------------------------------------------------------------------------

const EditableField* SettingsPage::field(const std::string& path_text) const {
    // 轉呼 E7-14 `find_field`——**不重造欄位尋址**。
    return find_field(fields, path_text);
}

// -----------------------------------------------------------------------------
// MemorySettingsContributions（相位 1 記憶體替身）
// -----------------------------------------------------------------------------

void MemorySettingsContributions::declare(SettingsContribution contribution) {
    check_contribution_id(contribution.package_id, "MemorySettingsContributions::declare");
    check_declared_is_map(contribution.package_id, contribution.declared,
                          "MemorySettingsContributions::declare");
    const std::string id = contribution.package_id;
    by_id_[id] = std::move(contribution);  // 同 id 覆寫：套件重新宣告自己那一頁是合法的。
}

void MemorySettingsContributions::declare(const std::string& package_id, Value declared) {
    SettingsContribution c;
    c.package_id = package_id;
    c.title = package_id;  // 無標題時以 package_id 當標題。
    c.declared = std::move(declared);
    declare(std::move(c));
}

bool MemorySettingsContributions::withdraw(const std::string& package_id) {
    return by_id_.erase(package_id) > 0;
}

void MemorySettingsContributions::clear() { by_id_.clear(); }

std::vector<std::string> MemorySettingsContributions::contributing_package_ids() const {
    std::vector<std::string> ids;
    ids.reserve(by_id_.size());
    for (const auto& kv : by_id_) ids.push_back(kv.first);  // 有序 map → 字典序。
    return ids;
}

bool MemorySettingsContributions::contribution(const std::string& package_id,
                                               SettingsContribution& out) const {
    auto it = by_id_.find(package_id);
    if (it == by_id_.end()) return false;
    out = it->second;
    return true;
}

// -----------------------------------------------------------------------------
// SettingsPageRegistry
// -----------------------------------------------------------------------------

SettingsPage SettingsPageRegistry::make_page(std::string id,
                                             std::string title,
                                             PageOrigin origin,
                                             std::string owner_package_id,
                                             Value values,
                                             EditorSchema schema) {
    SettingsPage page;
    page.id = std::move(id);
    page.title = std::move(title);
    page.origin = origin;
    page.owner_package_id = std::move(owner_package_id);
    page.values = std::move(values);
    page.schema = std::move(schema);
    // 欄位一律由 **E7-14 build_editor_model** 算出——不重造欄位模型（驗收標準明文要求）。
    page.fields = build_editor_model(page.values, page.schema);
    return page;
}

SettingsPageRegistry::SettingsPageRegistry(Value core_values, SettingsContributionSource& source)
    : SettingsPageRegistry(std::move(core_values), EditorSchema{}, source, std::string()) {}

SettingsPageRegistry::SettingsPageRegistry(Value core_values,
                                           EditorSchema core_schema,
                                           SettingsContributionSource& source,
                                           std::string core_title)
    : source_(source) {
    if (!core_values.is_map()) {
        throw std::runtime_error("SettingsPageRegistry: 核心設定根必須是 Map 型別");
    }
    core_ = make_page(kCorePageId,
                      core_title.empty() ? std::string(kCorePageTitle) : std::move(core_title),
                      PageOrigin::Core,
                      std::string(),  // 核心分頁無擁有套件。
                      std::move(core_values),
                      std::move(core_schema));
    refresh();
}

void SettingsPageRegistry::refresh() {
    // 先在暫存容器把整批套件分頁建好，全部成功才替換——失敗時登錄維持原狀，
    // 不留「一半新一半舊」的狀態（不靜默吞錯的另一面：錯誤不得留下殘局）。
    std::map<std::string, SettingsPage> rebuilt;
    std::set<std::string> seen;

    for (const std::string& id : source_.contributing_package_ids()) {
        // 保留 page id / 空 id：此處是**來源的**守門（任何來源實作都繞不過，
        // 不只 MemorySettingsContributions）。
        check_contribution_id(id, "SettingsPageRegistry::refresh");

        if (!seen.insert(id).second) {
            throw std::runtime_error("SettingsPageRegistry::refresh: 貢獻來源列出重複的 "
                                     "package_id '" + id + "'");
        }

        SettingsContribution c;
        if (!source_.contribution(id, c)) {
            // 來源自身不一致：列得出 id 卻取不到內容。靜默略過會讓一個分頁無聲消失。
            throw std::runtime_error("SettingsPageRegistry::refresh: 貢獻來源列出了 package_id '" +
                                     id + "' 卻取不到它的貢獻（來源不一致）");
        }
        check_declared_is_map(id, c.declared, "SettingsPageRegistry::refresh");

        rebuilt.emplace(id, make_page(id,
                                      c.title.empty() ? id : c.title,
                                      PageOrigin::Package,
                                      id,  // owner_package_id
                                      c.declared,
                                      c.schema));
    }

    // **只替換套件分頁集合**。`core_` 在本函式中從頭到尾未被讀寫——
    // 這就是 append-only 的結構保證（見標頭檔首保證 2）：套件貢獻沒有通往核心分頁的路徑。
    package_pages_ = std::move(rebuilt);
}

std::vector<std::string> SettingsPageRegistry::page_ids() const {
    std::vector<std::string> ids;
    ids.reserve(size());
    ids.push_back(core_.id);  // 核心分頁恆為第一（保證 4：順序固定，衝突無法被排序遮蔽）。
    for (const auto& kv : package_pages_) ids.push_back(kv.first);  // 有序 map → 字典序。
    return ids;
}

std::vector<SettingsPage> SettingsPageRegistry::pages() const {
    std::vector<SettingsPage> out;
    out.reserve(size());
    out.push_back(core_);
    for (const auto& kv : package_pages_) out.push_back(kv.second);
    return out;
}

const SettingsPage* SettingsPageRegistry::page(const std::string& page_id) const {
    if (page_id == core_.id) return &core_;
    auto it = package_pages_.find(page_id);
    return it == package_pages_.end() ? nullptr : &it->second;
}

bool SettingsPageRegistry::contains(const std::string& page_id) const {
    return page(page_id) != nullptr;
}

const Value* SettingsPageRegistry::package_values(const std::string& package_id) const {
    auto it = package_pages_.find(package_id);
    return it == package_pages_.end() ? nullptr : &it->second.values;
}

void SettingsPageRegistry::apply_page_edit(const std::string& page_id,
                                           const std::string& path_text,
                                           Value new_value) {
    // 取目標分頁的可寫參照。核心分頁與套件分頁是**兩個不同的物件**，
    // 故一次編輯只可能落在其中一個身上（分頁間互不影響）。
    SettingsPage* target = nullptr;
    if (page_id == core_.id) {
        target = &core_;
    } else {
        auto it = package_pages_.find(page_id);
        if (it != package_pages_.end()) target = &it->second;
    }
    if (target == nullptr) {
        throw std::runtime_error("SettingsPageRegistry::apply_page_edit: 分頁 '" + page_id +
                                 "' 不存在");
    }

    const EditableField* field = target->field(path_text);
    if (field == nullptr) {
        throw std::runtime_error("SettingsPageRegistry::apply_page_edit: 分頁 '" + page_id +
                                 "' 沒有欄位 '" + path_text + "'");
    }

    // 驗證 + 寫回一律經 **E7-14 apply_edit**（其內部走 E7-12 set_value）——不重造驗證規則。
    // 驗證不通過時 apply_edit 會 throw，下面兩行不會執行，故該分頁維持原狀。
    Value updated = apply_edit(target->values, *field, std::move(new_value));
    target->values = std::move(updated);
    // `values` 是唯一事實來源 → 重算欄位，使 fields 不與 values 漂移。
    target->fields = build_editor_model(target->values, target->schema);
}

}  // namespace ds::format
