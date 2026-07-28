// E10-04 事件廣播型模組協定 — gtest 契約測試。
//
// 覆蓋：發布→全訂閱者收到、主題過濾（非訂閱者不收）、多訂閱者、動態訂閱 / 退訂、
// 模組卸載時清理訂閱、無訂閱者的發布（回 0 不崩潰）、酬載 / 來源傳遞、主題宣告、
// 內省計數、巢狀發布安全、退訂邊界，以及與 E8-04 行程內模組載入的整合
//（模組 on_unload 驅動 unsubscribe_module）與契約版本標記。
#include "event_broadcast.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "module_loader.hpp"  // E8-04：整合測試（模組載入 / 卸載驅動訂閱註冊 / 註銷）

using ds::ipc::Event;
using ds::ipc::EventBroadcast;
using ds::ipc::EventSubscriberId;
using ds::command::CommandArgs;
using ds::command::CommandValue;

// ---------------------------------------------------------------------------
// 發布 → 全訂閱者收到
// ---------------------------------------------------------------------------

TEST(EventBroadcastPublish, DeliversToAllSubscribersOfTopic) {
    EventBroadcast eb;
    int a = 0, b = 0;
    eb.subscribe("mod.a", "sensor.updated", [&](const Event&) { ++a; });
    eb.subscribe("mod.b", "sensor.updated", [&](const Event&) { ++b; });

    std::size_t delivered = eb.publish("sensor.updated");
    EXPECT_EQ(delivered, 2u);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(EventBroadcastPublish, DeliveredEventCarriesTopic) {
    EventBroadcast eb;
    std::string seen;
    eb.subscribe("mod.a", "theme.changed", [&](const Event& e) { seen = e.topic; });
    eb.publish("theme.changed");
    EXPECT_EQ(seen, "theme.changed");
}

// ---------------------------------------------------------------------------
// 主題過濾：非訂閱該主題者不收
// ---------------------------------------------------------------------------

TEST(EventBroadcastFilter, NonSubscribersOfTopicDoNotReceive) {
    EventBroadcast eb;
    int cpu = 0, mem = 0;
    eb.subscribe("mod.a", "cpu.updated", [&](const Event&) { ++cpu; });
    eb.subscribe("mod.b", "mem.updated", [&](const Event&) { ++mem; });

    EXPECT_EQ(eb.publish("cpu.updated"), 1u);
    EXPECT_EQ(cpu, 1);
    EXPECT_EQ(mem, 0);  // mem 訂閱者不受 cpu 事件影響

    EXPECT_EQ(eb.publish("mem.updated"), 1u);
    EXPECT_EQ(cpu, 1);
    EXPECT_EQ(mem, 1);
}

// ---------------------------------------------------------------------------
// 多訂閱者 + 投遞依註冊序
// ---------------------------------------------------------------------------

TEST(EventBroadcastMulti, DeliveryFollowsSubscriptionOrder) {
    EventBroadcast eb;
    std::vector<std::string> order;
    eb.subscribe("mod.a", "evt", [&](const Event&) { order.push_back("a"); });
    eb.subscribe("mod.b", "evt", [&](const Event&) { order.push_back("b"); });
    eb.subscribe("mod.c", "evt", [&](const Event&) { order.push_back("c"); });

    EXPECT_EQ(eb.publish("evt"), 3u);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "a");
    EXPECT_EQ(order[1], "b");
    EXPECT_EQ(order[2], "c");
}

TEST(EventBroadcastMulti, OneModuleManyTopics) {
    EventBroadcast eb;
    int n = 0;
    eb.subscribe("mod.a", "t1", [&](const Event&) { ++n; });
    eb.subscribe("mod.a", "t2", [&](const Event&) { ++n; });
    EXPECT_EQ(eb.module_subscription_count("mod.a"), 2u);
    eb.publish("t1");
    eb.publish("t2");
    EXPECT_EQ(n, 2);
}

// ---------------------------------------------------------------------------
// 酬載 / 來源傳遞
// ---------------------------------------------------------------------------

TEST(EventBroadcastPayload, PayloadDeliveredToSubscribers) {
    EventBroadcast eb;
    std::int64_t got = 0;
    eb.subscribe("mod.a", "cpu.updated", [&](const Event& e) {
        got = e.payload.get_int("percent").value_or(-1);
    });
    Event ev{CommandArgs{}.set("percent", CommandValue{std::int64_t{73}})};
    EXPECT_EQ(eb.publish("cpu.updated", ev), 1u);
    EXPECT_EQ(got, 73);
}

