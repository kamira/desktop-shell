// E7-01 宣告式格式核心 — 實作（平台中立、純邏輯，無任何平台分支）。
#include "document.hpp"

#include <stdexcept>

namespace ds::format {

// =============================================================================
// Value
// =============================================================================

Value Value::null() { return Value{}; }

Value Value::boolean(bool b) {
    Value v;
    v.type_ = ValueType::Bool;
    v.bool_ = b;
    return v;
}

Value Value::number(double n, bool integral) {
    Value v;
    v.type_ = ValueType::Number;
    v.num_ = n;
    v.integral_ = integral;
    return v;
}

Value Value::integer(std::int64_t n) {
    return number(static_cast<double>(n), /*integral=*/true);
}

Value Value::string(std::string s) {
    Value v;
    v.type_ = ValueType::String;
    v.str_ = std::move(s);
    return v;
}

Value Value::list(std::vector<Value> items) {
    Value v;
    v.type_ = ValueType::List;
    v.list_ = std::move(items);
    return v;
}

Value Value::map(std::vector<Member> members) {
    Value v;
    v.type_ = ValueType::Map;
    v.map_ = std::move(members);
    return v;
}

bool Value::as_bool() const {
    if (type_ != ValueType::Bool) {
        throw std::runtime_error("Value::as_bool: 型別不是 Bool");
    }
    return bool_;
}

double Value::as_number() const {
    if (type_ != ValueType::Number) {
        throw std::runtime_error("Value::as_number: 型別不是 Number");
    }
    return num_;
}

std::int64_t Value::as_int() const {
    if (type_ != ValueType::Number) {
        throw std::runtime_error("Value::as_int: 型別不是 Number");
    }
    return static_cast<std::int64_t>(num_);
}

const std::string& Value::as_string() const {
    if (type_ != ValueType::String) {
        throw std::runtime_error("Value::as_string: 型別不是 String");
    }
    return str_;
}

const std::vector<Value>& Value::as_list() const {
    if (type_ != ValueType::List) {
        throw std::runtime_error("Value::as_list: 型別不是 List");
    }
    return list_;
}

const std::vector<Value::Member>& Value::as_map() const {
    if (type_ != ValueType::Map) {
        throw std::runtime_error("Value::as_map: 型別不是 Map");
    }
    return map_;
}

std::size_t Value::size() const {
    if (type_ == ValueType::List) {
        return list_.size();
    }
    if (type_ == ValueType::Map) {
        return map_.size();
    }
    throw std::runtime_error("Value::size: 型別不是 List 或 Map");
}

bool Value::contains(const std::string& key) const { return find(key) != nullptr; }

const Value* Value::find(const std::string& key) const {
    if (type_ != ValueType::Map) {
        throw std::runtime_error("Value::find: 型別不是 Map");
    }
    for (const auto& m : map_) {
        if (m.first == key) {
            return &m.second;
        }
    }
    return nullptr;
}

const Value& Value::at(const std::string& key) const {
    const Value* p = find(key);
    if (p == nullptr) {
        throw std::runtime_error("Value::at: 找不到 key: " + key);
    }
    return *p;
}

std::vector<std::string> Value::keys() const {
    if (type_ != ValueType::Map) {
        throw std::runtime_error("Value::keys: 型別不是 Map");
    }
    std::vector<std::string> out;
    out.reserve(map_.size());
    for (const auto& m : map_) {
        out.push_back(m.first);
    }
    return out;
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) {
        return false;
    }
    switch (type_) {
        case ValueType::Null:
            return true;
        case ValueType::Bool:
            return bool_ == o.bool_;
        case ValueType::Number:
            // 整數旗標與數值皆須相同（1 與 1.0 視為不同節點，來源型別有別）。
            return integral_ == o.integral_ && num_ == o.num_;
        case ValueType::String:
            return str_ == o.str_;
        case ValueType::List:
            return list_ == o.list_;
        case ValueType::Map:
            return map_ == o.map_;  // 保序比較。
    }
    return false;
}

// =============================================================================
// 版本相容
// =============================================================================

bool is_format_compatible(const FormatVersion& fmt, const FormatVersion& supported) noexcept {
    return fmt.major == supported.major && fmt.minor <= supported.minor;
}

// =============================================================================
// ParseResult
// =============================================================================

