// E7-15 拖放產生設定項 — 契約測試（gtest）
//
// 涵蓋：
//   - 內容類型偵測（檔案 / 圖片 / URL / 文字 / 顏色 / 無法辨識）。
//   - 各內容類型 → 對應設定項（圖片→背景設定、URL→連結元件、顏色、文字、一般檔案）。
//   - 經 E7-12 寫入文件（apply_drop / round-trip 序列化再解析一致）。
//   - 無法辨識內容明確回報（Unknown）且產生設定項時 throw（不靜默）。
//   - 與 E5-08 拖放事件整合（NullSystemEventSource 注入 → 轉 DropContent → 產生設定項）。
//   - 多項拖放（apply_drops 疊加）。
// 平台中立：不含任何平台分支。
#include "dropgen.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "document.hpp"
#include "system_event.hpp"
#include "writeback.hpp"

using ds::format::apply_drop;
using ds::format::apply_drops;
using ds::format::ConfigItem;
using ds::format::detect_drop_kind;
using ds::format::Document;
using ds::format::drop_from_system_event;
using ds::format::DropContent;
using ds::format::DropKind;
using ds::format::FormatVersion;
using ds::format::generate_config_item;
using ds::format::is_image_path;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::serialize;
using ds::format::set_value;
using ds::format::subscribe_drops;
using ds::format::Value;

using ds::events::NullSystemEventSource;
using ds::events::SystemEvent;
using ds::events::SystemEventType;