TEST(EventBroadcastPayload, SourceModuleDeliveredToSubscribers) {
    EventBroadcast eb;
    std::string src;
    eb.subscribe("mod.listener", "evt", [&](const Event& e) { src = e.source; });
    Event ev{CommandArgs{}, /*source=*/"mod.publisher"};
    eb.publish("evt", ev);
    EXPECT_EQ(src, "mod.publisher");
}

// ---------------------------------------------------------------------------
// 動態訂閱 / 退訂
// ---------------------------------------------------------------------------

TEST(EventBroadcastUnsubscribe, UnsubscribeStopsDelivery) {
    EventBroadcast eb;
    int n = 0;
    EventSubscriberId id = eb.subscribe("mod.a", "evt", [&](const Event&) { ++n; });
    ASSERT_NE(id, 0u);
    EXPECT_EQ(eb.publish("evt"), 1u);
    EXPECT_EQ(n, 1);

    EXPECT_TRUE(eb.unsubscribe("mod.a", id));
    EXPECT_EQ(eb.publish("evt"), 0u);  // 已退訂 → 無人收
    EXPECT_EQ(n, 1);
    EXPECT_FALSE(eb.has_module("mod.a"));  // 該模組已無活躍訂閱
}

TEST(EventBroadcastUnsubscribe, UnsubscribeUnknownIdReturnsFalse) {
    EventBroadcast eb;
    eb.subscribe("mod.a", "evt", [](const Event&) {});
    EXPECT_FALSE(eb.unsubscribe("mod.a", 9999u));  // 未知 id
    EXPECT_FALSE(eb.unsubscribe("mod.b", 1u));     // 未知模組
}

TEST(EventBroadcastUnsubscribe, UnsubscribeWrongModuleReturnsFalse) {
    EventBroadcast eb;
    EventSubscriberId id = eb.subscribe("mod.a", "evt", [](const Event&) {});
    // 該 id 屬 mod.a，用 mod.b 退訂不得成功。
    EXPECT_FALSE(eb.unsubscribe("mod.b", id));
    EXPECT_TRUE(eb.unsubscribe("mod.a", id));  // 正確模組可退
}

TEST(EventBroadcastUnsubscribe, UnsubscribeMidKeepsOthers) {
    EventBroadcast eb;
    int a = 0, b = 0, c = 0;
    eb.subscribe("m", "evt", [&](const Event&) { ++a; });
    EventSubscriberId mid = eb.subscribe("m", "evt", [&](const Event&) { ++b; });
    eb.subscribe("m", "evt", [&](const Event&) { ++c; });

    EXPECT_TRUE(eb.unsubscribe("m", mid));
    EXPECT_EQ(eb.publish("evt"), 2u);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 0);  // 中段退訂者不再收
    EXPECT_EQ(c, 1);
}

// ---------------------------------------------------------------------------
// 無效訂閱輸入
// ---------------------------------------------------------------------------

TEST(EventBroadcastSubscribe, RejectsEmptyInputs) {
    EventBroadcast eb;
    EXPECT_EQ(eb.subscribe("", "evt", [](const Event&) {}), 0u);   // 空模組
    EXPECT_EQ(eb.subscribe("m", "", [](const Event&) {}), 0u);     // 空主題
    EXPECT_EQ(eb.subscribe("m", "evt", nullptr), 0u);              // 空 handler
    EXPECT_EQ(eb.subscriber_count(), 0u);                          // 一個都沒掛上
}

TEST(EventBroadcastSubscribe, SubscriberIdsAreUnique) {
    EventBroadcast eb;
    EventSubscriberId a = eb.subscribe("m", "t", [](const Event&) {});
    EventSubscriberId b = eb.subscribe("m", "t", [](const Event&) {});
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);
    EXPECT_NE(a, b);
}

// ---------------------------------------------------------------------------
// 無訂閱者的發布：回 0、不崩潰
// ---------------------------------------------------------------------------

TEST(EventBroadcastNoSubscribers, PublishReturnsZeroNoCrash) {
    EventBroadcast eb;
    EXPECT_EQ(eb.publish("nobody.listening"), 0u);
    Event ev{CommandArgs{}.set("x", CommandValue{1})};
    EXPECT_EQ(eb.publish("nobody.listening", ev), 0u);
}

// ---------------------------------------------------------------------------
// 模組卸載時清理訂閱
// ---------------------------------------------------------------------------

