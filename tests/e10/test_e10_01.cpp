// E10-01 本機 IPC 訊息投遞 — gtest 契約測試。
//
// 覆蓋：點對點送收（FIFO 順序）、發布訂閱多訂閱者、投遞順序、無訂閱者、
// 型別過濾、萬用訂閱、退訂、酬載傳遞、與 E6-01 命令搭配、契約版本標記。
#include "message_channel.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

using ds::ipc::Message;
using ds::ipc::MessageChannel;
using ds::ipc::SubscriberId;
using ds::command::Command;
using ds::command::CommandArgs;

// ---------------------------------------------------------------------------
// 點對點佇列：send / receive
// ---------------------------------------------------------------------------

TEST(MessageChannelQueue, SendThenReceiveRoundTrip) {
    MessageChannel ch;
    EXPECT_FALSE(ch.has_pending());
    EXPECT_EQ(ch.pending(), 0u);

    ch.send(Message{"widget.refresh"});
    EXPECT_TRUE(ch.has_pending());
    EXPECT_EQ(ch.pending(), 1u);

    auto got = ch.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->type, "widget.refresh");
    EXPECT_FALSE(ch.has_pending());
    EXPECT_EQ(ch.pending(), 0u);
}

TEST(MessageChannelQueue, ReceiveEmptyReturnsNulloptNoCrash) {
    MessageChannel ch;
    auto got = ch.receive();
    EXPECT_FALSE(got.has_value());
}

TEST(MessageChannelQueue, FifoOrderPreserved) {
    MessageChannel ch;
    ch.send(Message{"a"});
    ch.send(Message{"b"});
    ch.send(Message{"c"});
    EXPECT_EQ(ch.pending(), 3u);

    std::vector<std::string> order;
    while (auto m = ch.receive()) order.push_back(m->type);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "a");
    EXPECT_EQ(order[1], "b");
    EXPECT_EQ(order[2], "c");
}

TEST(MessageChannelQueue, PayloadCarriedThroughQueue) {
    MessageChannel ch;
    CommandArgs args;
    args.set("level", 42).set("mute", false);
    ch.send(Message{"volume", args});

    auto got = ch.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->type, "volume");
    EXPECT_EQ(got->payload.get_int("level"), 42);
    EXPECT_EQ(got->payload.get_bool("mute"), false);
}

// ---------------------------------------------------------------------------
// 發布訂閱：subscribe / publish
// ---------------------------------------------------------------------------

TEST(MessageChannelPubSub, PublishToSingleSubscriber) {
    MessageChannel ch;
    int hits = 0;
    Message seen;
    SubscriberId id = ch.subscribe("tick", [&](const Message& m) {
        ++hits;
        seen = m;
    });
    EXPECT_NE(id, 0u);
    EXPECT_EQ(ch.subscriber_count(), 1u);

    std::size_t delivered = ch.publish(Message{"tick"});
    EXPECT_EQ(delivered, 1u);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(seen.type, "tick");
}

TEST(MessageChannelPubSub, MultipleSubscribersAllReceive) {
    MessageChannel ch;
    int a = 0, b = 0, c = 0;
    ch.subscribe("evt", [&](const Message&) { ++a; });
    ch.subscribe("evt", [&](const Message&) { ++b; });
    ch.subscribe("evt", [&](const Message&) { ++c; });
    EXPECT_EQ(ch.subscriber_count(), 3u);
    EXPECT_EQ(ch.subscriber_count("evt"), 3u);

    std::size_t delivered = ch.publish(Message{"evt"});
    EXPECT_EQ(delivered, 3u);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, 1);
}

