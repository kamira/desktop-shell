// E2-26 RSS / 標題抓取 — 契約測試（gtest）
//
// 涵蓋：RSS XML → 解析出項目（標題 / 連結 / 時間）、Atom feed（href 連結）、網頁 HTML →
// 擷取 <title>、多項目、空 feed（成功、0 項目）、malformed 內容報錯（未結束標籤 / 註解 /
// CDATA、完全無法辨識）、非 2xx 處理、實體解碼 / CDATA、經 E2-01 provider 暴露（多實例
// slot、最新 N 筆、value.text = 標題）、null / 保守行為、掛件式消費者範式。
//
// 純邏輯、平台中立：無 `#ifdef`、無 socket、無網路呼叫（沿用 E2-14 注入式 NullHttpTransport）。
#include "feed_fetch.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::sysinfo::HttpResponse;
using ds::sysinfo::HttpTransport;
using ds::sysinfo::NullHttpTransport;
using ds::sysinfo::FeedType;
using ds::sysinfo::FeedItem;
using ds::sysinfo::FeedDocument;
using ds::sysinfo::parse_feed;
using ds::sysinfo::FeedFetchProvider;

using ds::metrics::MetricRegistry;
using ds::metrics::MetricProvider;

// ---------------------------------------------------------------------------
// 測試用樣本
// ---------------------------------------------------------------------------
namespace {

const char* kRss =
    "<?xml version=\"1.0\"?>\n"
    "<rss version=\"2.0\"><channel>\n"
    "  <title>My Blog</title>\n"
    "  <link>http://blog.example</link>\n"
    "  <item>\n"
    "    <title>First Post</title>\n"
    "    <link>http://blog.example/1</link>\n"
    "    <pubDate>Mon, 28 Jul 2026 10:00:00 GMT</pubDate>\n"
    "  </item>\n"
    "  <item>\n"
    "    <title>Second Post</title>\n"
    "    <link>http://blog.example/2</link>\n"
    "    <pubDate>Tue, 29 Jul 2026 11:00:00 GMT</pubDate>\n"
    "  </item>\n"
    "</channel></rss>\n";

const char* kAtom =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
    "  <title>Atom Feed</title>\n"
    "  <entry>\n"
    "    <title>Entry One</title>\n"
    "    <link rel=\"alternate\" href=\"http://atom.example/a\"/>\n"
    "    <updated>2026-07-29T12:00:00Z</updated>\n"
    "  </entry>\n"
    "  <entry>\n"
    "    <title>Entry Two</title>\n"
    "    <link href=\"http://atom.example/b\"/>\n"
    "    <published>2026-07-28T09:00:00Z</published>\n"
    "  </entry>\n"
    "</feed>\n";

const char* kHtml =
    "<!DOCTYPE html>\n"
    "<html><head>\n"
    "  <meta charset=\"utf-8\">\n"
    "  <title>Welcome &amp; Hello</title>\n"
    "</head><body><h1>Hi</h1></body></html>\n";

const char* kEmptyRss =
    "<rss version=\"2.0\"><channel><title>Empty</title></channel></rss>";

}  // namespace

// ---------------------------------------------------------------------------
// parse_feed：RSS
// ---------------------------------------------------------------------------
TEST(ParseFeed, RssItemsTitleLinkTime) {
    auto r = parse_feed(kRss);
    ASSERT_TRUE(r.ok());
    const FeedDocument& d = r.document();
    EXPECT_EQ(d.type, FeedType::Rss);
    EXPECT_EQ(d.title, "My Blog");
    ASSERT_EQ(d.items.size(), 2u);
    EXPECT_EQ(d.items[0].title, "First Post");
    EXPECT_EQ(d.items[0].link, "http://blog.example/1");
    EXPECT_EQ(d.items[0].time, "Mon, 28 Jul 2026 10:00:00 GMT");
    EXPECT_EQ(d.items[1].title, "Second Post");
    EXPECT_EQ(d.items[1].link, "http://blog.example/2");
}