TEST(EventBroadcastModuleUnload, UnsubscribeModuleRemovesAllItsSubscriptions) {
    EventBroadcast eb;
    int a = 0, b = 0, keep = 0;
    eb.subscribe("mod.x", "t1", [&](const Event&) { ++a; });
    eb.subscribe("mod.x", "t2", [&](const Event&) { ++b; });
    eb.subscribe("mod.keep", "t1", [&](const Event&) { ++keep; });
    EXPECT_EQ(eb.subscriber_count(), 3u);

    std::size_t removed = eb.unsubscribe_module("mod.x");
    EXPECT_EQ(removed, 2u);
    EXPECT_FALSE(eb.has_module("mod.x"));
    EXPECT_EQ(eb.subscriber_count(), 1u);  // 只剩 mod.keep

    eb.publish("t1");
    eb.publish("t2");
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 0);
    EXPECT_EQ(keep, 1);  // 未卸載的模組照常收
}

TEST(EventBroadcastModuleUnload, UnsubscribeUnknownModuleReturnsZero) {
    EventBroadcast eb;
    EXPECT_EQ(eb.unsubscribe_module("never.loaded"), 0u);
}

TEST(EventBroadcastModuleUnload, ClearsDeclarationsToo) {
    EventBroadcast eb;
    eb.declare_publish("mod.x", "t1");
    eb.declare_subscribe("mod.x", "t2");
    eb.subscribe("mod.x", "t2", [](const Event&) {});

    eb.unsubscribe_module("mod.x");
    EXPECT_FALSE(eb.declares_publish("mod.x", "t1"));
    EXPECT_FALSE(eb.declares_subscribe("mod.x", "t2"));
}

// ---------------------------------------------------------------------------
// 主題宣告（模組宣告它 publish / subscribe 哪些主題）
// ---------------------------------------------------------------------------

TEST(EventBroadcastDeclare, DeclareAndQuery) {
    EventBroadcast eb;
    eb.declare_publish("mod.a", "sensor.cpu");
    eb.declare_publish("mod.a", "sensor.mem");
    eb.declare_subscribe("mod.a", "theme.changed");

    EXPECT_TRUE(eb.declares_publish("mod.a", "sensor.cpu"));
    EXPECT_TRUE(eb.declares_publish("mod.a", "sensor.mem"));
    EXPECT_FALSE(eb.declares_publish("mod.a", "theme.changed"));
    EXPECT_TRUE(eb.declares_subscribe("mod.a", "theme.changed"));

    std::vector<std::string> pubs = eb.declared_publish("mod.a");
    ASSERT_EQ(pubs.size(), 2u);
    EXPECT_EQ(pubs[0], "sensor.cpu");  // std::set 排序
    EXPECT_EQ(pubs[1], "sensor.mem");
}

TEST(EventBroadcastDeclare, IgnoresEmptyAndDeduplicates) {
    EventBroadcast eb;
    eb.declare_publish("", "t");        // 空模組忽略
    eb.declare_publish("m", "");        // 空主題忽略
    eb.declare_publish("m", "t");
    eb.declare_publish("m", "t");       // 重複去重
    EXPECT_FALSE(eb.declares_publish("", "t"));
    EXPECT_FALSE(eb.declares_publish("m", ""));
    EXPECT_EQ(eb.declared_publish("m").size(), 1u);
}

// ---------------------------------------------------------------------------
// 內省計數
// ---------------------------------------------------------------------------

TEST(EventBroadcastIntrospect, SubscriberCounts) {
    EventBroadcast eb;
    eb.subscribe("mod.a", "t1", [](const Event&) {});
    eb.subscribe("mod.b", "t1", [](const Event&) {});
    eb.subscribe("mod.b", "t2", [](const Event&) {});

    EXPECT_EQ(eb.subscriber_count(), 3u);
    EXPECT_EQ(eb.subscriber_count("t1"), 2u);
    EXPECT_EQ(eb.subscriber_count("t2"), 1u);
    EXPECT_EQ(eb.subscriber_count("none"), 0u);
    EXPECT_EQ(eb.module_subscription_count("mod.b"), 2u);

    std::vector<std::string> mods = eb.modules();
    ASSERT_EQ(mods.size(), 2u);
    EXPECT_EQ(mods[0], "mod.a");  // std::map 排序
    EXPECT_EQ(mods[1], "mod.b");
}

// ---------------------------------------------------------------------------
// 巢狀發布安全（處理器內再發布不破壞外層事件）
// ---------------------------------------------------------------------------

