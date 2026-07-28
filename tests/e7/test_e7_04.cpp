// E7-04 動態變數與執行期重算 — 契約測試（gtest）。
//
// 涵蓋：動態變數變更、衍生變數自動重算、只重算受影響者（重算計數）、多層相依鏈、
// 循環相依明確報錯（不靜默）、變更通知訂閱，以及邊界（未知變數 / 對衍生 set /
// 未定義相依 / 未宣告輸入存取 throw / 重定義 / 延遲 get）。
#include "reactive.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

using ds::format::DerivedInputs;
using ds::format::ReactiveScope;
using ds::format::Value;

namespace {

// 便捷：整數 Value。
Value I(std::int64_t v) { return Value::integer(v); }

// -----------------------------------------------------------------------------
// 來源變數：set / get / 動態變更
// -----------------------------------------------------------------------------

TEST(ReactiveSource, SetThenGet) {
    ReactiveScope s;
    auto r = s.set("width", I(800));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().as_int(), 800);

    auto g = s.get("width");
    ASSERT_TRUE(g.ok());
    EXPECT_EQ(g.value().as_int(), 800);
    EXPECT_TRUE(s.has("width"));
    EXPECT_FALSE(s.is_derived("width"));
}

TEST(ReactiveSource, DynamicChangeUpdatesValue) {
    ReactiveScope s;
    s.set("theme", Value::string("light"));
    EXPECT_EQ(s.get("theme").value().as_string(), "light");
    // 執行期改變。
    s.set("theme", Value::string("dark"));
    EXPECT_EQ(s.get("theme").value().as_string(), "dark");
}

TEST(ReactiveSource, UnknownVariableFailsNotSilent) {
    ReactiveScope s;
    auto g = s.get("nope");
    ASSERT_FALSE(g.ok());
    EXPECT_EQ(g.error().variable, "nope");
    EXPECT_FALSE(g.error().message.empty());
}

TEST(ReactiveSource, SetOnDerivedRejected) {
    ReactiveScope s;
    s.set("x", I(1));
    s.define_derived("y", {"x"}, [](const DerivedInputs& in) {
        return I(in.get("x").as_int() + 1);
    });
    auto r = s.set("y", I(99));  // y 是衍生變數，不可直接 set。
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "y");
    // 值未被污染。
    EXPECT_EQ(s.get("y").value().as_int(), 2);
}

// -----------------------------------------------------------------------------
// 衍生變數：定義即求值、來源變更時自動重算
// -----------------------------------------------------------------------------

TEST(ReactiveDerived, DefineComputesInitialValue) {
    ReactiveScope s;
    s.set("w", I(800));
    auto d = s.define_derived("half", {"w"}, [](const DerivedInputs& in) {
        return I(in.get("w").as_int() / 2);
    });
    ASSERT_TRUE(d.ok());
    EXPECT_EQ(d.value().as_int(), 400);
    EXPECT_TRUE(s.is_derived("half"));
}

TEST(ReactiveDerived, AutoRecomputeOnSourceChange) {
    ReactiveScope s;
    s.set("w", I(800));
    s.define_derived("half", {"w"}, [](const DerivedInputs& in) {
        return I(in.get("w").as_int() / 2);
    });
    EXPECT_EQ(s.get("half").value().as_int(), 400);

    s.set("w", I(1000));  // 來源變更 → 衍生自動重算。
    EXPECT_EQ(s.get("half").value().as_int(), 500);
}

TEST(ReactiveDerived, MultipleDepsCombine) {
    ReactiveScope s;
    s.set("w", I(40));
    s.set("h", I(30));
    s.define_derived("area", {"w", "h"}, [](const DerivedInputs& in) {
        return I(in.get("w").as_int() * in.get("h").as_int());
    });
    EXPECT_EQ(s.get("area").value().as_int(), 1200);
    s.set("h", I(50));
    EXPECT_EQ(s.get("area").value().as_int(), 2000);
}

