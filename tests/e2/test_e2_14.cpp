// E2-14 HTTP 取得與結構化解析 — 契約測試（gtest）
//
// 涵蓋：HttpResponse / null 傳輸行為、輕量 JSON 解析（成功與可定位錯誤）、JsonValue 查詢、
// 提供者身分 / 註冊、注入假回應→解析出結構化欄位、非 2xx 狀態處理、解析錯誤處理、
// 路徑不存在 / 非數值、null 傳輸 / 未 register 保守行為、透過 E2-02 頻率採集、掛件式消費者範式。
//
// 純邏輯、平台中立：無 `#ifdef`、無 socket、無網路呼叫、無真實時鐘（E2-02 用邏輯 tick）。
#include "http_fetch.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::sysinfo::HttpResponse;
using ds::sysinfo::HttpTransport;
using ds::sysinfo::NullHttpTransport;
using ds::sysinfo::JsonValue;
using ds::sysinfo::JsonType;
using ds::sysinfo::PathSeg;
using ds::sysinfo::JsonPath;
using ds::sysinfo::parse_json;
using ds::sysinfo::seek;
using ds::sysinfo::HttpFetchProvider;

using ds::metrics::MetricRegistry;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRange;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;

// ---------------------------------------------------------------------------
// HttpResponse
// ---------------------------------------------------------------------------
TEST(HttpResponse, SuccessRangeIs2xx) {
    EXPECT_TRUE(HttpResponse::of(200, "").is_success());
    EXPECT_TRUE(HttpResponse::of(204, "").is_success());
    EXPECT_TRUE(HttpResponse::of(299, "").is_success());
    EXPECT_FALSE(HttpResponse::of(199, "").is_success());
    EXPECT_FALSE(HttpResponse::of(300, "").is_success());
    EXPECT_FALSE(HttpResponse::of(404, "").is_success());
    EXPECT_FALSE(HttpResponse::of(500, "").is_success());
    EXPECT_FALSE(HttpResponse::none().is_success());  // status==0（無後端）
}

TEST(HttpResponse, HeaderLookupIsCaseInsensitive) {
    HttpResponse r = HttpResponse::ok("{}");
    r.headers.push_back({"Content-Type", "application/json"});
    ASSERT_NE(r.header("content-type"), nullptr);
    EXPECT_EQ(*r.header("CONTENT-TYPE"), "application/json");
    EXPECT_EQ(r.header("x-missing"), nullptr);
}

TEST(HttpResponse, OkFactoryDefaults200) {
    HttpResponse r = HttpResponse::ok("body");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "body");
    EXPECT_TRUE(r.is_success());
}

// ---------------------------------------------------------------------------
// NullHttpTransport
// ---------------------------------------------------------------------------
TEST(NullHttpTransport, DefaultIsNoBackend) {
    NullHttpTransport t;
    HttpResponse r = t.get("http://example/x");
    EXPECT_EQ(r.status, 0);      // 無後端
    EXPECT_TRUE(r.body.empty());
    EXPECT_FALSE(r.is_success());
    EXPECT_EQ(t.request_count(), 1u);
    EXPECT_EQ(t.last_url(), "http://example/x");
}

TEST(NullHttpTransport, InjectedResponseReturnedForMatchingUrl) {
    NullHttpTransport t;
    t.set_response("http://api/metric", HttpResponse::ok("{\"v\":1}"));
    HttpResponse r = t.get("http://api/metric");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "{\"v\":1}");
    // 未注入的 url 回預設（無後端）。
    EXPECT_EQ(t.get("http://api/other").status, 0);
}

TEST(NullHttpTransport, SetDefaultAndClear) {
    NullHttpTransport t;
    t.set_default(HttpResponse::of(503, "down"));
    EXPECT_EQ(t.get("http://any").status, 503);
    t.set_response("http://any", HttpResponse::ok("ok"));
    EXPECT_EQ(t.get("http://any").status, 200);
    t.clear();
    EXPECT_EQ(t.get("http://any").status, 0);  // 回無後端
}

TEST(NullHttpTransport, UsableThroughAbstractInterface) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("u", HttpResponse::ok("hi"));
    const HttpTransport& base = *t;
    EXPECT_EQ(base.get("u").body, "hi");
}