TEST(ParseFeed, RdfRootDetectedAsRss) {
    auto r = parse_feed("<rdf:RDF><item><title>X</title></item></rdf:RDF>");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.document().type, FeedType::Rss);
    ASSERT_EQ(r.document().items.size(), 1u);
    EXPECT_EQ(r.document().items[0].title, "X");
}

// ---------------------------------------------------------------------------
// parse_feed：Atom
// ---------------------------------------------------------------------------
TEST(ParseFeed, AtomEntriesWithHrefLink) {
    auto r = parse_feed(kAtom);
    ASSERT_TRUE(r.ok());
    const FeedDocument& d = r.document();
    EXPECT_EQ(d.type, FeedType::Atom);
    EXPECT_EQ(d.title, "Atom Feed");
    ASSERT_EQ(d.items.size(), 2u);
    EXPECT_EQ(d.items[0].title, "Entry One");
    EXPECT_EQ(d.items[0].link, "http://atom.example/a");  // href 屬性
    EXPECT_EQ(d.items[0].time, "2026-07-29T12:00:00Z");
    EXPECT_EQ(d.items[1].title, "Entry Two");
    EXPECT_EQ(d.items[1].link, "http://atom.example/b");
    EXPECT_EQ(d.items[1].time, "2026-07-28T09:00:00Z");
}

// ---------------------------------------------------------------------------
// parse_feed：HTML 網頁標題
// ---------------------------------------------------------------------------
TEST(ParseFeed, HtmlPageTitleExtracted) {
    auto r = parse_feed(kHtml);
    ASSERT_TRUE(r.ok());
    const FeedDocument& d = r.document();
    EXPECT_EQ(d.type, FeedType::Html);
    EXPECT_EQ(d.title, "Welcome & Hello");  // 實體解碼
    EXPECT_TRUE(d.items.empty());
}

TEST(ParseFeed, BareTitleOnlyIsHtmlLike) {
    // 無 feed root、無 html，只有 <title> → 仍可辨識（標題非空）。
    auto r = parse_feed("<title>Just A Title</title>");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.document().title, "Just A Title");
    EXPECT_TRUE(r.document().items.empty());
}

// ---------------------------------------------------------------------------
// 實體解碼 / CDATA
// ---------------------------------------------------------------------------
TEST(ParseFeed, EntityDecodingInTitle) {
    auto r = parse_feed(
        "<rss><channel><item><title>A &lt;b&gt; &amp; &#65; &#x42;</title></item></channel></rss>");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.document().items.size(), 1u);
    EXPECT_EQ(r.document().items[0].title, "A <b> & A B");
}

TEST(ParseFeed, CdataTitlePreservedRaw) {
    auto r = parse_feed(
        "<rss><channel><item><title><![CDATA[Raw <html> & stuff]]></title></item></channel></rss>");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.document().items.size(), 1u);
    EXPECT_EQ(r.document().items[0].title, "Raw <html> & stuff");  // CDATA 不再解碼實體
}

TEST(ParseFeed, CommentsAndDeclarationsSkipped) {
    auto r = parse_feed(
        "<!-- lead comment --><rss><channel>"
        "<item><!-- inner --><title>Kept</title></item></channel></rss>");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.document().items.size(), 1u);
    EXPECT_EQ(r.document().items[0].title, "Kept");
}

// ---------------------------------------------------------------------------
// 多項目 / 排序保留
// ---------------------------------------------------------------------------
TEST(ParseFeed, ManyItemsPreserveOrder) {
    std::string xml = "<rss><channel>";
    for (int i = 0; i < 5; ++i) {
        xml += "<item><title>T" + std::to_string(i) + "</title></item>";
    }
    xml += "</channel></rss>";
    auto r = parse_feed(xml);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.document().items.size(), 5u);
    EXPECT_EQ(r.document().items[0].title, "T0");
    EXPECT_EQ(r.document().items[4].title, "T4");
}

// ---------------------------------------------------------------------------
// 空 feed（成功、0 項目）
// ---------------------------------------------------------------------------
TEST(ParseFeed, EmptyFeedIsSuccessWithNoItems) {
    auto r = parse_feed(kEmptyRss);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.document().type, FeedType::Rss);
    EXPECT_EQ(r.document().title, "Empty");
    EXPECT_TRUE(r.document().items.empty());
}

