// E10-02 低延遲同機通道 — gtest 契約測試。
//
// 覆蓋：低延遲送收（邏輯延遲量測）、優先佇列（高優先先出 / 同級 FIFO）、
// 背壓（拒絕 / 汰換最舊低優先 / 部分批次接受）、批次收送、請求-回應、
// 可注入時脈、FIFO 順序、與 E10-01 / E6-01 整合、契約版本標記。
#include "low_latency_channel.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

using ds::ipc::BackpressurePolicy;
using ds::ipc::Envelope;
using ds::ipc::LogicalClock;
using ds::ipc::LowLatencyChannel;
using ds::ipc::Message;
using ds::ipc::MessageChannel;
using ds::ipc::Priority;
using ds::ipc::SendStatus;
using ds::command::Command;
using ds::command::CommandArgs;

// ---------------------------------------------------------------------------
// 低延遲送收 + 邏輯延遲量測
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelBasic, SendThenReceiveRoundTrip) {
    LowLatencyChannel ch;
    EXPECT_TRUE(ch.empty());
    EXPECT_EQ(ch.size(), 0u);

    EXPECT_EQ(ch.send(Message{"tick"}), SendStatus::Accepted);
    EXPECT_FALSE(ch.empty());
    EXPECT_EQ(ch.size(), 1u);

    auto got = ch.try_receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->message.type, "tick");
    EXPECT_TRUE(ch.empty());
}

TEST(LowLatencyChannelBasic, ReceiveEmptyReturnsNulloptNoCrash) {
    LowLatencyChannel ch;
    auto got = ch.try_receive();
    EXPECT_FALSE(got.has_value());
}

TEST(LowLatencyChannelBasic, LatencyMeasuredInLogicalTicks) {
    LogicalClock clk;
    LowLatencyChannel ch(&clk);

    ch.send(Message{"a"});          // enqueued at tick 0
    clk.advance(5);                 // 邏輯上經過 5 tick
    auto got = ch.try_receive();    // dequeued at tick 5
    ASSERT_TRUE(got.has_value());

    EXPECT_EQ(ch.latency().last, 5u);
    EXPECT_EQ(ch.latency().max, 5u);
    EXPECT_EQ(ch.latency().delivered, 1u);
    EXPECT_DOUBLE_EQ(ch.latency().average(), 5.0);
}

TEST(LowLatencyChannelBasic, LatencyMaxAndAverageAccumulate) {
    LogicalClock clk;
    LowLatencyChannel ch(&clk);

    ch.send(Message{"a"});          // tick 0
    clk.advance(2);
    ch.try_receive();               // lat 2
    ch.send(Message{"b"});          // tick 2
    clk.advance(8);
    ch.try_receive();               // lat 8

    EXPECT_EQ(ch.latency().last, 8u);
    EXPECT_EQ(ch.latency().max, 8u);
    EXPECT_EQ(ch.latency().delivered, 2u);
    EXPECT_DOUBLE_EQ(ch.latency().average(), 5.0);  // (2+8)/2
}

TEST(LowLatencyChannelBasic, InternalClockUsedWhenNoneInjected) {
    LowLatencyChannel ch;              // 無注入 → 內部時脈
    ch.send(Message{"a"});
    ch.clock().advance(3);
    auto got = ch.try_receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(ch.latency().last, 3u);
}

// ---------------------------------------------------------------------------
// 優先佇列：高優先先出、同優先級 FIFO
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelPriority, HigherPriorityDeliveredFirst) {
    LowLatencyChannel ch;
    ch.send(Message{"low"}, Priority::Low);
    ch.send(Message{"normal"}, Priority::Normal);
    ch.send(Message{"high"}, Priority::High);

    std::vector<std::string> order;
    while (auto e = ch.try_receive()) order.push_back(e->message.type);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "high");
    EXPECT_EQ(order[1], "normal");
    EXPECT_EQ(order[2], "low");
}

TEST(LowLatencyChannelPriority, FifoWithinSamePriority) {
    LowLatencyChannel ch;
    ch.send(Message{"a"}, Priority::Normal);
    ch.send(Message{"b"}, Priority::Normal);
    ch.send(Message{"c"}, Priority::Normal);

    std::vector<std::string> order;
    while (auto e = ch.try_receive()) order.push_back(e->message.type);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "a");
    EXPECT_EQ(order[1], "b");
    EXPECT_EQ(order[2], "c");
}

TEST(LowLatencyChannelPriority, HighPriorityInsertedLateStillJumpsQueue) {
    LowLatencyChannel ch;
    ch.send(Message{"n1"}, Priority::Normal);
    ch.send(Message{"n2"}, Priority::Normal);
    ch.send(Message{"urgent"}, Priority::High);  // 後到但插隊

    auto first = ch.try_receive();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->message.type, "urgent");
}