namespace {

// 小工具：以空內容根 + 版本組一份文件。
Document empty_doc() {
    Document d;
    d.format_version = FormatVersion{1, 0};
    d.root = Value::map({});
    return d;
}

// 小工具：解析文字，斷言成功並回傳 Document。
Document must_parse(const std::string& text) {
    ParseResult r = parse(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.document();
}

}  // namespace

// -----------------------------------------------------------------------------
// 內容類型偵測
// -----------------------------------------------------------------------------

TEST(DetectKind, AbsolutePathIsFile) {
    EXPECT_EQ(detect_drop_kind("/Users/me/notes.txt"), DropKind::File);
    EXPECT_EQ(detect_drop_kind("/etc/hosts.d/thing"), DropKind::File);  // 無副檔名但絕對路徑
}

TEST(DetectKind, ImageExtensionIsFile) {
    EXPECT_EQ(detect_drop_kind("wallpaper.png"), DropKind::File);
    EXPECT_EQ(detect_drop_kind("photo.JPG"), DropKind::File);  // 大小寫不敏感
}

TEST(DetectKind, FileUriIsFile) {
    EXPECT_EQ(detect_drop_kind("file:///Users/me/a.dat"), DropKind::File);
}

TEST(DetectKind, HttpAndFtpAreUrl) {
    EXPECT_EQ(detect_drop_kind("https://example.com/a"), DropKind::Url);
    EXPECT_EQ(detect_drop_kind("http://example.com"), DropKind::Url);
    EXPECT_EQ(detect_drop_kind("ftp://host/f"), DropKind::Url);
}

TEST(DetectKind, HexColors) {
    EXPECT_EQ(detect_drop_kind("#fff"), DropKind::Color);       // 3
    EXPECT_EQ(detect_drop_kind("#ffff"), DropKind::Color);      // 4
    EXPECT_EQ(detect_drop_kind("#1a2b3c"), DropKind::Color);    // 6
    EXPECT_EQ(detect_drop_kind("#1a2b3c80"), DropKind::Color);  // 8
    EXPECT_EQ(detect_drop_kind("#12345"), DropKind::Text);      // 5 位非法長度 → 非顏色
    EXPECT_EQ(detect_drop_kind("#gggggg"), DropKind::Text);     // 非十六進位
}

TEST(DetectKind, PlainTextFallback) {
    EXPECT_EQ(detect_drop_kind("hello world"), DropKind::Text);
    EXPECT_EQ(detect_drop_kind("just some words"), DropKind::Text);
}

TEST(DetectKind, EmptyAndWhitespaceAreUnknown) {
    EXPECT_EQ(detect_drop_kind(""), DropKind::Unknown);
    EXPECT_EQ(detect_drop_kind("   \t\n"), DropKind::Unknown);
}

TEST(DetectKind, ControlBytesAreUnknown) {
    std::string binary = "abc";
    binary.push_back('\0');   // 嵌入 NUL
    binary.push_back(0x01);
    EXPECT_EQ(detect_drop_kind(binary), DropKind::Unknown);
}

TEST(DetectKind, DetectFactoryPreservesPayload) {
    DropContent d = DropContent::detect("https://example.com/x");
    EXPECT_EQ(d.kind, DropKind::Url);
    EXPECT_EQ(d.payload, "https://example.com/x");

    DropContent u = DropContent::detect("");
    EXPECT_EQ(u.kind, DropKind::Unknown);
    EXPECT_EQ(u.payload, "");  // payload 仍保留原始字串
}

TEST(IsImagePath, RecognisesImageExtensions) {
    EXPECT_TRUE(is_image_path("a.png"));
    EXPECT_TRUE(is_image_path("/x/y/z.jpeg"));
    EXPECT_TRUE(is_image_path("ICON.ICO"));
    EXPECT_FALSE(is_image_path("notes.txt"));
    EXPECT_FALSE(is_image_path("/no/extension"));
    EXPECT_FALSE(is_image_path("archive.tar.gz"));  // gz 非圖片
}

// -----------------------------------------------------------------------------
// 各內容類型 → 對應設定項
// -----------------------------------------------------------------------------

TEST(GenerateConfigItem, ImageFileMapsToBackground) {
    ConfigItem item = generate_config_item(DropContent::file("/pics/bg.png"));
    EXPECT_EQ(item.path, "background.image");
    ASSERT_TRUE(item.value.is_string());
    EXPECT_EQ(item.value.as_string(), "/pics/bg.png");
}

TEST(GenerateConfigItem, NonImageFileMapsToFilePath) {
    ConfigItem item = generate_config_item(DropContent::file("/docs/report.pdf"));
    EXPECT_EQ(item.path, "file.path");
    EXPECT_EQ(item.value.as_string(), "/docs/report.pdf");
}

TEST(GenerateConfigItem, UrlMapsToLink) {
    ConfigItem item = generate_config_item(DropContent::url("https://ex.com"));
    EXPECT_EQ(item.path, "link.url");
    EXPECT_EQ(item.value.as_string(), "https://ex.com");
}

TEST(GenerateConfigItem, ColorMapsToColor) {
    ConfigItem item = generate_config_item(DropContent::color("#1a2b3c"));
    EXPECT_EQ(item.path, "color");
    EXPECT_EQ(item.value.as_string(), "#1a2b3c");
}

TEST(GenerateConfigItem, TextMapsToText) {
    ConfigItem item = generate_config_item(DropContent::text("some note"));
    EXPECT_EQ(item.path, "text");
    EXPECT_EQ(item.value.as_string(), "some note");
}

TEST(GenerateConfigItem, UnknownThrows) {
    DropContent unknown{DropKind::Unknown, "???"};
    EXPECT_THROW(generate_config_item(unknown), std::runtime_error);
}

TEST(GenerateConfigItem, DetectedImageThroughFactory) {
    // 以偵測工廠拿到 File，再確認落到背景設定。
    ConfigItem item = generate_config_item(DropContent::detect("cover.webp"));
    EXPECT_EQ(item.path, "background.image");
    EXPECT_EQ(item.value.as_string(), "cover.webp");
}

// -----------------------------------------------------------------------------
// 經 E7-12 寫入文件
// -----------------------------------------------------------------------------

TEST(ApplyDrop, WritesIntoDocumentRoot) {
    Document doc = empty_doc();
    Document out = apply_drop(doc, DropContent::url("https://ex.com/a"));

    ASSERT_TRUE(out.root.is_map());
    const Value* link = out.root.find("link");
    ASSERT_NE(link, nullptr);
    ASSERT_TRUE(link->is_map());
    const Value* url = link->find("url");
    ASSERT_NE(url, nullptr);
    EXPECT_EQ(url->as_string(), "https://ex.com/a");
    // 版本欄位保留。
    EXPECT_EQ(out.format_version, (FormatVersion{1, 0}));
}

TEST(ApplyDrop, IsPureDoesNotMutateInput) {
    Document doc = empty_doc();
    Document out = apply_drop(doc, DropContent::color("#abcdef"));
    // 原文件 root 仍為空 map（純函式）。
    EXPECT_EQ(doc.root.size(), 0u);
    EXPECT_EQ(out.root.size(), 1u);
}

TEST(ApplyDrop, ValueOverloadWritesViaE712) {
    Value root = Value::map({});
    Value out = apply_drop(root, DropContent::file("/pics/bg.png"));
    const Value* bg = out.find("background");
    ASSERT_NE(bg, nullptr);
    ASSERT_TRUE(bg->is_map());
    EXPECT_EQ(bg->at("image").as_string(), "/pics/bg.png");
}

TEST(ApplyDrop, RoundTripsThroughSerialize) {
    // 套用後序列化回 E7-01 文字，再解析，值一致（借 E7-12 serialize + E7-01 parse）。
    Document doc = empty_doc();
    Document out = apply_drop(doc, DropContent::url("https://ex.com/z"));
    const std::string text = serialize(out);
    Document reparsed = must_parse(text);
    const Value& url = reparsed.root.at("link").at("url");
    EXPECT_EQ(url.as_string(), "https://ex.com/z");
}

TEST(ApplyDrop, UnknownPropagatesThrow) {
    Document doc = empty_doc();
    DropContent unknown{DropKind::Unknown, ""};
    EXPECT_THROW(apply_drop(doc, unknown), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 多項拖放
// -----------------------------------------------------------------------------

TEST(ApplyDrops, MultipleDropsAccumulate) {
    Document doc = empty_doc();
    std::vector<DropContent> drops = {
        DropContent::file("/pics/bg.png"),
        DropContent::url("https://ex.com"),
        DropContent::color("#001122"),
        DropContent::text("caption"),
    };
    Document out = apply_drops(doc, drops);

    EXPECT_EQ(out.root.at("background").at("image").as_string(), "/pics/bg.png");
    EXPECT_EQ(out.root.at("link").at("url").as_string(), "https://ex.com");
    EXPECT_EQ(out.root.at("color").as_string(), "#001122");
    EXPECT_EQ(out.root.at("text").as_string(), "caption");
}

TEST(ApplyDrops, LaterSameKindOverwrites) {
    Document doc = empty_doc();
    std::vector<DropContent> drops = {
        DropContent::color("#111111"),
        DropContent::color("#222222"),  // 同鍵後者覆蓋前者
    };
    Document out = apply_drops(doc, drops);
    EXPECT_EQ(out.root.at("color").as_string(), "#222222");
    EXPECT_EQ(out.root.size(), 1u);
}

TEST(ApplyDrops, UnknownInBatchThrowsNotSilent) {
    Document doc = empty_doc();
    std::vector<DropContent> drops = {
        DropContent::color("#111111"),
        DropContent{DropKind::Unknown, "garbage"},
    };
    EXPECT_THROW(apply_drops(doc, drops), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 與 E5-08 系統事件整合
// -----------------------------------------------------------------------------

TEST(SystemEventBridge, DropFromEventDetectsFromDetail) {
    SystemEvent ev{SystemEventType::DisplayChanged, "https://ex.com/dropped"};
    DropContent d = drop_from_system_event(ev);
    EXPECT_EQ(d.kind, DropKind::Url);
    EXPECT_EQ(d.payload, "https://ex.com/dropped");
}

TEST(SystemEventBridge, SubscribeDropsDispatchesInjectedEvent) {
    NullSystemEventSource source;
    DropContent received{DropKind::Unknown, ""};
    int calls = 0;

    ds::events::SubscriptionId id = subscribe_drops(
        source, [&](const DropContent& d) {
            received = d;
            ++calls;
        });
    EXPECT_NE(id, 0u);
    EXPECT_EQ(source.listener_count(), 1u);

    // 注入攜帶拖放內容（圖片路徑）的事件。
    source.inject(SystemEvent{SystemEventType::DisplayChanged, "/pics/bg.png"});

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(received.kind, DropKind::File);
    EXPECT_EQ(received.payload, "/pics/bg.png");
    // 收到的內容能產生正確設定項。
    ConfigItem item = generate_config_item(received);
    EXPECT_EQ(item.path, "background.image");
}

TEST(SystemEventBridge, EndToEndEventToConfigDocument) {
    // 完整鏈路：E5-08 注入事件 → 轉 DropContent → 經 E7-12 寫入文件。
    NullSystemEventSource source;
    Document doc = empty_doc();

    subscribe_drops(source, [&](const DropContent& d) {
        doc = apply_drop(doc, d);
    });

    source.inject(SystemEvent{SystemEventType::SessionUnlocked, "#0a0b0c"});
    source.inject(SystemEvent{SystemEventType::SessionUnlocked, "https://ex.com/link"});

    EXPECT_EQ(doc.root.at("color").as_string(), "#0a0b0c");
    EXPECT_EQ(doc.root.at("link").at("url").as_string(), "https://ex.com/link");
}

TEST(SystemEventBridge, NullHandlerIsInvalidSubscription) {
    NullSystemEventSource source;
    ds::events::SubscriptionId id = subscribe_drops(source, nullptr);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(source.listener_count(), 0u);
}

TEST(SystemEventBridge, UnsubscribeStopsDelivery) {
    NullSystemEventSource source;
    int calls = 0;
    ds::events::SubscriptionId id =
        subscribe_drops(source, [&](const DropContent&) { ++calls; });
    source.inject(SystemEvent{SystemEventType::DisplayChanged, "hello"});
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(source.unsubscribe(id));
    source.inject(SystemEvent{SystemEventType::DisplayChanged, "hello again"});
    EXPECT_EQ(calls, 1);  // 解除後不再收到
}
