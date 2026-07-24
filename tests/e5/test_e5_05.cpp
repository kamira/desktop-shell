// E5-05 全域熱鍵註冊 — 契約測試（gtest）
//
// 驗證 null 後端的能力閘控與分派契約（相位 2 真實後端須遵守同一契約）：
//   has() 反映可用性；不可用時 register 拒絕（NFR-03 閘控）；
//   註冊→注入→觸發；unregister 後不再觸發；重複熱鍵衝突拒絕；
//   無效熱鍵 / 空回呼拒絕；未知 id 解除為 no-op；不同熱鍵各自獨立分派；
//   修飾鍵組合為熱鍵身分一部分；回呼中解除自己安全。
// 相位 1：只驗介面 + null（手動注入）行為，不含任何平台分支。
#include "global_hotkey.hpp"

#include <gtest/gtest.h>

#include <vector>

using ds::events::GlobalHotkeys;
using ds::events::Hotkey;
using ds::events::HotkeyId;
using ds::events::Key;
using ds::events::Modifier;
using ds::events::NullGlobalHotkeys;

namespace {

// 慣用建構子：以具名修飾鍵 + 具名鍵組出一個熱鍵。
Hotkey hk(Modifier mods, Key key) { return Hotkey{mods, key}; }

// has() 反映建構時宣告的可用性。
TEST(GlobalHotkey, HasReflectsAvailability) {
    NullGlobalHotkeys unavailable;                 // 預設不可用（對映能力矩陣 default）
    NullGlobalHotkeys available(/*available=*/true);
    EXPECT_FALSE(unavailable.has());
    EXPECT_TRUE(available.has());
}

// NFR-03 閘控：能力不存在時 register 一律回 0、不佔用註冊槽。
TEST(GlobalHotkey, RegisterRejectedWhenUnavailable) {
    NullGlobalHotkeys gh;  // 不可用
    const HotkeyId id = gh.register_hotkey(
        hk(Modifier::Control | Modifier::Shift, Key::K), [](const Hotkey&) {});
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(gh.count(), 0u);
}

// has()-先閘控的呼叫端樣式：has() 為真才註冊，回呼確實可觸發。
TEST(GlobalHotkey, GatedCallerPatternRegistersAndFires) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey combo = hk(Modifier::Meta | Modifier::Alt, Key::Space);

    int fired = 0;
    HotkeyId id = 0;
    if (gh.has()) {  // NFR-03：先閘控
        id = gh.register_hotkey(combo, [&](const Hotkey&) { ++fired; });
    }
    EXPECT_NE(id, 0u);
    EXPECT_EQ(gh.count(), 1u);

    gh.inject(combo);  // 模擬按下
    EXPECT_EQ(fired, 1);
}

// 註冊→注入→回呼收到，且帶入的熱鍵正確。
TEST(GlobalHotkey, RegisterThenInjectDelivers) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey combo = hk(Modifier::Control, Key::F5);

    std::vector<Hotkey> got;
    const HotkeyId id =
        gh.register_hotkey(combo, [&](const Hotkey& h) { got.push_back(h); });
    EXPECT_NE(id, 0u);

    gh.inject(combo);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_TRUE(got[0] == combo);
    EXPECT_EQ(got[0].key, Key::F5);
    EXPECT_TRUE(ds::events::has_modifier(got[0].modifiers, Modifier::Control));
}

// unregister 後注入不再觸發。
TEST(GlobalHotkey, UnregisterStopsDispatch) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey combo = hk(Modifier::Control | Modifier::Alt, Key::Delete);

    int fired = 0;
    const HotkeyId id =
        gh.register_hotkey(combo, [&](const Hotkey&) { ++fired; });
    ASSERT_NE(id, 0u);

    EXPECT_TRUE(gh.unregister(id));
    EXPECT_EQ(gh.count(), 0u);
    EXPECT_FALSE(gh.is_registered(combo));

    gh.inject(combo);
    EXPECT_EQ(fired, 0);  // 已解除，不再觸發
}

// 重複熱鍵：獨佔性——第二筆註冊衝突回 0，第一筆維持有效。
TEST(GlobalHotkey, DuplicateHotkeyRejected) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey combo = hk(Modifier::Meta, Key::Q);

    int first = 0, second = 0;
    const HotkeyId id1 =
        gh.register_hotkey(combo, [&](const Hotkey&) { ++first; });
    const HotkeyId id2 =
        gh.register_hotkey(combo, [&](const Hotkey&) { ++second; });

    EXPECT_NE(id1, 0u);
    EXPECT_EQ(id2, 0u);        // 衝突，被拒
    EXPECT_EQ(gh.count(), 1u);  // 仍只有第一筆

    gh.inject(combo);
    EXPECT_EQ(first, 1);   // 觸發第一筆
    EXPECT_EQ(second, 0);  // 第二筆從未註冊
}

