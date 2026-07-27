// E2-14 HTTP 取得與結構化解析 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：null 傳輸回注入的固定回應（不發網路）；自帶輕量 JSON 解析器把 body 轉成可查詢
// 的 JsonValue 樹；提供者把某數值欄位掛成 E2-01 指標，並沿用 E2-02 排程節奏。
// 無 `#ifdef`、無 socket、無系統呼叫、無真實後端——換平台一行不動。
#include "http_fetch.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace ds::sysinfo {

// ===========================================================================
// HttpResponse
// ===========================================================================
namespace {
// 大小寫不敏感的 ASCII 相等（HTTP 標頭名比對用）。
bool iequals_ascii(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}
}  // namespace

const std::string* HttpResponse::header(const std::string& name) const {
    for (const auto& h : headers) {
        if (iequals_ascii(h.first, name)) return &h.second;
    }
    return nullptr;
}

HttpResponse HttpResponse::ok(std::string body, int status) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

HttpResponse HttpResponse::of(int status, std::string body) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

// ===========================================================================
// NullHttpTransport
// ===========================================================================
HttpResponse NullHttpTransport::get(const std::string& url) const {
    ++request_count_;
    last_url_ = url;
    auto it = responses_.find(url);
    if (it != responses_.end()) return it->second;
    return default_;  // 未注入 → 「無後端」（或呼叫端設定的預設）
}

// ===========================================================================
// JsonValue
// ===========================================================================
JsonValue JsonValue::null_value() { return JsonValue{}; }

JsonValue JsonValue::boolean(bool b) {
    JsonValue v;
    v.type_ = JsonType::Bool;
    v.bool_ = b;
    return v;
}

JsonValue JsonValue::number(double n, bool integral) {
    JsonValue v;
    v.type_ = JsonType::Number;
    v.num_ = n;
    v.integral_ = integral;
    return v;
}

JsonValue JsonValue::integer(std::int64_t n) {
    return number(static_cast<double>(n), /*integral=*/true);
}

JsonValue JsonValue::string(std::string s) {
    JsonValue v;
    v.type_ = JsonType::String;
    v.str_ = std::move(s);
    return v;
}

JsonValue JsonValue::array(std::vector<JsonValue> items) {
    JsonValue v;
    v.type_ = JsonType::Array;
    v.arr_ = std::move(items);
    return v;
}

JsonValue JsonValue::object(std::vector<Member> members) {
    JsonValue v;
    v.type_ = JsonType::Object;
    v.obj_ = std::move(members);
    return v;
}

bool JsonValue::as_bool() const {
    if (type_ != JsonType::Bool) throw std::runtime_error("JsonValue: not a bool");
    return bool_;
}

double JsonValue::as_number() const {
    if (type_ != JsonType::Number) throw std::runtime_error("JsonValue: not a number");
    return num_;
}

std::int64_t JsonValue::as_int() const {
    if (type_ != JsonType::Number) throw std::runtime_error("JsonValue: not a number");
    return static_cast<std::int64_t>(num_);
}

const std::string& JsonValue::as_string() const {
    if (type_ != JsonType::String) throw std::runtime_error("JsonValue: not a string");
    return str_;
}

const std::vector<JsonValue>& JsonValue::as_array() const {
    if (type_ != JsonType::Array) throw std::runtime_error("JsonValue: not an array");
    return arr_;
}

const std::vector<JsonValue::Member>& JsonValue::as_object() const {
    if (type_ != JsonType::Object) throw std::runtime_error("JsonValue: not an object");
    return obj_;
}

std::size_t JsonValue::size() const {
    if (type_ == JsonType::Array) return arr_.size();
    if (type_ == JsonType::Object) return obj_.size();
    throw std::runtime_error("JsonValue: size() on non-container");
}

bool JsonValue::contains(const std::string& key) const {
    return find(key) != nullptr;
}

