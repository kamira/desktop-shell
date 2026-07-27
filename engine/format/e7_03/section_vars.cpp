// E7-03 段落變數 — 實作。見 section_vars.hpp 檔首語意。平台中立、無任何 `#ifdef`。
//
// 本單元刻意「薄」：真正的替換工作（引用解析、字串內插、型別保留、巢狀 / 循環偵測、
// 遞迴進入 List / Map）全數由 E7-02 `resolve()` 承擔。本檔只負責：
//   (1) 從文件的變數段落建出一個 `VariableScope`；
//   (2) 把「其餘內容」（或全部內容）交給 E7-02 展開；
//   (3) 保留 Document 的 `format_version`。
#include "section_vars.hpp"

#include <utility>
#include <vector>

namespace ds::format {

bool build_scope(const Value& section, VariableScope& scope, ResolveError& err) {
    if (!section.is_map()) {
        err = ResolveError{"variable section is not a map (expected name -> value)", {}};
        return false;
    }
    // 依插入序把段落成員定義進作用域；值原樣（未展開）插入——成員間的巢狀 / 互相引用
    // 由 E7-02 於使用時惰性遞迴求值，故宣告順序無關。
    for (const auto& member : section.as_map()) {
        scope.define(member.first, member.second);
    }
    return true;
}

ResolveResult expand(const Value& root, const ExpandOptions& opts) {
    // 作用域鏈接呼叫端提供的外層作用域（可為 nullptr）；段落變數遮蔽父層同名變數。
    VariableScope scope(opts.parent);

    if (!root.is_map()) {
        // 非映射：無段落可抽，直接展開整棵樹（僅父層作用域可見）。
        return resolve(root, scope);
    }

    // 映射：抽出變數段落（若有）建成作用域。
    if (const Value* section = root.find(opts.vars_key)) {
        ResolveError err;
        if (!build_scope(*section, scope, err)) {
            return ResolveResult::failure(std::move(err));
        }
    }

    // 組出待展開的內容映射：strip_vars 時剔除段落本身，否則保留全部。
    std::vector<Value::Member> members;
    members.reserve(root.size());
    for (const auto& member : root.as_map()) {
        if (opts.strip_vars && member.first == opts.vars_key) {
            continue;
        }
        members.push_back(member);
    }

    // 委派 E7-02 遞迴替換其餘內容中的所有 `${name}` 引用。
    return resolve(Value::map(std::move(members)), scope);
}

DocumentResolveResult expand(const Document& doc, const ExpandOptions& opts) {
    ResolveResult r = expand(doc.root, opts);
    if (!r.ok()) {
        return DocumentResolveResult::failure(r.error());
    }
    Document out;
    out.format_version = doc.format_version;  // 版本欄位原樣保留。
    out.root = r.value();                     // 恆為 Map（root 為 Map，展開後仍為 Map）。
    return DocumentResolveResult::success(std::move(out));
}

}  // namespace ds::format
