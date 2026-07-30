// E7-12 設定值寫回 — 實作（平台中立、純邏輯，無任何平台分支）。
//
// 完全消費 E7-01 契約：定位 / 更新以純函式重建 Value 樹；序列化產出 E7-01`parse()` 能再解析
// 的文字。序列化的字串是否加引號，透過 E7-01 的 `parse_scalar` 反推——凡「裸寫會被再解析成
// 非同一字串」者即加引號並轉義，確保型別 / 內容不因來回而漂移。
#include "writeback.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace ds::format {

// =============================================================================
// PathSegment
// =============================================================================

PathSegment PathSegment::of_key(std::string k) {
    PathSegment s;
    s.kind = Kind::Key;
    s.key = std::move(k);
    return s;
}

PathSegment PathSegment::of_index(std::size_t i) {
    PathSegment s;
    s.kind = Kind::Index;
    s.index = i;
    return s;
}

bool PathSegment::operator==(const PathSegment& o) const noexcept {
    if (kind != o.kind) {
        return false;
    }
    return kind == Kind::Key ? key == o.key : index == o.index;
}

// =============================================================================
// 路徑解析："window.width" / "layers[0].name" / "grid[2][3]"
// =============================================================================

bool parse_path(const std::string& text, Path& out) {
    Path result;
    std::size_t i = 0;
    const std::size_t n = text.size();
    bool prev_was_segment = false;  // 前一步是否已產出一段（決定 '.' / '[' 的合法性）。

    while (i < n) {
        const char c = text[i];
        if (c == '[') {
            // 索引段：讀取到 ']' 為止的十進位數字。
            const std::size_t close = text.find(']', i + 1);
            if (close == std::string::npos || close == i + 1) {
                return false;  // 未閉合或空括號。
            }
            std::size_t value = 0;
            for (std::size_t k = i + 1; k < close; ++k) {
                const char d = text[k];
                if (d < '0' || d > '9') {
                    return false;  // 括號內須為純數字。
                }
                value = value * 10 + static_cast<std::size_t>(d - '0');
            }
            result.push_back(PathSegment::of_index(value));
            prev_was_segment = true;
            i = close + 1;
        } else if (c == '.') {
            if (!prev_was_segment) {
                return false;  // 前導 '.' 或連續 '..'（無左段）。
            }
            prev_was_segment = false;  // 期待其後緊接一個鍵。
            ++i;
        } else {
            // 鍵段：讀到下一個 '.' 或 '[' 或字串結尾。
            const std::size_t start = i;
            while (i < n && text[i] != '.' && text[i] != '[') {
                ++i;
            }
            if (i == start) {
                return false;  // 空鍵。
            }
            result.push_back(PathSegment::of_key(text.substr(start, i - start)));
            prev_was_segment = true;
        }
    }

    if (!prev_was_segment && !result.empty()) {
        return false;  // 以懸空的 '.' 結尾。
    }
    out = std::move(result);
    return true;
}

// =============================================================================
// 設定值寫回：純函式重建
// =============================================================================

namespace {

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error("set_value: " + msg); }

Value set_impl(const Value& node, const Path& path, std::size_t depth, Value value) {
    const PathSegment& seg = path[depth];
    const bool last = depth + 1 == path.size();

    if (seg.kind == PathSegment::Kind::Key) {
        if (!node.is_map()) {
            fail("以鍵 '" + seg.key + "' 定位，但目標節點不是 Map");
        }
        std::vector<Value::Member> members = node.as_map();  // 複製（值語意）。
        for (auto& m : members) {
            if (m.first == seg.key) {
                m.second = last ? std::move(value)
                                : set_impl(m.second, path, depth + 1, std::move(value));
                return Value::map(std::move(members));
            }
        }
        // 鍵不存在 → 新增。
        if (last) {
            members.emplace_back(seg.key, std::move(value));
        } else {
            if (path[depth + 1].kind == PathSegment::Kind::Index) {
                fail("無法在缺失的中繼段 '" + seg.key + "' 以索引自動建立 List");
            }
            Value child = set_impl(Value::map({}), path, depth + 1, std::move(value));
            members.emplace_back(seg.key, std::move(child));
        }
        return Value::map(std::move(members));
    }

    // Index 段。
    if (!node.is_list()) {
        fail("以索引 " + std::to_string(seg.index) + " 定位，但目標節點不是 List");
    }
    std::vector<Value> items = node.as_list();  // 複製。
    if (seg.index < items.size()) {
        items[seg.index] = last ? std::move(value)
                                : set_impl(items[seg.index], path, depth + 1, std::move(value));
    } else if (seg.index == items.size() && last) {
        items.push_back(std::move(value));  // 附加於尾端。
    } else {
        fail("索引 " + std::to_string(seg.index) + " 越界（size=" +
             std::to_string(items.size()) + "）");
    }
    return Value::list(std::move(items));
}

}  // namespace

Value set_value(const Value& root, const Path& path, Value value) {
    if (path.empty()) {
        return value;  // 取代整棵。
    }
    return set_impl(root, path, 0, std::move(value));
}

