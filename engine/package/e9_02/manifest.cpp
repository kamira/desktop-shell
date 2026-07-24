// E9-02 manifest — 實作（平台中立、純邏輯，無任何平台分支）。
#include "manifest.hpp"

#include <utility>

namespace ds::package {

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

// 逗號分隔清單 → 去空白後的非空項。
std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = (comma == std::string::npos) ? value.size() : comma;
        std::string item = trim(value.substr(start, end - start));
        if (!item.empty()) {
            out.push_back(std::move(item));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
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
    if (!all_digits(maj) || !all_digits(min)) {
        return false;
    }
    // 次版號不得再含 '.'（拒絕 "1.2.3" 這類非 major.minor 形式）。
    if (min.find('.') != std::string::npos) {
        return false;
    }
    out.major = std::stoi(maj);
    out.minor = std::stoi(min);
    return true;
}

}  // namespace

ParseResult ParseResult::success(Manifest m) {
    ParseResult r;
    r.ok_ = true;
    r.manifest_ = std::move(m);
    return r;
}

ParseResult ParseResult::failure(ParseError e) {
    ParseResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

bool is_format_compatible(const FormatVersion& fmt, const FormatVersion& supported) noexcept {
    return fmt.major == supported.major && fmt.minor <= supported.minor;
}

ParseResult parse_manifest(const std::string& text) {
    Manifest m;
    bool seen_format = false;
    bool seen_name = false;
    // 追蹤已見過的 key，重複即報錯（可定位）。
    bool seen_version = false;
    bool seen_description = false;
    bool seen_requires = false;
    bool seen_permissions = false;

    std::size_t line_no = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = (nl == std::string::npos) ? text.size() : nl;
        const std::string raw = text.substr(pos, end - pos);
        ++line_no;

        const std::string line = trim(raw);
        // 空行與註解（'#' 開頭）略過。
        if (!line.empty() && line[0] != '#') {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                return ParseResult::failure(
                    {line_no, "缺少 ':' 分隔（每行須為 key: value）"});
            }
            const std::string key = trim(line.substr(0, colon));
            const std::string value = trim(line.substr(colon + 1));
            if (key.empty()) {
                return ParseResult::failure({line_no, "key 為空"});
            }

            if (key == "format_version") {
                if (seen_format) {
                    return ParseResult::failure({line_no, "重複的 key: format_version"});
                }
                seen_format = true;
                FormatVersion fv;
                if (!parse_version(value, fv)) {
                    return ParseResult::failure(
                        {line_no, "format_version 無法解析（需 major.minor，如 1.0）"});
                }
                m.format_version = fv;
            } else if (key == "name") {
                if (seen_name) {
                    return ParseResult::failure({line_no, "重複的 key: name"});
                }
                seen_name = true;
                if (value.empty()) {
                    return ParseResult::failure({line_no, "name 不得為空"});
                }
                m.name = value;
            } else if (key == "version") {
                if (seen_version) {
                    return ParseResult::failure({line_no, "重複的 key: version"});
                }
                seen_version = true;
                m.version = value;
            } else if (key == "description") {
                if (seen_description) {
                    return ParseResult::failure({line_no, "重複的 key: description"});
                }
                seen_description = true;
                m.description = value;
            } else if (key == "requires") {
                if (seen_requires) {
                    return ParseResult::failure({line_no, "重複的 key: requires"});
                }
                seen_requires = true;
                m.required_capabilities = split_csv(value);
            } else if (key == "permissions") {
                if (seen_permissions) {
                    return ParseResult::failure({line_no, "重複的 key: permissions"});
                }
                seen_permissions = true;
                m.permissions = split_csv(value);
            } else {
                // 未知欄位：明確報錯並定位（前向擴充請提升 format_version 次版號，
                // 不接受安靜忽略未知 key）。
                return ParseResult::failure({line_no, "未知的 key: " + key});
            }
        }

        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }

    // 必填欄位檢查（非特定行 → line = 0）。
    if (!seen_format) {
        return ParseResult::failure({0, "缺少必填欄位: format_version"});
    }
    if (!seen_name) {
        return ParseResult::failure({0, "缺少必填欄位: name"});
    }

    // 格式版本相容性。
    if (!is_format_compatible(m.format_version)) {
        return ParseResult::failure(
            {0, "format_version 不相容（本實作支援上限為 " +
                    std::to_string(kSupportedFormat.major) + "." +
                    std::to_string(kSupportedFormat.minor) + "）"});
    }

    return ParseResult::success(std::move(m));
}

}  // namespace ds::package