TEST(LowLatencyChannelPriority, PerPrioritySizeCounts) {
    LowLatencyChannel ch;
    ch.send(Message{"a"}, Priority::High);
    ch.send(Message{"b"}, Priority::High);
    ch.send(Message{"c"}, Priority::Low);

    EXPECT_EQ(ch.size(Priority::High), 2u);
    EXPECT_EQ(ch.size(Priority::Normal), 0u);
    EXPECT_EQ(ch.size(Priority::Low), 1u);
    EXPECT_EQ(ch.size(), 3u);
}

// ---------------------------------------------------------------------------
// 背壓：Reject 策略
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelBackpressure, RejectWhenFull) {
    LowLatencyChannel ch(nullptr, /*capacity=*/2, BackpressurePolicy::Reject);
    EXPECT_TRUE(ch.bounded());
    EXPECT_EQ(ch.capacity(), 2u);

    EXPECT_EQ(ch.send(Message{"a"}), SendStatus::Accepted);
    EXPECT_EQ(ch.send(Message{"b"}), SendStatus::Accepted);
    EXPECT_TRUE(ch.is_full());

    EXPECT_EQ(ch.send(Message{"c"}), SendStatus::RejectedFull);  // 背壓
    EXPECT_EQ(ch.size(), 2u);
    EXPECT_EQ(ch.rejected_count(), 1u);

    // 收掉一則後又可再送
    ch.try_receive();
    EXPECT_FALSE(ch.is_full());
    EXPECT_EQ(ch.send(Message{"d"}), SendStatus::Accepted);
}

TEST(LowLatencyChannelBackpressure, UnboundedNeverFull) {
    LowLatencyChannel ch;  // capacity 0 = 無界
    EXPECT_FALSE(ch.bounded());
    for (int i = 0; i < 1000; ++i) ch.send(Message{"x"});
    EXPECT_FALSE(ch.is_full());
    EXPECT_EQ(ch.size(), 1000u);
    EXPECT_EQ(ch.rejected_count(), 0u);
}

// ---------------------------------------------------------------------------
// 背壓：DropOldestLowPriority 策略
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelBackpressure, DropOldestLowPriorityEvictsToAdmit) {
    LowLatencyChannel ch(nullptr, 2, BackpressurePolicy::DropOldestLowPriority);
    ch.send(Message{"lo1"}, Priority::Low);
    ch.send(Message{"lo2"}, Priority::Low);
    EXPECT_TRUE(ch.is_full());

    // 新的 Normal（>= Low）進來：汰換最舊的 Low（lo1），入列成功。
    EXPECT_EQ(ch.send(Message{"norm"}, Priority::Normal), SendStatus::AcceptedEvicted);
    EXPECT_EQ(ch.evicted_count(), 1u);
    EXPECT_EQ(ch.size(), 2u);

    // 剩下應為 lo2（Low）與 norm（Normal）；Normal 先出。
    auto a = ch.try_receive();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->message.type, "norm");
    auto b = ch.try_receive();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->message.type, "lo2");
}

TEST(LowLatencyChannelBackpressure, DropPolicyProtectsHigherPriority) {
    LowLatencyChannel ch(nullptr, 2, BackpressurePolicy::DropOldestLowPriority);
    ch.send(Message{"h1"}, Priority::High);
    ch.send(Message{"h2"}, Priority::High);
    EXPECT_TRUE(ch.is_full());

    // 新的 Low 進來：佇列內全部嚴格高於 Low → 不汰換高優先，改為拒收新訊息。
    EXPECT_EQ(ch.send(Message{"lo"}, Priority::Low), SendStatus::RejectedFull);
    EXPECT_EQ(ch.evicted_count(), 0u);
    EXPECT_EQ(ch.rejected_count(), 1u);
    EXPECT_EQ(ch.size(), 2u);
}

// ---------------------------------------------------------------------------
// 批次收送
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelBatch, SendBatchAllAccepted) {
    LowLatencyChannel ch;
    std::vector<Envelope> batch;
    batch.emplace_back(Message{"a"}, Priority::Normal);
    batch.emplace_back(Message{"b"}, Priority::Normal);
    batch.emplace_back(Message{"c"}, Priority::Normal);

    EXPECT_EQ(ch.send_batch(std::move(batch)), 3u);
    EXPECT_EQ(ch.size(), 3u);
}