TEST(EventBroadcastReentrancy, NestedPublishSafe) {
    EventBroadcast eb;
    std::string outer_topic_after;
    int inner = 0;

    eb.subscribe("mod.b", "inner.evt", [&](const Event&) { ++inner; });
    eb.subscribe("mod.a", "outer.evt", [&](const Event& e) {
        // 處理外層事件時觸發一次內層發布；返回後外層 Event 仍須完好。
        eb.publish("inner.evt");
        outer_topic_after = e.topic;  // 巢狀返回後讀外層事件
    });

    EXPECT_EQ(eb.publish("outer.evt"), 1u);
    EXPECT_EQ(inner, 1);
    EXPECT_EQ(outer_topic_after, "outer.evt");  // 外層事件未被內層覆蓋
}

// ---------------------------------------------------------------------------
// 與 E8-04 行程內模組載入整合：模組 on_unload 驅動 unsubscribe_module
// ---------------------------------------------------------------------------

namespace {

ds::package::Manifest make_manifest(const std::string& name) {
    ds::package::Manifest mf;
    mf.format_version = ds::package::FormatVersion{1, 0};
    mf.name = name;
    return mf;
}

}  // namespace

TEST(EventBroadcastE804Integration, ModuleUnloadCleansUpSubscriptions) {
    ds::ext::HostEnvironment host;
    ds::ext::ModuleLoader loader(host);
    EventBroadcast eb;

    int received = 0;
    const std::string kModule = "com.example.sensor";

    // 模組：載入時登記能力並訂閱事件；卸載時（on_unload）註銷其全部訂閱。
    ds::ext::Module mod;
    mod.manifest = make_manifest(kModule);
    mod.on_register = [&](ds::ext::ModuleContext& ctx) {
        ctx.provide_sensor("cpu.usage");
        eb.declare_subscribe(kModule, "tick");
        eb.subscribe(kModule, "tick", [&](const Event&) { ++received; });
        return true;
    };
    mod.on_unload = [&]() { eb.unsubscribe_module(kModule); };

    ASSERT_TRUE(loader.load(mod).ok());
    EXPECT_TRUE(eb.has_module(kModule));
    EXPECT_EQ(eb.publish("tick"), 1u);
    EXPECT_EQ(received, 1);

    // 卸載模組 → on_unload 觸發 unsubscribe_module → 之後發布無人收。
    ASSERT_TRUE(loader.unload(kModule));
    EXPECT_FALSE(eb.has_module(kModule));
    EXPECT_EQ(eb.publish("tick"), 0u);
    EXPECT_EQ(received, 1);
}

TEST(EventBroadcastE804Integration, TwoModulesInterconnectViaTopics) {
    ds::ext::HostEnvironment host;
    ds::ext::ModuleLoader loader(host);
    EventBroadcast eb;

    std::int64_t consumer_saw = -1;
    const std::string kProducer = "com.example.producer";
    const std::string kConsumer = "com.example.consumer";

    // Consumer 模組：訂閱 "metric.cpu"。
    ds::ext::Module consumer;
    consumer.manifest = make_manifest(kConsumer);
    consumer.on_register = [&](ds::ext::ModuleContext& ctx) {
        ctx.provide_component("cpu.widget");
        eb.declare_subscribe(kConsumer, "metric.cpu");
        eb.subscribe(kConsumer, "metric.cpu", [&](const Event& e) {
            consumer_saw = e.payload.get_int("value").value_or(-1);
        });
        return true;
    };
    consumer.on_unload = [&]() { eb.unsubscribe_module(kConsumer); };

    // Producer 模組：宣告發布 "metric.cpu"（實際發布在載入後由 host 觸發）。
    ds::ext::Module producer;
    producer.manifest = make_manifest(kProducer);
    producer.on_register = [&](ds::ext::ModuleContext& ctx) {
        ctx.provide_sensor("cpu.usage");
        eb.declare_publish(kProducer, "metric.cpu");
        return true;
    };

    ASSERT_TRUE(loader.load(consumer).ok());
    ASSERT_TRUE(loader.load(producer).ok());

    // Producer 發布事件 → 透過協定廣播 → Consumer 收到（兩模組不直接相依）。
    Event ev{CommandArgs{}.set("value", CommandValue{std::int64_t{42}}), kProducer};
    EXPECT_EQ(eb.publish("metric.cpu", ev), 1u);
    EXPECT_EQ(consumer_saw, 42);
    EXPECT_TRUE(eb.declares_publish(kProducer, "metric.cpu"));
    EXPECT_TRUE(eb.declares_subscribe(kConsumer, "metric.cpu"));
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------

TEST(EventBroadcastContract, VersionTag) {
    EXPECT_STREQ(ds::ipc::event_broadcast_contract_version(), "e10_04/1.0.0");
}