ParseResult ParseResult::success(Document d) {
    ParseResult r;
    r.ok_ = true;
    r.document_ = std::move(d);
    return r;
}

ParseResult ParseResult::failure(ParseError e) {
    ParseResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// =============================================================================
// 純量型別推斷
// =============================================================================

namespace {

// 去除頭尾空白（space / tab / CR / LF）。
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return {};
    }
    const std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// 解析 "major.minor" 為 FormatVersion。回傳 false = 格式無法解析（不得靜默）。
bool parse_version(const std::string& value, FormatVersion& out) {
    const std::size_t dot = value.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= value.size()) {
        return false;
    }
    const std::string maj = value.substr(0, dot);
    const std::string min = value.substr(dot + 1);

    auto all_digits = [](const std::string& s) {
        if (s.empty()) {
            return false;
        }
        for (const char c : s) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    };
    // 次版號不得再含 '.'（拒絕 "1.2.3" 這類非 major.minor 形式）。
    if (!all_digits(maj) || !all_digits(min)) {
        return false;
    }
    out.major = std::stoi(maj);
    out.minor = std::stoi(min);
    return true;
}

// token 是否為合法數字字面值；同時回報是否為整數（無 '.' 與指數）。
bool numeric_token(const std::string& t, double& out, bool& integral) {
    if (t.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (t[i] == '+' || t[i] == '-') {
        ++i;
    }
    bool has_digit = false;
    bool is_int = true;

    // 整數部。
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
        has_digit = true;
        ++i;
    }
    // 小數部。
    if (i < t.size() && t[i] == '.') {
        is_int = false;
        ++i;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
            has_digit = true;
            ++i;
        }
    }
    if (!has_digit) {
        return false;
    }
    // 指數部。
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        is_int = false;
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
            ++i;
        }
        bool exp_digit = false;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
            exp_digit = true;
            ++i;
        }
        if (!exp_digit) {
            return false;
        }
    }
    if (i != t.size()) {
        return false;  // 有殘餘字元 → 非純數字（視為字串）。
    }
    out = std::stod(t);
    integral = is_int;
    return true;
}

// 解析雙引號字串（支援 \\ \" \n \t \r \0 轉義）。
// 回傳 false = 格式錯誤（未終止、殘餘字元或未知轉義）。以狀態機掃描，正確處理
// 「反斜線轉義結尾引號」這類未終止情形。
bool parse_quoted(const std::string& t, std::string& out) {
    if (t.size() < 2 || t.front() != '"') {
        return false;
    }
    std::string result;
    std::size_t i = 1;
    bool closed = false;
    while (i < t.size()) {
        const char c = t[i];
        if (c == '\\') {
            if (i + 1 >= t.size()) {
                return false;  // 懸空反斜線。
            }
            const char n = t[i + 1];
            switch (n) {
                case '\\': result.push_back('\\'); break;
                case '"': result.push_back('"'); break;
                case 'n': result.push_back('\n'); break;
                case 't': result.push_back('\t'); break;
                case 'r': result.push_back('\r'); break;
                case '0': result.push_back('\0'); break;
                default: return false;  // 未知轉義 → 錯誤（不靜默）。
            }
            i += 2;
        } else if (c == '"') {
            closed = true;
            ++i;
            break;
        } else {
            result.push_back(c);
            ++i;
        }
    }
    if (!closed) {
        return false;  // 無結尾引號（未終止）。
    }
    if (i != t.size()) {
        return false;  // 結尾引號後仍有殘餘字元。
    }
    out = std::move(result);
    return true;
}

// 解析器內部錯誤傳遞：於 helper 拋出、在 parse() 邊界捕捉並轉為 failure。
struct ParseFail {
    ParseError err;
};

[[noreturn]] void fail(std::size_t line, std::string msg) {
    throw ParseFail{ParseError{line, std::move(msg)}};
}

// 預處理後的一行邏輯行。
struct Line {
    std::size_t indent = 0;   // 前導空白（space）數。
    std::string content;      // 去縮排並 rstrip 後的內容。
    std::size_t line_no = 0;  // 1-based 原始行號。
};

// 是否為清單項目行（"-" 獨佔，或 "- " 起頭）。
bool is_list_item(const std::string& content) {
    return content == "-" || (content.size() >= 2 && content[0] == '-' && content[1] == ' ');
}