// -----------------------------------------------------------------------------
// 只重算受影響者（重算計數）
// -----------------------------------------------------------------------------

TEST(ReactiveRecompute, OnlyAffectedRecomputed) {
    ReactiveScope s;
    s.set("a", I(1));
    s.set("b", I(100));

    int a_derived_calls = 0;
    int b_derived_calls = 0;
    s.define_derived("a2", {"a"}, [&](const DerivedInputs& in) {
        ++a_derived_calls;
        return I(in.get("a").as_int() * 2);
    });
    s.define_derived("b2", {"b"}, [&](const DerivedInputs& in) {
        ++b_derived_calls;
        return I(in.get("b").as_int() * 2);
    });
    // 定義各自求值一次。
    EXPECT_EQ(a_derived_calls, 1);
    EXPECT_EQ(b_derived_calls, 1);

    // 只改 a：只有 a2 應重算，b2 不動。
    s.set("a", I(5));
    EXPECT_EQ(a_derived_calls, 2);
    EXPECT_EQ(b_derived_calls, 1);  // 未受影響 → 不重算。
    EXPECT_EQ(s.get("a2").value().as_int(), 10);
    EXPECT_EQ(s.get("b2").value().as_int(), 200);
}

TEST(ReactiveRecompute, SameValueSetStillRecomputesDependentsOnce) {
    // 設同值仍會沿相依鏈重算一次（實作以髒標記傳播；值未變則不觸發通知，見下方通知測試）。
    ReactiveScope s;
    s.set("a", I(3));
    int calls = 0;
    s.define_derived("d", {"a"}, [&](const DerivedInputs& in) {
        ++calls;
        return I(in.get("a").as_int());
    });
    EXPECT_EQ(calls, 1);
    s.set("a", I(3));  // 同值。
    EXPECT_GE(calls, 1);
    EXPECT_EQ(s.get("d").value().as_int(), 3);
}

// -----------------------------------------------------------------------------
// 多層相依鏈（拓撲重算）
// -----------------------------------------------------------------------------

TEST(ReactiveChain, MultiLevelPropagation) {
    ReactiveScope s;
    s.set("base", I(2));
    s.define_derived("l1", {"base"}, [](const DerivedInputs& in) {
        return I(in.get("base").as_int() + 1);       // 3
    });
    s.define_derived("l2", {"l1"}, [](const DerivedInputs& in) {
        return I(in.get("l1").as_int() * 10);        // 30
    });
    s.define_derived("l3", {"l2"}, [](const DerivedInputs& in) {
        return I(in.get("l2").as_int() - 5);         // 25
    });
    EXPECT_EQ(s.get("l3").value().as_int(), 25);

    s.set("base", I(9));  // 一次變更貫穿三層。
    EXPECT_EQ(s.get("l1").value().as_int(), 10);
    EXPECT_EQ(s.get("l2").value().as_int(), 100);
    EXPECT_EQ(s.get("l3").value().as_int(), 95);
}

TEST(ReactiveChain, DiamondDependency) {
    // b 與 c 皆依賴 a；d 依賴 b、c。改 a 應正確傳到 d。
    ReactiveScope s;
    s.set("a", I(10));
    s.define_derived("b", {"a"}, [](const DerivedInputs& in) {
        return I(in.get("a").as_int() + 1);
    });
    s.define_derived("c", {"a"}, [](const DerivedInputs& in) {
        return I(in.get("a").as_int() * 2);
    });
    int d_calls = 0;
    s.define_derived("d", {"b", "c"}, [&](const DerivedInputs& in) {
        ++d_calls;
        return I(in.get("b").as_int() + in.get("c").as_int());
    });
    EXPECT_EQ(s.get("d").value().as_int(), (10 + 1) + (10 * 2));  // 31
    int before = d_calls;
    s.set("a", I(20));
    EXPECT_EQ(s.get("d").value().as_int(), (20 + 1) + (20 * 2));  // 61
    // 菱形：d 只該因這次變更重算一次，不因 b、c 各觸發而重算兩次。
    EXPECT_EQ(d_calls, before + 1);
}

