// E9-10 已安裝套件登錄的持久化 — 實作。見 install_store.hpp。
//
// 平台中立、純邏輯：無任何 `#ifdef` / 平台分支 / 系統呼叫 / 真實檔案 I/O。
// 序列化一律經 E7-12 `serialize`，反序列化一律經 E7-01 `parse`；套件結構驗證一律經 E9-01
// `validate_package`——三者皆不重造。
#include "install_store.hpp"

#include <set>
#include <stdexcept>
#include <utility>

namespace ds::package {

using ds::format::Value;

namespace {

// --- 文件欄位名（單一出處，序列化 / 反序列化共用，避免字面字串漂移）-----------------
constexpr const char* kKeyPackages = "packages";
constexpr const char* kKeyName = "name";
constexpr const char* kKeyFormatMajor = "format_major";
constexpr const char* kKeyFormatMinor = "format_minor";
constexpr const char* kKeyVersion = "version";
constexpr const char* kKeyDescription = "description";
constexpr const char* kKeyRequires = "requires";
constexpr const char* kKeyPermissions = "permissions";
constexpr const char* kKeyEntries = "entries";
constexpr const char* kKeyKind = "kind";
constexpr const char* kKeyPath = "path";

// 以 E9-01 驗證一筆紀錄的套件結構；不合法即明確拋錯（不重造驗證規則）。
void require_valid_package(const Package& pkg, const std::string& where) {
    if (pkg.manifest.name.empty()) {
        throw std::runtime_error("E9-10: " + where + "：套件 manifest.name 不得為空");
    }
    const PackageResult r = validate_package(pkg);
    if (!r.ok()) {
        throw std::runtime_error("E9-10: " + where + "：套件 '" + pkg.manifest.name +
                                 "' 結構不合法 —— " + r.error().message);
    }
}

// 字串清單 -> Value::list。空清單照實寫出（序列化為 `key:`，再解析塌為 Null，由讀取端還原為空）。
Value strings_to_value(const std::vector<std::string>& items) {
    std::vector<Value> out;
    out.reserve(items.size());
    for (const std::string& s : items) {
        out.push_back(Value::string(s));
    }
    return Value::list(std::move(out));
}

// 取 Map 內某鍵的字串；缺鍵 / 型別不符 → throw（明確不靜默）。
std::string require_string(const Value& map, const std::string& key, const std::string& where) {
    const Value* v = map.find(key);
    if (v == nullptr) {
        throw std::runtime_error("E9-10: " + where + " 缺少必要欄位 '" + key + "'");
    }
    if (!v->is_string()) {
        throw std::runtime_error("E9-10: " + where + " 欄位 '" + key + "' 必須為字串");
    }
    return v->as_string();
}

// 取 Map 內某鍵的選填字串（缺鍵 / Null → 空字串；型別不符 → throw）。
std::string optional_string(const Value& map, const std::string& key, const std::string& where) {
    const Value* v = map.find(key);
    if (v == nullptr || v->is_null()) {
        return {};
    }
    if (!v->is_string()) {
        throw std::runtime_error("E9-10: " + where + " 欄位 '" + key + "' 必須為字串");
    }
    return v->as_string();
}

// 取 Map 內某鍵的整數；缺鍵 / 非整數 → throw。
int require_int(const Value& map, const std::string& key, const std::string& where) {
    const Value* v = map.find(key);
    if (v == nullptr) {
        throw std::runtime_error("E9-10: " + where + " 缺少必要欄位 '" + key + "'");
    }
    if (!v->is_integer()) {
        throw std::runtime_error("E9-10: " + where + " 欄位 '" + key + "' 必須為整數");
    }
    return static_cast<int>(v->as_int());
}

// 取 Map 內某鍵的字串清單。
// 缺鍵 / Null → 空清單（E7-01 已知限制：空清單序列化後再解析塌為 Null，此處還原為空）。
// 非清單、或項目非字串 → throw。
std::vector<std::string> optional_string_list(const Value& map, const std::string& key,
                                              const std::string& where) {
    const Value* v = map.find(key);
    if (v == nullptr || v->is_null()) {
        return {};
    }
    if (!v->is_list()) {
        throw std::runtime_error("E9-10: " + where + " 欄位 '" + key + "' 必須為清單");
    }
    std::vector<std::string> out;
    for (const Value& item : v->as_list()) {
        if (!item.is_string()) {
            throw std::runtime_error("E9-10: " + where + " 欄位 '" + key +
                                     "' 的每個項目必須為字串");
        }
        out.push_back(item.as_string());
    }
    return out;
}

// 一筆紀錄 -> Value（Map）。見 install_store.hpp 檔首的格式說明。
Value record_to_value(const InstallRecord& rec) {
    const Manifest& m = rec.package.manifest;

    std::vector<Value::Member> members;
    // 套件名放在**值**的位置（非 map 鍵），以規避 E7-12 的「map 鍵不支援引號」限制。
    members.emplace_back(kKeyName, Value::string(m.name));
    // manifest 自身的格式版本以兩個整數存放：寫成 "1.0" 字串會被 E7-01 型別推斷讀回成 Number，
    // 且無法區分 1.1 與 1.10；拆成整數則型別與語意皆不漂移。
    members.emplace_back(kKeyFormatMajor, Value::integer(m.format_version.major));
    members.emplace_back(kKeyFormatMinor, Value::integer(m.format_version.minor));
    if (!m.version.empty()) {  // 選填：空則不寫出（讀取端把缺鍵視為空）。
        members.emplace_back(kKeyVersion, Value::string(m.version));
    }
    if (!m.description.empty()) {
        members.emplace_back(kKeyDescription, Value::string(m.description));
    }
    members.emplace_back(kKeyRequires, strings_to_value(m.required_capabilities));
    members.emplace_back(kKeyPermissions, strings_to_value(m.permissions));

    std::vector<Value> entries;
    entries.reserve(rec.package.entries.size());
    for (const PackageEntry& e : rec.package.entries) {
        entries.push_back(Value::map({
            {kKeyKind, Value::string(e.kind)},
            {kKeyPath, Value::string(e.logical_path)},
        }));
    }
    members.emplace_back(kKeyEntries, Value::list(std::move(entries)));

    return Value::map(std::move(members));
}

// Value（Map）-> 一筆紀錄。結構違反 → throw。
InstallRecord value_to_record(const Value& item, std::size_t index) {
    const std::string where = "packages[" + std::to_string(index) + "]";
    if (!item.is_map()) {
        throw std::runtime_error("E9-10: " + where + " 必須為 Map");
    }

    InstallRecord rec;
    Manifest& m = rec.package.manifest;
    m.name = require_string(item, kKeyName, where);
    m.format_version.major = require_int(item, kKeyFormatMajor, where);
    m.format_version.minor = require_int(item, kKeyFormatMinor, where);
    m.version = optional_string(item, kKeyVersion, where);
    m.description = optional_string(item, kKeyDescription, where);
    m.required_capabilities = optional_string_list(item, kKeyRequires, where);
    m.permissions = optional_string_list(item, kKeyPermissions, where);

    const Value* entries = item.find(kKeyEntries);
    // 缺鍵 / Null = 空內含項目清單（E7-01 空容器限制；僅含 manifest 的套件亦屬合法，見 E9-01）。
    if (entries != nullptr && !entries->is_null()) {
        if (!entries->is_list()) {
            throw std::runtime_error("E9-10: " + where + " 欄位 'entries' 必須為清單");
        }
        for (const Value& e : entries->as_list()) {
            if (!e.is_map()) {
                throw std::runtime_error("E9-10: " + where + " 的每個 entries 項目必須為 Map");
            }
            PackageEntry pe;
            pe.kind = require_string(e, kKeyKind, where + ".entries");
            pe.logical_path = require_string(e, kKeyPath, where + ".entries");
            rec.package.entries.push_back(std::move(pe));
        }
    }

    // 讀回的套件仍須通過 E9-01 結構完整性驗證（含 format_version 相容性、邏輯路徑合法性、
    // 路徑不重複）——損毀的持久化內容不得悄悄變成一筆「看起來正常」的安裝紀錄。
    require_valid_package(rec.package, where);
    return rec;
}

}  // namespace

// -----------------------------------------------------------------------------
// InstallRecord 相等
// -----------------------------------------------------------------------------

bool InstallRecord::operator==(const InstallRecord& o) const {
    const Manifest& a = package.manifest;
    const Manifest& b = o.package.manifest;
    if (a.name != b.name || a.format_version != b.format_version || a.version != b.version ||
        a.description != b.description || a.required_capabilities != b.required_capabilities ||
        a.permissions != b.permissions) {
        return false;
    }
    if (package.entries.size() != o.package.entries.size()) {
        return false;
    }
    for (std::size_t i = 0; i < package.entries.size(); ++i) {
        if (package.entries[i].kind != o.package.entries[i].kind ||
            package.entries[i].logical_path != o.package.entries[i].logical_path) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// 序列化 / 反序列化（透過 E7-12 / E7-01）
// -----------------------------------------------------------------------------

std::string serialize_install_records(const std::vector<InstallRecord>& records) {
    std::set<std::string> seen;
    std::vector<Value> items;
    items.reserve(records.size());
    for (const InstallRecord& rec : records) {
        require_valid_package(rec.package, "序列化");
        if (!seen.insert(rec.package_id()).second) {
            throw std::runtime_error("E9-10: 序列化：登錄內出現重複的套件識別 '" +
                                     rec.package_id() + "'");
        }
        items.push_back(record_to_value(rec));
    }

    std::vector<Value::Member> root;
    root.emplace_back(kKeyPackages, Value::list(std::move(items)));
    // E7-12 `serialize(root, version)`：產出帶 format_version 首行的 E7-01 宣告式文字。
    return ds::format::serialize(Value::map(std::move(root)), kInstallRegistryFormat);
}

std::vector<InstallRecord> deserialize_install_records(const std::string& text) {
    const ds::format::ParseResult result = ds::format::parse(text);
    if (!result.ok()) {
        const ds::format::ParseError& err = result.error();
        throw std::runtime_error("E9-10: 已安裝登錄文字解析失敗（行 " +
                                 std::to_string(err.line) + "）：" + err.message);
    }

    const ds::format::Document& doc = result.document();
    if (!doc.root.is_map()) {
        throw std::runtime_error("E9-10: 已安裝登錄根節點必須為 Map");
    }

    std::vector<InstallRecord> out;
    const Value* packages = doc.root.find(kKeyPackages);
    // 缺鍵或 Null（空清單於 E7-01 塌為 Null）→ 空登錄，屬合法狀態（尚未安裝任何套件）。
    if (packages == nullptr || packages->is_null()) {
        return out;
    }
    if (!packages->is_list()) {
        throw std::runtime_error("E9-10: 欄位 'packages' 必須為清單");
    }

    std::set<std::string> seen;
    const std::vector<Value>& items = packages->as_list();
    for (std::size_t i = 0; i < items.size(); ++i) {
        InstallRecord rec = value_to_record(items[i], i);
        if (!seen.insert(rec.package_id()).second) {
            throw std::runtime_error("E9-10: 已安裝登錄內出現重複的套件識別 '" +
                                     rec.package_id() + "'");
        }
        out.push_back(std::move(rec));
    }
    return out;
}

// -----------------------------------------------------------------------------
// MemoryInstallStorage（相位 1 替身後端）
// -----------------------------------------------------------------------------

void MemoryInstallStorage::put(const std::string& key, const std::string& text) {
    data_[key] = text;  // 覆寫既有。
}

bool MemoryInstallStorage::get(const std::string& key, std::string& out) const {
    const auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool MemoryInstallStorage::has(const std::string& key) const {
    return data_.find(key) != data_.end();
}

bool MemoryInstallStorage::erase(const std::string& key) {
    return data_.erase(key) > 0;
}

std::vector<std::string> MemoryInstallStorage::names() const {
    std::vector<std::string> out;
    out.reserve(data_.size());
    for (const auto& kv : data_) {
        out.push_back(kv.first);  // std::map 有序 → 字典序。
    }
    return out;
}

// -----------------------------------------------------------------------------
// InstalledPackageStore
// -----------------------------------------------------------------------------

InstalledPackageStore::InstalledPackageStore(InstallStorage& storage, std::string slot)
    : storage_(storage), slot_(std::move(slot)) {
    if (slot_.empty()) {
        throw std::runtime_error("E9-10: 儲存 slot 名稱不得為空");
    }
    reload();  // 建構即自後端重建 —— 這就是相位 1 的「重啟」rehydrate 點。
}

void InstalledPackageStore::reload() {
    std::string text;
    by_id_.clear();
    if (!storage_.get(slot_, text)) {
        return;  // 後端尚無此 slot = 初次啟動，空登錄，**不是錯誤**。
    }
    // 有內容但損毀 → 由 deserialize 拋錯傳遞出去；絕不靜默當成空登錄（那會遺失所有安裝紀錄）。
    for (InstallRecord& rec : deserialize_install_records(text)) {
        const std::string id = rec.package_id();
        by_id_.emplace(id, std::move(rec));
    }
}

void InstalledPackageStore::flush() {
    std::vector<InstallRecord> all;
    all.reserve(by_id_.size());
    for (const auto& kv : by_id_) {
        all.push_back(kv.second);
    }
    storage_.put(slot_, serialize_install_records(all));
}

bool InstalledPackageStore::record(const Package& pkg) {
    require_valid_package(pkg, "登錄");
    if (by_id_.find(pkg.manifest.name) != by_id_.end()) {
        return false;  // 同名不覆寫（與 E9-07 InstallRegistry::add 同語意）；後端不被改寫。
    }
    InstallRecord rec;
    rec.package = pkg;
    by_id_.emplace(pkg.manifest.name, std::move(rec));
    flush();  // write-through：不留「忘了存檔」的窗口。
    return true;
}

bool InstalledPackageStore::forget(const std::string& package_id) {
    if (by_id_.erase(package_id) == 0) {
        return false;  // 查無此套件；後端不變（語意與回報由 E9-06 卸載流程負責）。
    }
    flush();
    return true;
}

bool InstalledPackageStore::contains(const std::string& package_id) const {
    return by_id_.find(package_id) != by_id_.end();
}

const Package* InstalledPackageStore::find(const std::string& package_id) const {
    const auto it = by_id_.find(package_id);
    return it == by_id_.end() ? nullptr : &it->second.package;
}

std::vector<std::string> InstalledPackageStore::list() const {
    std::vector<std::string> out;
    out.reserve(by_id_.size());
    for (const auto& kv : by_id_) {
        out.push_back(kv.first);  // std::map 有序 → 字典序。
    }
    return out;
}

std::vector<InstallRecord> InstalledPackageStore::records() const {
    std::vector<InstallRecord> out;
    out.reserve(by_id_.size());
    for (const auto& kv : by_id_) {
        out.push_back(kv.second);
    }
    return out;
}

std::string InstalledPackageStore::serialized() const {
    std::string text;
    if (!storage_.get(slot_, text)) {
        return {};
    }
    return text;
}

// -----------------------------------------------------------------------------
// 回填 E9-07 InstallRegistry
// -----------------------------------------------------------------------------

std::size_t mirror_into(const InstalledPackageStore& store, InstallRegistry& registry) {
    std::size_t added = 0;
    for (const InstallRecord& rec : store.records()) {
        if (registry.add(rec.package)) {  // 同名不覆寫 → 不計入。
            ++added;
        }
    }
    return added;
}

}  // namespace ds::package