// 前置宣告（map / list 區塊互相遞迴）。
Value parse_block(const std::vector<Line>& L, std::size_t& i, std::size_t block_indent,
                  bool is_root, FormatVersion& ver, bool& saw_ver);

// 解析一個純量 token（token 已去頭尾空白且非空）。
Value scalar_value(const std::string& token, std::size_t line_no) {
    if (token == "null") {
        return Value::null();
    }
    if (token == "true") {
        return Value::boolean(true);
    }
    if (token == "false") {
        return Value::boolean(false);
    }
    if (!token.empty() && token.front() == '"') {
        std::string s;
        if (!parse_quoted(token, s)) {
            fail(line_no, "引號字串格式錯誤（未終止或未知轉義）");
        }
        return Value::string(std::move(s));
    }
    double num = 0.0;
    bool integral = false;
    if (numeric_token(token, num, integral)) {
        return Value::number(num, integral);
    }
    // 其餘一律裸字串（保留原樣，可含空白）。
    return Value::string(token);
}

// 從 `key:` 條目的下一行起，若存在更深縮排則解析為子區塊，否則回 null。
Value parse_child_or_null(const std::vector<Line>& L, std::size_t& i, std::size_t block_indent,
                          FormatVersion& ver, bool& saw_ver) {
    if (i < L.size() && L[i].indent > block_indent) {
        return parse_block(L, i, L[i].indent, /*is_root=*/false, ver, saw_ver);
    }
    return Value::null();
}

// 解析一個 map 區塊：所有兄弟條目共用 block_indent。
Value parse_map_block(const std::vector<Line>& L, std::size_t& i, std::size_t block_indent,
                      bool is_root, FormatVersion& ver, bool& saw_ver) {
    std::vector<Value::Member> members;

    while (i < L.size() && L[i].indent >= block_indent) {
        const Line& ln = L[i];
        if (ln.indent > block_indent) {
            fail(ln.line_no, "非預期的縮排（無對應的父項）");
        }
        if (is_list_item(ln.content)) {
            fail(ln.line_no, "此處預期 map 條目（key: value），卻遇到清單項目 '-'");
        }
        const std::size_t colon = ln.content.find(':');
        if (colon == std::string::npos) {
            fail(ln.line_no, "map 條目缺少 ':'（每行須為 key: value）");
        }
        const std::string key = trim(ln.content.substr(0, colon));
        const std::string rest = trim(ln.content.substr(colon + 1));
        if (key.empty()) {
            fail(ln.line_no, "key 為空");
        }
        // 重複 key 偵測（可定位）。
        for (const auto& m : members) {
            if (m.first == key) {
                fail(ln.line_no, "重複的 key: " + key);
            }
        }

        // 根層 format_version：特殊處理，取型別化版本、不進 root map（避免數字歧義如 1.10）。
        if (is_root && key == "format_version") {
            if (saw_ver) {
                fail(ln.line_no, "重複的 key: format_version");
            }
            if (rest.empty()) {
                fail(ln.line_no, "format_version 不得為空（需 major.minor，如 1.0）");
            }
            FormatVersion fv;
            if (!parse_version(rest, fv)) {
                fail(ln.line_no, "format_version 無法解析（需 major.minor，如 1.0）");
            }
            ver = fv;
            saw_ver = true;
            ++i;
            continue;
        }

        ++i;  // 消耗 key 行。
        Value value;
        if (rest.empty()) {
            // 巢狀子區塊，或無子區塊則為 null。
            value = parse_child_or_null(L, i, block_indent, ver, saw_ver);
        } else {
            value = scalar_value(rest, ln.line_no);
        }
        members.emplace_back(key, std::move(value));
    }

    return Value::map(std::move(members));
}

// 解析一個 list 區塊：所有兄弟項目共用 block_indent。
Value parse_list_block(const std::vector<Line>& L, std::size_t& i, std::size_t block_indent,
                       FormatVersion& ver, bool& saw_ver) {
    std::vector<Value> items;

    while (i < L.size() && L[i].indent >= block_indent) {
        const Line& ln = L[i];
        if (ln.indent > block_indent) {
            fail(ln.line_no, "非預期的縮排（無對應的父項）");
        }
        if (!is_list_item(ln.content)) {
            fail(ln.line_no, "此處預期清單項目（以 '- ' 開頭）");
        }
        if (ln.content == "-") {
            // 獨佔的 '-'：其下縮排為巢狀 map / list，否則為 null 元素。
            ++i;
            items.push_back(parse_child_or_null(L, i, block_indent, ver, saw_ver));
        } else {
            // "- 純量"。
            const std::string token = trim(ln.content.substr(2));
            ++i;
            items.push_back(scalar_value(token, ln.line_no));
        }
    }

    return Value::list(std::move(items));
}