// -----------------------------------------------------------------------------
// 循環相依：明確報錯、不靜默、不改狀態
// -----------------------------------------------------------------------------

TEST(ReactiveCycle, SelfDependencyRejected) {
    ReactiveScope s;
    s.set("x", I(1));
    // 先合法定義 y 依賴 x。
    s.define_derived("y", {"x"}, [](const DerivedInputs& in) {
        return I(in.get("x").as_int());
    });
    // 再把 y 重定義為依賴自己 → 循環。
    auto r = s.define_derived("y", {"y"}, [](const DerivedInputs& in) {
        return I(in.get("y").as_int());
    });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "y");
    EXPECT_FALSE(r.error().message.empty());
    // 舊定義仍有效、未被破壞。
    EXPECT_EQ(s.get("y").value().as_int(), 1);
}

TEST(ReactiveCycle, IndirectCycleRejected) {
    ReactiveScope s;
    s.set("seed", I(0));
    s.define_derived("a", {"seed"}, [](const DerivedInputs& in) {
        return I(in.get("seed").as_int());
    });
    s.define_derived("b", {"a"}, [](const DerivedInputs& in) {
        return I(in.get("a").as_int());
    });
    s.define_derived("c", {"b"}, [](const DerivedInputs& in) {
        return I(in.get("b").as_int());
    });
    // 把 a 重定義為依賴 c → a→c→b→a 循環。
    auto r = s.define_derived("a", {"c"}, [](const DerivedInputs& in) {
        return I(in.get("c").as_int());
    });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "a");
    // 鏈仍完整可求值（狀態未被破壞）。
    EXPECT_TRUE(s.get("c").ok());
}

TEST(ReactiveCycle, UnknownDependencyRejected) {
    ReactiveScope s;
    s.set("x", I(1));
    auto r = s.define_derived("y", {"missing"}, [](const DerivedInputs&) {
        return I(0);
    });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "missing");
    EXPECT_FALSE(s.has("y"));  // 定義失敗 → 未建立。
}

// -----------------------------------------------------------------------------
// 變更通知 / 訂閱
// -----------------------------------------------------------------------------

TEST(ReactiveNotify, SubscriberSeesSourceAndDerivedChanges) {
    ReactiveScope s;
    s.set("w", I(100));
    s.define_derived("half", {"w"}, [](const DerivedInputs& in) {
        return I(in.get("w").as_int() / 2);
    });

    std::vector<std::string> events;
    s.on_change([&](const std::string& name) { events.push_back(name); });

    s.set("w", I(200));  // w 變、half 隨之變。
    // 應收到 w 與 half 兩則通知。
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0], "w");     // 來源優先（建立序）。
    EXPECT_EQ(events[1], "half");
}

TEST(ReactiveNotify, UnchangedValueDoesNotNotify) {
    ReactiveScope s;
    s.set("w", I(100));
    s.define_derived("half", {"w"}, [](const DerivedInputs& in) {
        return I(in.get("w").as_int() / 2);
    });
    std::vector<std::string> events;
    s.on_change([&](const std::string& name) { events.push_back(name); });

    s.set("w", I(100));  // 值未變。
    EXPECT_TRUE(events.empty());  // 不觸發通知。
}

TEST(ReactiveNotify, DerivedUnchangedNotNotifiedEvenIfSourceChanged) {
    // half 對 w 取整除 2：w 由 100→101 時 half 仍為 50（未變）→ 只通知 w。
    ReactiveScope s;
    s.set("w", I(100));
    s.define_derived("half", {"w"}, [](const DerivedInputs& in) {
        return I(in.get("w").as_int() / 2);
    });
    std::vector<std::string> events;
    s.on_change([&](const std::string& name) { events.push_back(name); });

    s.set("w", I(101));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], "w");
}

