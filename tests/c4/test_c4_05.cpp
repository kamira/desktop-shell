// tests/c4/test_c4_05.cpp — C4-05 AI 對話面板 — gtest 單元測試
//
// 涵蓋：載入對話 / 附加多角色訊息、E4-01（草稿量測）+ E4-11（逐字顯示，含 E4-09 動畫驅動源
// 推進）顯示、submit_input（面板未開啟拒絕 / 空白 no-op / 注入回應 stub / 自訂回應器）、
// 捲動（依訊息序數，越界 / 空對話 / auto_scroll 跟進與解除、夾範圍）、E2-14 上下文指標
// （未注入回應 → 無讀值 / 注入後成功採集）、空對話查詢、越界例外、非法 UTF-8 不靜默、
// C1-05 生命週期（面板收起不清空既有對話）、to_string 具名字串。相位 1：純注入式，
// 無真實網路、無真實 LLM 呼叫。
#include "chat_panel.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "animation_driver.hpp"    // 上游 E4-09（透傳自 e4_11）
#include "heartbeat_source.hpp"    // 上游 E5-04（透傳自 e4_09）
#include "metric.hpp"              // 上游 E2-01（透傳自 e2_14）
#include "summon_panel_profile.hpp"  // 上游 C1-05
#include "text_layout.hpp"         // 上游 E4-01（透傳自 e4_11）

using ds::apps::ChatMessageView;
using ds::apps::ChatPanelApp;
using ds::apps::ConversationTurn;
using ds::apps::MessageRole;
using ds::apps::SubmitStatus;
using ds::apps::to_string;

using ds::events::HeartbeatSource;
using ds::events::NullGlobalHotkeys;
using ds::events::TimeoutTimer;

using ds::kernel::TransientProfileManager;

using ds::metrics::MetricRegistry;
using ds::metrics::MetricValue;

using ds::profiles::SummonPanelProfile;

using ds::render::AnimationDriver;
using ds::render::FixedFontMetrics;
using ds::render::LayoutConstraints;
using ds::render::LayoutResult;

using ds::sysinfo::HttpFetchProvider;
using ds::sysinfo::HttpResponse;
using ds::sysinfo::JsonPath;
using ds::sysinfo::NullHttpTransport;
using ds::sysinfo::PathSeg;

namespace {

// 等寬度量：advance=10、行高=20、ascent=16（承 E4-01 / E4-11 測試慣例）。
FixedFontMetrics mono() { return FixedFontMetrics(10.0, 20.0, 16.0); }

class ChatPanelAppTest : public ::testing::Test {
protected:
    TimeoutTimer timer;
    TransientProfileManager lifecycle{timer};
    NullGlobalHotkeys hotkeys{true};
    SummonPanelProfile base{"panel.chat", lifecycle, hotkeys};

    FixedFontMetrics metrics = mono();

    std::shared_ptr<NullHttpTransport> transport = std::make_shared<NullHttpTransport>();
    HttpFetchProvider context_provider{transport,
                                       "chat.context.tokens",
                                       "Context Tokens",
                                       "https://example.invalid/context",
                                       JsonPath{PathSeg::field("tokens")}};
    MetricRegistry registry;

    ChatPanelAppTest() { registry.add_provider(context_provider); }
};

}  // namespace

// ===========================================================================
// 組裝正確 / 空對話
// ===========================================================================

TEST_F(ChatPanelAppTest, ConstructedEmptyAndPanelClosed) {
    ChatPanelApp panel(base, metrics, context_provider);
    EXPECT_TRUE(panel.empty());
    EXPECT_EQ(panel.message_count(), 0u);
    EXPECT_FALSE(panel.is_open());
    EXPECT_EQ(panel.scroll_index(), 0u);
    EXPECT_TRUE(panel.auto_scroll());
}

TEST_F(ChatPanelAppTest, EmptyConversationMessageAtThrows) {
    ChatPanelApp panel(base, metrics, context_provider);
    EXPECT_THROW(panel.message_at(0), std::out_of_range);
    EXPECT_THROW(panel.render_message(0), std::out_of_range);
}

TEST_F(ChatPanelAppTest, EmptyConversationScrollFails) {
    ChatPanelApp panel(base, metrics, context_provider);
    EXPECT_FALSE(panel.scroll_to(0));
    EXPECT_FALSE(panel.scroll_by(1));
    panel.scroll_to_latest();  // 空對話恆安全：索引維持 0。
    EXPECT_EQ(panel.scroll_index(), 0u);
    EXPECT_TRUE(panel.auto_scroll());
}

