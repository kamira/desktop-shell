// H1-09 就地編輯裝配 —— 實作。見 edit_assembly.hpp。
//
// 本檔不含任何平台 API、不含 `#ifdef`、對外不 throw。
#include "edit_assembly.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <set>
#include <utility>

namespace ds::host::edit {

using ds::format::FormatVersion;
using ds::format::Path;
using ds::format::PathSegment;
using ds::format::Value;
using ds::layout::LayoutElement;
using ds::layout::LayoutState;

namespace {

// 裝置像素的夾限：(座標上界 + 尺寸上界) × 縮放上界 = 3.2e7，遠小於 int 值域。
// double→int 轉換前一定先夾到這個範圍內（`-fsanitize=float-cast-overflow`）。
constexpr double kDeviceLimit = (kCoordLimit + kExtentLimit) * kMaxDpiScale;

bool is_blank(char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_blank(s[b])) {
        ++b;
    }
    while (e > b && is_blank(s[e - 1])) {
        --e;
    }
    return s.substr(b, e - b);
}

// 把文字切成邏輯行（不含換行字元），並記下原本是否以換行結尾。
std::vector<std::string> split_lines(const std::string& text, bool& trailing_newline) {
    std::vector<std::string> lines;
    std::string cur;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    trailing_newline = cur.empty() && !text.empty();
    if (!cur.empty()) {
        lines.push_back(cur);
    }
    return lines;
}

// 這一行是根層（零縮排）的 `format_version: ...` 嗎？是 → 回傳冒號後的值（已 trim）。
bool root_version_line(const std::string& line, std::string& value_out) {
    if (line.empty() || is_blank(line[0])) {
        return false;  // 有縮排 = 不是根層；全空白行也在此被排除。
    }
    if (line[0] == '#') {
        return false;  // 註解行（E7-01 只略過整行註解，不剝行尾註解）。
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    if (trim(line.substr(0, colon)) != "format_version") {
        return false;
    }
    value_out = trim(line.substr(colon + 1));
    return true;
}

// `major.minor` → FormatVersion。**不用 std::stoi**：位數爆掉時它丟 std::out_of_range，
// 而本層對外承諾不 throw（E7-01 的 parse_version 用了 stoi，見標頭 §3 的說明）。
bool parse_version_strict(const std::string& text, FormatVersion& out) {
    const std::size_t dot = text.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= text.size()) {
        return false;
    }
    auto to_int = [](const std::string& s, int& value) noexcept {
        if (s.empty()) {
            return false;
        }
        long long acc = 0;
        for (const char c : s) {
            if (c < '0' || c > '9') {
                return false;
            }
            acc = acc * 10 + (c - '0');
            if (acc > 2147483647LL) {  // 放不進 int → 明確判為 Malformed，不溢位、不丟例外
                return false;
            }
        }
        value = static_cast<int>(acc);
        return true;
    };
    FormatVersion v;
    if (!to_int(text.substr(0, dot), v.major) || !to_int(text.substr(dot + 1), v.minor)) {
        return false;
    }
    out = v;
    return true;
}

// 走訪路徑取節點；任何一段對不上 → nullptr（不 throw）。
const Value* locate(const Value& root, const Path& path) noexcept {
    const Value* cur = &root;
    for (const PathSegment& seg : path) {
        if (seg.kind == PathSegment::Kind::Key) {
            if (!cur->is_map()) {
                return nullptr;
            }
            cur = cur->find(seg.key);
            if (cur == nullptr) {
                return nullptr;
            }
        } else {
            if (!cur->is_list() || seg.index >= cur->as_list().size()) {
                return nullptr;
            }
            cur = &cur->as_list()[seg.index];
        }
    }
    return cur;
}

// properties 一律正規化為 Map（Null / 其他型別 → 空 Map），與 E9-05 的語意一致。
Value as_props(const Value& v) {
    return v.is_map() ? v : Value::map({});
}

// --- LayoutState 的**全域**比較（任何輸入都給得出答案，不 throw）---
//
// 為什麼不能直接用 E9-05 的 `LayoutState::operator==`：它經由
// `normalized_properties()` 比較 properties，而那個函式對「既非 Map 也非 Null」
// **會 throw**（`layout_store.cpp`）。本層的 `reset()` 是標頭明文「不消毒」的公開後門，
// 所以 `baseline_` 裡真的可能出現 `properties` 是 Number 的元件——於是
// `overwrite_check()` / `target_is_safe_to_overwrite()` 會把例外丟回呼叫端，
// 破掉標頭 §「公開 API 一律不 throw」那條契約（驗收 R18，修訂 5 引進的回歸：
// 在那之前 `overwrite_check()` 從不比較 `LayoutState`）。
//
// **不用 try/catch 包起來**：那會把「我不知道這兩個狀態是不是一樣」偽裝成一個答案。
// 改成問一個本來就有答案的問題——`ds::format::Value::operator==` 是全域的（只比型別與
// 內容，不 throw），而 Null 與空 Map 視為相同只是沿用 E9-05 對**合法**輸入的語意。
// 非法的 properties（Number / String / List）就誠實地「與任何 Map 都不相等」。
bool same_properties(const Value& a, const Value& b) {
    // `Value::size()` 對非容器型別會 throw，所以 `is_map()` 必須短路在前。
    const bool a_empty = a.is_null() || (a.is_map() && a.size() == 0);
    const bool b_empty = b.is_null() || (b.is_map() && b.size() == 0);
    if (a_empty || b_empty) {
        return a_empty && b_empty;
    }
    return a == b;
}

bool same_layout(const LayoutState& a, const LayoutState& b) {
    if (a.elements.size() != b.elements.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.elements.size(); ++i) {
        const LayoutElement& x = a.elements[i];
        const LayoutElement& y = b.elements[i];
        if (x.id != y.id || x.type != y.type || !same_properties(x.properties, y.properties)) {
            return false;
        }
    }
    return true;
}

// 取 Map 內的 double（缺鍵 / 非 Number → fallback）。
double number_or(const Value& props, const char* key, double fallback) {
    const Value* v = props.find(key);
    if (v == nullptr || !v->is_number()) {
        return fallback;
    }
    return v->as_number();
}

bool bool_or(const Value& props, const char* key, bool fallback) {
    const Value* v = props.find(key);
    if (v == nullptr || !v->is_bool()) {
        return fallback;
    }
    return v->as_bool();
}

// 本層認得語意的屬性鍵；其餘的數值屬性原樣倒進 ElementDraw::params（H1-06 的擴充袋）。
bool is_known_key(const std::string& key) {
    static const char* const kKnown[] = {
        kPropX,     kPropY,         kPropWidth, kPropHeight,       kPropFillRatio,
        kPropLabel, kPropAvailable, kPropVertical, kPropStartAngle, kPropSweep,
        kPropAngle, kPropLengthRatio, kPropThicknessRatio};
    for (const char* k : kKnown) {
        if (key == k) {
            return true;
        }
    }
    return false;
}

}  // namespace

// =============================================================================
// LogicalPlacement / 消毒
// =============================================================================

