// E7-14 圖形化設定與就地編輯 — 實作（見 editor_model.hpp）。
// engine 層 / 平台中立：無任何 #ifdef / 系統呼叫 / 真實 GUI 後端。
#include "editor_model.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace ds::format {

namespace {

// 路徑末段推出的預設標籤：Key → 鍵名；Index → "[i]"；空路徑 → ""（根純量）。
std::string default_label(const Path& path) {
    if (path.empty()) return std::string();
    const PathSegment& last = path.back();
    if (last.kind == PathSegment::Kind::Key) return last.key;
    return "[" + std::to_string(last.index) + "]";
}

// 由純量 Value 推斷控制項型別（容器不應走到這裡）。
EditKind infer_kind(const Value& v) {
    switch (v.type()) {
        case ValueType::Bool:
            return EditKind::Boolean;
        case ValueType::Number:
            return v.is_integer() ? EditKind::Integer : EditKind::Number;
        case ValueType::String:
            return is_color_literal(v.as_string()) ? EditKind::Color : EditKind::Text;
        case ValueType::Null:
        default:
            return EditKind::Text;
    }
}

// 於 schema 中找命中 path 的提示（回 nullptr = 未命中）。
const FieldHint* find_hint(const EditorSchema& schema, const Path& path) {
    for (const FieldHint& h : schema) {
        if (h.path == path) return &h;
    }
    return nullptr;
}

// 前序走訪：把每個純量葉節點收成一個 EditableField；容器遞迴；空容器不產欄位。
void collect(const Value& node, Path& prefix, const EditorSchema& schema,
             std::vector<EditableField>& out) {
    if (node.is_map()) {
        for (const Value::Member& m : node.as_map()) {
            prefix.push_back(PathSegment::of_key(m.first));
            collect(m.second, prefix, schema, out);
            prefix.pop_back();
        }
        return;
    }
    if (node.is_list()) {
        const std::vector<Value>& items = node.as_list();
        for (std::size_t i = 0; i < items.size(); ++i) {
            prefix.push_back(PathSegment::of_index(i));
            collect(items[i], prefix, schema, out);
            prefix.pop_back();
        }
        return;
    }

    // 純量葉節點。
    EditableField f;
    f.path = prefix;
    f.current = node;
    f.kind = infer_kind(node);
    f.label = default_label(prefix);
    if (const FieldHint* h = find_hint(schema, prefix)) {
        f.kind = h->kind;
        f.constraints = h->constraints;
        if (h->has_label) f.label = h->label;
    }
    out.push_back(std::move(f));
}

// 沿 path 在 root 內定位節點（唯讀）；契約違反（型別不符 / 越界 / 缺鍵）→ throw。
const Value& locate(const Value& root, const Path& path) {
    const Value* cur = &root;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const PathSegment& seg = path[i];
        if (seg.kind == PathSegment::Kind::Key) {
            if (!cur->is_map()) {
                throw std::runtime_error("apply_edit: 路徑段 '" + seg.key +
                                         "' 需 Map，但當前節點非 Map");
            }
            const Value* next = cur->find(seg.key);
            if (next == nullptr) {
                throw std::runtime_error("apply_edit: 路徑鍵 '" + seg.key + "' 不存在於文件");
            }
            cur = next;
        } else {
            if (!cur->is_list()) {
                throw std::runtime_error("apply_edit: 索引段 [" + std::to_string(seg.index) +
                                         "] 需 List，但當前節點非 List");
            }
            if (seg.index >= cur->size()) {
                throw std::runtime_error("apply_edit: 索引 [" + std::to_string(seg.index) +
                                         "] 越界");
            }
            cur = &cur->as_list()[seg.index];
        }
    }
    return *cur;
}

// 路徑 → 字串（"a.b[0].c"）。
std::string path_to_string(const Path& path) {
    std::string out;
    for (const PathSegment& seg : path) {
        if (seg.kind == PathSegment::Kind::Key) {
            if (!out.empty()) out.push_back('.');
            out += seg.key;
        } else {
            out += "[" + std::to_string(seg.index) + "]";
        }
    }
    return out;
}

}  // namespace

// -----------------------------------------------------------------------------
// EditKind / 顏色
// -----------------------------------------------------------------------------

const char* to_string(EditKind kind) noexcept {
    switch (kind) {
        case EditKind::Text:
            return "Text";
        case EditKind::Integer:
            return "Integer";
        case EditKind::Number:
            return "Number";
        case EditKind::Boolean:
            return "Boolean";
        case EditKind::Enum:
            return "Enum";
        case EditKind::Color:
            return "Color";
    }
    return "Text";
}