// ===========================================================================
// load_conversation：批次載入多角色訊息
// ===========================================================================

TEST_F(ChatPanelAppTest, LoadConversationPopulatesMessagesAndScrollsToLatest) {
    ChatPanelApp panel(base, metrics, context_provider);
    std::vector<ConversationTurn> turns = {
        {MessageRole::System, "歡迎使用"},
        {MessageRole::User, "hello"},
        {MessageRole::Assistant, "hi there"},
    };
    panel.load_conversation(turns);

    ASSERT_EQ(panel.message_count(), 3u);
    EXPECT_FALSE(panel.empty());

    ChatMessageView v0 = panel.message_at(0);
    EXPECT_EQ(v0.role, MessageRole::System);
    EXPECT_EQ(v0.text, "歡迎使用");
    EXPECT_EQ(v0.visible_count, 0u);  // 剛載入：尚未推進，逐字顯示從 0 開始。
    EXPECT_FALSE(v0.reveal_complete);

    ChatMessageView v1 = panel.message_at(1);
    EXPECT_EQ(v1.role, MessageRole::User);
    EXPECT_EQ(v1.text, "hello");

    ChatMessageView v2 = panel.message_at(2);
    EXPECT_EQ(v2.role, MessageRole::Assistant);
    EXPECT_EQ(v2.text, "hi there");

    // 載入後捲動歸底（最新一則）。
    EXPECT_EQ(panel.scroll_index(), 2u);
    EXPECT_TRUE(panel.auto_scroll());
}

TEST_F(ChatPanelAppTest, LoadConversationReplacesExistingMessages) {
    ChatPanelApp panel(base, metrics, context_provider);
    panel.load_conversation({{MessageRole::User, "first"}});
    ASSERT_EQ(panel.message_count(), 1u);

    panel.load_conversation({{MessageRole::User, "a"}, {MessageRole::Assistant, "b"}});
    ASSERT_EQ(panel.message_count(), 2u);
    EXPECT_EQ(panel.message_at(0).text, "a");
    EXPECT_EQ(panel.message_at(1).text, "b");
}

TEST_F(ChatPanelAppTest, LoadConversationWithInvalidUtf8Throws) {
    ChatPanelApp panel(base, metrics, context_provider);
    std::vector<ConversationTurn> bad = {{MessageRole::User, std::string("\xFF")}};
    EXPECT_THROW(panel.load_conversation(bad), std::invalid_argument);
}

// ===========================================================================
// append_message：附加訊息 + auto_scroll 跟進 / 解除
// ===========================================================================

TEST_F(ChatPanelAppTest, AppendMessageFollowsAutoScrollUntilUserScrollsAway) {
    ChatPanelApp panel(base, metrics, context_provider);
    panel.load_conversation({{MessageRole::User, "one"}, {MessageRole::Assistant, "two"}});
    ASSERT_EQ(panel.scroll_index(), 1u);

    panel.append_message(MessageRole::User, "three");
    EXPECT_EQ(panel.message_count(), 3u);
    EXPECT_EQ(panel.scroll_index(), 2u);  // auto_scroll 跟進到新訊息。
    EXPECT_TRUE(panel.auto_scroll());

    ASSERT_TRUE(panel.scroll_to(0));  // 使用者往回捲：解除 auto_scroll。
    EXPECT_FALSE(panel.auto_scroll());

    panel.append_message(MessageRole::Assistant, "four");
    EXPECT_EQ(panel.message_count(), 4u);
    EXPECT_EQ(panel.scroll_index(), 0u);  // 未跟進：保留使用者目前位置。

    panel.scroll_to_latest();
    EXPECT_EQ(panel.scroll_index(), 3u);
    EXPECT_TRUE(panel.auto_scroll());
}

TEST_F(ChatPanelAppTest, AppendMessageWithInvalidUtf8Throws) {
    ChatPanelApp panel(base, metrics, context_provider);
    EXPECT_THROW(panel.append_message(MessageRole::User, std::string("\xC0")),
                std::invalid_argument);
    EXPECT_TRUE(panel.empty());  // 失敗不留半份訊息。
}

// ===========================================================================
// submit_input：面板生命週期（C1-05）+ 注入式回應 stub
// ===========================================================================

TEST_F(ChatPanelAppTest, SubmitInputRejectedWhenPanelClosed) {
    ChatPanelApp panel(base, metrics, context_provider);
    ASSERT_FALSE(panel.is_open());
    EXPECT_EQ(panel.submit_input("hi"), SubmitStatus::PanelClosed);
    EXPECT_TRUE(panel.empty());
}

