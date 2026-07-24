// E5-13 鍵盤與輸入提交事件 — gtest 測試
//
// 驗證兩條頻道（按鍵 / 文字提交）的訂閱與分派契約：
//   按鍵注入→收到（key/modifiers/action 正確）、修飾鍵遮罩組合與查詢、
//   便捷 press/release、文字提交（含空字串）、多訂閱者皆收、分派順序穩定、
//   取消訂閱後不再收、未知 / 無效 id 取消為 no-op、空 listener 拒收、
//   兩頻道彼此隔離（跨頻道不誤收）、回呼中取消自己安全。
// engine 層純邏輯：事件由測試手動注入，不含任何平台分支。
#include "keyboard_input.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::has_modifier;
using ds::events::Key;
using ds::events::KeyAction;
using ds::events::KeyboardInputSource;
using ds::events::KeyEvent;
using ds::events::Modifier;
using ds::events::Modifiers;
using ds::events::SubscriptionId;
using ds::events::TextInputEvent;

namespace {

// 訂閱按鍵→注入→收到：key / modifiers / action 皆正確傳遞。
TEST(KeyboardInput, SubscribeKeyThenInjectDelivers) {
    KeyboardInputSource src;
    std::vector<KeyEvent> received;
    const SubscriptionId id =
        src.subscribe_key([&](const KeyEvent& e) { received.push_back(e); });

    EXPECT_NE(id, 0u);
    EXPECT_EQ(src.key_listener_count(), 1u);

    src.inject_key(KeyEvent{Key::A, Modifier::Shift, KeyAction::Press});

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].key, Key::A);
    EXPECT_EQ(received[0].action, KeyAction::Press);
    EXPECT_TRUE(has_modifier(received[0].modifiers, Modifier::Shift));
    EXPECT_FALSE(has_modifier(received[0].modifiers, Modifier::Control));
}

// 便捷 press / release：inject_key_press / inject_key_release 產生正確 action。
TEST(KeyboardInput, ConveniencePressAndRelease) {
    KeyboardInputSource src;
    std::vector<KeyEvent> got;
    src.subscribe_key([&](const KeyEvent& e) { got.push_back(e); });

    src.inject_key_press(Key::Enter);
    src.inject_key_release(Key::Enter);

    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0].key, Key::Enter);
    EXPECT_EQ(got[0].action, KeyAction::Press);
    EXPECT_EQ(got[1].action, KeyAction::Release);
    // 未指定修飾鍵時預設為 None。
    EXPECT_FALSE(has_modifier(got[0].modifiers, Modifier::Shift));
    EXPECT_FALSE(has_modifier(got[0].modifiers, Modifier::Control));
    EXPECT_FALSE(has_modifier(got[0].modifiers, Modifier::Alt));
    EXPECT_FALSE(has_modifier(got[0].modifiers, Modifier::Meta));
}

// 修飾鍵遮罩：多個修飾鍵可位元或組合，has_modifier 精準查詢。
TEST(KeyboardInput, ModifierMaskCombination) {
    KeyboardInputSource src;
    KeyEvent last{Key::Unknown, Modifier::None, KeyAction::Press};
    src.subscribe_key([&](const KeyEvent& e) { last = e; });

    const Modifiers combo = Modifier::Control | Modifier::Meta;
    src.inject_key_press(Key::S, combo);  // 例如 Ctrl+Cmd+S

    EXPECT_TRUE(has_modifier(last.modifiers, Modifier::Control));
    EXPECT_TRUE(has_modifier(last.modifiers, Modifier::Meta));
    EXPECT_FALSE(has_modifier(last.modifiers, Modifier::Shift));
    EXPECT_FALSE(has_modifier(last.modifiers, Modifier::Alt));
    EXPECT_EQ(last.key, Key::S);
}

// 文字提交：submit_text 分派 TextInputEvent，text 正確。
TEST(KeyboardInput, SubmitTextDelivers) {
    KeyboardInputSource src;
    std::vector<std::string> texts;
    const SubscriptionId id = src.subscribe_text(
        [&](const TextInputEvent& e) { texts.push_back(e.text); });

    EXPECT_NE(id, 0u);
    EXPECT_EQ(src.text_listener_count(), 1u);

    src.submit_text("hello world");  // 例如就地輸入框 commit

    ASSERT_EQ(texts.size(), 1u);
    EXPECT_EQ(texts[0], std::string("hello world"));
}

// 空字串提交仍為有效事件（例如清空後 commit）。
TEST(KeyboardInput, SubmitEmptyTextIsValid) {
    KeyboardInputSource src;
    int hits = 0;
    std::string seen = "unset";
    src.subscribe_text([&](const TextInputEvent& e) {
        ++hits;
        seen = e.text;
    });

    src.inject_text(TextInputEvent{""});

    EXPECT_EQ(hits, 1);
    EXPECT_EQ(seen, std::string(""));
}