bool operator==(const LogicalPlacement& a, const LogicalPlacement& b) noexcept {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator!=(const LogicalPlacement& a, const LogicalPlacement& b) noexcept {
    return !(a == b);
}

double clamp_finite(double v, double fallback, double lo, double hi) noexcept {
    // **`std::isfinite` 明寫在最前面**（K-002 / K-016）：若只寫
    // `v < lo ? lo : (v > hi ? hi : v)`，NaN 的兩個比較都是 false，於是 NaN 原封回傳。
    if (!std::isfinite(fallback)) {
        fallback = lo;
    }
    if (!std::isfinite(v)) {
        return fallback;
    }
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

double sanitize_coord(double v) noexcept {
    return clamp_finite(v, 0.0, -kCoordLimit, kCoordLimit);
}

double sanitize_extent(double v) noexcept {
    // 負尺寸歸 0 而非翻正：翻正是在猜使用者的意圖，歸 0 之後是具名的 EmptyArea，看得見。
    return clamp_finite(v, 0.0, 0.0, kExtentLimit);
}

double sanitize_dpi(double scale) noexcept {
    if (!std::isfinite(scale) || scale <= 0.0) {
        return 1.0;
    }
    return scale > kMaxDpiScale ? kMaxDpiScale : scale;
}

LogicalPlacement sanitize_placement(const LogicalPlacement& p, int* repaired) noexcept {
    LogicalPlacement s;
    s.x = sanitize_coord(p.x);
    s.y = sanitize_coord(p.y);
    s.width = sanitize_extent(p.width);
    s.height = sanitize_extent(p.height);
    if (repaired != nullptr) {
        // NaN 與任何值都「不相等」，所以 NaN → 0 這種修補也會被算進來（這是要的）。
        *repaired += (p.x != s.x) ? 1 : 0;
        *repaired += (p.y != s.y) ? 1 : 0;
        *repaired += (p.width != s.width) ? 1 : 0;
        *repaired += (p.height != s.height) ? 1 : 0;
    }
    return s;
}

int round_to_px(double v) noexcept {
    // **這三步的順序就是這條防線本身**：先判有限（NaN / ±Inf 不得往下走），再夾到遠小於
    // `int` 值域的範圍，最後才 `lround`。少任何一步，`-fsanitize=float-cast-overflow`
    // 就會在某個輸入上當場 FAILED（不是印訊息而已）。本函式是公開且被直接測試的，
    // 所以拿掉其中任何一步都會有測試紅 —— 這正是第一版缺的東西（V1.7 的 MX5 存活）。
    if (!std::isfinite(v)) {
        return 0;
    }
    if (v < -kDeviceLimit) {
        v = -kDeviceLimit;
    } else if (v > kDeviceLimit) {
        v = kDeviceLimit;
    }
    return static_cast<int>(std::lround(v));
}

ds::host::PaintBox normalize_box(int left, int top, int right, int bottom) noexcept {
    ds::host::PaintBox box;
    box.left = left;
    box.top = top;
    // 半開區間 [left,right) x [top,bottom) 的慣例：右 / 下不得小於左 / 上。
    // 反過來的框會讓 `width()` / `height()` 變負，而 `PaintBox::empty()` 查的是 `<= 0`，
    // 所以負寬度會被當成「退化」—— 但沿途任何用 `right - left` 當長度的計算都會拿到負數。
    box.right = std::max(right, left);
    box.bottom = std::max(bottom, top);
    return box;
}

ds::host::PaintBox resolve_placement(const LogicalPlacement& placement,
                                     double dpi_scale) noexcept {
    const LogicalPlacement s = sanitize_placement(placement, nullptr);
    const double scale = sanitize_dpi(dpi_scale);

    const double left = clamp_finite(s.x * scale, 0.0, -kDeviceLimit, kDeviceLimit);
    const double top = clamp_finite(s.y * scale, 0.0, -kDeviceLimit, kDeviceLimit);
    const double right =
        clamp_finite((s.x + s.width) * scale, left, -kDeviceLimit, kDeviceLimit);
    const double bottom =
        clamp_finite((s.y + s.height) * scale, top, -kDeviceLimit, kDeviceLimit);

    return normalize_box(round_to_px(left), round_to_px(top), round_to_px(right),
                         round_to_px(bottom));
}

// ---------------------------------------------------------------------------
// 與 H1-07 的橋接
// ---------------------------------------------------------------------------

ds::host::ComponentPlacement to_component_placement(
    const LogicalPlacement& placement, ds::host::ComponentPresentation presentation) {
    const LogicalPlacement s = sanitize_placement(placement, nullptr);
    ds::host::ComponentPlacement out;
    out.origin.x = round_to_px(s.x);
    out.origin.y = round_to_px(s.y);
    out.size.width = round_to_px(s.width);
    out.size.height = round_to_px(s.height);
    out.presentation = std::move(presentation);
    return out;
}

LogicalPlacement from_component_placement(
    const ds::host::ComponentPlacement& placement) noexcept {
    LogicalPlacement out;
    out.x = static_cast<double>(placement.origin.x);
    out.y = static_cast<double>(placement.origin.y);
    out.width = static_cast<double>(placement.size.width);
    out.height = static_cast<double>(placement.size.height);
    return sanitize_placement(out, nullptr);
}

// ---------------------------------------------------------------------------
// 非有限數值的深層偵測
// ---------------------------------------------------------------------------

namespace {

void collect_non_finite(const Value& node, const std::string& prefix,
                        std::vector<std::string>& out) {
    switch (node.type()) {
        case ds::format::ValueType::Number:
            if (!std::isfinite(node.as_number())) {
                out.push_back(prefix);
            }
            return;
        case ds::format::ValueType::Map:
            for (const Value::Member& m : node.as_map()) {
                collect_non_finite(m.second, prefix.empty() ? m.first : prefix + "." + m.first,
                                   out);
            }
            return;
        case ds::format::ValueType::List: {
            const std::vector<Value>& items = node.as_list();
            for (std::size_t i = 0; i < items.size(); ++i) {
                collect_non_finite(items[i], prefix + "[" + std::to_string(i) + "]", out);
            }
            return;
        }
        case ds::format::ValueType::Null:
        case ds::format::ValueType::Bool:
        case ds::format::ValueType::String:
            return;
    }
}

}  // namespace

std::vector<std::string> non_finite_paths(const Value& root) {
    std::vector<std::string> out;
    collect_non_finite(root, std::string(), out);
    return out;
}

bool contains_non_finite(const Value& root) noexcept {
    switch (root.type()) {
        case ds::format::ValueType::Number:
            return !std::isfinite(root.as_number());
        case ds::format::ValueType::Map:
            for (const Value::Member& m : root.as_map()) {
                if (contains_non_finite(m.second)) {
                    return true;
                }
            }
            return false;
        case ds::format::ValueType::List:
            for (const Value& item : root.as_list()) {
                if (contains_non_finite(item)) {
                    return true;
                }
            }
            return false;
        case ds::format::ValueType::Null:
        case ds::format::ValueType::Bool:
        case ds::format::ValueType::String:
            return false;
    }
    return false;
}

// =============================================================================
// format_version 預掃 / 換版本標記
// =============================================================================

const char* to_string(VersionPeek peek) noexcept {
    switch (peek) {
        case VersionPeek::Found: return "Found";
        case VersionPeek::Missing: return "Missing";
        case VersionPeek::Malformed: return "Malformed";
    }
    return "Found";
}

VersionPeek peek_format_version(const std::string& text, FormatVersion& out) {
    bool trailing = false;
    const std::vector<std::string> lines = split_lines(text, trailing);
    for (const std::string& line : lines) {
        std::string raw;
        if (!root_version_line(line, raw)) {
            continue;
        }
        if (raw.empty()) {
            return VersionPeek::Malformed;
        }
        FormatVersion v;
        if (!parse_version_strict(raw, v)) {
            return VersionPeek::Malformed;
        }
        out = v;
        return VersionPeek::Found;
    }
    return VersionPeek::Missing;
}

std::string restamp_format_version(const std::string& text, const FormatVersion& version) {
    bool trailing = false;
    std::vector<std::string> lines = split_lines(text, trailing);
    for (std::string& line : lines) {
        std::string raw;
        if (!root_version_line(line, raw)) {
            continue;
        }
        line = "format_version: " + std::to_string(version.major) + "." +
               std::to_string(version.minor);
        break;  // 只換第一個；重複的 key 留給 E7-01 去報錯（它會，帶行號）。
    }
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || trailing) {
            out += '\n';
        }
    }
    return out;
}