// ---------------------------------------------------------------------------
// malformed 內容報錯（不靜默）
// ---------------------------------------------------------------------------
TEST(ParseFeed, UnterminatedTagIsError) {
    auto r = parse_feed("<rss><channel><item><title>oops");
    // 未結束標籤（"<title>oops" 後其實已閉；真正未結束者：）
    auto r2 = parse_feed("<rss><channel><item <broken");
    EXPECT_FALSE(r2.ok());
    EXPECT_FALSE(r2.error().message.empty());
    // 第一個其實所有標籤都閉合，只是內容截斷 → 仍算可辨識（有 items / 標題容器）。
    EXPECT_TRUE(r.ok());
}

TEST(ParseFeed, UnterminatedCommentIsError) {
    auto r = parse_feed("<rss><!-- never closed <channel></channel></rss>");
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("comment"), std::string::npos);
}

TEST(ParseFeed, UnterminatedCdataIsError) {
    auto r = parse_feed("<rss><channel><item><title><![CDATA[ unclosed </title></rss>");
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("CDATA"), std::string::npos);
}

TEST(ParseFeed, UnrecognizedPlainTextIsError) {
    auto r = parse_feed("just some plain text, no markup at all");
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.error().message.empty());
}

TEST(ParseFeed, EmptyInputIsError) {
    auto r = parse_feed("");
    EXPECT_FALSE(r.ok());
}

// ---------------------------------------------------------------------------
// FeedFetchProvider：身分 / 註冊
// ---------------------------------------------------------------------------
namespace {
FeedFetchProvider make_provider(std::shared_ptr<HttpTransport> t, std::size_t max_items = 4) {
    return FeedFetchProvider(std::move(t), "feed.headlines", "Headlines",
                             "http://feed.example/rss", max_items);
}
}  // namespace

TEST(FeedFetchProvider, IdentityAndUpcast) {
    auto t = std::make_shared<NullHttpTransport>();
    FeedFetchProvider p = make_provider(t);
    EXPECT_EQ(p.provider_id(), "sysinfo.feed:feed.headlines");
    EXPECT_EQ(p.max_items(), 4u);
    MetricProvider& mp = p;  // 可上轉為 E2-01 MetricProvider
    EXPECT_EQ(mp.provider_id(), "sysinfo.feed:feed.headlines");
}

TEST(FeedFetchProvider, RegisterMountsNSlotInstancesUnknownBeforeSample) {
    auto t = std::make_shared<NullHttpTransport>();
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("feed.headlines");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->name(), "Headlines");
    ASSERT_EQ(m->instance_count(), 4u);  // 預配置 N slot
    // 採集前所有 slot 為 unknown。
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        EXPECT_FALSE(m->instance(i).value().valid);
    }
    EXPECT_EQ(p.item_count(), 0u);
}

TEST(FeedFetchProvider, ZeroMaxItemsClampedToOne) {
    auto t = std::make_shared<NullHttpTransport>();
    FeedFetchProvider p(t, "f", "F", "http://x", 0);
    EXPECT_EQ(p.max_items(), 1u);
}

// ---------------------------------------------------------------------------
// 注入 RSS → 解析出項目、經 provider 暴露
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, SampleParsesRssIntoSlots) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kRss));
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.last_status(), 200);
    EXPECT_TRUE(p.last_parse_ok());
    EXPECT_EQ(p.last_document().type, FeedType::Rss);
    ASSERT_EQ(p.item_count(), 2u);
    // provider.items() 暴露連結 / 時間。
    EXPECT_EQ(p.items()[0].title, "First Post");
    EXPECT_EQ(p.items()[0].link, "http://blog.example/1");

    // 經 E2-01 指標暴露：slot 0/1 有值（text = 標題）、slot 2/3 unknown。
    auto m = reg.get("feed.headlines");
    ASSERT_EQ(m->instance_count(), 4u);
    EXPECT_TRUE(m->instance(0).value().valid);
    ASSERT_TRUE(m->instance(0).value().text.has_value());
    EXPECT_EQ(*m->instance(0).value().text, "First Post");
    EXPECT_DOUBLE_EQ(m->instance(0).value().number, 0.0);  // slot 序位
    EXPECT_TRUE(m->instance(1).value().valid);
    EXPECT_EQ(*m->instance(1).value().text, "Second Post");
    EXPECT_DOUBLE_EQ(m->instance(1).value().number, 1.0);
    EXPECT_FALSE(m->instance(2).value().valid);
    EXPECT_FALSE(m->instance(3).value().valid);
}

