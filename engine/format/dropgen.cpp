// E7-15 拖放產生設定項 — 實作
//
// 平台中立的內容偵測 / 設定項產生 / 經 E7-12 寫回 / 與 E5-08 事件橋接。
// 此檔不含任何平台分支或真實後端（相位 1 約束）。
#include "dropgen.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace ds::format {

namespace {

// 去除前後空白（空白定義為 space / \t / \n / \r / \f / \v）。
std::string trim(const std::string& s) {
    const auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    std::size_t begin = 0;
    while (begin < s.size() && is_ws(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && is_ws(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// 是否含「不可列印」控制位元組（byte < 0x20 且非 \t / \n / \r，或 0x7f DEL）。
// 用於把二進位 / 損毀內容明確歸為 Unknown，而非硬塞成文字。
bool has_control_byte(const std::string& s) {
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == 0x7f) {
            return true;
        }
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            return true;
        }
    }
    return false;
}

bool is_hex_digit(unsigned char c) {
    return std::isdigit(c) != 0 || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// #RGB / #RGBA / #RRGGBB / #RRGGBBAA（3/4/6/8 位十六進位）。
bool looks_like_color(const std::string& s) {
    if (s.size() < 2 || s.front() != '#') {
        return false;
    }
    const std::size_t n = s.size() - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) {
        return false;
    }
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (!is_hex_digit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

bool starts_with(const std::string& s, const char* prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// http:// https:// ftp:// 前綴（協定明確者才算 URL，避免把裸字串誤判）。
bool looks_like_url(const std::string& s) {
    return starts_with(s, "http://") || starts_with(s, "https://") ||
           starts_with(s, "ftp://");
}

bool contains_whitespace(const std::string& s) {
    return std::any_of(s.begin(), s.end(), [](char ch) {
        return std::isspace(static_cast<unsigned char>(ch)) != 0;
    });
}

// 取小寫副檔名（最後一個 '.' 之後；無副檔名回空字串）。
std::string lower_extension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of('/');
    if (dot == std::string::npos) {
        return {};
    }
    // '.' 須在最後一個路徑分隔之後，且不在字尾。
    if (slash != std::string::npos && dot < slash) {
        return {};
    }
    if (dot + 1 >= path.size()) {
        return {};
    }
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// 檔案啟發：絕對路徑（'/' 開頭）/ file:// / 具副檔名且無空白。
bool looks_like_file(const std::string& s) {
    if (starts_with(s, "file://")) {
        return true;
    }
    if (contains_whitespace(s)) {
        return false;  // 有空白多半是文字，不當檔案路徑。
    }
    if (!s.empty() && s.front() == '/') {
        return true;
    }
    return !lower_extension(s).empty();
}

}  // namespace

// -----------------------------------------------------------------------------
// DropContent 工廠 / 偵測
// -----------------------------------------------------------------------------

DropContent DropContent::file(std::string path) {
    return DropContent{DropKind::File, std::move(path)};
}

DropContent DropContent::url(std::string u) {
    return DropContent{DropKind::Url, std::move(u)};
}

DropContent DropContent::text(std::string t) {
    return DropContent{DropKind::Text, std::move(t)};
}

DropContent DropContent::color(std::string c) {
    return DropContent{DropKind::Color, std::move(c)};
}

DropContent DropContent::detect(std::string raw) {
    const DropKind kind = detect_drop_kind(raw);
    return DropContent{kind, std::move(raw)};
}

DropKind detect_drop_kind(const std::string& raw) {
    if (has_control_byte(raw)) {
        return DropKind::Unknown;  // 二進位 / 損毀：明確回報。
    }
    const std::string s = trim(raw);
    if (s.empty()) {
        return DropKind::Unknown;  // 空 / 僅空白：明確回報。
    }
    if (looks_like_color(s)) {
        return DropKind::Color;
    }
    if (looks_like_url(s)) {
        return DropKind::Url;
    }
    if (looks_like_file(s)) {
        return DropKind::File;
    }
    return DropKind::Text;  // 其餘可列印文字。
}

bool is_image_path(const std::string& path) {
    const std::string ext = lower_extension(path);
    if (ext.empty()) {
        return false;
    }
    static const char* kImageExts[] = {"png", "jpg",  "jpeg", "gif", "bmp",
                                       "webp", "svg", "tiff", "tif", "ico"};
    for (const char* e : kImageExts) {
        if (ext == e) {
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// 設定項產生
// -----------------------------------------------------------------------------

ConfigItem generate_config_item(const DropContent& drop) {
    switch (drop.kind) {
        case DropKind::File:
            if (is_image_path(drop.payload)) {
                // 拖入圖片 → 背景設定。
                return ConfigItem{"background.image", Value::string(drop.payload)};
            }
            // 拖入一般檔案 → 檔案路徑設定。
            return ConfigItem{"file.path", Value::string(drop.payload)};
        case DropKind::Url:
            // 拖入 URL → 連結元件。
            return ConfigItem{"link.url", Value::string(drop.payload)};
        case DropKind::Color:
            return ConfigItem{"color", Value::string(drop.payload)};
        case DropKind::Text:
            return ConfigItem{"text", Value::string(drop.payload)};
        case DropKind::Unknown:
            break;  // 落到下方明確 throw。
    }
    throw std::runtime_error(
        "generate_config_item: 無法辨識的拖放內容（DropKind::Unknown），payload=\"" +
        drop.payload + "\"");
}

// -----------------------------------------------------------------------------
// 套用（經 E7-12）
// -----------------------------------------------------------------------------

Value apply_drop(const Value& root, const DropContent& drop) {
    const ConfigItem item = generate_config_item(drop);
    return set_value(root, item.path, item.value);
}

Document apply_drop(const Document& doc, const DropContent& drop) {
    Document out;
    out.format_version = doc.format_version;
    out.root = apply_drop(doc.root, drop);
    return out;
}

Document apply_drops(const Document& doc, const std::vector<DropContent>& drops) {
    Document out;
    out.format_version = doc.format_version;
    out.root = doc.root;
    for (const DropContent& drop : drops) {
        out.root = apply_drop(out.root, drop);  // Unknown → throw，不靜默略過。
    }
    return out;
}

// -----------------------------------------------------------------------------
// 與 E5-08 整合
// -----------------------------------------------------------------------------

DropContent drop_from_system_event(const ds::events::SystemEvent& event) {
    return DropContent::detect(event.detail);
}

ds::events::SubscriptionId subscribe_drops(
    ds::events::SystemEventSource& source,
    std::function<void(const DropContent&)> handler) {
    if (!handler) {
        return 0;  // 無效 handler：不佔用訂閱代號（與 E5-08 空 listener 語意一致）。
    }
    return source.subscribe(
        [handler = std::move(handler)](const ds::events::SystemEvent& event) {
            handler(drop_from_system_event(event));
        });
}

}  // namespace ds::format