TEST(MessageChannelPubSub, DeliveryFollowsRegistrationOrder) {
    MessageChannel ch;
    std::vector<int> order;
    ch.subscribe("evt", [&](const Message&) { order.push_back(1); });
    ch.subscribe("evt", [&](const Message&) { order.push_back(2); });
    ch.subscribe("evt", [&](const Message&) { order.push_back(3); });

    ch.publish(Message{"evt"});
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(MessageChannelPubSub, MessageSequenceDeliveredInPublishOrder) {
    MessageChannel ch;
    std::vector<std::string> got;
    ch.subscribe("evt", [&](const Message& m) {
        got.push_back(*m.payload.get_string("seq"));
    });

    ch.publish(Message{"evt", CommandArgs{}.set("seq", std::string("first"))});
    ch.publish(Message{"evt", CommandArgs{}.set("seq", std::string("second"))});
    ch.publish(Message{"evt", CommandArgs{}.set("seq", std::string("third"))});

    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], "first");
    EXPECT_EQ(got[1], "second");
    EXPECT_EQ(got[2], "third");
}

TEST(MessageChannelPubSub, NoSubscribersDeliversZeroNoCrash) {
    MessageChannel ch;
    std::size_t delivered = ch.publish(Message{"nobody.listening"});
    EXPECT_EQ(delivered, 0u);
}

TEST(MessageChannelPubSub, PublishOnlyMatchingType) {
    MessageChannel ch;
    int onA = 0, onB = 0;
    ch.subscribe("A", [&](const Message&) { ++onA; });
    ch.subscribe("B", [&](const Message&) { ++onB; });

    EXPECT_EQ(ch.publish(Message{"A"}), 1u);
    EXPECT_EQ(onA, 1);
    EXPECT_EQ(onB, 0);

    EXPECT_EQ(ch.publish(Message{"B"}), 1u);
    EXPECT_EQ(onA, 1);
    EXPECT_EQ(onB, 1);

    // 無人訂閱的型別
    EXPECT_EQ(ch.publish(Message{"C"}), 0u);
}

TEST(MessageChannelPubSub, SubscribeAllReceivesEveryType) {
    MessageChannel ch;
    int all = 0, onA = 0;
    ch.subscribe_all([&](const Message&) { ++all; });
    ch.subscribe("A", [&](const Message&) { ++onA; });

    EXPECT_EQ(ch.publish(Message{"A"}), 2u);  // 萬用 + 型別 A
    EXPECT_EQ(ch.publish(Message{"Z"}), 1u);  // 只有萬用
    EXPECT_EQ(all, 2);
    EXPECT_EQ(onA, 1);
    EXPECT_EQ(ch.subscriber_count("Z"), 1u);  // 萬用計入
}

TEST(MessageChannelPubSub, PayloadDeliveredToSubscriber) {
    MessageChannel ch;
    CommandArgs seen;
    ch.subscribe("volume.set", [&](const Message& m) { seen = m.payload; });

    ch.publish(Message{"volume.set", CommandArgs{}.set("level", 70).set("mute", true)});
    EXPECT_EQ(seen.get_int("level"), 70);
    EXPECT_EQ(seen.get_bool("mute"), true);
}

// ---------------------------------------------------------------------------
// 退訂
// ---------------------------------------------------------------------------

TEST(MessageChannelPubSub, UnsubscribeStopsDelivery) {
    MessageChannel ch;
    int hits = 0;
    SubscriberId id = ch.subscribe("evt", [&](const Message&) { ++hits; });
    EXPECT_EQ(ch.publish(Message{"evt"}), 1u);
    EXPECT_EQ(hits, 1);

    EXPECT_TRUE(ch.unsubscribe(id));
    EXPECT_EQ(ch.subscriber_count(), 0u);
    EXPECT_EQ(ch.publish(Message{"evt"}), 0u);
    EXPECT_EQ(hits, 1);  // 未再增加
}

TEST(MessageChannelPubSub, UnsubscribeUnknownIdReturnsFalse) {
    MessageChannel ch;
    EXPECT_FALSE(ch.unsubscribe(999));
    ch.subscribe("evt", [&](const Message&) {});
    EXPECT_FALSE(ch.unsubscribe(12345));  // 有效訂閱者但 id 不符
}