TEST(LowLatencyChannelBatch, SendBatchPartialAcceptUnderBackpressure) {
    LowLatencyChannel ch(nullptr, 2, BackpressurePolicy::Reject);
    std::vector<Envelope> batch;
    batch.emplace_back(Message{"a"}, Priority::Normal);
    batch.emplace_back(Message{"b"}, Priority::Normal);
    batch.emplace_back(Message{"c"}, Priority::Normal);  // 將被背壓擋下

    EXPECT_EQ(ch.send_batch(std::move(batch)), 2u);  // 部分接受
    EXPECT_EQ(ch.size(), 2u);
    EXPECT_EQ(ch.rejected_count(), 1u);
}

TEST(LowLatencyChannelBatch, ReceiveBatchRespectsPriorityAndMax) {
    LowLatencyChannel ch;
    ch.send(Message{"lo"}, Priority::Low);
    ch.send(Message{"hi1"}, Priority::High);
    ch.send(Message{"hi2"}, Priority::High);

    auto out = ch.receive_batch(2);  // 取 2 則，最高優先優先
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].message.type, "hi1");
    EXPECT_EQ(out[1].message.type, "hi2");
    EXPECT_EQ(ch.size(), 1u);  // 剩 lo
}

TEST(LowLatencyChannelBatch, ReceiveBatchDrainsWhenMaxExceedsSize) {
    LowLatencyChannel ch;
    ch.send(Message{"a"});
    ch.send(Message{"b"});
    auto out = ch.receive_batch(10);
    EXPECT_EQ(out.size(), 2u);
    EXPECT_TRUE(ch.empty());
}

// ---------------------------------------------------------------------------
// 請求-回應（快速同機 RPC）
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelRequestResponse, RespondsWithComputedMessage) {
    LowLatencyChannel ch;
    Message req{"echo"};
    req.payload.set("value", 21);

    auto resp = ch.request_response(req, [](const Message& m) {
        Message out{"echo.reply"};
        out.payload.set("value", *m.payload.get_int("value") * 2);
        return out;
    });

    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->type, "echo.reply");
    EXPECT_EQ(resp->payload.get_int("value"), 42);
    EXPECT_EQ(ch.rr_delivered(), 1u);
}

TEST(LowLatencyChannelRequestResponse, NullResponderReturnsNullopt) {
    LowLatencyChannel ch;
    ds::ipc::Responder none;  // 空
    auto resp = ch.request_response(Message{"x"}, none);
    EXPECT_FALSE(resp.has_value());
    EXPECT_EQ(ch.rr_rejected(), 1u);
    EXPECT_EQ(ch.rr_delivered(), 0u);
}

TEST(LowLatencyChannelRequestResponse, RoundTripLatencyMeasuredLogically) {
    LogicalClock clk;
    LowLatencyChannel ch(&clk);

    auto resp = ch.request_response(Message{"slow"}, [&](const Message&) {
        clk.advance(7);  // 回應器內模擬 7 tick 的處理 / 傳輸耗時
        return Message{"done"};
    });

    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(ch.latency().last, 7u);
    EXPECT_EQ(ch.latency().delivered, 1u);
}

// ---------------------------------------------------------------------------
// 與 E10-01 / E6-01 整合
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelIntegration, CarriesE6CommandPayload) {
    LowLatencyChannel ch;
    Command cmd;
    cmd.id = "volume.set";
    cmd.args.set("level", 55).set("mute", false);

    ch.send(Message::from_command(cmd), Priority::High);
    auto got = ch.try_receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->message.type, "volume.set");
    EXPECT_EQ(got->message.payload.get_int("level"), 55);
    EXPECT_EQ(got->message.payload.get_bool("mute"), false);
}

TEST(LowLatencyChannelIntegration, DrainsIntoE10_01MessageChannel) {
    // 低延遲通道（優先排序）之輸出可餵入 E10-01 通道的發布訂閱做後續路由。
    LowLatencyChannel low;
    low.send(Message{"lo"}, Priority::Low);
    low.send(Message{"hi"}, Priority::High);

    MessageChannel bus;  // E10-01
    std::vector<std::string> seen;
    bus.subscribe_all([&](const Message& m) { seen.push_back(m.type); });

    while (auto e = low.try_receive()) bus.publish(e->message);

    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], "hi");  // 高優先先出、先發布
    EXPECT_EQ(seen[1], "lo");
}

TEST(LowLatencyChannelIntegration, EnvelopePreservesInjectedPriority) {
    LowLatencyChannel ch;
    ch.send(Envelope{Message{"x"}, Priority::High});
    auto got = ch.try_receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->priority, Priority::High);
    EXPECT_NE(got->seq, 0u);  // 通道已蓋章序號
}

// ---------------------------------------------------------------------------
// 契約版本
// ---------------------------------------------------------------------------

TEST(LowLatencyChannelContract, VersionTag) {
    EXPECT_STREQ(ds::ipc::low_latency_contract_version(), "e10_02/1.0.0");
}
