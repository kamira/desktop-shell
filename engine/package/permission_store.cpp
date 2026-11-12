// E9-11 權限授予狀態與決策記錄 — 實作。見 permission_store.hpp。
//
// 平台中立、純邏輯：無任何 `#ifdef` / 平台分支 / 系統呼叫 / 真實檔案 I/O。
// 序列化一律經 E7-12 `serialize`，反序列化一律經 E7-01 `parse`——不重造格式。
// E8-04 只被**呼叫**（`provide_capability` / `grant_permission`），一行未改。
#include "permission_store.hpp"

#include <set>
#include <stdexcept>
#include <utility>

namespace ds::package {

using ds::format::Value;

namespace {

// --- 文件欄位名與字面值（單一出處，序列化 / 反序列化共用，避免字面字串漂移）-----------
constexpr const char* kKeyDecisions = "decisions";
constexpr const char* kKeyPackage = "package";
constexpr const char* kKeyPermission = "permission";
constexpr const char* kKeyDecision = "decision";

constexpr const char* kLiteralUndecided = "undecided";
constexpr const char* kLiteralGranted = "granted";
constexpr const char* kLiteralDenied = "denied";

// 取 Map 內某鍵的非空字串；缺鍵 / 型別不符 / 空字串 → throw（明確不靜默）。
std::string require_nonempty_string(const Value& map, const std::string& key,
                                    const std::string& where) {
    const Value* v = map.find(key);
    if (v == nullptr) {
        throw std::runtime_error("E9-11: " + where + " 缺少必要欄位 '" + key + "'");
    }
    if (!v->is_string()) {
        throw std::runtime_error("E9-11: " + where + " 欄位 '" + key + "' 必須為字串");
    }
    if (v->as_string().empty()) {
        throw std::runtime_error("E9-11: " + where + " 欄位 '" + key + "' 不得為空");
    }
    return v->as_string();
}

// 去重並保留首次出現順序。manifest 內重複宣告同一權限時使用（E9-02 不禁止重複項）。
std::vector<std::string> dedupe_keeping_order(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    out.reserve(items.size());
    for (const std::string& s : items) {
        if (seen.insert(s).second) {
            out.push_back(s);
        }
    }
    return out;
}

void require_nonempty_ids(const std::string& package_id, const std::string& permission_id,
                          const std::string& where) {
    if (package_id.empty()) {
        throw std::runtime_error("E9-11: " + where + "：package_id 不得為空");
    }
    if (permission_id.empty()) {
        throw std::runtime_error("E9-11: " + where + "：permission_id 不得為空");
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// 三態 ↔ 字面值
// -----------------------------------------------------------------------------

const char* to_string(PermissionDecision decision) noexcept {
    switch (decision) {
        case PermissionDecision::Undecided:
            return kLiteralUndecided;
        case PermissionDecision::Granted:
            return kLiteralGranted;
        case PermissionDecision::Denied:
            return kLiteralDenied;
    }
    return "unknown";
}

bool parse_permission_decision(const std::string& text, PermissionDecision& out) noexcept {
    if (text == kLiteralUndecided) {
        out = PermissionDecision::Undecided;
        return true;
    }
    if (text == kLiteralGranted) {
        out = PermissionDecision::Granted;
        return true;
    }
    if (text == kLiteralDenied) {
        out = PermissionDecision::Denied;
        return true;
    }
    return false;  // 無法辨識 → 呼叫端必須明確處理（不靜默當成某一態）。
}

// -----------------------------------------------------------------------------
// 序列化 / 反序列化（透過 E7-12 / E7-01）
// -----------------------------------------------------------------------------

std::string serialize_permission_records(const std::vector<PermissionRecord>& records) {
    std::set<std::pair<std::string, std::string>> seen;
    std::vector<Value> items;
    items.reserve(records.size());

    for (const PermissionRecord& rec : records) {
        require_nonempty_ids(rec.package_id, rec.permission_id, "序列化");
        if (rec.decision == PermissionDecision::Undecided) {
            // 未決 = 沒有紀錄。寫出一筆「未決紀錄」是語意矛盾，明確拒絕而非靜默略過。
            throw std::runtime_error("E9-11: 序列化：未決不得被寫成一筆紀錄（套件 '" +
                                     rec.package_id + "' 權限 '" + rec.permission_id + "'）");
        }
        if (!seen.insert({rec.package_id, rec.permission_id}).second) {
            throw std::runtime_error("E9-11: 序列化：重複的 (套件, 權限) = ('" + rec.package_id +
                                     "', '" + rec.permission_id + "')");
        }
        // 套件名 / 權限名皆放在**值**的位置（非 map 鍵），以規避 E7-12「map 鍵不支援引號」。
        items.push_back(Value::map({
            {kKeyPackage, Value::string(rec.package_id)},
            {kKeyPermission, Value::string(rec.permission_id)},
            {kKeyDecision, Value::string(to_string(rec.decision))},
        }));
    }

    std::vector<Value::Member> root;
    root.emplace_back(kKeyDecisions, Value::list(std::move(items)));
    // E7-12 `serialize(root, version)`：產出帶 format_version 首行的 E7-01 宣告式文字。
    return ds::format::serialize(Value::map(std::move(root)), kPermissionLedgerFormat);
}

std::vector<PermissionRecord> deserialize_permission_records(const std::string& text) {
    const ds::format::ParseResult result = ds::format::parse(text);
    if (!result.ok()) {
        const ds::format::ParseError& err = result.error();
        throw std::runtime_error("E9-11: 權限帳本文字解析失敗（行 " + std::to_string(err.line) +
                                 "）：" + err.message);
    }

    const ds::format::Document& doc = result.document();
    if (!doc.root.is_map()) {
        throw std::runtime_error("E9-11: 權限帳本根節點必須為 Map");
    }

    std::vector<PermissionRecord> out;
    const Value* decisions = doc.root.find(kKeyDecisions);
    // 缺鍵或 Null（空清單於 E7-01 塌為 Null）→ 空帳本，屬合法狀態（尚未做過任何決定）。
    if (decisions == nullptr || decisions->is_null()) {
        return out;
    }
    if (!decisions->is_list()) {
        throw std::runtime_error("E9-11: 欄位 'decisions' 必須為清單");
    }

    std::set<std::pair<std::string, std::string>> seen;
    const std::vector<Value>& items = decisions->as_list();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string where = "decisions[" + std::to_string(i) + "]";
        if (!items[i].is_map()) {
            throw std::runtime_error("E9-11: " + where + " 必須為 Map");
        }
        PermissionRecord rec;
        rec.package_id = require_nonempty_string(items[i], kKeyPackage, where);
        rec.permission_id = require_nonempty_string(items[i], kKeyPermission, where);

        const std::string literal = require_nonempty_string(items[i], kKeyDecision, where);
        if (!parse_permission_decision(literal, rec.decision)) {
            throw std::runtime_error("E9-11: " + where + " 無法辨識的 decision 值 '" + literal +
                                     "'（僅接受 granted / denied / undecided）");
        }

        if (!seen.insert({rec.package_id, rec.permission_id}).second) {
            throw std::runtime_error("E9-11: " + where + " 重複的 (套件, 權限) = ('" +
                                     rec.package_id + "', '" + rec.permission_id + "')");
        }
        if (rec.decision == PermissionDecision::Undecided) {
            continue;  // 字面 undecided 不帶任何資訊（見 .hpp 檔首）：接受並略過。
        }
        out.push_back(std::move(rec));
    }
    return out;
}

// -----------------------------------------------------------------------------
// PermissionLedger
// -----------------------------------------------------------------------------

PermissionLedger::PermissionLedger(PermissionStorage& storage, std::string slot)
    : storage_(storage), slot_(std::move(slot)) {
    if (slot_.empty()) {
        throw std::runtime_error("E9-11: 儲存 slot 名稱不得為空");
    }
    reload();  // 建構即自後端重建 —— 這就是相位 1 的「重啟」rehydrate 點。
}

void PermissionLedger::reload() {
    std::string text;
    by_key_.clear();
    if (!storage_.get(slot_, text)) {
        return;  // 後端尚無此 slot = 初次啟動，空帳本，**不是錯誤**。
    }
    // 有內容但損毀 → 由反序列化拋錯傳遞出去；絕不靜默當成空帳本（那會把所有「已拒絕」
    // 悄悄變回「未決」，下一次安裝流程就會重新詢問已被拒絕過的權限）。
    for (const PermissionRecord& rec : deserialize_permission_records(text)) {
        by_key_[Key{rec.package_id, rec.permission_id}] = rec.decision;
    }
}

void PermissionLedger::flush() {
    std::vector<PermissionRecord> all;
    all.reserve(by_key_.size());
    for (const auto& kv : by_key_) {
        all.push_back(PermissionRecord{kv.first.first, kv.first.second, kv.second});
    }
    storage_.put(slot_, serialize_permission_records(all));
}

PermissionDecision PermissionLedger::decision(const std::string& package_id,
                                              const std::string& permission_id) const {
    const auto it = by_key_.find(Key{package_id, permission_id});
    // 查無紀錄 = **未決**，不是已拒絕。二者語意不同（見 .hpp 檔首）。
    return it == by_key_.end() ? PermissionDecision::Undecided : it->second;
}

bool PermissionLedger::is_decided(const std::string& package_id,
                                 const std::string& permission_id) const {
    return decision(package_id, permission_id) != PermissionDecision::Undecided;
}

bool PermissionLedger::set(const std::string& package_id, const std::string& permission_id,
                           PermissionDecision decided) {
    require_nonempty_ids(package_id, permission_id, "記錄決定");
    const Key key{package_id, permission_id};
    const auto it = by_key_.find(key);
    if (it != by_key_.end() && it->second == decided) {
        return false;  // 狀態未變 → 後端不被改寫。
    }
    by_key_[key] = decided;
    flush();  // write-through：不留「忘了存檔」的窗口。
    return true;
}

bool PermissionLedger::grant(const std::string& package_id, const std::string& permission_id) {
    return set(package_id, permission_id, PermissionDecision::Granted);
}

bool PermissionLedger::deny(const std::string& package_id, const std::string& permission_id) {
    return set(package_id, permission_id, PermissionDecision::Denied);
}

bool PermissionLedger::revoke(const std::string& package_id, const std::string& permission_id) {
    require_nonempty_ids(package_id, permission_id, "撤銷決定");
    if (by_key_.erase(Key{package_id, permission_id}) == 0) {
        return false;  // 本來就沒有決定（未決）→ 後端不變。
    }
    flush();  // **撤銷也要落盤**：否則「已授予 → 撤銷 → 重啟 → 又變成已授予」。
    return true;
}

std::size_t PermissionLedger::revoke_package(const std::string& package_id) {
    if (package_id.empty()) {
        throw std::runtime_error("E9-11: 撤銷套件決定：package_id 不得為空");
    }
    std::size_t removed = 0;
    for (auto it = by_key_.begin(); it != by_key_.end();) {
        if (it->first.first == package_id) {
            it = by_key_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        flush();
    }
    return removed;
}

std::vector<PermissionRecord> PermissionLedger::records() const {
    std::vector<PermissionRecord> out;
    out.reserve(by_key_.size());
    for (const auto& kv : by_key_) {  // std::map 有序 → (套件, 權限) 字典序。
        out.push_back(PermissionRecord{kv.first.first, kv.first.second, kv.second});
    }
    return out;
}

std::vector<PermissionRecord> PermissionLedger::records_for(const std::string& package_id) const {
    std::vector<PermissionRecord> out;
    for (const auto& kv : by_key_) {
        if (kv.first.first == package_id) {
            out.push_back(PermissionRecord{kv.first.first, kv.first.second, kv.second});
        }
    }
    return out;
}

std::vector<std::string> PermissionLedger::packages() const {
    std::vector<std::string> out;
    for (const auto& kv : by_key_) {
        if (out.empty() || out.back() != kv.first.first) {  // 已排序 → 相鄰去重即可。
            out.push_back(kv.first.first);
        }
    }
    return out;
}

std::string PermissionLedger::serialized() const {
    std::string text;
    if (!storage_.get(slot_, text)) {
        return {};
    }
    return text;
}

// -----------------------------------------------------------------------------
// 閘門評估
// -----------------------------------------------------------------------------

PermissionGate evaluate_permissions(const PermissionLedger& ledger, const std::string& package_id,
                                    const std::vector<std::string>& permissions) {
    PermissionGate gate;
    for (const std::string& perm : dedupe_keeping_order(permissions)) {
        switch (ledger.decision(package_id, perm)) {
            case PermissionDecision::Granted:
                gate.granted.push_back(perm);
                break;
            case PermissionDecision::Undecided:
                gate.undecided.push_back(perm);
                gate.withheld.push_back(perm);  // 宣告順序：與 E8-04 missing 逐項同序。
                break;
            case PermissionDecision::Denied:
                gate.denied.push_back(perm);
                gate.withheld.push_back(perm);
                break;
        }
    }
    // 可載入 iff 每一項宣告的權限都是 Granted。**不存在「部分授權也可載入」的路徑。**
    gate.loadable = gate.withheld.empty();
    return gate;
}

PermissionGate evaluate_permissions(const PermissionLedger& ledger, const Package& pkg) {
    return evaluate_permissions(ledger, pkg.manifest.name, pkg.manifest.permissions);
}

// -----------------------------------------------------------------------------
// 投影：組出 E8-04 HostEnvironment（只呼叫其公開介面，E8-04 一行未改）
// -----------------------------------------------------------------------------

ds::ext::HostEnvironment compose_host_environment(
    const PermissionLedger& ledger, const std::string& package_id,
    const std::vector<std::string>& permissions,
    const std::vector<std::string>& host_capabilities) {
    ds::ext::HostEnvironment env;
    for (const std::string& cap : host_capabilities) {
        env.provide_capability(cap);  // 能力由 host 自身決定，不是權限決策的一部分。
    }
    for (const std::string& perm : permissions) {
        // **只有 Granted 才授予**。Undecided 與 Denied 都不呼叫 grant_permission()——
        // 有損投影的安全方向；嚴禁把未決當成預設授予（樂觀預設 = 靜默降級）。
        if (ledger.decision(package_id, perm) == PermissionDecision::Granted) {
            env.grant_permission(perm);
        }
    }
    return env;
}

ds::ext::HostEnvironment compose_host_environment(
    const PermissionLedger& ledger, const Package& pkg,
    const std::vector<std::string>& host_capabilities) {
    return compose_host_environment(ledger, pkg.manifest.name, pkg.manifest.permissions,
                                    host_capabilities);
}

// -----------------------------------------------------------------------------
// 與 E9-10 的接點
// -----------------------------------------------------------------------------

std::vector<PermissionPrompt> permissions_awaiting_decision(const InstalledPackageStore& installed,
                                                            const PermissionLedger& ledger) {
    std::vector<PermissionPrompt> out;
    for (const std::string& package_id : installed.list()) {  // E9-10 list() = 字典序
        const Package* pkg = installed.find(package_id);      // 只讀中繼資料，不載入套件本體
        if (pkg == nullptr) {
            continue;
        }
        // 只收**未決**：已拒絕的不要再問（三態的用處正在此），已授予的也不必問。
        for (const std::string& perm : evaluate_permissions(ledger, *pkg).undecided) {
            out.push_back(PermissionPrompt{package_id, perm});
        }
    }
    return out;
}

}  // namespace ds::package