Value set_value(const Value& root, const std::string& path, Value value) {
    Path parsed;
    if (!parse_path(path, parsed)) {
        throw std::runtime_error("set_value: 路徑語法錯誤: " + path);
    }
    return set_value(root, parsed, std::move(value));
}

// =============================================================================
// 序列化
// =============================================================================

namespace {

// 非整數 Number → 可 round-trip 的最短十進位表示（確保再解析為相同 double 且仍為非整數）。
std::string format_double(double d) {
    char buf[64];
    for (int prec : {15, 16, 17}) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) {
            break;
        }
    }
    std::string s(buf);
    // 確保帶小數 / 指數標記，否則再解析會被當成整數（integral 旗標漂移）。
    if (s.find_first_of(".eEnN") == std::string::npos) {
        s += ".0";
    }
    return s;
}

// 以雙引號包裹並轉義字串（對應 E7-01 parse_quoted 支援的轉義集）。
std::string quote(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case '\0': out += "\\0"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

// 判斷裸字串是否需加引號：凡再解析後不等於原字串（型別漂移 / 被 trim / 需轉義）者皆須。
bool needs_quote(const std::string& s) {
    if (s.empty()) {
        return true;  // 空字串裸寫會被當成巢狀 null。
    }
    for (const char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == '\0') {
            return true;  // 破壞行結構 / 需轉義。
        }
    }
    // 借 E7-01 的型別推斷反推：裸寫 s 再解析須仍為「內容完全相同的 String」。
    Value v;
    if (!parse_scalar(s, v)) {
        return true;  // 形如未終止引號等 → 必須引號化。
    }
    if (v.type() != ValueType::String || v.as_string() != s) {
        return true;  // 會被解析成 null/bool/number，或被 trim / 轉義改寫。
    }
    return false;
}

// 純量節點 → 單行文字表示。
std::string scalar_text(const Value& v) {
    switch (v.type()) {
        case ValueType::Null:
            return "null";
        case ValueType::Bool:
            return v.as_bool() ? "true" : "false";
        case ValueType::Number:
            return v.is_integer() ? std::to_string(v.as_int()) : format_double(v.as_number());
        case ValueType::String:
            return needs_quote(v.as_string()) ? quote(v.as_string()) : v.as_string();
        default:
            return {};  // 容器不由此處理。
    }
}

bool is_scalar(const Value& v) {
    return v.is_null() || v.is_bool() || v.is_number() || v.is_string();
}

const std::string kIndentUnit = "  ";  // 每層 2 空白。

std::string indent_of(std::size_t level) {
    std::string s;
    s.reserve(level * kIndentUnit.size());
    for (std::size_t i = 0; i < level; ++i) {
        s += kIndentUnit;
    }
    return s;
}

void emit_block(const Value& node, std::size_t level, std::string& out);

// 附加一個「命名子項」：於 map 條目（`key:`）或 list 項目（`-`）之後接純量 / 巢狀區塊。
// prefix 為該行已含縮排的引導（如 "  key:" 或 "  -"）。
void emit_child(const std::string& prefix, const Value& child, std::size_t child_level,
                std::string& out) {
    if (is_scalar(child)) {
        out += prefix;
        out += ' ';
        out += scalar_text(child);
        out += '\n';
    } else if ((child.is_map() || child.is_list()) && child.size() == 0) {
        // 空容器無法 round-trip 為同型（見 header 限制）；輸出無子項的引導，再解析為 null。
        out += prefix;
        out += '\n';
    } else {
        out += prefix;
        out += '\n';
        emit_block(child, child_level, out);
    }
}

// 序列化一個 Map / List 區塊（其兄弟項皆在 level 縮排）。
void emit_block(const Value& node, std::size_t level, std::string& out) {
    const std::string ind = indent_of(level);
    if (node.is_map()) {
        for (const auto& m : node.as_map()) {
            emit_child(ind + m.first + ":", m.second, level + 1, out);
        }
    } else if (node.is_list()) {
        for (const auto& item : node.as_list()) {
            emit_child(ind + "-", item, level + 1, out);
        }
    }
}

}  // namespace

std::string serialize_value(const Value& root) {
    if (is_scalar(root)) {
        return scalar_text(root) + "\n";
    }
    std::string out;
    emit_block(root, 0, out);
    return out;
}

std::string serialize(const Document& doc) {
    std::string out = "format_version: " + std::to_string(doc.format_version.major) + "." +
                      std::to_string(doc.format_version.minor) + "\n";
    if (doc.root.is_map()) {
        emit_block(doc.root, 0, out);
    } else if (!doc.root.is_null()) {
        // 契約上 root 恆為 Map；防禦性地容納非 map（不靜默丟棄）。
        out += serialize_value(doc.root);
    }
    return out;
}

std::string serialize(const Value& root, const FormatVersion& version) {
    Document doc;
    doc.format_version = version;
    doc.root = root;
    return serialize(doc);
}

}  // namespace ds::format
