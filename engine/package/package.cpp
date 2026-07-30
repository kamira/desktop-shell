// E9-01 統一套件格式定義 — 實作（平台中立、純邏輯，無任何平台分支）。
#include "package.hpp"

#include <set>
#include <utility>

namespace ds::package {

namespace {

// 去除頭尾空白（space / tab / CR / LF）。與 E9-02 一致的本地實作（不跨單元共用私有 helper）。
std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) {
        return {};
    }
    const std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// 一行是否為可略過的空行或註解（'#' 開頭）。傳入應為已 trim 的行。
bool is_skippable(const std::string& trimmed_line) {
    return trimmed_line.empty() || trimmed_line[0] == '#';
}

}  // namespace

PackageResult PackageResult::success(Package p) {
    PackageResult r;
    r.ok_ = true;
    r.package_ = std::move(p);
    return r;
}

PackageResult PackageResult::failure(ParseError e) {
    PackageResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

bool is_valid_logical_path(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    if (path.front() == '/') {  // 須為相對路徑，不得為絕對路徑。
        return false;
    }
    // 逐 '/' 區段檢查：不得有空區段（如 "a//b" 或結尾 '/'）、不得含 '..'（禁逃逸）。
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = (slash == std::string::npos) ? path.size() : slash;
        const std::string seg = path.substr(start, end - start);
        if (seg.empty() || seg == "..") {
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    return true;
}

PackageResult validate_package(const Package& pkg) {
    // manifest 有效性：複用 E9-02 的相容性判定 + name 非空。
    if (pkg.manifest.name.empty()) {
        return PackageResult::failure({0, "manifest 無效：name 為空"});
    }
    if (!is_format_compatible(pkg.manifest.format_version)) {
        return PackageResult::failure({0, "manifest 無效：format_version 不相容"});
    }

    // 內容清單完整性：kind 非空、path 合法、無重複。
    std::set<std::string> seen_paths;
    for (const PackageEntry& e : pkg.entries) {
        if (e.kind.empty()) {
            return PackageResult::failure({0, "內容清單項目 kind 為空"});
        }
        if (!is_valid_logical_path(e.logical_path)) {
            return PackageResult::failure(
                {0, "內容清單項目 logical_path 不合法：" + e.logical_path});
        }
        if (!seen_paths.insert(e.logical_path).second) {
            return PackageResult::failure(
                {0, "內容清單 logical_path 重複：" + e.logical_path});
        }
    }
    return PackageResult::success(pkg);
}

PackageResult parse_package(const std::string& text) {
    // 第一遍：以「trim 後恰為 '---'」的行切成 manifest 區段與內容清單區段，並記錄各行絕對行號。
    std::string manifest_block;   // 原樣（含換行）交給 E9-02；行號與絕對行號對齊（區段在頂部）。
    bool have_delimiter = false;
    std::size_t delimiter_line = 0;
    bool manifest_has_content = false;

    // 內容清單行：(絕對行號, 原始行)。
    std::vector<std::pair<std::size_t, std::string>> content_lines;

    std::size_t line_no = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = (nl == std::string::npos) ? text.size() : nl;
        const std::string raw = text.substr(pos, end - pos);
        ++line_no;
        const std::string line = trim(raw);

        if (!have_delimiter && line == "---") {
            have_delimiter = true;
            delimiter_line = line_no;
        } else if (!have_delimiter) {
            // 仍在 manifest 區段：原樣累積。
            if (!manifest_block.empty()) {
                manifest_block.push_back('\n');
            }
            manifest_block += raw;
            if (!is_skippable(line)) {
                manifest_has_content = true;
            }
        } else {
            // 在內容清單區段：略過空行/註解，其餘留待第二遍解析。
            if (!is_skippable(line)) {
                content_lines.emplace_back(line_no, line);
            }
        }

        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }

    // 缺 manifest：manifest 區段沒有任何實質內容（不論有無分隔線）。給出明確的套件級訊息。
    if (!manifest_has_content) {
        const std::size_t at = have_delimiter ? delimiter_line : 0;
        return PackageResult::failure({at, "套件缺少 manifest 區段"});
    }

    // manifest 區段交由 E9-02 解析（不重造）。錯誤含行號，原樣傳出（區段在頂部，行號對齊）。
    ParseResult mr = parse_manifest(manifest_block);
    if (!mr.ok()) {
        return PackageResult::failure(mr.error());
    }

    Package pkg;
    pkg.manifest = mr.manifest();

    // 第二遍：逐條內容清單行解析為 PackageEntry，並就地做可定位的結構驗證。
    std::set<std::string> seen_paths;
    for (const auto& item : content_lines) {
        const std::size_t at = item.first;
        const std::string& line = item.second;

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return PackageResult::failure(
                {at, "內容清單缺少 ':' 分隔（每行須為 kind: logical_path）"});
        }
        PackageEntry entry;
        entry.kind = trim(line.substr(0, colon));
        entry.logical_path = trim(line.substr(colon + 1));

        if (entry.kind.empty()) {
            return PackageResult::failure({at, "內容清單項目 kind 為空"});
        }
        if (entry.logical_path.empty()) {
            return PackageResult::failure({at, "內容清單項目 logical_path 為空"});
        }
        if (!is_valid_logical_path(entry.logical_path)) {
            return PackageResult::failure(
                {at, "內容清單項目 logical_path 不合法（須相對、不含 '..'、無空區段）：" +
                         entry.logical_path});
        }
        if (!seen_paths.insert(entry.logical_path).second) {
            return PackageResult::failure(
                {at, "內容清單 logical_path 重複：" + entry.logical_path});
        }
        pkg.entries.push_back(std::move(entry));
    }

    return PackageResult::success(std::move(pkg));
}

}  // namespace ds::package