const JsonValue* JsonValue::find(const std::string& key) const {
    if (type_ != JsonType::Object) throw std::runtime_error("JsonValue: find() on non-object");
    for (const auto& m : obj_) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

const JsonValue& JsonValue::at(const std::string& key) const {
    const JsonValue* v = find(key);
    if (v == nullptr) throw std::runtime_error("JsonValue: key not found: " + key);
    return *v;
}

std::vector<std::string> JsonValue::keys() const {
    if (type_ != JsonType::Object) throw std::runtime_error("JsonValue: keys() on non-object");
    std::vector<std::string> ks;
    ks.reserve(obj_.size());
    for (const auto& m : obj_) ks.push_back(m.first);
    return ks;
}

bool JsonValue::operator==(const JsonValue& o) const {
    if (type_ != o.type_) return false;
    switch (type_) {
        case JsonType::Null:   return true;
        case JsonType::Bool:   return bool_ == o.bool_;
        case JsonType::Number: return num_ == o.num_ && integral_ == o.integral_;
        case JsonType::String: return str_ == o.str_;
        case JsonType::Array:  return arr_ == o.arr_;
        case JsonType::Object: return obj_ == o.obj_;
    }
    return false;  // 不可達
}

// ===========================================================================
// seek（依路徑走訪）
// ===========================================================================
const JsonValue* seek(const JsonValue& root, const JsonPath& path) {
    const JsonValue* cur = &root;
    for (const auto& seg : path) {
        if (seg.is_index) {
            if (!cur->is_array()) return nullptr;
            const auto& arr = cur->as_array();
            if (seg.index >= arr.size()) return nullptr;
            cur = &arr[seg.index];
        } else {
            if (!cur->is_object()) return nullptr;
            const JsonValue* next = cur->find(seg.key);
            if (next == nullptr) return nullptr;
            cur = next;
        }
    }
    return cur;
}

// ===========================================================================
// JsonParseResult
// ===========================================================================
JsonParseResult JsonParseResult::success(JsonValue v) {
    JsonParseResult r;
    r.ok_ = true;
    r.value_ = std::move(v);
    return r;
}

JsonParseResult JsonParseResult::failure(JsonError e) {
    JsonParseResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// ===========================================================================
// JSON 解析器（遞迴下降，純邏輯）
// ===========================================================================
namespace {

// 以例外於內部傳遞解析錯誤（帶位移），於頂層轉為 JsonParseResult::failure。
struct JsonSyntaxError {
    std::size_t offset;
    std::string message;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    JsonValue parse_document() {
        skip_ws();
        JsonValue v = parse_value();
        skip_ws();
        if (i_ != s_.size()) fail("trailing content after JSON value");
        return v;
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(const std::string& msg) {
        throw JsonSyntaxError{i_, msg};
    }

    bool eof() const { return i_ >= s_.size(); }
    char peek() const { return s_[i_]; }

    void skip_ws() {
        while (!eof()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
            } else {
                break;
            }
        }
    }

    JsonValue parse_value() {
        if (eof()) fail("unexpected end of input, expected a value");
        const char c = peek();
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return JsonValue::string(parse_string());
            case 't': case 'f': return parse_bool();
            case 'n': return parse_null();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
                fail("unexpected character, expected a value");
        }
        return JsonValue::null_value();  // 不可達（fail 擲例外）
    }

    JsonValue parse_object() {
        ++i_;  // 吃 '{'
        std::vector<JsonValue::Member> members;
        skip_ws();
        if (!eof() && peek() == '}') { ++i_; return JsonValue::object(std::move(members)); }
        while (true) {
            skip_ws();
            if (eof() || peek() != '"') fail("expected string key in object");
            std::string key = parse_string();
            skip_ws();
            if (eof() || peek() != ':') fail("expected ':' after object key");
            ++i_;  // 吃 ':'
            skip_ws();
            JsonValue val = parse_value();
            members.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (eof()) fail("unterminated object, expected ',' or '}'");
            const char c = peek();
            if (c == ',') { ++i_; continue; }
            if (c == '}') { ++i_; break; }
            fail("expected ',' or '}' in object");
        }
        return JsonValue::object(std::move(members));
    }

    JsonValue parse_array() {
        ++i_;  // 吃 '['
        std::vector<JsonValue> items;
        skip_ws();
        if (!eof() && peek() == ']') { ++i_; return JsonValue::array(std::move(items)); }
        while (true) {
            skip_ws();
            items.push_back(parse_value());
            skip_ws();
            if (eof()) fail("unterminated array, expected ',' or ']'");
            const char c = peek();
            if (c == ',') { ++i_; continue; }
            if (c == ']') { ++i_; break; }
            fail("expected ',' or ']' in array");
        }
        return JsonValue::array(std::move(items));
    }

    // 讀取一個 JSON 字串（假設目前位於開頭 '"'）。回傳解碼後內容。
    std::string parse_string() {
        ++i_;  // 吃開頭 '"'
        std::string out;
        while (true) {
            if (eof()) fail("unterminated string");
            const char c = s_[i_++];
            if (c == '"') break;
            if (c == '\\') {
                if (eof()) fail("unterminated escape in string");
                const char e = s_[i_++];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':  decode_unicode(out); break;
                    default:   fail("invalid escape sequence in string");
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string must be escaped");
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    // 解碼 \uXXXX（已吃掉 'u'）。以 UTF-8 追加到 out；代理對成對處理。
    void decode_unicode(std::string& out) {
        const std::uint32_t cp = read_hex4();
        std::uint32_t code = cp;
        if (cp >= 0xD800 && cp <= 0xDBFF) {  // 高代理，需接低代理
            if (i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                i_ += 2;  // 吃 "\u"
                const std::uint32_t lo = read_hex4();
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    code = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    fail("invalid low surrogate in \\u escape");
                }
            } else {
                fail("unpaired high surrogate in \\u escape");
            }
        }
        append_utf8(out, code);
    }

    std::uint32_t read_hex4() {
        std::uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            if (eof()) fail("incomplete \\u escape");
            const char c = s_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9')      v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else fail("invalid hex digit in \\u escape");
        }
        return v;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    JsonValue parse_bool() {
        if (s_.compare(i_, 4, "true") == 0) { i_ += 4; return JsonValue::boolean(true); }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return JsonValue::boolean(false); }
        fail("invalid literal, expected 'true' or 'false'");
        return JsonValue::null_value();  // 不可達
    }

    JsonValue parse_null() {
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return JsonValue::null_value(); }
        fail("invalid literal, expected 'null'");
        return JsonValue::null_value();  // 不可達
    }

    JsonValue parse_number() {
        const std::size_t start = i_;
        bool is_integral = true;
        if (!eof() && peek() == '-') ++i_;
        if (eof() || !(peek() >= '0' && peek() <= '9')) fail("invalid number");
        while (!eof() && peek() >= '0' && peek() <= '9') ++i_;
        if (!eof() && peek() == '.') {
            is_integral = false;
            ++i_;
            if (eof() || !(peek() >= '0' && peek() <= '9')) fail("invalid fraction in number");
            while (!eof() && peek() >= '0' && peek() <= '9') ++i_;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            is_integral = false;
            ++i_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++i_;
            if (eof() || !(peek() >= '0' && peek() <= '9')) fail("invalid exponent in number");
            while (!eof() && peek() >= '0' && peek() <= '9') ++i_;
        }
        const std::string tok = s_.substr(start, i_ - start);
        const double d = std::strtod(tok.c_str(), nullptr);
        return JsonValue::number(d, is_integral);
    }
};

}  // namespace

JsonParseResult parse_json(const std::string& text) {
    try {
        JsonParser p(text);
        return JsonParseResult::success(p.parse_document());
    } catch (const JsonSyntaxError& e) {
        return JsonParseResult::failure(JsonError{e.offset, e.message});
    }
}

// ===========================================================================
// HttpFetchProvider
// ===========================================================================
HttpFetchProvider::HttpFetchProvider(std::shared_ptr<HttpTransport> transport,
                                     ds::metrics::MetricId metric_id, std::string metric_name,
                                     std::string url, JsonPath value_path, std::string unit,
                                     ds::metrics::MetricRange range,
                                     std::size_t history_capacity)
    : transport_(std::move(transport)),
      metric_id_(std::move(metric_id)),
      metric_name_(std::move(metric_name)),
      url_(std::move(url)),
      value_path_(std::move(value_path)),
      unit_(std::move(unit)),
      range_(range),
      history_capacity_(history_capacity),
      provider_id_(std::string(kProviderPrefix) + ":" + metric_id_) {}

void HttpFetchProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        metric_id_, metric_name_, unit_, range_);

    // 單一端點為單值來源 → 單一實例（沿用 E2-01 的單值實例慣例 id ""）。
    auto& inst = metric->add_instance(ds::metrics::Metric::kSingleInstanceId,
                                      /*label=*/metric_name_, history_capacity_);
    // 初始 value 為 unknown：尚未採集，不把 0 誤當真實讀值。由 sample() 驅動更新。
    inst.set_value(ds::metrics::MetricValue::unknown());

    if (registry.register_metric(metric)) {
        metric_ = std::move(metric);
        instance_ = &inst;
    } else {
        // 掛上失敗（同 id 已存在）：不保留參照，sample() 隨即成為 no-op（保守不崩）。
        metric_ = nullptr;
        instance_ = nullptr;
    }
}