// =============================================================================
// 載入
// =============================================================================

const char* to_string(SourceOrigin origin) noexcept {
    switch (origin) {
        case SourceOrigin::Empty: return "Empty";
        case SourceOrigin::Authored: return "Authored";
        case SourceOrigin::Loaded: return "Loaded";
        case SourceOrigin::Injected: return "Injected";
        case SourceOrigin::Reloaded: return "Reloaded";
    }
    return "Empty";
}

const char* to_string(IntakeStatus status) noexcept {
    switch (status) {
        case IntakeStatus::Ok: return "Ok";
        case IntakeStatus::Repaired: return "Repaired";
        case IntakeStatus::Migrated: return "Migrated";
        case IntakeStatus::NotFound: return "NotFound";
        case IntakeStatus::MissingVersion: return "MissingVersion";
        case IntakeStatus::MalformedVersion: return "MalformedVersion";
        case IntakeStatus::VersionTooNew: return "VersionTooNew";
        case IntakeStatus::MigrationUnavailable: return "MigrationUnavailable";
        case IntakeStatus::MigrationFailed: return "MigrationFailed";
        case IntakeStatus::Malformed: return "Malformed";
        case IntakeStatus::StructureInvalid: return "StructureInvalid";
    }
    return "Malformed";
}

namespace {

// 幾何欄位消毒：壞值就地換成消毒後的值（經 **E7-12 set_value**），並累加修補數。
// 非 Number 的幾何欄位（字串 / 布林 / 容器）一律換成 0 並計為一次修補。
Value sanitize_geometry(const Value& props, IntakeReport& rep) {
    // 前兩個是座標、後兩個是尺寸（夾限規則不同）。以索引區分，不靠字串比較。
    static const char* const kGeom[] = {kPropX, kPropY, kPropWidth, kPropHeight};
    Value out = props;
    for (std::size_t gi = 0; gi < 4; ++gi) {
        const char* const key = kGeom[gi];
        const Value* cur = out.find(key);
        if (cur == nullptr) {
            continue;  // 缺欄位不算壞：之後走預設 0，退化尺寸是具名的 EmptyArea。
        }
        const bool is_extent = (gi >= 2);
        if (!cur->is_number()) {
            // 值根本不是數字（字串 / 布林 / 整棵子樹 / list）→ 整個被扔掉，
            // 那是**內容遺失**，不是把一個數字夾回範圍內（驗收 R12）。
            out = ds::format::set_value(out, Path{PathSegment::of_key(key)},
                                        Value::number(0.0));
            ++rep.discarded_values;
            continue;
        }
        const double raw = cur->as_number();
        const double safe = is_extent ? sanitize_extent(raw) : sanitize_coord(raw);
        // NaN != 任何值 → NaN 一定會走進這裡被換掉（這正是要的；K-016）。
        if (raw != safe) {
            ++rep.repaired_fields;  // 數字 → 夾回範圍內的數字：自我修復
            out = ds::format::set_value(out, Path{PathSegment::of_key(key)},
                                        Value::number(safe));
        }
    }
    return out;
}

}  // namespace

IntakeReport ingest_layout_text(const std::string& text,
                                const ds::format::MigrationRegistry* migrations) {
    IntakeReport rep;

    // --- 1. 版本分流（在 parse 之前；parse 分不出太新 / 太舊 / 缺失）---
    FormatVersion ver{};
    switch (peek_format_version(text, ver)) {
        case VersionPeek::Missing:
            rep.status = IntakeStatus::MissingVersion;
            rep.message = "存檔缺少 format_version；本層不猜版本（猜錯 = 默默改語意）";
            return rep;
        case VersionPeek::Malformed:
            rep.status = IntakeStatus::MalformedVersion;
            rep.message = "format_version 無法解析（需 major.minor，如 1.0）";
            return rep;
        case VersionPeek::Found:
            break;
    }
    rep.has_version = true;
    rep.source_version = ver;

    const FormatVersion target = ds::layout::kLayoutFormat;
    if (ds::format::version_less(target, ver)) {
        rep.status = IntakeStatus::VersionTooNew;
        rep.message = "存檔版本 " + ds::format::version_to_string(ver) + " 比本實作 " +
                      ds::format::version_to_string(target) + " 新；本實作不降級";
        return rep;
    }
    const bool needs_migration = ds::format::version_less(ver, target);

    // --- 2. 解析（舊版先換版本標記以通過 E7-01 的版本閘；語意轉換仍交給 E7-08）---
    ds::format::ParseResult parsed = ds::format::ParseResult::failure({0, ""});
    try {
        parsed = ds::format::parse(needs_migration ? restamp_format_version(text, target)
                                                   : text);
    } catch (const std::exception& ex) {
        rep.status = IntakeStatus::Malformed;
        rep.message = std::string("解析器丟出例外：") + ex.what();
        return rep;
    }
    if (!parsed.ok()) {
        const ds::format::ParseError& err = parsed.error();
        rep.status = IntakeStatus::Malformed;
        rep.message = "行 " + std::to_string(err.line) + "：" + err.message;
        return rep;
    }

    Value root = parsed.document().root;

    // --- 3. 遷移（E7-08）---
    if (needs_migration) {
        if (migrations == nullptr) {
            rep.status = IntakeStatus::MigrationUnavailable;
            rep.message = "存檔版本 " + ds::format::version_to_string(ver) +
                          " 比本實作舊，但沒有註冊任何遷移鏈（E7-08）";
            return rep;
        }
        try {
            const ds::format::MigrateResult mr = migrations->migrate(root, ver, target);
            if (!mr.ok()) {
                rep.status = (mr.error().status == ds::format::MigrateStatus::NoPath)
                                 ? IntakeStatus::MigrationUnavailable
                                 : IntakeStatus::MigrationFailed;
                rep.message = mr.error().message;
                return rep;
            }
            root = mr.value();
        } catch (const std::exception& ex) {
            rep.status = IntakeStatus::MigrationFailed;
            rep.message = std::string("遷移步驟丟出例外：") + ex.what();
            return rep;
        }
    }

    // --- 4. 容忍式結構讀取（壞元件具名丟棄，不炸掉整份佈局）---
    if (!root.is_map()) {
        rep.status = IntakeStatus::StructureInvalid;
        rep.message = "佈局根節點必須為 Map";
        return rep;
    }
    const Value* elems = root.find("elements");
    if (elems != nullptr && !elems->is_null() && !elems->is_list()) {
        rep.status = IntakeStatus::StructureInvalid;
        rep.message = "'elements' 必須為清單";
        return rep;
    }

    // 根層除了 `elements` 之外的鍵，本層的資料模型帶不走（`serialize_layout` 只寫 elements）。
    // `format_version` 不在這裡——E7-01 把它收進 Document，不進 root map。
    for (const Value::Member& m : root.as_map()) {
        if (m.first != "elements") {
            ++rep.unconsumed_keys;
        }
    }

    std::set<std::string> seen;
    if (elems != nullptr && elems->is_list()) {
        for (const Value& item : elems->as_list()) {
            if (!item.is_map()) {
                ++rep.dropped_elements;
                continue;
            }
            const Value* idv = item.find("id");
            const Value* tv = item.find("type");
            if (idv == nullptr || !idv->is_string() || idv->as_string().empty() ||
                tv == nullptr || !tv->is_string() || tv->as_string().empty()) {
                ++rep.dropped_elements;  // 缺 id / type 的元件無法定位，丟棄並計數
                continue;
            }
            if (!seen.insert(idv->as_string()).second) {
                ++rep.dropped_elements;  // 重複 id 會讓之後的編輯指向不明，丟棄後者
                continue;
            }
            LayoutElement e;
            e.id = idv->as_string();
            e.type = tv->as_string();
            // 元件層級除了 id / type / properties 之外的鍵同樣帶不走。
            for (const Value::Member& m : item.as_map()) {
                if (m.first != "id" && m.first != "type" && m.first != "properties") {
                    ++rep.unconsumed_keys;
                }
            }
            const Value* pv = item.find("properties");
            if (pv != nullptr && !pv->is_null() && !pv->is_map()) {
                // 整個 properties 區塊被丟掉 —— 這是**內容遺失**，不是欄位消毒。
                // 修訂 2 把它算進 `repaired_fields`，於是它被當成「自我修復」而放行覆寫。
                ++rep.discarded_properties;
            }
            Value props = (pv == nullptr) ? Value::map({}) : as_props(*pv);
            e.properties = sanitize_geometry(props, rep);
            rep.state.elements.push_back(std::move(e));
        }
    }

    if (needs_migration) {
        rep.status = IntakeStatus::Migrated;  // 修補數仍由計數器照實回報
    } else if (rep.degraded()) {
        rep.status = IntakeStatus::Repaired;
    } else {
        rep.status = IntakeStatus::Ok;
    }
    return rep;
}

