// H1-07 反向斷言的機器化判定 —— 實作。零相依（只用標準庫），見 `pixel_boundary.hpp` 檔頭。
#include "pixel_boundary.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace ds::host::pixel_audit {
namespace {

// R1：`host/` 像素層自有的像素幾何型別名 + 平台的像素矩形。
// **刻意不收** `Rect` / `Point` / `Size` 這些混合大小寫通名 —— 理由見 hpp 檔頭。
const std::set<std::string>& host_pixel_type_names() {
    static const std::set<std::string> kNames = {
        // H1-06 繪製層
        "PaintBox",
        // H1-07 本單元
        "PixelPoint", "PixelSize", "ComponentPlacement", "ResolvedComponent", "WidgetLayout",
        "ComponentAppearance",
        // 平台的像素矩形 / 點 / 尺寸（全大寫是 win32 的寫法，不會與本庫型別撞名）
        "RECT", "POINT", "SIZE", "CGRect", "CGPoint", "CGSize", "NSRect", "NSPoint", "NSSize",
    };
    return kNames;
}

// R2 的「像素單位詞」。
const std::set<std::string>& pixel_unit_words() {
    static const std::set<std::string> kWords = {"px", "pixel", "pixels"};
    return kWords;
}

// R2 / R3 的「幾何角色詞」（座標 / 尺寸 / 偏移）。
const std::set<std::string>& geometry_words() {
    static const std::set<std::string> kWords = {
        "x", "y", "left", "top", "right", "bottom", "width", "height", "size", "offset",
        "pos", "position", "coord", "coords", "rect", "box", "margin", "padding", "gap",
        "inset", "origin", "bounds", "extent", "stride", "thickness", "radius", "diameter",
        "dx", "dy", "cx", "cy",
    };
    return kWords;
}

// R3 的排除詞：名稱自己宣告了「我不是像素」的單位。
const std::set<std::string>& non_pixel_unit_words() {
    static const std::set<std::string> kWords = {
        "line", "lines", "ratio", "fraction", "percent", "index", "indices", "count",
        "rows", "columns", "chars", "cells", "glyphs", "pages", "items",
    };
    return kWords;
}

// R3 的整數型別 token。**`bool` 不在內**（旗標不是座標，見 hpp 檔頭）。
const std::set<std::string>& integral_type_tokens() {
    static const std::set<std::string> kTokens = {
        "int", "unsigned", "signed", "long", "short", "char", "size_t", "ptrdiff_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    };
    return kTokens;
}

bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
bool is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

char to_lower(char c) { return is_upper(c) ? static_cast<char>(c - 'A' + 'a') : c; }

bool has_any(const std::vector<std::string>& words, const std::set<std::string>& set) {
    for (const std::string& w : words) {
        if (set.count(w) != 0) return true;
    }
    return false;
}

// 一行裡的所有識別字（連同它在該行的位置）。
std::vector<std::string> identifiers_in(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (is_ident_start(text[i])) {
            const std::size_t start = i;
            while (i < text.size() && is_ident_char(text[i])) ++i;
            out.push_back(text.substr(start, i - start));
        } else {
            ++i;
        }
    }
    return out;
}

std::string trim(const std::string& s) {
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    std::size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

// R3：對一行（已去註解 / 去字面值）判定是否為「整數型幾何欄位」的宣告，並回傳
// {型別文字, 欄位名} 的清單（支援 `int x = 0, y = 0;` 這種同行多宣告子）。
struct DeclaredField {
    std::string type_text;
    std::string name;
};

std::vector<DeclaredField> integral_geometry_fields_in_line(const std::string& raw_line) {
    std::vector<DeclaredField> out;
    const std::string line = trim(raw_line);
    if (line.empty() || line.back() != ';') return out;       // 只看以 ';' 收尾的宣告
    if (line.find('(') != std::string::npos) return out;      // 函式宣告 / 呼叫 / 初始化式，不是欄位
    if (line.find('{') != std::string::npos) return out;      // 行內定義 / 聚合初始化
    if (line.find("return") != std::string::npos) return out;
    if (line.find("using") != std::string::npos) return out;
    if (line.find("typedef") != std::string::npos) return out;

    std::string body = line.substr(0, line.size() - 1);  // 去掉結尾 ';'

    // 宣告子以 ',' 分隔；但型別本身可能含模板參數的 ','，故只在沒有 '<' 時才切。
    std::vector<std::string> declarators;
    if (body.find('<') == std::string::npos) {
        std::string cur;
        for (char c : body) {
            if (c == ',') {
                declarators.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        declarators.push_back(cur);
    } else {
        declarators.push_back(body);
    }

    // 第一個宣告子帶型別；其後的宣告子沿用它。
    std::string type_text;
    for (std::size_t d = 0; d < declarators.size(); ++d) {
        std::string piece = trim(declarators[d]);
        // 去掉初始化式（`= 0` / `= 0.0f`）。
        const std::size_t eq = piece.find('=');
        if (eq != std::string::npos) piece = trim(piece.substr(0, eq));
        if (piece.empty()) continue;

        const std::vector<std::string> idents = identifiers_in(piece);
        if (idents.empty()) continue;
        const std::string name = idents.back();

        if (d == 0) {
            if (idents.size() < 2) continue;  // 只有一個識別字 = 沒有型別，不是欄位宣告
            std::ostringstream ty;
            for (std::size_t k = 0; k + 1 < idents.size(); ++k) {
                if (k != 0) ty << ' ';
                ty << idents[k];
            }
            type_text = ty.str();
        }
        if (type_text.empty()) continue;

        // 型別須含整數型別 token。
        const std::vector<std::string> type_tokens = identifiers_in(type_text);
        bool integral = false;
        for (const std::string& t : type_tokens) {
            if (integral_type_tokens().count(t) != 0) {
                integral = true;
                break;
            }
        }
        if (!integral) continue;

        if (!name.empty() && name.back() == '_') continue;  // 私有成員慣例，非公開介面

        const std::vector<std::string> name_words = split_identifier_words(name);
        if (!has_any(name_words, geometry_words())) continue;
        if (has_any(name_words, non_pixel_unit_words())) continue;  // 名稱自稱非像素單位

        out.push_back(DeclaredField{type_text, name});
    }
    return out;
}

}  // namespace

const char* to_string(Rule rule) noexcept {
    switch (rule) {
        case Rule::HostPixelTypeName: return "R1-host-pixel-type-name";
        case Rule::PixelUnitIdentifier: return "R2-pixel-unit-identifier";
        case Rule::IntegralGeometryField: return "R3-integral-geometry-field";
    }
    return "R?-unknown";
}

bool operator==(const Finding& a, const Finding& b) {
    return a.file == b.file && a.line == b.line && a.identifier == b.identifier &&
           a.rule == b.rule && a.detail == b.detail;
}

std::string strip_comments_and_literals(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    const std::size_t n = source.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = source[i];
        if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            while (i < n && source[i] != '\n') {
                out.push_back(' ');
                ++i;
            }
        } else if (c == '/' && i + 1 < n && source[i + 1] == '*') {
            out.append("  ");
            i += 2;
            while (i < n && !(source[i] == '*' && i + 1 < n && source[i + 1] == '/')) {
                out.push_back(source[i] == '\n' ? '\n' : ' ');  // 行號必須保留
                ++i;
            }
            if (i < n) {
                out.append("  ");
                i += 2;
            }
        } else if (c == '"' || c == '\'') {
            const char quote = c;
            out.push_back(' ');
            ++i;
            while (i < n && source[i] != quote) {
                if (source[i] == '\\' && i + 1 < n) {
                    out.push_back(' ');
                    ++i;
                }
                out.push_back(source[i] == '\n' ? '\n' : ' ');
                ++i;
            }
            if (i < n) {
                out.push_back(' ');
                ++i;
            }
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::vector<std::string> split_identifier_words(const std::string& identifier) {
    std::vector<std::string> words;
    std::string cur;
    const auto flush = [&]() {
        if (!cur.empty()) {
            words.push_back(cur);
            cur.clear();
        }
    };
    for (std::size_t i = 0; i < identifier.size(); ++i) {
        const char c = identifier[i];
        if (c == '_') {
            flush();
            continue;
        }
        if (is_upper(c)) {
            // 新詞的開頭有兩種情形：小寫/數字之後（`fooBar`），
            // 或連續大寫之後緊跟小寫（`HTTPServer` → http | server）。
            const bool prev_lower_or_digit =
                i > 0 && (is_lower(identifier[i - 1]) || is_digit(identifier[i - 1]));
            const bool next_lower = i + 1 < identifier.size() && is_lower(identifier[i + 1]);
            const bool prev_upper = i > 0 && is_upper(identifier[i - 1]);
            if (prev_lower_or_digit || (prev_upper && next_lower)) flush();
        }
        cur.push_back(to_lower(c));
    }
    flush();
    return words;
}

bool is_host_pixel_type_name(const std::string& identifier) {
    return host_pixel_type_names().count(identifier) != 0;
}

bool is_pixel_geometry_identifier(const std::string& identifier) {
    const std::vector<std::string> words = split_identifier_words(identifier);
    // **合取**：必須同時有像素單位詞與幾何角色詞。少了後者，`PerPixel` /
    // `kPerPixelAlphaCapability` 這類 alpha 合成模式的名稱就會被誤判成滲透。
    return has_any(words, pixel_unit_words()) && has_any(words, geometry_words());
}

std::vector<Finding> scan_source(const std::string& file_label, const std::string& source) {
    const std::string code = strip_comments_and_literals(source);
    std::vector<Finding> findings;

    int line_no = 0;
    std::istringstream in(code);
    std::string line;
    while (std::getline(in, line)) {
        ++line_no;

        // R1 / R2：行內每個識別字。
        for (const std::string& ident : identifiers_in(line)) {
            if (is_host_pixel_type_name(ident)) {
                findings.push_back(
                    Finding{file_label, line_no, ident, Rule::HostPixelTypeName, {}});
            }
            if (is_pixel_geometry_identifier(ident)) {
                findings.push_back(
                    Finding{file_label, line_no, ident, Rule::PixelUnitIdentifier, {}});
            }
        }

        // R3：整數型幾何欄位。
        for (const DeclaredField& f : integral_geometry_fields_in_line(line)) {
            findings.push_back(
                Finding{file_label, line_no, f.name, Rule::IntegralGeometryField, f.type_text});
        }
    }
    return findings;
}

bool read_file(const std::string& path, std::string& out, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        error = path + "：檔案不存在";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = path + "：開檔失敗";
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    if (in.bad()) {
        error = path + "：讀取失敗";
        return false;
    }
    out = buf.str();
    // **空檔不算讀到**：空字串掃出來是零命中，而零命中在本斷言裡會被誤讀成「乾淨」。
    if (out.empty()) {
        error = path + "：檔案是空的（零命中不代表乾淨）";
        return false;
    }
    error.clear();
    return true;
}

ScanReport scan_files(const std::string& repo_root, const std::vector<std::string>& rel_paths) {
    ScanReport report;
    for (const std::string& rel : rel_paths) {
        const std::string full = (std::filesystem::path(repo_root) / rel).string();
        std::string text;
        std::string error;
        if (!read_file(full, text, error)) {
            report.errors.push_back(error);
            continue;
        }
        report.files.push_back(rel);
        report.bytes += text.size();
        const std::vector<Finding> hits = scan_source(rel, text);
        report.findings.insert(report.findings.end(), hits.begin(), hits.end());
    }
    return report;
}

std::vector<std::string> interface_headers(const std::string& repo_root) {
    std::vector<std::string> rel;
    const std::vector<std::string> dirs = {"modules/elements", "src/kernel"};
    for (const std::string& dir : dirs) {
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::path(repo_root) / dir;
        // **不遞迴**：`src/kernel/backend/` 就是知道平台細節的地方，不是 kernel 的公開介面。
        for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".hpp") continue;
            rel.push_back(dir + "/" + entry.path().filename().string());
        }
    }
    std::sort(rel.begin(), rel.end());
    return rel;
}

const std::vector<BaselineEntry>& expected_baseline() {
    // 見 hpp 檔頭 §預期基線。這**不是豁免清單** —— 斷言的形式是「命中集合 == 本清單」，
    // 所以這兩筆消失（掃描器瞎了）跟多出一筆（真的滲透）一樣是紅的。
    static const std::vector<BaselineEntry> kBaseline = {
        BaselineEntry{"modules/elements/image_element.hpp", "width", Rule::IntegralGeometryField,
                      "ImageDimensions::width —— 來源影像自身資料的固有像素寬，"
                      "換平台後端不會改變；非 widget / 螢幕的擺放幾何"},
        BaselineEntry{"modules/elements/image_element.hpp", "height", Rule::IntegralGeometryField,
                      "ImageDimensions::height —— 同上（固有像素高）"},
    };
    return kBaseline;
}

std::vector<std::string> finding_keys(const std::vector<Finding>& findings) {
    std::vector<std::string> keys;
    keys.reserve(findings.size());
    for (const Finding& f : findings) {
        keys.push_back(f.file + " | " + f.identifier + " | " + to_string(f.rule));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<std::string> baseline_keys() {
    std::vector<std::string> keys;
    for (const BaselineEntry& b : expected_baseline()) {
        keys.push_back(b.file + " | " + b.identifier + " | " + to_string(b.rule));
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

}  // namespace ds::host::pixel_audit