// ---------------------------------------------------------------------------
// JSON 解析：成功
// ---------------------------------------------------------------------------
TEST(JsonParse, ScalarsAndTypes) {
    EXPECT_TRUE(parse_json("null").value().is_null());
    EXPECT_TRUE(parse_json("true").value().as_bool());
    EXPECT_FALSE(parse_json("false").value().as_bool());
    EXPECT_EQ(parse_json("\"hi\"").value().as_string(), "hi");
}

TEST(JsonParse, IntegerVsFloat) {
    auto ri = parse_json("42");
    ASSERT_TRUE(ri.ok());
    EXPECT_TRUE(ri.value().is_number());
    EXPECT_TRUE(ri.value().is_integer());
    EXPECT_EQ(ri.value().as_int(), 42);

    auto rf = parse_json("3.14");
    ASSERT_TRUE(rf.ok());
    EXPECT_TRUE(rf.value().is_number());
    EXPECT_FALSE(rf.value().is_integer());
    EXPECT_DOUBLE_EQ(rf.value().as_number(), 3.14);

    auto re = parse_json("-1.5e3");
    ASSERT_TRUE(re.ok());
    EXPECT_FALSE(re.value().is_integer());
    EXPECT_DOUBLE_EQ(re.value().as_number(), -1500.0);

    auto rneg = parse_json("-7");
    ASSERT_TRUE(rneg.ok());
    EXPECT_TRUE(rneg.value().is_integer());
    EXPECT_EQ(rneg.value().as_int(), -7);
}

TEST(JsonParse, ObjectPreservesOrderAndQueries) {
    auto r = parse_json("{ \"a\": 1, \"b\": \"x\", \"c\": true }");
    ASSERT_TRUE(r.ok());
    const JsonValue& v = r.value();
    ASSERT_TRUE(v.is_object());
    EXPECT_EQ(v.size(), 3u);
    EXPECT_TRUE(v.contains("a"));
    EXPECT_FALSE(v.contains("z"));
    EXPECT_EQ(v.at("a").as_int(), 1);
    EXPECT_EQ(v.at("b").as_string(), "x");
    EXPECT_TRUE(v.at("c").as_bool());
    EXPECT_EQ(v.find("z"), nullptr);
    const std::vector<std::string> ks = v.keys();
    ASSERT_EQ(ks.size(), 3u);
    EXPECT_EQ(ks[0], "a");
    EXPECT_EQ(ks[1], "b");
    EXPECT_EQ(ks[2], "c");
}

TEST(JsonParse, NestedArraysAndObjects) {
    auto r = parse_json("{\"items\":[{\"n\":10},{\"n\":20}],\"empty\":[]}");
    ASSERT_TRUE(r.ok());
    const JsonValue& v = r.value();
    ASSERT_TRUE(v.at("items").is_array());
    EXPECT_EQ(v.at("items").size(), 2u);
    EXPECT_EQ(v.at("items").as_array()[1].at("n").as_int(), 20);
    EXPECT_EQ(v.at("empty").size(), 0u);
}

TEST(JsonParse, StringEscapesDecoded) {
    auto r = parse_json("\"a\\tb\\n\\\"q\\\"\\u0041\"");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_string(), "a\tb\n\"q\"A");
}

TEST(JsonParse, UnicodeSurrogatePair) {
    // U+1F600 (😀) = 😀 -> 4-byte UTF-8 F0 9F 98 80
    auto r = parse_json("\"\\uD83D\\uDE00\"");
    ASSERT_TRUE(r.ok());
    const std::string& s = r.value().as_string();
    ASSERT_EQ(s.size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(s[0]), 0xF0u);
    EXPECT_EQ(static_cast<unsigned char>(s[3]), 0x80u);
}

TEST(JsonParse, LeadingAndTrailingWhitespace) {
    auto r = parse_json("  \n\t {\"format_version\": 1}  \n");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().at("format_version").as_int(), 1);
}

// ---------------------------------------------------------------------------
// JSON 解析：可定位錯誤（不靜默）
// ---------------------------------------------------------------------------
TEST(JsonParseError, EmptyInput) {
    auto r = parse_json("   ");
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_FALSE(r.error().message.empty());
}

TEST(JsonParseError, UnterminatedString) {
    auto r = parse_json("\"abc");
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}

TEST(JsonParseError, TrailingContent) {
    auto r = parse_json("{} garbage");
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("trailing"), std::string::npos);
}

TEST(JsonParseError, MissingColonInObject) {
    auto r = parse_json("{\"a\" 1}");
    EXPECT_FALSE(r.ok());
}

