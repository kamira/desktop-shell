// E2-26 RSS / 標題抓取 — 實作（module 層 / 子系統 sysinfo）
//
// 純文字處理：自帶輕量標記掃描器把 RSS / Atom / HTML body 轉成 FeedDocument；提供者沿用
// E2-14 的可注入 HttpTransport 抓內容、以 E2-01 的多實例指標暴露最新 N 筆標題。
// 無 `#ifdef`、無 socket、無系統呼叫、無真實網路後端——換平台一行不動。
#include "feed_fetch.hpp"

#include <cctype>
#include <cstdint>
#include <cstddef>

namespace ds::sysinfo {

// ===========================================================================
// FeedParseResult
// ===========================================================================
FeedParseResult FeedParseResult::success(FeedDocument doc) {
    FeedParseResult r;
    r.ok_ = true;
    r.doc_ = std::move(doc);
    return r;
}

FeedParseResult FeedParseResult::failure(FeedError err) {
    FeedParseResult r;
    r.ok_ = false;
    r.error_ = std::move(err);
    return r;
}

// ===========================================================================
// 標記掃描 + 純文字解析（遞迴下降式 tokenizer，純邏輯）
// ===========================================================================
namespace {

// --- 小工具：ASCII 小寫化 / 去頭尾空白 / 大小寫不敏感相等 -------------------
char lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(lower_ascii(c));
    return out;
}

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// --- 實體解碼（XML / HTML 常見具名 + 數值實體）------------------------------
void append_utf8(std::string& out, std::uint32_t cp) {
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

// 解碼一段文字中的實體。未知 / 不完整實體原樣保留（寬鬆——內容文字非嚴格 XML 驗證處）。
std::string decode_entities(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        if (in[i] != '&') {
            out.push_back(in[i++]);
            continue;
        }
        // 找出 ';' 結尾的實體（限制在合理長度內）。
        std::size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(in[i++]);
            continue;
        }
        const std::string ent = in.substr(i + 1, semi - i - 1);
        bool handled = true;
        if (ent == "amp") {
            out.push_back('&');
        } else if (ent == "lt") {
            out.push_back('<');
        } else if (ent == "gt") {
            out.push_back('>');
        } else if (ent == "quot") {
            out.push_back('"');
        } else if (ent == "apos") {
            out.push_back('\'');
        } else if (!ent.empty() && ent[0] == '#') {
            // 數值實體：&#nn; 或 &#xNN;
            std::uint32_t cp = 0;
            bool ok = false;
            if (ent.size() >= 2 && (ent[1] == 'x' || ent[1] == 'X')) {
                ok = ent.size() > 2;
                for (std::size_t k = 2; k < ent.size() && ok; ++k) {
                    const char c = ent[k];
                    cp <<= 4;
                    if (c >= '0' && c <= '9') cp |= static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') cp |= static_cast<std::uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') cp |= static_cast<std::uint32_t>(c - 'A' + 10);
                    else ok = false;
                }
            } else {
                ok = ent.size() > 1;
                for (std::size_t k = 1; k < ent.size() && ok; ++k) {
                    const char c = ent[k];
                    if (c >= '0' && c <= '9') cp = cp * 10 + static_cast<std::uint32_t>(c - '0');
                    else ok = false;
                }
            }
            if (ok && cp != 0 && cp <= 0x10FFFF) append_utf8(out, cp);
            else handled = false;
        } else {
            handled = false;
        }
        if (handled) {
            i = semi + 1;
        } else {
            out.push_back(in[i++]);  // 原樣保留 '&'
        }
    }
    return out;
}

// --- Token 流 ---------------------------------------------------------------
enum class TokKind { Start, End, Text, CData };

struct Token {
    TokKind kind;
    std::string name;                                        // Start / End：標籤名（原樣，含前綴）
    std::vector<std::pair<std::string, std::string>> attrs;  // Start：屬性（名小寫、值已解碼實體）
    bool self_closing = false;                               // Start：<x/>
    std::string text;                                        // Text / CData：內容（Text 已解碼實體）
};

// 掃描器結果：成功 → tokens；失敗 → 帶訊息（malformed）。
struct ScanResult {
    bool ok = false;
    std::vector<Token> tokens;
    std::string message;
};

class Scanner {
public:
    explicit Scanner(const std::string& s) : s_(s) {}