// =============================================================================
// 編輯 / 存檔 / 重載的具名結果
// =============================================================================

const char* to_string(EditStatus status) noexcept {
    switch (status) {
        case EditStatus::Ok: return "Ok";
        case EditStatus::EmptyId: return "EmptyId";
        case EditStatus::EmptyKind: return "EmptyKind";
        case EditStatus::DuplicateId: return "DuplicateId";
        case EditStatus::UnknownElement: return "UnknownElement";
        case EditStatus::NonFiniteValue: return "NonFiniteValue";
        case EditStatus::OutOfRange: return "OutOfRange";
        case EditStatus::BadPath: return "BadPath";
        case EditStatus::RejectedByModel: return "RejectedByModel";
        case EditStatus::UnknownDropContent: return "UnknownDropContent";
    }
    return "Ok";
}

const char* to_string(SaveStatus status) noexcept {
    switch (status) {
        case SaveStatus::Ok: return "Ok";
        case SaveStatus::EmptyName: return "EmptyName";
        case SaveStatus::SerializeFailed: return "SerializeFailed";
        case SaveStatus::VerifyFailed: return "VerifyFailed";
        case SaveStatus::RefusedUnreadableTarget: return "RefusedUnreadableTarget";
        case SaveStatus::RefusedIncompleteTarget: return "RefusedIncompleteTarget";
        case SaveStatus::RefusedSourceNotDerived: return "RefusedSourceNotDerived";
        case SaveStatus::RefusedTargetChangedSinceLoad:
            return "RefusedTargetChangedSinceLoad";
        case SaveStatus::RefusedBackupSlotExhausted: return "RefusedBackupSlotExhausted";
    }
    return "Ok";
}

const char* to_string(Overwrite policy) noexcept {
    switch (policy) {
        case Overwrite::Refuse: return "Refuse";
        case Overwrite::DiscardTargetContent: return "DiscardTargetContent";
    }
    return "Refuse";
}

const char* to_string(ReloadOutcome outcome) noexcept {
    switch (outcome) {
        case ReloadOutcome::NoSource: return "NoSource";
        case ReloadOutcome::Unchanged: return "Unchanged";
        case ReloadOutcome::Reloaded: return "Reloaded";
        case ReloadOutcome::Rejected: return "Rejected";
    }
    return "NoSource";
}

// =============================================================================
// 繪製橋接
// =============================================================================

PaintReport paint_layout(const std::vector<PlacedDraw>& placed,
                         const ds::host::ElementRendererTable& table,
                         ds::host::PaintSink& sink) {
    PaintReport report;
    for (const PlacedDraw& p : placed) {
        switch (table.draw(p.draw, p.box, sink)) {
            case ds::host::DispatchResult::Drawn: ++report.drawn; break;
            case ds::host::DispatchResult::UnknownKind: ++report.unknown_kind; break;
            case ds::host::DispatchResult::EmptyArea: ++report.empty_area; break;
        }
    }
    return report;
}

// =============================================================================
// EditAssembly
// =============================================================================

EditAssembly::EditAssembly(ds::layout::LayoutStorage& storage) : storage_(storage) {
    last_intake_.status = IntakeStatus::Ok;
}