Value parse_block(const std::vector<Line>& L, std::size_t& i, std::size_t block_indent,
                  bool is_root, FormatVersion& ver, bool& saw_ver) {
    if (is_list_item(L[i].content)) {
        return parse_list_block(L, i, block_indent, ver, saw_ver);
    }
    return parse_map_block(L, i, block_indent, is_root, ver, saw_ver);
}

}  // namespace

bool parse_scalar(const std::string& token, Value& out) {
    const std::string t = trim(token);
    if (t.empty()) {
        out = Value::null();
        return true;
    }
    if (!t.empty() && t.front() == '"') {
        std::string s;
        if (!parse_quoted(t, s)) {
            return false;  // 引號字串格式錯誤。
        }
        out = Value::string(std::move(s));
        return true;
    }
    if (t == "null") {
        out = Value::null();
        return true;
    }
    if (t == "true") {
        out = Value::boolean(true);
        return true;
    }
    if (t == "false") {
        out = Value::boolean(false);
        return true;
    }
    double num = 0.0;
    bool integral = false;
    if (numeric_token(t, num, integral)) {
        out = Value::number(num, integral);
        return true;
    }
    out = Value::string(t);
    return true;
}

ParseResult parse(const std::string& text) {
    // --- 1. 預處理：切成邏輯行，計算縮排，略過空行與註解，拒絕 tab 縮排 ---
    std::vector<Line> lines;
    std::size_t pos = 0;
    std::size_t line_no = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = (nl == std::string::npos) ? text.size() : nl;
        const std::string raw = text.substr(pos, end - pos);
        ++line_no;

        // 前導空白：偵測 tab。
        std::size_t indent = 0;
        bool tab_in_indent = false;
        while (indent < raw.size() && (raw[indent] == ' ' || raw[indent] == '\t')) {
            if (raw[indent] == '\t') {
                tab_in_indent = true;
            }
            ++indent;
        }

        std::string content = raw.substr(indent);
        // rstrip（不影響引號字串內容——尾隨空白在引號內須用 \n/顯式表示；此處僅去行尾裸空白）。
        content = trim(content);

        if (!content.empty() && content[0] != '#') {
            if (tab_in_indent) {
                return ParseResult::failure({line_no, "縮排不得使用 tab（請用空白）"});
            }
            lines.push_back({indent, content, line_no});
        }

        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }

    // --- 2. 空文件 ---
    if (lines.empty()) {
        return ParseResult::failure({0, "空文件：缺少必填欄位 format_version"});
    }

    // --- 3. 根層不得縮排、且必須是 map ---
    if (lines.front().indent != 0) {
        return ParseResult::failure({lines.front().line_no, "根層不得縮排"});
    }
    if (is_list_item(lines.front().content)) {
        return ParseResult::failure(
            {lines.front().line_no, "根必須為 map（鍵值文件，需含 format_version）"});
    }

    // --- 4. 遞迴解析 ---
    FormatVersion ver;
    bool saw_ver = false;
    Document doc;
    try {
        std::size_t i = 0;
        doc.root = parse_map_block(lines, i, /*block_indent=*/0, /*is_root=*/true, ver, saw_ver);
        // 若仍有未消耗的行 → 結構性縮排錯誤（不得靜默）。
        if (i < lines.size()) {
            return ParseResult::failure({lines[i].line_no, "非預期的縮排或孤立行"});
        }
    } catch (const ParseFail& f) {
        return ParseResult::failure(f.err);
    }

    // --- 5. 必填 format_version + 相容性 ---
    if (!saw_ver) {
        return ParseResult::failure({0, "缺少必填欄位: format_version"});
    }
    if (!is_format_compatible(ver)) {
        return ParseResult::failure(
            {0, "format_version 不相容（本實作支援上限為 " +
                    std::to_string(kSupportedFormat.major) + "." +
                    std::to_string(kSupportedFormat.minor) + "）"});
    }
    doc.format_version = ver;

    return ParseResult::success(std::move(doc));
}

}  // namespace ds::format