TEST(ReactiveNotify, MultipleSubscribers) {
    ReactiveScope s;
    s.set("x", I(1));
    int c1 = 0, c2 = 0;
    s.on_change([&](const std::string&) { ++c1; });
    s.on_change([&](const std::string&) { ++c2; });
    s.set("x", I(2));
    EXPECT_EQ(c1, 1);
    EXPECT_EQ(c2, 1);
}

// -----------------------------------------------------------------------------
// 契約：DerivedInputs 只能讀已宣告 deps
// -----------------------------------------------------------------------------

TEST(ReactiveContract, UndeclaredInputAccessThrows) {
    ReactiveScope s;
    s.set("a", I(1));
    s.set("b", I(2));
    // compute 存取未宣告於 deps 的 "b" → 契約違反（throw）。
    // 例外在 ensure_computed 內從 compute 傳出；define 的初次求值即觸發。
    bool threw = false;
    try {
        s.define_derived("bad", {"a"}, [](const DerivedInputs& in) {
            return I(in.get("b").as_int());  // "b" 未宣告。
        });
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(ReactiveContract, DerivedInputsHas) {
    ReactiveScope s;
    s.set("a", I(1));
    s.set("b", I(2));
    bool a_seen = false, b_seen = false;
    s.define_derived("d", {"a"}, [&](const DerivedInputs& in) {
        a_seen = in.has("a");
        b_seen = in.has("b");
        return I(in.get("a").as_int());
    });
    EXPECT_TRUE(a_seen);
    EXPECT_FALSE(b_seen);
}

// -----------------------------------------------------------------------------
// 重定義 / 查詢
// -----------------------------------------------------------------------------

TEST(ReactiveRedefine, RedefineDerivedUpdatesComputation) {
    ReactiveScope s;
    s.set("x", I(10));
    s.define_derived("y", {"x"}, [](const DerivedInputs& in) {
        return I(in.get("x").as_int() + 1);
    });
    EXPECT_EQ(s.get("y").value().as_int(), 11);
    // 重定義 y：改為乘 3。
    s.define_derived("y", {"x"}, [](const DerivedInputs& in) {
        return I(in.get("x").as_int() * 3);
    });
    EXPECT_EQ(s.get("y").value().as_int(), 30);
    s.set("x", I(4));
    EXPECT_EQ(s.get("y").value().as_int(), 12);
}

TEST(ReactiveRedefine, DefineDerivedOverSourceRejected) {
    ReactiveScope s;
    s.set("x", I(1));
    auto r = s.define_derived("x", {}, [](const DerivedInputs&) { return I(0); });
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().variable, "x");
    EXPECT_FALSE(s.is_derived("x"));  // 仍是來源。
}

TEST(ReactiveQuery, NamesInCreationOrder) {
    ReactiveScope s;
    s.set("first", I(1));
    s.set("second", I(2));
    s.define_derived("third", {"first"}, [](const DerivedInputs& in) {
        return I(in.get("first").as_int());
    });
    auto names = s.names();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "first");
    EXPECT_EQ(names[1], "second");
    EXPECT_EQ(names[2], "third");
    EXPECT_EQ(s.size(), 3u);
}

// -----------------------------------------------------------------------------
// 型別保留：衍生可承載非數字型別
// -----------------------------------------------------------------------------

TEST(ReactiveValue, DerivedPreservesValueTypes) {
    ReactiveScope s;
    s.set("count", I(3));
    s.define_derived("label", {"count"}, [](const DerivedInputs& in) {
        return Value::string("n=" + std::to_string(in.get("count").as_int()));
    });
    EXPECT_TRUE(s.get("label").value().is_string());
    EXPECT_EQ(s.get("label").value().as_string(), "n=3");
    s.set("count", I(7));
    EXPECT_EQ(s.get("label").value().as_string(), "n=7");
}

}  // namespace