TEST(JsonParseError, UnterminatedObjectAndArray) {
    EXPECT_FALSE(parse_json("{\"a\":1").ok());
    EXPECT_FALSE(parse_json("[1,2").ok());
}

TEST(JsonParseError, InvalidLiteralAndNumber) {
    EXPECT_FALSE(parse_json("tru").ok());
    EXPECT_FALSE(parse_json("-").ok());
    EXPECT_FALSE(parse_json("1.").ok());
    EXPECT_FALSE(parse_json("nope").ok());
}

TEST(JsonParseError, BadEscape) {
    auto r = parse_json("\"a\\xb\"");
    EXPECT_FALSE(r.ok());
}

// ---------------------------------------------------------------------------
// JsonValue 型別誤用 → throw（NFR-04：明確失敗）
// ---------------------------------------------------------------------------
TEST(JsonValue, TypeMismatchThrows) {
    JsonValue n = JsonValue::number(1, true);
    EXPECT_THROW(n.as_string(), std::runtime_error);
    EXPECT_THROW(n.as_array(), std::runtime_error);
    JsonValue s = JsonValue::string("x");
    EXPECT_THROW(s.as_number(), std::runtime_error);
    EXPECT_THROW(s.find("k"), std::runtime_error);  // find on non-object
    EXPECT_THROW(s.size(), std::runtime_error);
}

TEST(JsonValue, EqualityIsDeepAndOrdered) {
    auto a = parse_json("{\"x\":1,\"y\":2}").value();
    auto b = parse_json("{\"x\":1,\"y\":2}").value();
    auto c = parse_json("{\"y\":2,\"x\":1}").value();  // 順序不同
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);  // Object 比較保序
}

// ---------------------------------------------------------------------------
// seek（路徑走訪）
// ---------------------------------------------------------------------------
TEST(Seek, NavigatesObjectAndArray) {
    auto doc = parse_json("{\"data\":{\"cpu\":[{\"load\":55}]}}").value();
    JsonPath path{PathSeg::field("data"), PathSeg::field("cpu"),
                  PathSeg::elem(0), PathSeg::field("load")};
    const JsonValue* n = seek(doc, path);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->as_int(), 55);
}

TEST(Seek, EmptyPathReturnsRoot) {
    auto doc = parse_json("42").value();
    const JsonValue* n = seek(doc, JsonPath{});
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->as_int(), 42);
}

TEST(Seek, MissingKeyIndexOrTypeMismatchReturnsNull) {
    auto doc = parse_json("{\"a\":[1,2]}").value();
    EXPECT_EQ(seek(doc, JsonPath{PathSeg::field("z")}), nullptr);            // 鍵不存在
    EXPECT_EQ(seek(doc, JsonPath{PathSeg::field("a"), PathSeg::elem(9)}), nullptr);  // 越界
    EXPECT_EQ(seek(doc, JsonPath{PathSeg::elem(0)}), nullptr);              // 根非陣列
    EXPECT_EQ(seek(doc, JsonPath{PathSeg::field("a"), PathSeg::field("k")}), nullptr);  // 陣列非物件
}

// ---------------------------------------------------------------------------
// HttpFetchProvider：身分
// ---------------------------------------------------------------------------
namespace {
JsonPath value_path() { return JsonPath{PathSeg::field("value")}; }

HttpFetchProvider make_provider(std::shared_ptr<HttpTransport> t) {
    return HttpFetchProvider(std::move(t), "http.value", "HTTP Value",
                             "http://api/metric", value_path(), "%",
                             MetricRange::bounded(0, 100));
}
}  // namespace

TEST(HttpFetchProvider, IdentityAndSuggestedTier) {
    auto t = std::make_shared<NullHttpTransport>();
    HttpFetchProvider p = make_provider(t);
    EXPECT_EQ(p.provider_id(), "sysinfo.http:http.value");
    EXPECT_EQ(HttpFetchProvider::kSuggestedTier, SamplingTier::Low);
    // 可上轉為 E2-01 MetricProvider。
    MetricProvider& mp = p;
    EXPECT_EQ(mp.provider_id(), "sysinfo.http:http.value");
}

TEST(HttpFetchProvider, RegisterMountsSingleInstanceMetricUnknownBeforeSample) {
    auto t = std::make_shared<NullHttpTransport>();
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("http.value");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name(), "HTTP Value");
    EXPECT_EQ(m->unit(), "%");
    EXPECT_TRUE(m->range().is_bounded());
    ASSERT_EQ(m->instance_count(), 1u);
    // 採集前 value 為 unknown（不把 0 誤當真實讀值）。
    EXPECT_FALSE(m->single().value().valid);
}