TEST_F(ChatPanelAppTest, SubmitInputAppendsUserAndStubAssistantReply) {
    ChatPanelApp panel(base, metrics, context_provider);
    ASSERT_TRUE(panel.open(50));
    ASSERT_TRUE(panel.is_open());

    EXPECT_EQ(panel.submit_input("hello"), SubmitStatus::Ok);
    ASSERT_EQ(panel.message_count(), 2u);

    ChatMessageView user = panel.message_at(0);
    EXPECT_EQ(user.role, MessageRole::User);
    EXPECT_EQ(user.text, "hello");

    ChatMessageView reply = panel.message_at(1);
    EXPECT_EQ(reply.role, MessageRole::Assistant);
    EXPECT_NE(reply.text.find("hello"), std::string::npos);  // stub 回應含回聲原文。
    EXPECT_NE(reply.text.find("stub"), std::string::npos);   // 明確標示為注入 stub，非真實 LLM。
}

TEST_F(ChatPanelAppTest, SubmitInputWhitespaceOnlyIsEmptyNoOp) {
    ChatPanelApp panel(base, metrics, context_provider);
    ASSERT_TRUE(panel.open(50));
    EXPECT_EQ(panel.submit_input("   \t\n  "), SubmitStatus::Empty);
    EXPECT_TRUE(panel.empty());
}

TEST_F(ChatPanelAppTest, SubmitInputUsesCustomInjectedResponder) {
    ChatPanelApp panel(base, metrics, context_provider);
    panel.set_responder([](const std::string& text, std::size_t turn) {
        return "echo(" + std::to_string(turn) + "):" + text;
    });
    ASSERT_TRUE(panel.open(50));

    ASSERT_EQ(panel.submit_input("a"), SubmitStatus::Ok);
    EXPECT_EQ(panel.message_at(1).text, "echo(0):a");

    ASSERT_EQ(panel.submit_input("b"), SubmitStatus::Ok);
    EXPECT_EQ(panel.message_at(3).text, "echo(1):b");  // turn_index 逐輪遞增。
}

TEST_F(ChatPanelAppTest, PanelCloseDoesNotClearConversationHistory) {
    ChatPanelApp panel(base, metrics, context_provider);
    ASSERT_TRUE(panel.open(50));
    ASSERT_EQ(panel.submit_input("x"), SubmitStatus::Ok);
    ASSERT_EQ(panel.message_count(), 2u);

    ASSERT_TRUE(panel.close());
    EXPECT_FALSE(panel.is_open());
    EXPECT_EQ(panel.message_count(), 2u);  // 對話資料獨立於面板顯隱，不被清空。

    EXPECT_EQ(panel.submit_input("y"), SubmitStatus::PanelClosed);
    EXPECT_EQ(panel.message_count(), 2u);
}

// ===========================================================================
// 捲動：越界 / 夾範圍
// ===========================================================================

TEST_F(ChatPanelAppTest, ScrollToOutOfRangeReturnsFalseAndKeepsState) {
    ChatPanelApp panel(base, metrics, context_provider);
    panel.load_conversation({{MessageRole::User, "a"}, {MessageRole::Assistant, "b"}});
    ASSERT_EQ(panel.scroll_index(), 1u);

    EXPECT_FALSE(panel.scroll_to(2));  // 越界（僅 0..1 合法）。
    EXPECT_EQ(panel.scroll_index(), 1u);  // 不動既有狀態。
}

TEST_F(ChatPanelAppTest, ScrollByClampsToValidRange) {
    ChatPanelApp panel(base, metrics, context_provider);
    panel.load_conversation({{MessageRole::User, "a"},
                             {MessageRole::Assistant, "b"},
                             {MessageRole::User, "c"}});
    ASSERT_EQ(panel.scroll_index(), 2u);

    EXPECT_TRUE(panel.scroll_by(-10));
    EXPECT_EQ(panel.scroll_index(), 0u);
    EXPECT_FALSE(panel.auto_scroll());

    EXPECT_TRUE(panel.scroll_by(100));
    EXPECT_EQ(panel.scroll_index(), 2u);
    EXPECT_TRUE(panel.auto_scroll());  // 回到最新一則：auto_scroll 重新開啟。
}

// ===========================================================================
// E4-01（草稿量測）+ E4-11（逐字顯示，含 E4-09 動畫驅動源推進）
// ===========================================================================