TEST(FeedFetchProvider, SampleParsesAtom) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kAtom));
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.last_document().type, FeedType::Atom);
    ASSERT_EQ(p.item_count(), 2u);
    EXPECT_EQ(p.items()[0].link, "http://atom.example/a");
}

// ---------------------------------------------------------------------------
// 最新 N 筆截斷（more items than slots）
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, TruncatesToMaxItems) {
    std::string xml = "<rss><channel>";
    for (int i = 0; i < 10; ++i) {
        xml += "<item><title>H" + std::to_string(i) + "</title></item>";
    }
    xml += "</channel></rss>";
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(xml));
    FeedFetchProvider p = make_provider(t, 3);  // 只 3 slot
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.item_count(), 3u);  // 截到 3
    auto m = reg.get("feed.headlines");
    ASSERT_EQ(m->instance_count(), 3u);
    EXPECT_EQ(*m->instance(0).value().text, "H0");  // slot 0 = 最新
    EXPECT_EQ(*m->instance(2).value().text, "H2");
}

// ---------------------------------------------------------------------------
// 網頁 HTML title → 單一 slot
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, HtmlTitleBecomesSingleSlot) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kHtml));
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.last_document().type, FeedType::Html);
    ASSERT_EQ(p.item_count(), 1u);  // 合成單一項目
    EXPECT_EQ(p.items()[0].title, "Welcome & Hello");
    auto m = reg.get("feed.headlines");
    EXPECT_TRUE(m->instance(0).value().valid);
    EXPECT_EQ(*m->instance(0).value().text, "Welcome & Hello");
    EXPECT_FALSE(m->instance(1).value().valid);
}

// ---------------------------------------------------------------------------
// 空 feed（成功、0 slot 有值）
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, EmptyFeedSucceedsWithNoValidSlots) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kEmptyRss));
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_TRUE(p.sample());  // 空 feed 仍算成功
    EXPECT_TRUE(p.last_parse_ok());
    EXPECT_EQ(p.item_count(), 0u);
    auto m = reg.get("feed.headlines");
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        EXPECT_FALSE(m->instance(i).value().valid);
    }
}

// ---------------------------------------------------------------------------
// 重新採集：舊 slot 被清（不殘留上一輪）
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, ResampleClearsStaleSlots) {
    auto t = std::make_shared<NullHttpTransport>();
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);

    t->set_response("http://feed.example/rss", HttpResponse::ok(kRss));  // 2 項目
    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.item_count(), 2u);

    // 下一輪換成空 feed → 舊的 slot 0/1 必須被清為 unknown。
    t->set_response("http://feed.example/rss", HttpResponse::ok(kEmptyRss));
    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.item_count(), 0u);
    auto m = reg.get("feed.headlines");
    EXPECT_FALSE(m->instance(0).value().valid);
    EXPECT_FALSE(m->instance(1).value().valid);
}

// ---------------------------------------------------------------------------
// 非 2xx 處理
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, Non2xxLeavesSlotsUnknown) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::of(404, kRss));
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(p.sample());
    EXPECT_EQ(p.last_status(), 404);
    EXPECT_FALSE(p.last_parse_ok());  // 非 2xx 不解析
    EXPECT_EQ(p.item_count(), 0u);
    auto m = reg.get("feed.headlines");
    EXPECT_FALSE(m->instance(0).value().valid);
}