    ScanResult run() {
        ScanResult r;
        while (i_ < s_.size()) {
            if (s_[i_] == '<') {
                if (!scan_markup(r)) return r;  // r.message 已填
            } else {
                scan_text(r);
            }
        }
        r.ok = true;
        return r;
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;

    ScanResult fail(const std::string& msg) {
        ScanResult r;
        r.ok = false;
        r.message = msg;
        return r;
    }

    void scan_text(ScanResult& r) {
        const std::size_t start = i_;
        while (i_ < s_.size() && s_[i_] != '<') ++i_;
        std::string raw = s_.substr(start, i_ - start);
        Token t;
        t.kind = TokKind::Text;
        t.text = decode_entities(raw);
        r.tokens.push_back(std::move(t));
    }

    // 位於 '<'。回傳 false 表 malformed（r 被設為 failure）。
    bool scan_markup(ScanResult& r) {
        // 註解 <!-- ... -->
        if (s_.compare(i_, 4, "<!--") == 0) {
            const std::size_t end = s_.find("-->", i_ + 4);
            if (end == std::string::npos) { r = fail("unterminated comment"); return false; }
            i_ = end + 3;
            return true;
        }
        // CDATA <![CDATA[ ... ]]>
        if (s_.compare(i_, 9, "<![CDATA[") == 0) {
            const std::size_t end = s_.find("]]>", i_ + 9);
            if (end == std::string::npos) { r = fail("unterminated CDATA section"); return false; }
            Token t;
            t.kind = TokKind::CData;
            t.text = s_.substr(i_ + 9, end - (i_ + 9));  // CDATA 內容不解碼實體
            r.tokens.push_back(std::move(t));
            i_ = end + 3;
            return true;
        }
        // 宣告 <!DOCTYPE ...> / 其他 <! ...>
        if (i_ + 1 < s_.size() && s_[i_ + 1] == '!') {
            const std::size_t end = s_.find('>', i_ + 2);
            if (end == std::string::npos) { r = fail("unterminated declaration"); return false; }
            i_ = end + 1;
            return true;
        }
        // 處理指令 / XML 宣告 <? ... ?>
        if (i_ + 1 < s_.size() && s_[i_ + 1] == '?') {
            const std::size_t end = s_.find('>', i_ + 2);
            if (end == std::string::npos) { r = fail("unterminated processing instruction"); return false; }
            i_ = end + 1;
            return true;
        }
        // 一般標籤 <name ...> / </name> / <name/>
        const std::size_t gt = s_.find('>', i_ + 1);
        if (gt == std::string::npos) { r = fail("unterminated tag"); return false; }
        std::string inner = s_.substr(i_ + 1, gt - (i_ + 1));  // '<' 與 '>' 之間
        i_ = gt + 1;
        parse_tag(inner, r);
        return true;
    }

    // 解析標籤內文（'<' 與 '>' 之間），產出 Start / End token。
    void parse_tag(const std::string& inner, ScanResult& r) {
        std::size_t p = 0;
        const std::size_t n = inner.size();
        bool is_end = false;
        if (p < n && inner[p] == '/') { is_end = true; ++p; }

        // 標籤名（到空白 / '/' 為止）。
        const std::size_t name_start = p;
        while (p < n && !is_ws(inner[p]) && inner[p] != '/') ++p;
        std::string name = inner.substr(name_start, p - name_start);
        if (name.empty()) {
            // 退化標籤（如 "< >"）——當作文字忽略，不視為致命。
            Token t;
            t.kind = TokKind::Text;
            t.text = "";
            r.tokens.push_back(std::move(t));
            return;
        }

        Token t;
        if (is_end) {
            t.kind = TokKind::End;
            t.name = name;
            r.tokens.push_back(std::move(t));
            return;
        }

        t.kind = TokKind::Start;
        t.name = name;
        // 自閉：結尾為 '/'
        std::string body = inner.substr(p);
        std::string trimmed = trim(body);
        if (!trimmed.empty() && trimmed.back() == '/') {
            t.self_closing = true;
        }
        parse_attrs(body, t.attrs);
        r.tokens.push_back(std::move(t));
    }

    // 解析屬性序列： name="value" / name='value' / name=value / name（布林）。
    static void parse_attrs(const std::string& body,
                            std::vector<std::pair<std::string, std::string>>& attrs) {
        std::size_t p = 0;
        const std::size_t n = body.size();
        while (p < n) {
            while (p < n && (is_ws(body[p]) || body[p] == '/')) ++p;
            if (p >= n) break;
            const std::size_t ns = p;
            while (p < n && !is_ws(body[p]) && body[p] != '=' && body[p] != '/') ++p;
            std::string aname = to_lower(body.substr(ns, p - ns));
            if (aname.empty()) { ++p; continue; }
            while (p < n && is_ws(body[p])) ++p;
            std::string aval;
            if (p < n && body[p] == '=') {
                ++p;
                while (p < n && is_ws(body[p])) ++p;
                if (p < n && (body[p] == '"' || body[p] == '\'')) {
                    const char q = body[p++];
                    const std::size_t vs = p;
                    while (p < n && body[p] != q) ++p;
                    aval = body.substr(vs, p - vs);
                    if (p < n) ++p;  // 吃掉結尾引號
                } else {
                    const std::size_t vs = p;
                    while (p < n && !is_ws(body[p]) && body[p] != '/') ++p;
                    aval = body.substr(vs, p - vs);
                }
            }
            attrs.emplace_back(std::move(aname), decode_entities(aval));
        }
    }
};

const std::string* find_attr(const std::vector<std::pair<std::string, std::string>>& attrs,
                             const std::string& name) {
    for (const auto& kv : attrs) {
        if (kv.first == name) return &kv.second;
    }
    return nullptr;
}

// 標籤名去命名空間前綴（"atom:title" → "title"）並小寫。
std::string local_name(const std::string& tag) {
    const std::size_t colon = tag.find(':');
    const std::string bare = (colon == std::string::npos) ? tag : tag.substr(colon + 1);
    return to_lower(bare);
}

bool is_time_tag(const std::string& ln) {
    return ln == "pubdate" || ln == "published" || ln == "updated" || ln == "date" || ln == "issued";
}

}  // namespace