LayoutElement* EditAssembly::find(const std::string& id) {
    for (LayoutElement& e : state_.elements) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const LayoutElement* EditAssembly::find(const std::string& id) const {
    for (const LayoutElement& e : state_.elements) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

EditStatus EditAssembly::note_edit(EditStatus status, std::string message) {
    last_edit_ = status;
    last_message_ = std::move(message);
    if (status == EditStatus::Ok) {
        note_authored();
        ++stats_.edits;
    } else {
        ++stats_.edits_rejected;
    }
    return status;
}

// 路徑已存在 → E7-14 `apply_edit`（型別 / 約束驗證）；不存在 → E7-12 `set_value`（新增）。
// **不碰計數器**：一次公開編輯可能要寫好幾個欄位（見 set_placement），計數由公開層做一次。
EditStatus EditAssembly::write_property(LayoutElement& element, const std::string& path,
                                        const Value& value, std::string& message) {
    // **深層**非有限檢查放在這裡（而不是 `edit_property`）：`apply_drop` 也走這條路，
    // 掛在呼叫端等於漏掉它（驗收 R10）。F2 的教訓就是「守衛放錯位置」，修它的時候
    // 不該又放錯一次。
    if (contains_non_finite(value)) {
        std::string where;
        for (const std::string& bad : non_finite_paths(value)) {
            if (!where.empty()) {
                where += "、";
            }
            where += bad.empty() ? "（值本身）" : bad;
        }
        message = "候選值含非有限數，拒絕寫入持久化狀態：" + path + " → " + where;
        return EditStatus::NonFiniteValue;
    }
    Path parsed;
    if (!ds::format::parse_path(path, parsed)) {
        message = "路徑語法錯誤：" + path;
        return EditStatus::BadPath;
    }
    const Value props = as_props(element.properties);
    const bool exists = (locate(props, parsed) != nullptr);
    try {
        element.properties = exists ? ds::format::apply_edit(props, parsed, value)
                                    : ds::format::set_value(props, parsed, value);
    } catch (const std::exception& ex) {
        message = std::string(ex.what());
        return exists ? EditStatus::RejectedByModel : EditStatus::BadPath;
    }
    message.clear();
    return EditStatus::Ok;
}

EditStatus EditAssembly::add_element(const std::string& id, const std::string& kind,
                                     const LogicalPlacement& placement) {
    if (id.empty()) {
        return note_edit(EditStatus::EmptyId, "元件 id 不得為空");
    }
    if (kind.empty()) {
        return note_edit(EditStatus::EmptyKind, "元件 kind 不得為空");
    }
    if (find(id) != nullptr) {
        return note_edit(EditStatus::DuplicateId, "已存在 id 為 '" + id + "' 的元件");
    }
    if (!placement.finite()) {
        return note_edit(EditStatus::NonFiniteValue, "位置 / 大小含非有限數，拒絕寫入");
    }
    if (!placement.within_limits()) {
        return note_edit(EditStatus::OutOfRange, "位置 / 大小超出夾限");
    }
    LayoutElement e;
    e.id = id;
    e.type = kind;
    e.properties = Value::map({{kPropX, Value::number(placement.x)},
                               {kPropY, Value::number(placement.y)},
                               {kPropWidth, Value::number(placement.width)},
                               {kPropHeight, Value::number(placement.height)}});
    state_.elements.push_back(std::move(e));
    return note_edit(EditStatus::Ok, std::string());
}

EditStatus EditAssembly::remove_element(const std::string& id) {
    for (std::size_t i = 0; i < state_.elements.size(); ++i) {
        if (state_.elements[i].id == id) {
            state_.elements.erase(state_.elements.begin() + static_cast<std::ptrdiff_t>(i));
            return note_edit(EditStatus::Ok, std::string());
        }
    }
    return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
}

bool EditAssembly::contains(const std::string& id) const { return find(id) != nullptr; }

std::vector<std::string> EditAssembly::ids() const {
    std::vector<std::string> out;
    out.reserve(state_.elements.size());
    for (const LayoutElement& e : state_.elements) {
        out.push_back(e.id);
    }
    return out;
}

EditStatus EditAssembly::drag_element(const std::string& id, double dx, double dy) {
    LayoutElement* e = find(id);
    if (e == nullptr) {
        return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
    }
    // 位移量先擋：NaN / Inf 的拖曳不得進入持久化狀態（K-016：明寫 isfinite）。
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return note_edit(EditStatus::NonFiniteValue, "拖曳位移含非有限數，拒絕寫入");
    }
    const Value props = as_props(e->properties);
    const double nx = sanitize_coord(number_or(props, kPropX, 0.0)) + dx;
    const double ny = sanitize_coord(number_or(props, kPropY, 0.0)) + dy;
    if (!std::isfinite(nx) || !std::isfinite(ny)) {
        return note_edit(EditStatus::NonFiniteValue, "拖曳後座標非有限數，拒絕寫入");
    }
    LogicalPlacement next;
    next.x = nx;
    next.y = ny;
    next.width = sanitize_extent(number_or(props, kPropWidth, 0.0));
    next.height = sanitize_extent(number_or(props, kPropHeight, 0.0));
    if (!next.within_limits()) {
        return note_edit(EditStatus::OutOfRange, "拖曳後座標超出夾限");
    }
    return set_placement(id, next);
}

EditStatus EditAssembly::set_placement(const std::string& id,
                                       const LogicalPlacement& placement) {
    LayoutElement* e = find(id);
    if (e == nullptr) {
        return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
    }
    if (!placement.finite()) {
        return note_edit(EditStatus::NonFiniteValue, "位置 / 大小含非有限數，拒絕寫入");
    }
    if (!placement.within_limits()) {
        return note_edit(EditStatus::OutOfRange, "位置 / 大小超出夾限");
    }
    // 四筆一起成立才提交（先寫在副本上，避免「x 寫進去了、y 失敗」的半套狀態）。
    LayoutElement draft = *e;
    const std::pair<const char*, double> fields[] = {{kPropX, placement.x},
                                                     {kPropY, placement.y},
                                                     {kPropWidth, placement.width},
                                                     {kPropHeight, placement.height}};
    std::string message;
    for (const auto& f : fields) {
        const EditStatus st = write_property(draft, f.first, Value::number(f.second), message);
        if (st != EditStatus::Ok) {
            return note_edit(st, std::move(message));  // *e 完全沒被動過
        }
    }
    e->properties = draft.properties;
    return note_edit(EditStatus::Ok, std::string());
}

bool EditAssembly::placement_of(const std::string& id, LogicalPlacement& out) const {
    const LayoutElement* e = find(id);
    if (e == nullptr) {
        return false;
    }
    const Value props = as_props(e->properties);
    LogicalPlacement p;
    p.x = number_or(props, kPropX, 0.0);
    p.y = number_or(props, kPropY, 0.0);
    p.width = number_or(props, kPropWidth, 0.0);
    p.height = number_or(props, kPropHeight, 0.0);
    out = sanitize_placement(p, nullptr);
    return true;
}

EditStatus EditAssembly::set_kind(const std::string& id, const std::string& kind) {
    LayoutElement* e = find(id);
    if (e == nullptr) {
        return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
    }
    if (kind.empty()) {
        return note_edit(EditStatus::EmptyKind, "元件 kind 不得為空");
    }
    e->type = kind;
    return note_edit(EditStatus::Ok, std::string());
}

std::string EditAssembly::kind_of(const std::string& id) const {
    const LayoutElement* e = find(id);
    return (e == nullptr) ? std::string() : e->type;
}

EditStatus EditAssembly::edit_property(const std::string& id, const std::string& path,
                                       const Value& value) {
    LayoutElement* e = find(id);
    if (e == nullptr) {
        return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
    }
    std::string message;
    const EditStatus st = write_property(*e, path, value, message);
    return note_edit(st, std::move(message));
}

bool EditAssembly::property_of(const std::string& id, const std::string& path,
                               Value& out) const {
    const LayoutElement* e = find(id);
    if (e == nullptr) {
        return false;
    }
    Path parsed;
    if (!ds::format::parse_path(path, parsed)) {
        return false;
    }
    const Value props = as_props(e->properties);
    const Value* found = locate(props, parsed);
    if (found == nullptr) {
        return false;
    }
    out = *found;
    return true;
}

EditStatus EditAssembly::remove_property(const std::string& id, const std::string& path) {
    LayoutElement* e = find(id);
    if (e == nullptr) {
        return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
    }
    Path parsed;
    if (!ds::format::parse_path(path, parsed) || parsed.empty() ||
        parsed.back().kind != PathSegment::Kind::Key) {
        // 只支援移除 map 鍵：從 list 中間抽掉一個元素會讓其後所有索引位移，
        // 而呼叫端手上的路徑是按索引寫的——那是靜默的錯位，不如明確拒絕。
        return note_edit(EditStatus::BadPath, "只能移除 map 鍵（且路徑不得為空）：" + path);
    }
    const Value props = as_props(e->properties);
    const Path parent(parsed.begin(), parsed.end() - 1);
    const Value* owner = locate(props, parent);
    if (owner == nullptr || !owner->is_map() || owner->find(parsed.back().key) == nullptr) {
        return note_edit(EditStatus::BadPath, "路徑不存在：" + path);
    }
    std::vector<Value::Member> kept;
    for (const Value::Member& m : owner->as_map()) {
        if (m.first != parsed.back().key) {
            kept.push_back(m);
        }
    }
    try {
        e->properties = parent.empty() ? Value::map(std::move(kept))
                                       : ds::format::set_value(props, parent,
                                                               Value::map(std::move(kept)));
    } catch (const std::exception& ex) {
        return note_edit(EditStatus::BadPath, std::string(ex.what()));
    }
    return note_edit(EditStatus::Ok, std::string());
}

EditStatus EditAssembly::apply_drop(const std::string& id,
                                    const ds::format::DropContent& drop) {
    LayoutElement* e = find(id);
    if (e == nullptr) {
        return note_edit(EditStatus::UnknownElement, "查無 id 為 '" + id + "' 的元件");
    }
    if (drop.kind == ds::format::DropKind::Unknown) {
        return note_edit(EditStatus::UnknownDropContent,
                         "E7-15 認不出這段拖放內容，不產生設定項");
    }
    ds::format::ConfigItem item;
    try {
        item = ds::format::generate_config_item(drop);
    } catch (const std::exception& ex) {
        return note_edit(EditStatus::UnknownDropContent, std::string(ex.what()));
    }
    std::string message;
    const EditStatus st = write_property(*e, item.path, item.value, message);
    return note_edit(st, std::move(message));
}

// --- 存檔 ---

SaveStatus EditAssembly::serialize_verified(std::string& text, std::string& message) const {
    // --- 先指名道姓地找非有限數值 ---
    // 它是往返自驗失敗最常見的原因，而「往返不一致」這個訊息說不出是哪一欄：第一版的
    // `last_message()` 只有「往返自驗不通過（Ok）：」——狀態 Ok、原因空字串，呼叫端拿不到
    // 任何線索去定位被汙染的路徑（V1.7 的 F2）。這裡一次列出**全部**路徑，讓呼叫端能一輪
    // 修完（而不是修一個、再 save 一次、再發現下一個）。
    std::string bad;
    for (const LayoutElement& e : state_.elements) {
        for (const std::string& path : non_finite_paths(as_props(e.properties))) {
            if (!bad.empty()) {
                bad += "、";
            }
            bad += "'" + e.id + "'.properties" + (path.empty() ? "" : "." + path);
        }
    }
    if (!bad.empty()) {
        message = "持久化狀態含非有限數值，拒絕存檔：" + bad;
        return SaveStatus::VerifyFailed;
    }

    try {
        // 格式的唯一事實來源：E9-05（`LayoutStore::save` 用的也是這個函式）。
        text = ds::layout::serialize_layout(state_);
    } catch (const std::exception& ex) {
        message = std::string(ex.what());
        return SaveStatus::SerializeFailed;
    }
    // 往返自驗：序列化 → 以本層讀回 → 逐欄比對。過了才准提交。
    const IntakeReport check = ingest_layout_text(text, nullptr);
    if (check.status != IntakeStatus::Ok || check.state != state_) {
        message = "往返自驗不通過（" + std::string(to_string(check.status)) + "）" +
                  (check.message.empty() ? std::string() : "：" + check.message) +
                  describe_mismatch(state_, check.state);
        return SaveStatus::VerifyFailed;
    }
    message.clear();
    return SaveStatus::Ok;
}

// 往返前後第一個對不上的地方（給人看的，不是給機器解析的）。
std::string EditAssembly::describe_mismatch(const ds::layout::LayoutState& before,
                                            const ds::layout::LayoutState& after) {
    if (before.elements.size() != after.elements.size()) {
        return "；元件數 " + std::to_string(before.elements.size()) + " → " +
               std::to_string(after.elements.size());
    }
    for (std::size_t i = 0; i < before.elements.size(); ++i) {
        const LayoutElement& a = before.elements[i];
        const LayoutElement& b = after.elements[i];
        if (a.id != b.id) {
            return "；第 " + std::to_string(i) + " 個元件的 id '" + a.id + "' → '" + b.id + "'";
        }
        if (a.type != b.type) {
            return "；元件 '" + a.id + "' 的 kind '" + a.type + "' → '" + b.type + "'";
        }
        if (a != b) {
            return "；元件 '" + a.id + "' 的 properties 往返後不一致";
        }
    }
    return std::string();
}

SaveStatus EditAssembly::overwrite_check(const std::string& name,
                                         IntakeReport* target_out) const {
    // 覆寫決定的**單一述詞**。`save()` / `dry_run_save()` /
    // `target_is_safe_to_overwrite()` 三者都呼叫這裡，不得各自為政
    // ——修訂 2 的預檢與 save() 用了不同述詞，於是預檢在資料將毀的那一刻給綠燈。
    std::string existing;
    if (!storage_.get(name, existing)) {
        if (target_out != nullptr) {
            IntakeReport miss;
            miss.status = IntakeStatus::NotFound;
            miss.message = "storage 裡沒有名為 '" + name + "' 的佈局";
            *target_out = std::move(miss);
        }
        return SaveStatus::Ok;  // 沒有東西會被毀掉
    }
    IntakeReport target = ingest_layout_text(existing, migrations_);
    SaveStatus verdict = SaveStatus::Ok;
    if (!target.carries_everything()) {
        // 目標側：那份檔案本身讀不出來 / 本層帶不走它的全部內容。
        verdict = target.usable() ? SaveStatus::RefusedIncompleteTarget
                                  : SaveStatus::RefusedUnreadableTarget;
    } else if (!source_covers(name)) {
        // 來歷側：目標是完好的，但我手上這份模型**不是源自它**。
        verdict = SaveStatus::RefusedSourceNotDerived;
    } else if (!same_layout(target.state, baseline_)) {
        // **統一判據**（F6）：目標在我上次看過它之後被改過，所以它現在有一些
        // **本 session 從未見過**的內容。覆寫會把那些內容刪掉。
        //
        // 反過來，目標 == baseline 時，「目標有而模型沒有」的東西只可能是
        // **我自己刪的**（`remove_element` / `remove_property` / 編輯），那是使用者的意圖，
        // 必須放行——只比內容不比 baseline 會把那條正當操作一起擋死。
        verdict = SaveStatus::RefusedTargetChangedSinceLoad;
    }
    if (target_out != nullptr) {
        *target_out = std::move(target);
    }
    return verdict;
}

bool EditAssembly::target_is_safe_to_overwrite(const std::string& name,
                                               IntakeReport* report) const {
    return overwrite_check(name, report) == SaveStatus::Ok;
}

SaveStatus EditAssembly::dry_run_save(const std::string& name) const {
    std::string text;
    std::string message;
    const SaveStatus st = serialize_verified(text, message);
    if (st != SaveStatus::Ok) {
        return st;
    }
    return name.empty() ? SaveStatus::Ok : overwrite_check(name, nullptr);
}

bool EditAssembly::next_backup_name(const std::string& name, std::string& out) const {
    // 備份**永遠不覆寫任何既有的名字**（驗收 R8）。第一個用 `<name>.rejected`，
    // 之後依序加序號；全部占用就讓呼叫端拒絕，寧可存不了也不蓋掉東西。
    for (int i = 1; i <= kMaxRejectedBackups; ++i) {
        std::string candidate = name + kRejectedSuffix;
        if (i > 1) {
            candidate += "." + std::to_string(i);
        }
        if (!storage_.has(candidate)) {
            out = std::move(candidate);
            return true;
        }
    }
    return false;
}

void EditAssembly::note_origin(SourceOrigin origin, const std::string& name, bool complete,
                               const LayoutState& baseline) {
    origin_ = origin;
    origin_name_ = (origin == SourceOrigin::Loaded) ? name : std::string();
    origin_complete_ = complete;
    baseline_ = baseline;
}

void EditAssembly::note_authored() {
    // 編輯**不會**破壞推導關係：從某份存檔完整載入之後改了幾個值，模型仍然源自那份存檔
    // （使用者的編輯就是他的意圖）。只有「本來什麼都沒有」才因為編輯而變成 Authored。
    if (origin_ == SourceOrigin::Empty) {
        note_origin(SourceOrigin::Authored, std::string(), false);
    }
    // 編輯**不動 baseline**：baseline 記的是「上次看到的**目標**長什麼樣」，
    // 不是「模型長什麼樣」。兩者的差就是使用者自己做的修改（含 remove_element），
    // 那正是必須被允許的那一半。
}

SaveStatus EditAssembly::save(const std::string& name, Overwrite policy) {
    if (name.empty()) {
        last_save_ = SaveStatus::EmptyName;
        last_message_ = "佈局名稱不得為空（E9-05 契約）";
        ++stats_.saves_rejected;
        return last_save_;
    }
    std::string text;
    std::string message;
    const SaveStatus st = serialize_verified(text, message);
    if (st != SaveStatus::Ok) {
        last_save_ = st;
        last_message_ = std::move(message);
        ++stats_.saves_rejected;
        return st;  // **沒有碰過 storage**：既有存檔原封不動
    }

    // --- 覆寫守衛：**對稱**的兩側 ---
    //
    //   目標側：我從目標那裡帶得全嗎？   `IntakeReport::carries_everything()`
    //   來源側：我手上這份模型確實是那份目標的內容嗎？  `source_covers(name)`
    //
    // 修訂 1 沒有守衛、修訂 2 問「目標可不可讀」、修訂 3 問「目標帶不帶得全」——
    // 每一輪都擴大了判據，但都只擴大到剛被展示的那個方向，於是同一個形狀被攻破三次。
    // 第三次（F5）最諷刺：觸發它的是本層自己的拒絕訊息——叫使用者去修檔案，
    // 他照做了，修好之後目標側全部通過，然後他的資料被一份空模型蓋掉。
    IntakeReport target;
    const SaveStatus verdict = overwrite_check(name, &target);
    if (verdict != SaveStatus::Ok) {
        if (policy == Overwrite::Refuse) {
            last_save_ = verdict;
            last_message_ = describe_unsafe_target(name, target, verdict);
            ++stats_.saves_rejected;
            switch (verdict) {
                case SaveStatus::RefusedUnreadableTarget: ++stats_.saves_refused_target; break;
                case SaveStatus::RefusedIncompleteTarget:
                    ++stats_.saves_refused_incomplete;
                    break;
                case SaveStatus::RefusedTargetChangedSinceLoad:
                    ++stats_.saves_refused_changed;
                    break;
                default: ++stats_.saves_refused_source; break;
            }
            return last_save_;  // **沒有碰過 storage**
        }
        // 明示丟棄：先把目標原文另存一份，再覆寫。連「明示丟棄」都不真的丟掉位元組，
        // 而且備份**不覆寫任何既有的名字**（R8）。
        std::string existing;
        std::string backup;
        // **只在真的會失去東西時才備份**（驗收 R16）：目標現在的內容若與即將寫出去的
        // 內容相同，覆寫不會損失任何東西，產生一份備份只會白白吃掉備份槽
        //（未宣告的 reset() + 明示丟棄迴圈到第 33 次就會讓那個名字再也存不了）。
        // 注意用的是 `carries_everything()` 而不是 `usable()`：目標若有內容沒被帶進來
        // （`lost_content() > 0`），檔案裡就有 `target.state` 看不到的東西——那時
        // `target.state == state_` 仍可能成立，但覆寫**還是**會毀掉那些沒被帶走的內容。
        // 第一版寫成 `usable()`，`ExplicitDiscardWorksForPartiallyCarriedTargetsToo`
        // 當場紅燈（備份沒產生）。
        // 同樣用全域比較：`state_` 一樣可能來自未消毒的 `reset()`（R18）。
        // 今日這一行走到時 `serialize_verified()` 已經成功，所以 `state_` 必為可序列化，
        // 但讓「本檔案裡的 LayoutState 比較一律不 throw」成為一條不變式，比依賴
        // 「呼叫順序剛好保護了它」可靠。
        const bool would_lose =
            !target.carries_everything() || !same_layout(target.state, state_);
        if (would_lose && storage_.get(name, existing)) {
            if (!next_backup_name(name, backup)) {
                last_save_ = SaveStatus::RefusedBackupSlotExhausted;
                last_message_ = "'" + name + "' 的備份名稱已全部占用（上限 " +
                                std::to_string(kMaxRejectedBackups) +
                                "），拒絕覆寫——寧可存不了，也不蓋掉任何東西";
                ++stats_.saves_rejected;
                return last_save_;  // **沒有碰過 storage**
            }
            storage_.put(backup, existing);
            ++stats_.rejected_backups;
        }
    }

    storage_.put(name, text);  // 唯一的提交點
    // 寫出去之後，手上的模型就是這份存檔的完整內容了——下一次存同一個名字不必再問。
    // 寫出去之後，目標的內容就是我剛寫的這一份——baseline 隨之更新。
    note_origin(SourceOrigin::Loaded, name, true, state_);
    last_save_ = SaveStatus::Ok;
    last_message_.clear();
    ++stats_.saves;
    return SaveStatus::Ok;
}

// 「為什麼不讓你覆寫」的人類可讀說明：說得出是哪一種、少了什麼、以及怎麼跨過去。
std::string EditAssembly::describe_unsafe_target(const std::string& name,
                                                 const IntakeReport& target,
                                                 SaveStatus verdict) const {
    std::string why;
    if (verdict == SaveStatus::RefusedSourceNotDerived) {
        // 來源側：目標是完好的，壞的是「我手上這份模型不是它」。
        return "目標存檔 '" + name +
               "' 是完好的，但手上這份佈局並非它的內容（來源：" +
               std::string(to_string(origin_)) +
               (origin_ == SourceOrigin::Loaded
                    ? "，來自 '" + origin_name_ + "'" +
                          (origin_complete_ ? "" : "（載入時有內容沒帶走）")
                    : "") +
               "）；直接覆寫會把 '" + name +
               "' 的內容換成一份它從來沒有過的佈局。請先成功載入 '" + name +
               "'，或以 Overwrite::DiscardTargetContent 明示丟棄（原文會先另存為 '" +
               name + kRejectedSuffix + "'）";
    }
    if (!target.usable()) {
        why = "目前讀不出來（" + std::string(to_string(target.status)) + "）：" +
              target.message;
    } else {
        why = "讀得出來（" + std::string(to_string(target.status)) +
              "），但本層沒能帶走它的全部內容：";
        std::string parts;
        auto add = [&parts](int n, const char* label) {
            if (n > 0) {
                if (!parts.empty()) {
                    parts += "、";
                }
                parts += std::string(label) + " " + std::to_string(n);
            }
        };
        add(target.dropped_elements, "被丟棄的元件");
        add(target.discarded_properties, "被丟棄的 properties 區塊");
        add(target.unconsumed_keys, "帶不走的鍵");
        why += parts;
    }
    return "目標存檔 '" + name + "' " + why +
           "；覆寫它會永久毀掉使用者的佈局。先修好那份檔案，或以 "
           "Overwrite::DiscardTargetContent 明示丟棄（原文會先另存為 '" +
           name + kRejectedSuffix + "'）";
}

// --- 還原 ---

void EditAssembly::adopt(IntakeReport report) {
    stats_.elements_dropped += report.dropped_elements;
    stats_.fields_repaired += report.repaired_fields;
    stats_.content_lost += report.lost_content();
    last_message_ = report.message;
    if (report.usable()) {
        state_ = report.state;
    }
    // 不可用 → **保留現有佈局**，而且完全沒有把壞狀態寫回 storage。
    last_intake_ = std::move(report);
}

IntakeStatus EditAssembly::restore(const std::string& name) {
    ++stats_.loads;
    std::string text;
    if (!storage_.get(name, text)) {
        IntakeReport miss;
        miss.status = IntakeStatus::NotFound;
        miss.message = "查無名為 '" + name + "' 的佈局";
        adopt(std::move(miss));
        ++stats_.loads_failed;
        return IntakeStatus::NotFound;
    }
    IntakeReport rep = ingest_layout_text(text, migrations_);
    const bool usable = rep.usable();
    const bool complete = rep.carries_everything();
    const IntakeStatus status = rep.status;
    adopt(std::move(rep));
    if (usable) {
        // 載入成功 → 模型源自這份存檔；`complete` 記下當時有沒有東西沒帶走，
        // baseline 記下**當時目標長什麼樣**（F6 的另一半）。
        note_origin(SourceOrigin::Loaded, name, complete, state_);
    } else {
        // 載入失敗 → 狀態沒變，來源也沒變（**不會**因此變成「源自 name」）。
        ++stats_.loads_failed;
    }
    return status;
}

void EditAssembly::reset(const LayoutState& state, const std::string& derived_from) {
    state_ = state;
    if (derived_from.empty()) {
        // 呼叫端沒說這是誰的內容 → 推導關係失效，覆寫既有存檔需要明示丟棄。
        note_origin(SourceOrigin::Injected, std::string(), false);
    } else {
        // 呼叫端的斷言：這份狀態就是那個存檔的內容。
        // **修訂 5 起這個斷言會被驗證**（驗收 R15）：它只是提供一份 baseline，
        // 而 baseline 在提交前會與目標的實際內容比對。宣告錯了 → 具名拒絕，
        // 不再是「盲目信任 → 無聲毀資料、連備份都沒有」。
        note_origin(SourceOrigin::Loaded, derived_from, true, state);
    }
}

// --- 熱重載 ---

void EditAssembly::attach_source(ds::format::ReloadSource& source,
                                 const std::string& backing_name) {
    source_ = &source;
    source_backing_name_ = backing_name;
    reloader_ = std::make_unique<ds::format::HotReloader>(source);
    last_reload_report_.clear();
}

void EditAssembly::detach_source() noexcept {
    source_ = nullptr;
    source_backing_name_.clear();
    reloader_.reset();
}

ReloadOutcome EditAssembly::poll_reload() {
    if (reloader_ == nullptr || source_ == nullptr) {
        last_message_ = "沒有接上任何 ReloadSource";
        return ReloadOutcome::NoSource;
    }
    bool unchanged = false;
    try {
        const ds::format::ReloadResult r = reloader_->poll();
        unchanged = r.unchanged();
        // E7-06 定位到行的報告（E7-07 失敗時產生）；成功時為空字串。
        last_reload_report_ = r.report;
    } catch (const std::exception& ex) {
        // E7-01 的 parse 在極端 format_version 下會丟例外（見標頭 §3）；不讓它穿出本層。
        last_reload_report_ = std::string("E7-07 重載丟出例外：") + ex.what();
    }
    if (unchanged) {
        return ReloadOutcome::Unchanged;
    }
    const std::string content = source_->content();  // 按值取用（K-013：不綁臨時物件）
    IntakeReport rep = ingest_layout_text(content, migrations_);
    const bool usable = rep.usable();
    const bool complete = rep.carries_everything();
    adopt(std::move(rep));
    if (usable) {
        if (!source_backing_name_.empty()) {
            // 呼叫端宣告了「這個來源就是那份存檔」：完整重載才保住推導關係，
            // 並以重載到的內容當 baseline。**那份 baseline 一樣會在提交前被驗證**
            // （R15）——相位 1 的 ReloadSource 與 storage 是兩個互不相識的物件，
            // 宣告錯了就會在 save 時變成具名拒絕，而不是毀資料。
            note_origin(SourceOrigin::Loaded, source_backing_name_, complete, state_);
        } else {
            // 沒有宣告對應存檔 → 本層不猜它是誰的內容。
            note_origin(SourceOrigin::Reloaded, std::string(), complete);
        }
        ++stats_.reloads;
        return ReloadOutcome::Reloaded;
    }
    ++stats_.reloads_rejected;
    return ReloadOutcome::Rejected;  // 舊佈局原樣保留
}

// --- 繪製 ---

std::vector<PlacedDraw> EditAssembly::resolve_draws(double dpi_scale) const {
    const double scale = sanitize_dpi(dpi_scale);
    std::vector<PlacedDraw> out;
    out.reserve(state_.elements.size());
    for (const LayoutElement& e : state_.elements) {
        const Value props = as_props(e.properties);
        PlacedDraw placed;
        placed.id = e.id;
        placed.draw.kind = e.type;  // kind 是具名字串鍵：新增元件型別不必改本層（H1-06）

        LogicalPlacement p;
        p.x = number_or(props, kPropX, 0.0);
        p.y = number_or(props, kPropY, 0.0);
        p.width = number_or(props, kPropWidth, 0.0);
        p.height = number_or(props, kPropHeight, 0.0);
        placed.box = resolve_placement(p, scale);

        placed.draw.available = bool_or(props, kPropAvailable, true);
        placed.draw.vertical = bool_or(props, kPropVertical, false);
        placed.draw.fill_ratio =
            clamp_finite(number_or(props, kPropFillRatio, 0.0), 0.0, 0.0, 1.0);

        const Value* label = props.find(kPropLabel);
        if (label != nullptr && label->is_string()) {
            placed.draw.show_label = true;
            placed.draw.label = label->as_string();
        }

        const Value* start = props.find(kPropStartAngle);
        const Value* sweep = props.find(kPropSweep);
        if (start != nullptr && start->is_number() && sweep != nullptr && sweep->is_number() &&
            std::isfinite(start->as_number()) && std::isfinite(sweep->as_number())) {
            placed.draw.has_angle = true;
            placed.draw.start_angle_degrees = start->as_number();
            placed.draw.sweep_degrees = sweep->as_number();
        }
        placed.draw.angle_degrees =
            clamp_finite(number_or(props, kPropAngle, 0.0), 0.0, -3600.0, 3600.0);
        placed.draw.length_ratio =
            clamp_finite(number_or(props, kPropLengthRatio, 1.0), 1.0, 0.0, 1.0);
        placed.draw.thickness_ratio =
            clamp_finite(number_or(props, kPropThicknessRatio, 0.05), 0.05, 0.0, 1.0);

        // 其餘的有限數值屬性原樣遞進 H1-06 的擴充袋：新 kind 自帶的參數不必改本層。
        for (const Value::Member& m : props.as_map()) {
            if (is_known_key(m.first) || !m.second.is_number()) {
                continue;
            }
            const double v = m.second.as_number();
            if (std::isfinite(v)) {
                placed.draw.params[m.first] = v;
            }
        }
        out.push_back(std::move(placed));
    }
    return out;
}

}  // namespace ds::host::edit