TEST_F(ChatPanelAppTest, MeasureDraftUsesTextLayoutDirectly) {
    ChatPanelApp panel(base, metrics, context_provider);
    ds::render::Size size = panel.measure_draft("abc");  // 3 碼位 * advance 10 = 30；行高 20。
    EXPECT_DOUBLE_EQ(size.width, 30.0);
    EXPECT_DOUBLE_EQ(size.height, 20.0);
}

TEST_F(ChatPanelAppTest, MeasureDraftEmptyIsZeroSize) {
    ChatPanelApp panel(base, metrics, context_provider);
    ds::render::Size size = panel.measure_draft("");
    EXPECT_DOUBLE_EQ(size.width, 0.0);
    EXPECT_DOUBLE_EQ(size.height, 0.0);
}

TEST_F(ChatPanelAppTest, RenderMessageRevealsIncrementallyViaAdvance) {
    ChatPanelApp panel(base, metrics, context_provider);
    panel.load_conversation({{MessageRole::Assistant, "hi"}});  // 2 碼位。

    LayoutResult before = panel.render_message(0);
    EXPECT_TRUE(before.glyphs.empty());  // 尚未推進：逐字顯示從 0 開始。

    panel.advance(1);  // 預設速度 1 char/tick。
    EXPECT_EQ(panel.message_at(0).visible_count, 1u);
    LayoutResult mid = panel.render_message(0);
    EXPECT_EQ(mid.glyphs.size(), 1u);

    panel.advance(5);  // 超量推進：安全夾在總字數。
    EXPECT_TRUE(panel.message_at(0).reveal_complete);
    LayoutResult done = panel.render_message(0);
    EXPECT_EQ(done.glyphs.size(), 2u);
}

TEST_F(ChatPanelAppTest, AttachBindsAnimationDriverAndAutoAdvancesOnHeartbeat) {
    HeartbeatSource hb;
    AnimationDriver driver(hb, /*pulse_interval=*/1);

    ChatPanelApp panel(base, metrics, context_provider);
    panel.load_conversation({{MessageRole::User, "ab"}});
    panel.attach(driver);  // 綁定既有訊息。

    EXPECT_EQ(panel.message_at(0).visible_count, 0u);
    hb.tick();  // 一次心跳脈衝 → 驅動源餵 dt=1 給已綁動畫。
    EXPECT_EQ(panel.message_at(0).visible_count, 1u);

    // 之後新附加的訊息也自動一併掛上。
    panel.append_message(MessageRole::Assistant, "c");
    EXPECT_EQ(panel.message_at(1).visible_count, 0u);
    hb.tick();
    EXPECT_EQ(panel.message_at(1).visible_count, 1u);
}

// ===========================================================================
// E2-14 上下文指標
// ===========================================================================

TEST_F(ChatPanelAppTest, ContextValueUnknownBeforeAnySuccessfulSample) {
    ChatPanelApp panel(base, metrics, context_provider);
    MetricValue v = panel.context_value();
    EXPECT_FALSE(v.valid);
}

TEST_F(ChatPanelAppTest, RefreshContextWithoutInjectedResponseFails) {
    ChatPanelApp panel(base, metrics, context_provider);
    EXPECT_FALSE(panel.refresh_context());  // null 傳輸未注入回應 → 無讀值（status==0）。
    EXPECT_FALSE(panel.context_value().valid);
}

TEST_F(ChatPanelAppTest, RefreshContextWithInjectedResponseSucceeds) {
    transport->set_response("https://example.invalid/context",
                            HttpResponse::ok("{\"tokens\": 42}"));
    ChatPanelApp panel(base, metrics, context_provider);

    EXPECT_TRUE(panel.refresh_context());
    MetricValue v = panel.context_value();
    ASSERT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 42.0);
}

// ===========================================================================
// 具名字串（NFR-02）
// ===========================================================================

TEST(ChatPanelToString, MessageRoleNames) {
    EXPECT_STREQ(to_string(MessageRole::User), "User");
    EXPECT_STREQ(to_string(MessageRole::Assistant), "Assistant");
    EXPECT_STREQ(to_string(MessageRole::System), "System");
}

TEST(ChatPanelToString, SubmitStatusNames) {
    EXPECT_STREQ(to_string(SubmitStatus::Ok), "Ok");
    EXPECT_STREQ(to_string(SubmitStatus::Empty), "Empty");
    EXPECT_STREQ(to_string(SubmitStatus::PanelClosed), "PanelClosed");
}