bool HttpFetchProvider::sample() {
    // 未掛上指標（未 register 或註冊被拒）→ no-op。
    if (instance_ == nullptr) return false;

    // 每輪先重置本次解析診斷。
    last_parse_ok_ = false;
    last_doc_ = JsonValue::null_value();
    last_error_ = JsonError{};

    // transport 為 null → 保守視為「無後端」（不崩、不發請求）。
    const HttpResponse resp =
        transport_ ? transport_->get(url_) : HttpResponse::none();
    last_status_ = resp.status;

    auto mark_unknown = [this]() {
        // 只設 value 為 unknown，不推歷史（不以未知污染時序）。
        instance_->set_value(ds::metrics::MetricValue::unknown());
    };

    // 非 2xx（含「無後端」status==0）→ 無讀值。
    if (!resp.is_success()) {
        mark_unknown();
        return false;
    }

    // 結構化解析（失敗不靜默：記錄可定位錯誤）。
    JsonParseResult pr = parse_json(resp.body);
    if (!pr.ok()) {
        last_error_ = pr.error();
        mark_unknown();
        return false;
    }
    last_parse_ok_ = true;
    last_doc_ = pr.value();

    // 依路徑取數值欄位。
    const JsonValue* node = seek(last_doc_, value_path_);
    if (node == nullptr || !node->is_number()) {
        mark_unknown();
        return false;
    }

    // 有效數值：更新 value（number = 欄位值）並推入歷史環。
    instance_->update(ds::metrics::MetricValue::of(node->as_number()));
    return true;
}

}  // namespace ds::sysinfo