// ---------------------------------------------------------------------------
// 注入假回應 → 解析出結構化欄位（核心路徑）
// ---------------------------------------------------------------------------
TEST(HttpFetchProvider, SampleParsesInjectedJsonField) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": 73, \"unit\":\"%\"}"));
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.last_status(), 200);
    EXPECT_TRUE(p.last_parse_ok());

    auto m = reg.get("http.value");
    ASSERT_NE(m, nullptr);
    const auto v = m->single().value();
    EXPECT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 73.0);
    // 歷史推入一筆。
    EXPECT_EQ(m->single().history().size(), 1u);
}

TEST(HttpFetchProvider, MultipleSamplesAccumulateHistory) {
    auto t = std::make_shared<NullHttpTransport>();
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);

    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": 10}"));
    EXPECT_TRUE(p.sample());
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": 20}"));
    EXPECT_TRUE(p.sample());
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": 30}"));
    EXPECT_TRUE(p.sample());

    auto m = reg.get("http.value");
    const auto& h = m->single().history();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_DOUBLE_EQ(h.at(0), 10.0);  // 最舊
    EXPECT_DOUBLE_EQ(h.at(2), 30.0);  // 最新
    EXPECT_DOUBLE_EQ(m->single().value().number, 30.0);
}

TEST(HttpFetchProvider, NestedPathExtraction) {
    auto t = std::make_shared<NullHttpTransport>();
    HttpFetchProvider p(t, "http.load", "Load", "http://api/x",
                        JsonPath{PathSeg::field("data"), PathSeg::elem(1), PathSeg::field("load")});
    MetricRegistry reg;
    reg.add_provider(p);
    t->set_response("http://api/x", HttpResponse::ok("{\"data\":[{\"load\":1},{\"load\":88}]}"));
    EXPECT_TRUE(p.sample());
    EXPECT_DOUBLE_EQ(reg.get("http.load")->single().value().number, 88.0);
}

// ---------------------------------------------------------------------------
// 非 2xx 狀態處理
// ---------------------------------------------------------------------------
TEST(HttpFetchProvider, Non2xxLeavesValueUnknown) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::of(404, "{\"value\": 5}"));
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_FALSE(p.sample());
    EXPECT_EQ(p.last_status(), 404);
    EXPECT_FALSE(p.last_parse_ok());  // 非 2xx 不解析
    auto m = reg.get("http.value");
    EXPECT_FALSE(m->single().value().valid);  // 保持 unknown
    EXPECT_EQ(m->single().history().size(), 0u);  // 不污染歷史
}

// ---------------------------------------------------------------------------
// 解析錯誤處理
// ---------------------------------------------------------------------------
TEST(HttpFetchProvider, ParseErrorLeavesValueUnknownAndRecordsError) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::ok("not json <<<"));
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_FALSE(p.sample());
    EXPECT_EQ(p.last_status(), 200);
    EXPECT_FALSE(p.last_parse_ok());
    EXPECT_FALSE(p.last_error().message.empty());  // 不靜默：記錄可讀原因
    EXPECT_FALSE(reg.get("http.value")->single().value().valid);
}

TEST(HttpFetchProvider, PathMissingOrNonNumericLeavesUnknown) {
    auto t = std::make_shared<NullHttpTransport>();
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);

    // 路徑存在但非數值。
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": \"high\"}"));
    EXPECT_FALSE(p.sample());
    EXPECT_TRUE(p.last_parse_ok());  // 解析成功，但欄位非數值
    EXPECT_FALSE(reg.get("http.value")->single().value().valid);

    // 路徑不存在。
    t->set_response("http://api/metric", HttpResponse::ok("{\"other\": 1}"));
    EXPECT_FALSE(p.sample());
    EXPECT_FALSE(reg.get("http.value")->single().value().valid);
}

// ---------------------------------------------------------------------------
// null 傳輸 / 保守行為
// ---------------------------------------------------------------------------
TEST(HttpFetchProvider, NullTransportNoBackendYieldsUnknown) {
    auto t = std::make_shared<NullHttpTransport>();  // 未注入任何回應
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(p.sample());          // 無後端（status 0）
    EXPECT_EQ(p.last_status(), 0);
    EXPECT_FALSE(reg.get("http.value")->single().value().valid);
}