bool is_color_literal(const std::string& text) noexcept {
    if (text.size() != 4 && text.size() != 7) return false;
    if (text[0] != '#') return false;
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// EditConstraints
// -----------------------------------------------------------------------------

EditConstraints EditConstraints::range(double lo, double hi) {
    EditConstraints c;
    c.has_min = true;
    c.has_max = true;
    c.min_value = lo;
    c.max_value = hi;
    return c;
}

EditConstraints EditConstraints::min_of(double lo) {
    EditConstraints c;
    c.has_min = true;
    c.min_value = lo;
    return c;
}

EditConstraints EditConstraints::max_of(double hi) {
    EditConstraints c;
    c.has_max = true;
    c.max_value = hi;
    return c;
}

EditConstraints EditConstraints::enum_of(std::vector<std::string> options) {
    EditConstraints c;
    c.enum_options = std::move(options);
    return c;
}

bool EditConstraints::operator==(const EditConstraints& o) const {
    return has_min == o.has_min && has_max == o.has_max && min_value == o.min_value &&
           max_value == o.max_value && enum_options == o.enum_options;
}

// -----------------------------------------------------------------------------
// EditableField / FieldHint
// -----------------------------------------------------------------------------

std::string EditableField::path_string() const { return path_to_string(path); }

FieldHint FieldHint::at(const std::string& path_text, EditKind kind) {
    Path p;
    if (!parse_path(path_text, p)) {
        throw std::runtime_error("FieldHint::at: 路徑語法錯誤: '" + path_text + "'");
    }
    FieldHint h;
    h.path = std::move(p);
    h.kind = kind;
    return h;
}

FieldHint& FieldHint::with_label(std::string text) {
    label = std::move(text);
    has_label = true;
    return *this;
}

FieldHint& FieldHint::with_constraints(EditConstraints c) {
    constraints = std::move(c);
    return *this;
}

// -----------------------------------------------------------------------------
// 建模
// -----------------------------------------------------------------------------

std::vector<EditableField> build_editor_model(const Value& root) {
    static const EditorSchema kEmpty;
    return build_editor_model(root, kEmpty);
}

std::vector<EditableField> build_editor_model(const Value& root, const EditorSchema& schema) {
    std::vector<EditableField> out;
    Path prefix;
    collect(root, prefix, schema, out);
    return out;
}

std::vector<EditableField> build_editor_model(const Document& doc) {
    return build_editor_model(doc.root);
}

std::vector<EditableField> build_editor_model(const Document& doc, const EditorSchema& schema) {
    return build_editor_model(doc.root, schema);
}

const EditableField* find_field(const std::vector<EditableField>& model, const Path& path) {
    for (const EditableField& f : model) {
        if (f.path == path) return &f;
    }
    return nullptr;
}

const EditableField* find_field(const std::vector<EditableField>& model,
                                const std::string& path_text) {
    Path p;
    if (!parse_path(path_text, p)) return nullptr;
    return find_field(model, p);
}

// -----------------------------------------------------------------------------
// 驗證
// -----------------------------------------------------------------------------

namespace {

// 數值範圍檢查（越界 → throw，訊息含路徑）。
void check_range(const EditConstraints& c, double v, const std::string& where) {
    if (c.has_min && v < c.min_value) {
        throw std::runtime_error("validate_edit: 欄位 '" + where + "' 值 " + std::to_string(v) +
                                 " 小於下限 " + std::to_string(c.min_value));
    }
    if (c.has_max && v > c.max_value) {
        throw std::runtime_error("validate_edit: 欄位 '" + where + "' 值 " + std::to_string(v) +
                                 " 大於上限 " + std::to_string(c.max_value));
    }
}

}  // namespace

void validate_edit(const EditableField& field, const Value& new_value) {
    const std::string where = field.path_string();
    switch (field.kind) {
        case EditKind::Boolean:
            if (!new_value.is_bool()) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Boolean，需 Bool 值");
            }
            return;

        case EditKind::Integer:
            if (!new_value.is_number()) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Integer，需數值");
            }
            if (!new_value.is_integer()) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Integer，但給的是非整數 Number");
            }
            check_range(field.constraints, new_value.as_number(), where);
            return;

        case EditKind::Number:
            if (!new_value.is_number()) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Number，需數值");
            }
            check_range(field.constraints, new_value.as_number(), where);
            return;

        case EditKind::Text:
            if (!new_value.is_string()) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Text，需字串");
            }
            return;

        case EditKind::Color:
            if (!new_value.is_string() || !is_color_literal(new_value.as_string())) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Color，需 #RGB / #RRGGBB 字串");
            }
            return;

        case EditKind::Enum: {
            if (!new_value.is_string()) {
                throw std::runtime_error("validate_edit: 欄位 '" + where +
                                         "' 為 Enum，需字串");
            }
            const std::string& s = new_value.as_string();
            for (const std::string& opt : field.constraints.enum_options) {
                if (opt == s) return;
            }
            throw std::runtime_error("validate_edit: 欄位 '" + where + "' 值 '" + s +
                                     "' 不屬列舉選項");
        }
    }
    throw std::runtime_error("validate_edit: 欄位 '" + where + "' 未知控制項型別");
}

// -----------------------------------------------------------------------------
// 就地套用（經 E7-12 set_value 寫回）
// -----------------------------------------------------------------------------

Value apply_edit(const Value& root, const EditableField& field, Value new_value) {
    validate_edit(field, new_value);
    return set_value(root, field.path, std::move(new_value));  // E7-12
}

Value apply_edit(const Value& root, const Path& path, Value new_value) {
    // 由當前值推斷欄位型別後型別驗證（無範圍 / 列舉約束）。
    const Value& current = locate(root, path);  // 路徑不存在 → throw
    EditableField f;
    f.path = path;
    f.current = current;
    f.kind = infer_kind(current);
    validate_edit(f, new_value);
    return set_value(root, path, std::move(new_value));  // E7-12
}

Value apply_edit(const Value& root, const std::string& path_text, Value new_value) {
    Path p;
    if (!parse_path(path_text, p)) {
        throw std::runtime_error("apply_edit: 路徑語法錯誤: '" + path_text + "'");
    }
    return apply_edit(root, p, std::move(new_value));
}

}  // namespace ds::format