// 無效熱鍵（key == Unknown）不可註冊。
TEST(GlobalHotkey, InvalidHotkeyRejected) {
    NullGlobalHotkeys gh(/*available=*/true);
    const HotkeyId id =
        gh.register_hotkey(hk(Modifier::Control, Key::Unknown),
                           [](const Hotkey&) {});
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(gh.count(), 0u);
}

// 空回呼為無效註冊。
TEST(GlobalHotkey, EmptyCallbackRejected) {
    NullGlobalHotkeys gh(/*available=*/true);
    const HotkeyId id = gh.register_hotkey(hk(Modifier::Control, Key::A),
                                           nullptr);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(gh.count(), 0u);
}

// 未知 id（含 0）解除為 no-op、回傳 false，不影響現有註冊。
TEST(GlobalHotkey, UnregisterUnknownIdIsNoOp) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey combo = hk(Modifier::Shift, Key::F1);
    const HotkeyId id = gh.register_hotkey(combo, [](const Hotkey&) {});
    ASSERT_NE(id, 0u);

    EXPECT_FALSE(gh.unregister(0));       // 無效 id
    EXPECT_FALSE(gh.unregister(999999));  // 未知 id
    EXPECT_EQ(gh.count(), 1u);
    EXPECT_TRUE(gh.is_registered(combo));
}

// 注入未註冊的熱鍵為 no-op、不崩潰。
TEST(GlobalHotkey, InjectUnregisteredHotkeyIsNoOp) {
    NullGlobalHotkeys gh(/*available=*/true);
    int fired = 0;
    gh.register_hotkey(hk(Modifier::Control, Key::C),
                       [&](const Hotkey&) { ++fired; });
    gh.inject(hk(Modifier::Control, Key::V));  // 不同熱鍵，未註冊
    EXPECT_EQ(fired, 0);
}

// 不同熱鍵各自獨立：注入其一只觸發其一。
TEST(GlobalHotkey, DistinctHotkeysDispatchIndependently) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey a = hk(Modifier::Control, Key::Num1);
    const Hotkey b = hk(Modifier::Control, Key::Num2);

    int hitA = 0, hitB = 0;
    gh.register_hotkey(a, [&](const Hotkey&) { ++hitA; });
    gh.register_hotkey(b, [&](const Hotkey&) { ++hitB; });
    EXPECT_EQ(gh.count(), 2u);

    gh.inject(a);
    EXPECT_EQ(hitA, 1);
    EXPECT_EQ(hitB, 0);

    gh.inject(b);
    EXPECT_EQ(hitA, 1);
    EXPECT_EQ(hitB, 1);
}

// 修飾鍵組合是熱鍵身分一部分：Ctrl+K 與 Ctrl+Shift+K 為不同熱鍵。
TEST(GlobalHotkey, ModifierCombinationDistinguishesHotkeys) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey ctrlK = hk(Modifier::Control, Key::K);
    const Hotkey ctrlShiftK = hk(Modifier::Control | Modifier::Shift, Key::K);
    EXPECT_TRUE(ctrlK != ctrlShiftK);

    int plain = 0, withShift = 0;
    const HotkeyId id1 = gh.register_hotkey(ctrlK, [&](const Hotkey&) { ++plain; });
    const HotkeyId id2 =
        gh.register_hotkey(ctrlShiftK, [&](const Hotkey&) { ++withShift; });
    EXPECT_NE(id1, 0u);
    EXPECT_NE(id2, 0u);  // 不衝突：不同熱鍵
    EXPECT_EQ(gh.count(), 2u);

    gh.inject(ctrlShiftK);
    EXPECT_EQ(plain, 0);
    EXPECT_EQ(withShift, 1);
}

// 回呼中解除自己：本次分派安全，下一次不再觸發。
TEST(GlobalHotkey, CallbackUnregisteringSelfIsSafe) {
    NullGlobalHotkeys gh(/*available=*/true);
    const Hotkey combo = hk(Modifier::Meta, Key::Enter);

    int fired = 0;
    HotkeyId id = 0;
    id = gh.register_hotkey(combo, [&](const Hotkey&) {
        ++fired;
        gh.unregister(id);  // 回呼中解除自己
    });
    ASSERT_NE(id, 0u);

    gh.inject(combo);  // 本次：觸發並解除
    gh.inject(combo);  // 下次：已解除，不再觸發
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(gh.count(), 0u);
}

// 透過抽象介面使用（多型契約）：呼叫端只依賴 GlobalHotkeys。
TEST(GlobalHotkey, UsableThroughAbstractInterface) {
    NullGlobalHotkeys backend(/*available=*/true);
    GlobalHotkeys& gh = backend;  // 以基底介面操作

    int fired = 0;
    const Hotkey combo = hk(Modifier::Control | Modifier::Alt, Key::T);
    HotkeyId id = 0;
    if (gh.has()) {
        id = gh.register_hotkey(combo, [&](const Hotkey&) { ++fired; });
    }
    EXPECT_NE(id, 0u);

    backend.inject(combo);  // inject 是 null 後端專屬（測試入口）
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(gh.unregister(id));
}

}  // namespace