TEST(HttpFetchProvider, NullptrTransportIsNoOpNoCrash) {
    HttpFetchProvider p(nullptr, "http.value", "V", "http://api", value_path());
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(p.sample());  // transport 為 null → 保守 no-op、不崩
    EXPECT_FALSE(reg.get("http.value")->single().value().valid);
}

TEST(HttpFetchProvider, SampleBeforeRegisterIsNoOp) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\":1}"));
    HttpFetchProvider p = make_provider(t);
    // 未 register_metrics → 無實例 → no-op。
    EXPECT_FALSE(p.sample());
    EXPECT_EQ(p.metric(), nullptr);
}

TEST(HttpFetchProvider, DuplicateIdRejectedAndFailedProviderSampleNoOp) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\":1}"));
    MetricRegistry reg;
    HttpFetchProvider p1 = make_provider(t);
    HttpFetchProvider p2 = make_provider(t);  // 同 id "http.value"
    EXPECT_EQ(reg.add_provider(p1), 1u);
    EXPECT_EQ(reg.add_provider(p2), 0u);  // 重複 id 被保守拒絕
    EXPECT_EQ(p2.metric(), nullptr);
    EXPECT_FALSE(p2.sample());  // 失敗提供者 sample 為 no-op
    EXPECT_TRUE(p1.sample());   // 成功提供者仍運作
}

// ---------------------------------------------------------------------------
// 透過 E2-02 頻率採集
// ---------------------------------------------------------------------------
TEST(HttpFetchProvider, RegisterDemandUsesLowTier) {
    auto t = std::make_shared<NullHttpTransport>();
    HttpFetchProvider p = make_provider(t);
    SamplingScheduler scheduler;  // 預設 policy：Low = 每 64 tick
    const auto demand = p.register_demand(scheduler);
    EXPECT_NE(demand, 0u);
    EXPECT_TRUE(scheduler.tracks("http.value"));
    EXPECT_EQ(scheduler.effective_tier("http.value"), SamplingTier::Low);
    const auto interval = scheduler.effective_interval("http.value");
    ASSERT_TRUE(interval.has_value());
    EXPECT_EQ(*interval, 64u);
}

TEST(HttpFetchProvider, SchedulerDueDrivesSampling) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": 42}"));
    HttpFetchProvider p = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(p);

    SamplingScheduler scheduler;
    p.register_demand(scheduler);  // Low：間隔 64；新需求首採排在下一次 advance

    auto due_contains = [](const std::vector<ds::metrics::MetricId>& due) {
        for (const auto& id : due) if (id == "http.value") return true;
        return false;
    };

    // 首採：新需求首採排在「下一次 advance 到達目前 tick」→ tick 1 即到期。
    ASSERT_TRUE(due_contains(scheduler.advance(1)));
    EXPECT_TRUE(p.sample());
    EXPECT_DOUBLE_EQ(reg.get("http.value")->single().value().number, 42.0);

    // 未到下一間隔（+64）→ 不到期。
    EXPECT_FALSE(due_contains(scheduler.advance(40)));
    // 到達下一間隔 → 再次到期，驅動下一次採集。
    EXPECT_TRUE(due_contains(scheduler.advance(65)));
}

// ---------------------------------------------------------------------------
// 掛件式消費者範式：只走 E2-01 registry / Metric 介面，全程無 sysinfo 型別
// ---------------------------------------------------------------------------
TEST(Consumer, WidgetStyleReadsThroughE2_01Only) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://api/metric", HttpResponse::ok("{\"value\": 61}"));
    HttpFetchProvider provider = make_provider(t);
    MetricRegistry reg;
    reg.add_provider(provider);
    provider.sample();

    // 以下僅用 E2-01 抽象介面（無 HttpFetchProvider / JsonValue 型別）。
    std::shared_ptr<ds::metrics::Metric> metric = reg.get("http.value");
    ASSERT_NE(metric, nullptr);
    const ds::metrics::MetricInstance& inst = metric->single();
    EXPECT_TRUE(inst.value().valid);
    EXPECT_DOUBLE_EQ(inst.value().number, 61.0);
    // 正規化到 [0,100] 範圍。
    const auto norm = metric->range().normalized(inst.value().number);
    ASSERT_TRUE(norm.has_value());
    EXPECT_DOUBLE_EQ(*norm, 0.61);
}