// ---------------------------------------------------------------------------
// parse_feed：token 流 → FeedDocument
// ---------------------------------------------------------------------------
FeedParseResult parse_feed(const std::string& markup) {
    Scanner scanner(markup);
    ScanResult scan = scanner.run();
    if (!scan.ok) {
        return FeedParseResult::failure(FeedError{scan.message});
    }

    FeedDocument doc;
    bool in_item = false;
    FeedItem cur;

    // 捕捉狀態：開啟某葉節點（title / link / time）時把文字導入 target，直到其 End 標籤。
    std::string* cap = nullptr;
    std::string cap_name;  // 用以匹配關閉的標籤 local name

    for (const Token& t : scan.tokens) {
        switch (t.kind) {
            case TokKind::Text:
            case TokKind::CData:
                if (cap != nullptr) *cap += t.text;
                break;

            case TokKind::Start: {
                const std::string ln = local_name(t.name);
                if (cap != nullptr) {
                    // 捕捉中遇到巢狀標籤：忽略其結構（其文字仍會被累加）。
                    break;
                }
                if (ln == "rss" || ln == "rdf") {
                    if (doc.type == FeedType::Unknown) doc.type = FeedType::Rss;
                } else if (ln == "feed") {
                    if (doc.type == FeedType::Unknown) doc.type = FeedType::Atom;
                } else if (ln == "html") {
                    if (doc.type == FeedType::Unknown) doc.type = FeedType::Html;
                }

                if (ln == "item" || ln == "entry") {
                    if (doc.type == FeedType::Unknown) {
                        doc.type = (ln == "item") ? FeedType::Rss : FeedType::Atom;
                    }
                    in_item = true;
                    cur = FeedItem{};
                    if (t.self_closing) {
                        doc.items.push_back(cur);
                        in_item = false;
                    }
                } else if (in_item) {
                    if (ln == "title" && cur.title.empty() && !t.self_closing) {
                        cap = &cur.title;
                        cap_name = ln;
                    } else if (ln == "link") {
                        // Atom：href 屬性；RSS：文字內容。
                        const std::string* href = find_attr(t.attrs, "href");
                        if (href != nullptr && !href->empty()) {
                            if (cur.link.empty()) cur.link = *href;
                        } else if (cur.link.empty() && !t.self_closing) {
                            cap = &cur.link;
                            cap_name = ln;
                        }
                    } else if (is_time_tag(ln) && cur.time.empty() && !t.self_closing) {
                        cap = &cur.time;
                        cap_name = ln;
                    }
                } else {
                    // feed / channel 層級標題（第一個非項目內的 <title>）。
                    if (ln == "title" && doc.title.empty() && !t.self_closing) {
                        cap = &doc.title;
                        cap_name = ln;
                    }
                }
                break;
            }

            case TokKind::End: {
                const std::string ln = local_name(t.name);
                if (cap != nullptr) {
                    if (ln == cap_name) {
                        cap = nullptr;
                        cap_name.clear();
                    }
                    break;
                }
                if ((ln == "item" || ln == "entry") && in_item) {
                    doc.items.push_back(cur);
                    in_item = false;
                }
                break;
            }
        }
    }

    // 收尾清整：去頭尾空白。
    doc.title = trim(doc.title);
    for (auto& it : doc.items) {
        it.title = trim(it.title);
        it.link = trim(it.link);
        it.time = trim(it.time);
    }

    // 完全無法辨識（既非 feed、亦無標題、亦無項目）→ 失敗（不靜默）。
    if (doc.type == FeedType::Unknown && doc.title.empty() && doc.items.empty()) {
        return FeedParseResult::failure(
            FeedError{"unrecognized content: no feed root, title, or items found"});
    }

    return FeedParseResult::success(std::move(doc));
}