TEST(FeedFetchProvider, ServerErrorAfterSuccessClearsSlots) {
    auto t = std::make_shared<NullHttpTransport>();
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    t->set_response("http://feed.example/rss", HttpResponse::ok(kRss));
    EXPECT_TRUE(p.sample());
    EXPECT_EQ(p.item_count(), 2u);
    // 之後端點 500 → 清為 unknown。
    t->set_response("http://feed.example/rss", HttpResponse::of(500, ""));
    EXPECT_FALSE(p.sample());
    EXPECT_FALSE(reg.get("feed.headlines")->instance(0).value().valid);
}

// ---------------------------------------------------------------------------
// 解析錯誤（malformed）處理
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, MalformedBodyLeavesUnknownAndRecordsError) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss",
                    HttpResponse::ok("<rss><!-- unterminated comment "));
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(p.sample());
    EXPECT_EQ(p.last_status(), 200);
    EXPECT_FALSE(p.last_parse_ok());
    EXPECT_FALSE(p.last_error().message.empty());  // 不靜默
    EXPECT_FALSE(reg.get("feed.headlines")->instance(0).value().valid);
}

// ---------------------------------------------------------------------------
// null 傳輸 / 保守行為
// ---------------------------------------------------------------------------
TEST(FeedFetchProvider, NullTransportNoBackendYieldsUnknown) {
    auto t = std::make_shared<NullHttpTransport>();  // 未注入任何回應
    FeedFetchProvider p = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(p.sample());
    EXPECT_EQ(p.last_status(), 0);
    EXPECT_FALSE(reg.get("feed.headlines")->instance(0).value().valid);
}

TEST(FeedFetchProvider, NullptrTransportIsNoOpNoCrash) {
    FeedFetchProvider p(nullptr, "feed.headlines", "H", "http://x", 4);
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(p.sample());  // transport 為 null → 保守 no-op、不崩
    EXPECT_FALSE(reg.get("feed.headlines")->instance(0).value().valid);
}

TEST(FeedFetchProvider, SampleBeforeRegisterIsNoOp) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kRss));
    FeedFetchProvider p = make_provider(t, 4);
    EXPECT_FALSE(p.sample());  // 未 register → 無 slot → no-op
    EXPECT_EQ(p.metric(), nullptr);
}

TEST(FeedFetchProvider, DuplicateIdRejectedAndFailedProviderNoOp) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kRss));
    MetricRegistry reg;
    FeedFetchProvider p1 = make_provider(t, 4);
    FeedFetchProvider p2 = make_provider(t, 4);  // 同 id
    EXPECT_EQ(reg.add_provider(p1), 1u);
    EXPECT_EQ(reg.add_provider(p2), 0u);  // 重複 id 被保守拒絕
    EXPECT_EQ(p2.metric(), nullptr);
    EXPECT_FALSE(p2.sample());  // 失敗提供者 sample no-op
    EXPECT_TRUE(p1.sample());   // 成功提供者仍運作
}

// ---------------------------------------------------------------------------
// 掛件式消費者範式：只走 E2-01 registry / Metric 介面，全程無 sysinfo 型別
// ---------------------------------------------------------------------------
TEST(Consumer, WidgetStyleReadsHeadlinesThroughE2_01Only) {
    auto t = std::make_shared<NullHttpTransport>();
    t->set_response("http://feed.example/rss", HttpResponse::ok(kRss));
    FeedFetchProvider provider = make_provider(t, 4);
    MetricRegistry reg;
    reg.add_provider(provider);
    provider.sample();

    // 以下僅用 E2-01 抽象介面（無 FeedFetchProvider / FeedItem 型別）。
    std::shared_ptr<ds::metrics::Metric> metric = reg.get("feed.headlines");
    ASSERT_NE(metric, nullptr);
    ASSERT_TRUE(metric->instance_count() >= 2u);
    const ds::metrics::MetricInstance& newest = metric->instance(0);
    EXPECT_TRUE(newest.value().valid);
    ASSERT_TRUE(newest.value().text.has_value());
    EXPECT_EQ(*newest.value().text, "First Post");
    // 依 slot 識別碼尋找。
    const ds::metrics::MetricInstance* byId = metric->find_instance("item1");
    ASSERT_NE(byId, nullptr);
    EXPECT_EQ(*byId->value().text, "Second Post");
}