TEST(MessageChannelPubSub, UnsubscribeMiddlePreservesOrderOfRest) {
    MessageChannel ch;
    std::vector<int> order;
    SubscriberId /*id1*/ i1 = ch.subscribe("evt", [&](const Message&) { order.push_back(1); });
    SubscriberId id2 = ch.subscribe("evt", [&](const Message&) { order.push_back(2); });
    SubscriberId /*id3*/ i3 = ch.subscribe("evt", [&](const Message&) { order.push_back(3); });
    (void)i1;
    (void)i3;

    EXPECT_TRUE(ch.unsubscribe(id2));
    ch.publish(Message{"evt"});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 3);
}

// ---------------------------------------------------------------------------
// 無效訂閱輸入
// ---------------------------------------------------------------------------

TEST(MessageChannelPubSub, EmptyTypeRejected) {
    MessageChannel ch;
    SubscriberId id = ch.subscribe("", [&](const Message&) {});
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(ch.subscriber_count(), 0u);
}

TEST(MessageChannelPubSub, EmptyCallbackRejected) {
    MessageChannel ch;
    ds::ipc::Subscriber empty;  // 空 std::function
    EXPECT_EQ(ch.subscribe("evt", empty), 0u);
    EXPECT_EQ(ch.subscribe_all(empty), 0u);
    EXPECT_EQ(ch.subscriber_count(), 0u);
}

TEST(MessageChannelPubSub, SubscriberIdsAreUnique) {
    MessageChannel ch;
    SubscriberId a = ch.subscribe("x", [&](const Message&) {});
    SubscriberId b = ch.subscribe("x", [&](const Message&) {});
    SubscriberId c = ch.subscribe_all([&](const Message&) {});
    EXPECT_NE(a, 0u);
    EXPECT_NE(b, 0u);
    EXPECT_NE(c, 0u);
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
}

// ---------------------------------------------------------------------------
// 兩模型互不干擾
// ---------------------------------------------------------------------------

TEST(MessageChannelIsolation, PublishDoesNotEnqueue) {
    MessageChannel ch;
    ch.subscribe("evt", [&](const Message&) {});
    ch.publish(Message{"evt"});
    EXPECT_EQ(ch.pending(), 0u);  // publish 不入列
}

TEST(MessageChannelIsolation, SendDoesNotTriggerSubscribers) {
    MessageChannel ch;
    int hits = 0;
    ch.subscribe("evt", [&](const Message&) { ++hits; });
    ch.send(Message{"evt"});
    EXPECT_EQ(hits, 0);          // send 不觸發訂閱者
    EXPECT_EQ(ch.pending(), 1u);
}

// ---------------------------------------------------------------------------
// 與 E6-01 命令搭配
// ---------------------------------------------------------------------------

TEST(MessageChannelCommand, CarriesCommandViaFromCommand) {
    MessageChannel ch;
    Command cmd;
    cmd.id = "power.sleep";
    cmd.args.set("delay_s", 30);

    Message received;
    ch.subscribe("power.sleep", [&](const Message& m) { received = m; });

    std::size_t delivered = ch.publish(Message::from_command(cmd));
    EXPECT_EQ(delivered, 1u);
    EXPECT_EQ(received.type, "power.sleep");
    EXPECT_EQ(received.payload.get_int("delay_s"), 30);
}

TEST(MessageChannelCommand, CommandRoundTripsThroughQueue) {
    MessageChannel ch;
    Command cmd;
    cmd.id = "volume.set";
    cmd.args.set("level", 55);

    ch.send(Message::from_command(cmd));
    auto got = ch.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->type, "volume.set");
    EXPECT_EQ(got->payload.get_int("level"), 55);
}

// ---------------------------------------------------------------------------
// 契約版本
// ---------------------------------------------------------------------------

TEST(MessageChannelContract, VersionTag) {
    EXPECT_STREQ(ds::ipc::contract_version(), "e10_01/1.0.0");
}