// 多按鍵訂閱者皆收，且依訂閱順序（SubscriptionId 遞增）分派。
TEST(KeyboardInput, MultipleKeySubscribersOrderedDelivery) {
    KeyboardInputSource src;
    std::vector<int> order;
    src.subscribe_key([&](const KeyEvent&) { order.push_back(1); });
    src.subscribe_key([&](const KeyEvent&) { order.push_back(2); });
    src.subscribe_key([&](const KeyEvent&) { order.push_back(3); });
    EXPECT_EQ(src.key_listener_count(), 3u);

    src.inject_key_press(Key::Space);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// 取消訂閱後不再收；其他 listener 不受影響。
TEST(KeyboardInput, UnsubscribeStopsDelivery) {
    KeyboardInputSource src;
    int kept = 0, dropped = 0;
    src.subscribe_key([&](const KeyEvent&) { ++kept; });
    const SubscriptionId drop_id =
        src.subscribe_key([&](const KeyEvent&) { ++dropped; });

    EXPECT_TRUE(src.unsubscribe(drop_id));
    EXPECT_EQ(src.key_listener_count(), 1u);

    src.inject_key_press(Key::Escape);
    EXPECT_EQ(kept, 1);
    EXPECT_EQ(dropped, 0);
}

// 未知 / 無效 id 取消為 no-op、回 false，不影響現有訂閱。
TEST(KeyboardInput, UnsubscribeUnknownIdIsNoOp) {
    KeyboardInputSource src;
    int hits = 0;
    src.subscribe_key([&](const KeyEvent&) { ++hits; });

    EXPECT_FALSE(src.unsubscribe(0));       // 無效 id
    EXPECT_FALSE(src.unsubscribe(999999));  // 未知 id
    EXPECT_EQ(src.key_listener_count(), 1u);

    src.inject_key_press(Key::Tab);
    EXPECT_EQ(hits, 1);
}

// 空 listener 為無效訂閱：兩頻道皆回 0、不佔用訂閱槽。
TEST(KeyboardInput, EmptyListenerRejected) {
    KeyboardInputSource src;
    EXPECT_EQ(src.subscribe_key(nullptr), 0u);
    EXPECT_EQ(src.subscribe_text(nullptr), 0u);
    EXPECT_EQ(src.key_listener_count(), 0u);
    EXPECT_EQ(src.text_listener_count(), 0u);
}

// 兩頻道彼此隔離：按鍵事件不會分派給文字訂閱者，反之亦然。
TEST(KeyboardInput, ChannelsAreIsolated) {
    KeyboardInputSource src;
    int key_hits = 0, text_hits = 0;
    src.subscribe_key([&](const KeyEvent&) { ++key_hits; });
    src.subscribe_text([&](const TextInputEvent&) { ++text_hits; });

    src.inject_key_press(Key::A);
    EXPECT_EQ(key_hits, 1);
    EXPECT_EQ(text_hits, 0);  // 文字頻道不收按鍵

    src.submit_text("x");
    EXPECT_EQ(key_hits, 1);   // 按鍵頻道不收文字
    EXPECT_EQ(text_hits, 1);
}

// 兩頻道共用 id 空間：unsubscribe 能正確作用於文字頻道，且 id 不與按鍵頻道相撞。
TEST(KeyboardInput, SharedIdSpaceUnsubscribeText) {
    KeyboardInputSource src;
    const SubscriptionId key_id = src.subscribe_key([](const KeyEvent&) {});
    const SubscriptionId text_id = src.subscribe_text([](const TextInputEvent&) {});

    EXPECT_NE(key_id, text_id);  // 共用遞增 id，彼此不同

    EXPECT_TRUE(src.unsubscribe(text_id));
    EXPECT_EQ(src.text_listener_count(), 0u);
    EXPECT_EQ(src.key_listener_count(), 1u);  // 按鍵頻道不受影響
}

// listener 於回呼中取消自己：不影響本輪分派（快照語意），下一輪不再收。
TEST(KeyboardInput, ListenerUnsubscribingDuringDispatchIsSafe) {
    KeyboardInputSource src;
    int self_hits = 0, other_hits = 0;
    SubscriptionId self_id = 0;
    self_id = src.subscribe_key([&](const KeyEvent&) {
        ++self_hits;
        src.unsubscribe(self_id);  // 回呼中取消自己
    });
    src.subscribe_key([&](const KeyEvent&) { ++other_hits; });

    src.inject_key_press(Key::ArrowDown);  // 本輪：兩者皆收
    src.inject_key_press(Key::ArrowUp);    // 下一輪：self 已取消

    EXPECT_EQ(self_hits, 1);
    EXPECT_EQ(other_hits, 2);
}

}  // namespace
