// E7-09 缺席模組的降級解析 — 實作。見 fallback.hpp 檔首。
#include "fallback.hpp"

#include <algorithm>
#include <utility>

namespace ds::format {

// -----------------------------------------------------------------------------
// 可用性來源
// -----------------------------------------------------------------------------

ModuleSet::ModuleSet(std::vector<std::string> ids) {
    for (auto& id : ids) {
        add(std::move(id));
    }
}

void ModuleSet::add(std::string id) {
    if (std::find(ids_.begin(), ids_.end(), id) == ids_.end()) {
        ids_.push_back(std::move(id));
    }
}

bool ModuleSet::has(const std::string& module_id) const {
    return std::find(ids_.begin(), ids_.end(), module_id) != ids_.end();
}

FnAvailability::FnAvailability(std::function<bool(const std::string&)> fn)
    : fn_(std::move(fn)) {}

bool FnAvailability::has(const std::string& module_id) const {
    return fn_ ? fn_(module_id) : false;
}

ModuleSet available_from_manifests(const std::vector<ds::package::Manifest>& installed) {
    ModuleSet s;
    for (const auto& m : installed) {
        if (!m.name.empty()) {
            s.add(m.name);
        }
    }
    return s;
}

// -----------------------------------------------------------------------------
// 走訪
// -----------------------------------------------------------------------------

namespace {

// 降級走訪過程中共享的累積器（缺席清單 + 決策清單 + 可用性判定）。
struct Ctx {
    const ModuleAvailability& avail;
    std::vector<std::string>& missing;
    std::vector<DegradeNote>& notes;
};

// 去重收進缺席清單（保首見序）。
void add_missing(Ctx& ctx, const std::string& id) {
    if (std::find(ctx.missing.begin(), ctx.missing.end(), id) == ctx.missing.end()) {
        ctx.missing.push_back(id);
    }
}

// 若 v 為「模組區塊」（Map 且含字串 `module` 鍵），回傳其非空 module id；否則回傳空字串。
std::string module_id_of(const Value& v) {
    if (!v.is_map()) {
        return std::string();
    }
    const Value* m = v.find(kModuleKey);
    if (m != nullptr && m->is_string()) {
        return m->as_string();
    }
    return std::string();
}

// 建立停用佔位節點：保留 module id，明確標記 disabled 並附原因（可見、不靜默）。
Value make_placeholder(const std::string& id) {
    std::vector<Value::Member> members;
    members.emplace_back(std::string(kModuleKey), Value::string(id));
    members.emplace_back(std::string("disabled"), Value::boolean(true));
    members.emplace_back(std::string("degraded_reason"),
                         Value::string("module '" + id + "' is not available; block disabled"));
    return Value::map(std::move(members));
}

// 純回報：掃描子樹，將所有缺席模組引用記入 notes / missing（不改值）。
// 用於「缺席區塊」之下——其值已被佔位取代，但巢狀缺席引用仍須完整回報。
void collect_missing(const Value& v, const std::string& path, Ctx& ctx) {
    if (v.is_map()) {
        const std::string id = module_id_of(v);
        if (!id.empty() && !ctx.avail.has(id)) {
            add_missing(ctx, id);
            ctx.notes.push_back(DegradeNote{
                id, path,
                "referenced module '" + id + "' is absent (nested under a disabled block)"});
        }
        for (const auto& kv : v.as_map()) {
            collect_missing(kv.second, path + "." + kv.first, ctx);
        }
    } else if (v.is_list()) {
        const auto& items = v.as_list();
        for (std::size_t i = 0; i < items.size(); ++i) {
            collect_missing(items[i], path + "[" + std::to_string(i) + "]", ctx);
        }
    }
}

// 降級轉換：回傳降級後的 Value；沿途將決策 / 缺席記入 ctx。
Value transform(const Value& v, const std::string& path, Ctx& ctx) {
    if (v.is_map()) {
        const std::string id = module_id_of(v);
        if (!id.empty() && !ctx.avail.has(id)) {
            // 缺席模組區塊 → 以停用佔位取代（決策可見）。
            add_missing(ctx, id);
            ctx.notes.push_back(DegradeNote{
                id, path,
                "referenced module '" + id +
                    "' is absent; replaced with disabled placeholder"});
            // 仍掃描其子樹以完整回報巢狀缺席引用（僅回報，不入值）。
            for (const auto& kv : v.as_map()) {
                collect_missing(kv.second, path + "." + kv.first, ctx);
            }
            return make_placeholder(id);
        }
        // 一般 Map 或可用模組區塊：遞迴處理成員（處理巢狀引用），原結構保留。
        std::vector<Value::Member> out;
        out.reserve(v.as_map().size());
        for (const auto& kv : v.as_map()) {
            out.emplace_back(kv.first, transform(kv.second, path + "." + kv.first, ctx));
        }
        return Value::map(std::move(out));
    }
    if (v.is_list()) {
        const auto& items = v.as_list();
        std::vector<Value> out;
        out.reserve(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            out.push_back(transform(items[i], path + "[" + std::to_string(i) + "]", ctx));
        }
        return Value::list(std::move(out));
    }
    // 純量：原樣。
    return v;
}

}  // namespace

DegradeResult resolve_with_fallback(const Value& doc, const ModuleAvailability& avail) {
    DegradeResult r;
    Ctx ctx{avail, r.missing, r.notes};
    r.value = transform(doc, "root", ctx);
    return r;
}

}  // namespace ds::format
