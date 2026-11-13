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
Value sanitize_geometry(const Value& props, int& repaired) {
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
            out = ds::format::set_value(out, Path{PathSegment::of_key(key)},
                                        Value::number(0.0));
            ++repaired;
            continue;
        }
        const double raw = cur->as_number();
        const double safe = is_extent ? sanitize_extent(raw) : sanitize_coord(raw);
        // NaN != 任何值 → NaN 一定會走進這裡被換掉（這正是要的；K-016）。
        if (raw != safe) {
            out = ds::format::set_value(out, Path{PathSegment::of_key(key)},
                                        Value::number(safe));
            ++repaired;
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
            const Value* pv = item.find("properties");
            if (pv != nullptr && !pv->is_null() && !pv->is_map()) {
                ++rep.repaired_fields;  // properties 型別不對 → 當成空 Map（具名修補）
            }
            Value props = (pv == nullptr) ? Value::map({}) : as_props(*pv);
            e.properties = sanitize_geometry(props, rep.repaired_fields);
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
    }
    return "Ok";
}

const char* to_string(Overwrite policy) noexcept {
    switch (policy) {
        case Overwrite::Refuse: return "Refuse";
        case Overwrite::DiscardUnreadable: return "DiscardUnreadable";
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
    // **深層**檢查：第一版只查 `value.is_number()`，於是巢狀的 `{inner: NaN}` / `[Inf]`
    // 整個繞過守衛進了持久化狀態（V1.7 的 F2）。
    if (contains_non_finite(value)) {
        std::string where;
        for (const std::string& bad : non_finite_paths(value)) {
            if (!where.empty()) {
                where += "、";
            }
            where += bad.empty() ? "（值本身）" : bad;
        }
        return note_edit(EditStatus::NonFiniteValue,
                         "候選值含非有限數，拒絕寫入持久化狀態：" + path + " → " + where);
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

bool EditAssembly::target_is_readable(const std::string& name, IntakeStatus* why) const {
    std::string existing;
    if (!storage_.get(name, existing)) {
        if (why != nullptr) {
            *why = IntakeStatus::NotFound;
        }
        return true;  // 沒有東西會被毀掉
    }
    const IntakeReport target = ingest_layout_text(existing, migrations_);
    if (why != nullptr) {
        *why = target.status;
    }
    return target.usable();
}

SaveStatus EditAssembly::dry_run_save(const std::string& name) const {
    std::string text;
    std::string message;
    const SaveStatus st = serialize_verified(text, message);
    if (st != SaveStatus::Ok) {
        return st;
    }
    if (!name.empty() && !target_is_readable(name, nullptr)) {
        return SaveStatus::RefusedUnreadableTarget;
    }
    return SaveStatus::Ok;
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

    // --- F1 防線：不覆寫一份「本層讀不出來」的既有存檔 ---
    //
    // 刻意**在提交的當下重讀目標**，而不是查「這個 session 的上一次 restore 結果」：
    // 後者只擋得住「restore 失敗 → save」這一條路，擋不住「從來沒 restore 過就 save」
    // ——而那一條毀的資料一模一樣。重讀一次的成本是一次解析，佈局檔很小。
    std::string existing;
    if (storage_.get(name, existing)) {
        const IntakeReport target = ingest_layout_text(existing, migrations_);
        if (!target.usable()) {
            if (policy == Overwrite::Refuse) {
                last_save_ = SaveStatus::RefusedUnreadableTarget;
                last_message_ =
                    "目標存檔 '" + name + "' 目前讀不出來（" +
                    std::string(to_string(target.status)) + "）：" + target.message +
                    "；覆寫它會永久毀掉使用者的佈局。先修好那份檔案，或以 "
                    "Overwrite::DiscardUnreadable 明示丟棄（原文會另存為 '" +
                    name + kRejectedSuffix + "'）";
                ++stats_.saves_rejected;
                ++stats_.saves_refused_target;
                return last_save_;  // **沒有碰過 storage**
            }
            // 明示丟棄：先把讀不出來的原文另存一份，再覆寫。
            // 連「明示丟棄」都不真的丟掉位元組 —— 使用者按錯了還有得救。
            storage_.put(name + kRejectedSuffix, existing);
            ++stats_.rejected_backups;
        }
    }

    storage_.put(name, text);  // 唯一的提交點
    last_save_ = SaveStatus::Ok;
    last_message_.clear();
    ++stats_.saves;
    return SaveStatus::Ok;
}

// --- 還原 ---

void EditAssembly::adopt(IntakeReport report) {
    stats_.elements_dropped += report.dropped_elements;
    stats_.fields_repaired += report.repaired_fields;
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
    const IntakeStatus status = rep.status;
    adopt(std::move(rep));
    if (!usable) {
        ++stats_.loads_failed;
    }
    return status;
}

void EditAssembly::reset(const LayoutState& state) { state_ = state; }

// --- 熱重載 ---

void EditAssembly::attach_source(ds::format::ReloadSource& source) {
    source_ = &source;
    reloader_ = std::make_unique<ds::format::HotReloader>(source);
    last_reload_report_.clear();
}

void EditAssembly::detach_source() noexcept {
    source_ = nullptr;
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
    adopt(std::move(rep));
    if (usable) {
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
