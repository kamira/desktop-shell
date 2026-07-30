// E1-22 建置期能力閘控 lint — 實作
//
// 純文字 / 規則分析。無平台分支、無真實後端。以逐行掃描 + std::regex 實作，
// 讓每條違規都能定位到 1-based 行號。
#include "capability_lint.hpp"

#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace ds::kernel {

namespace {

// 去掉行內註解與字面上的 block 註解片段，避免註解裡的示例文字造成誤判。
// 注意：這是 lint 核心的保守簡化（不做完整字串 / 跨行 block 狀態機）；真正的
// CI 驅動可接更完整的 tokenizer。相位 1 只需可測核心。
std::string strip_comments(const std::string& line) {
    std::string out = line;
    // 行內 // 註解：截到行尾。
    const std::size_t slashes = out.find("//");
    if (slashes != std::string::npos) {
        out.erase(slashes);
    }
    // 同行 /* ... */：逐一移除。
    for (;;) {
        const std::size_t open = out.find("/*");
        if (open == std::string::npos) break;
        const std::size_t close = out.find("*/", open + 2);
        if (close == std::string::npos) {
            out.erase(open);  // 未閉合：截掉其後（保守）。
            break;
        }
        out.erase(open, close + 2 - open);
    }
    return out;
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// 在 line 中尋找「以 name 為函式名的呼叫」：name 前為非識別字元（或行首）、
// 其後允許空白再接 '('。回傳呼叫名起始位置，無則 npos。
std::size_t find_call(const std::string& line, const std::string& name) {
    std::size_t from = 0;
    while (true) {
        const std::size_t pos = line.find(name, from);
        if (pos == std::string::npos) return std::string::npos;
        const bool left_ok = (pos == 0) || !is_ident_char(line[pos - 1]);
        std::size_t after = pos + name.size();
        const bool right_boundary = (after >= line.size()) || !is_ident_char(line[after]);
        if (left_ok && right_boundary) {
            // 跳過空白後必須是 '('，才算呼叫。
            while (after < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[after]))) {
                ++after;
            }
            if (after < line.size() && line[after] == '(') {
                return pos;
            }
        }
        from = pos + 1;
    }
}

// NFR-02：數字 z-order（z_order / z-index / zorder ... 指派數字字面量）。
const std::regex& zorder_re() {
    static const std::regex re(R"((z[_-]?order|z[_-]?index)\s*[=:]\s*[+-]?[0-9])",
                               std::regex::icase);
    return re;
}

// NFR-02：絕對 / 像素座標指派到座標型欄位。
const std::regex& coord_assign_re() {
    static const std::regex re(
        R"(\b(pos_x|pos_y|abs_x|abs_y|pixel_x|pixel_y|screen_x|screen_y|coord_x|coord_y|left|top|right|bottom)\s*[=:]\s*[+-]?[0-9])",
        std::regex::icase);
    return re;
}

// NFR-02：以數字座標呼叫定位型 API。
const std::regex& coord_call_re() {
    static const std::regex re(
        R"(\b(set_position|move_to|set_bounds|set_frame|set_origin|set_geometry)\s*\(\s*[+-]?[0-9])",
        std::regex::icase);
    return re;
}

// 行內的 has() 閘控位置（npos 表示本行無閘控）。
std::size_t has_guard_pos(const std::string& line) {
    return find_call(line, "has");
}

}  // namespace

std::vector<std::string> CapabilityLint::gated_apis_from_matrix(const CapabilityMatrix& m) {
    std::vector<std::string> names;
    for (const CapabilityDecl& d : m.all()) {
        if (!d.optional) continue;  // 只有 optional 能力需要 has() 閘控。
        const std::size_t dot = d.id.rfind('.');
        const std::string leaf = (dot == std::string::npos) ? d.id : d.id.substr(dot + 1);
        if (!leaf.empty()) {
            names.push_back(leaf);
        }
    }
    return names;
}

CapabilityLint::CapabilityLint()
    : gated_apis_(gated_apis_from_matrix(CapabilityMatrix::defaults())) {}

CapabilityLint::CapabilityLint(std::vector<std::string> gated_api_names)
    : gated_apis_(std::move(gated_api_names)) {}

std::vector<LintDiagnostic> CapabilityLint::lint(const std::string& source) const {
    std::vector<LintDiagnostic> out;

    // NFR-03：一旦在較前面的行看過 has() 閘控，即視為後續能力呼叫已被保護。
    // 這是靜態核心的保守作用域近似（整檔一次閘控即算保護）；精確作用域分析
    // 留待 CI 驅動。同行內另判斷 has() 是否在該呼叫之前。
    bool seen_guard = false;

    std::size_t line_no = 0;
    std::size_t start = 0;
    const std::string& s = source;
    while (start <= s.size()) {
        std::size_t nl = s.find('\n', start);
        const bool last = (nl == std::string::npos);
        const std::string raw = s.substr(start, last ? std::string::npos : nl - start);
        ++line_no;

        const std::string line = strip_comments(raw);

        // --- NFR-02：數字 z-order ---
        if (std::regex_search(line, zorder_re())) {
            out.push_back({LintRule::NumericZOrder, "NFR-02", line_no,
                           "數字 z-order 禁用；改用具名層級（NFR-02）"});
        }
        // --- NFR-02：絕對 / 像素座標 ---
        if (std::regex_search(line, coord_assign_re()) ||
            std::regex_search(line, coord_call_re())) {
            out.push_back({LintRule::AbsoluteCoordinate, "NFR-02", line_no,
                           "疑似硬編碼絕對座標；改用具名錨點 / 相對版面（NFR-02）"});
        }

        // --- NFR-03：能力閘控呼叫必須有 has() 保護 ---
        const std::size_t guard = has_guard_pos(line);
        for (const std::string& api : gated_apis_) {
            const std::size_t call = find_call(line, api);
            if (call == std::string::npos) continue;
            const bool guarded_before_on_line = (guard != std::string::npos && guard < call);
            if (!seen_guard && !guarded_before_on_line) {
                out.push_back({LintRule::UnguardedCapability, "NFR-03", line_no,
                               "能力呼叫 '" + api + "' 缺少 has() 閘控保護（NFR-03）"});
            }
        }
        // 本行有 has() 閘控 → 之後的能力呼叫視為已保護。
        if (guard != std::string::npos) {
            seen_guard = true;
        }

        if (last) break;
        start = nl + 1;
    }

    return out;
}

}  // namespace ds::kernel