// ===========================================================================
// FeedFetchProvider
// ===========================================================================
namespace {
// 單一 slot 實例的歷史容量：標題 slot 的數值維度為序位，歷史意義有限——保留少量即可。
constexpr std::size_t kSlotHistoryCapacity = 4;

// slot 實例識別碼："item0"、"item1"…（於指標內穩定）。
std::string slot_id(std::size_t i) { return "item" + std::to_string(i); }
}  // namespace

FeedFetchProvider::FeedFetchProvider(std::shared_ptr<HttpTransport> transport,
                                     ds::metrics::MetricId metric_id, std::string metric_name,
                                     std::string url, std::size_t max_items)
    : transport_(std::move(transport)),
      metric_id_(std::move(metric_id)),
      metric_name_(std::move(metric_name)),
      url_(std::move(url)),
      max_items_(max_items == 0 ? 1 : max_items),  // 至少 1 個 slot（保守）
      provider_id_(std::string(kProviderPrefix) + ":" + metric_id_) {}

void FeedFetchProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 記憶體內實作，不自造指標模型。標題無數值單位 / 值域 → 無單位、無界。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        metric_id_, metric_name_, /*unit=*/"", ds::metrics::MetricRange::unbounded());

    // 預配置 N 個 slot 實例（slot 0 = 最新）。初值 unknown：尚未採集。
    std::vector<ds::metrics::InMemoryMetricInstance*> slots;
    slots.reserve(max_items_);
    for (std::size_t i = 0; i < max_items_; ++i) {
        auto& inst = metric->add_instance(slot_id(i), /*label=*/slot_id(i), kSlotHistoryCapacity);
        inst.set_value(ds::metrics::MetricValue::unknown());
        slots.push_back(&inst);
    }

    if (registry.register_metric(metric)) {
        metric_ = std::move(metric);
        slots_ = std::move(slots);
    } else {
        // 掛上失敗（同 id 已存在）：不保留參照，sample() 隨即成為 no-op（保守不崩）。
        metric_ = nullptr;
        slots_.clear();
    }
}

void FeedFetchProvider::clear_slots() {
    for (auto* inst : slots_) {
        inst->set_value(ds::metrics::MetricValue::unknown());  // 不動歷史
    }
}

bool FeedFetchProvider::sample() {
    // 未掛上指標（未 register 或註冊被拒）→ no-op。
    if (slots_.empty()) return false;

    // 每輪先重置本次診斷與項目。
    last_parse_ok_ = false;
    last_doc_ = FeedDocument{};
    last_error_ = FeedError{};
    items_.clear();

    // transport 為 null → 保守視為「無後端」（不崩、不發請求）。
    const HttpResponse resp =
        transport_ ? transport_->get(url_) : HttpResponse::none();
    last_status_ = resp.status;

    // 非 2xx（含「無後端」status==0）→ 無讀值。
    if (!resp.is_success()) {
        clear_slots();
        return false;
    }

    // 結構化解析（失敗不靜默：記錄可讀錯誤）。
    FeedParseResult pr = parse_feed(resp.body);
    if (!pr.ok()) {
        last_error_ = pr.error();
        clear_slots();
        return false;
    }
    last_parse_ok_ = true;
    last_doc_ = pr.document();

    // 建立「有效項目」清單：
    //   - 有 feed 項目 → 用之。
    //   - 無項目但為網頁 / 純標題（非 RSS/Atom）→ 以文件 <title> 合成單一項目。
    //   - 無項目且為 RSS/Atom → **空 feed**（0 有效項目），不把 channel/feed 標題誤當項目。
    std::vector<FeedItem> effective;
    if (!last_doc_.items.empty()) {
        effective = last_doc_.items;
    } else if (last_doc_.type != FeedType::Rss && last_doc_.type != FeedType::Atom &&
               !last_doc_.title.empty()) {
        effective.push_back(FeedItem{last_doc_.title, url_, ""});
    }
    // 空 feed（0 有效項目）：解析成功，所有 slot unknown、item_count()==0——與 malformed 區分。

    // 取最新 N 筆填入 slot（假設來源已依新到舊排序：slot 0 = 第 0 筆）。
    const std::size_t k = effective.size() < max_items_ ? effective.size() : max_items_;
    for (std::size_t i = 0; i < k; ++i) {
        items_.push_back(effective[i]);
        // value：number = slot 序位（0 = 最新）、text = 標題、valid = 本 slot 有項目。
        slots_[i]->set_value(
            ds::metrics::MetricValue::of(static_cast<double>(i), effective[i].title));
    }
    // 多出的 slot 標為 unknown。
    for (std::size_t i = k; i < slots_.size(); ++i) {
        slots_[i]->set_value(ds::metrics::MetricValue::unknown());
    }

    return true;  // 抓取 + 解析成功（含空 feed）。
}

}  // namespace ds::sysinfo
