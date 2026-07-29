// E2-21 使用者文字輸入值 — 測試（gtest）
//
// 覆蓋：提供者身分、可上轉為 E2-01 MetricProvider（以 provider 介面走訪）、
// 註冊到 E2-01 registry、無輸入框（instance_count()==0，保守不崩）、
// 讀輸入框文字經 E2-01 暴露（單一輸入框）、多輸入框（欄位數=實例數、依綁定順序列舉）、
// 空值誠實處理（輸入框目前文字為空 ""，valid==true 而非 unknown）、
// 輸入變更後 refresh() 更新對應實例、register_metrics 前 refresh() 為 no-op、
// unbind 後既有實例設為 unknown 且不再隨 refresh 更新、rebind 後恢復更新、
// 無時序歷史（history_capacity=0）、範圍 unbounded、重複註冊保守拒絕。
// 相位 1：只驗介面 + 純邏輯，不含任何平台分支 / 真實鍵盤 / IME。
#include "user_text_input.hpp"

#include <gtest/gtest.h>

#include <string>

#include "metric.hpp"
#include "text_layout.hpp"

using ds::elements::TextInputElement;
using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::render::FixedFontMetrics;
using ds::sysinfo::UserTextInputProvider;

namespace {

// 等寬字型：每字元 advance=10、行高=20——與 E4-15 自身測試同構（本單元不關心排版本身，
// 只讀 TextInputElement::text()，此處僅需一份合法的 FontMetrics 供建構輸入框）。
FixedFontMetrics MakeMetrics() { return FixedFontMetrics(10.0, 20.0); }

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(UserTextInputProvider, ProviderIdIsStable) {
    UserTextInputProvider p;
    EXPECT_EQ(p.provider_id(), "sysinfo.user_text_input");
    EXPECT_EQ(std::string(UserTextInputProvider::kMetricId), "input.text");
    EXPECT_EQ(std::string(UserTextInputProvider::kMetricName), "User Text Input");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(UserTextInputProvider, IsAMetricProvider) {
    UserTextInputProvider provider;
    MetricProvider* base = &provider;  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.user_text_input");
}

// ===========================================================================
// 無輸入框：誠實處理（保守而不崩）
// ===========================================================================
TEST(UserTextInputProvider, NoInputBoxYieldsEmptyMetric) {
    MetricRegistry registry;
    UserTextInputProvider provider;  // 從未 bind() 過任何輸入框

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);  // 指標本身仍掛上
    ASSERT_TRUE(registry.contains("input.text"));

    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 0u);  // 無輸入框 → 0 實例，不崩
    EXPECT_EQ(metric->name(), "User Text Input");
    EXPECT_EQ(metric->unit(), "");
    EXPECT_EQ(provider.bound_count(), 0u);
}

// ===========================================================================
// 讀輸入框文字經 E2-01 暴露（單一輸入框）
// ===========================================================================
TEST(UserTextInputProvider, SingleBoundInputIsExposedViaMetric) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement search_box(metrics);
    search_box.set_text("hello");

    UserTextInputProvider provider;
    provider.bind("search_box", "Search Box", search_box);
    EXPECT_EQ(provider.bound_count(), 1u);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    ASSERT_EQ(metric->instance_count(), 1u);

    const auto& inst = metric->instance(0);
    EXPECT_EQ(inst.instance_id(), "search_box");
    EXPECT_EQ(inst.label(), "Search Box");
    const auto v = inst.value();
    EXPECT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 0.0);
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "hello");

    // find_instance（E2-01 便利查詢，走抽象介面）。
    const auto* found = metric->find_instance("search_box");
    ASSERT_NE(found, nullptr);
    ASSERT_TRUE(found->value().text.has_value());
    EXPECT_EQ(*found->value().text, "hello");
}

// ===========================================================================
// 空值誠實處理：輸入框目前文字為空 "" → 實例存在、valid==true、text==""
// （與「無輸入框」在指標層級無此實例明確區分，不混為一談）。
// ===========================================================================
TEST(UserTextInputProvider, EmptyInputTextIsHonestlyValidNotUnknown) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement empty_box(metrics);  // 預設狀態：text() == ""

    UserTextInputProvider provider;
    provider.bind("empty_box", "Empty Box", empty_box);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    ASSERT_EQ(metric->instance_count(), 1u);

    const auto v = metric->instance(0).value();
    EXPECT_TRUE(v.valid);        // 空字串是「目前真實值」，非「無讀值」
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "");
}

// ===========================================================================
// 多輸入框：欄位數 = 實例數、依綁定順序列舉
// ===========================================================================
TEST(UserTextInputProvider, MultipleBoundInputsAreEnumeratedInBindOrder) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement search_box(metrics);
    search_box.set_text("query");
    TextInputElement amount_box(metrics);
    amount_box.set_text("42");
    TextInputElement note_box(metrics);
    note_box.set_text("");

    UserTextInputProvider provider;
    provider.bind("search_box", "Search Box", search_box);
    provider.bind("amount_box", "Amount", amount_box);
    provider.bind("note_box", "Note", note_box);
    EXPECT_EQ(provider.bound_count(), 3u);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 3u);

    EXPECT_EQ(metric->instance(0).instance_id(), "search_box");
    ASSERT_TRUE(metric->instance(0).value().text.has_value());
    EXPECT_EQ(*metric->instance(0).value().text, "query");

    EXPECT_EQ(metric->instance(1).instance_id(), "amount_box");
    ASSERT_TRUE(metric->instance(1).value().text.has_value());
    EXPECT_EQ(*metric->instance(1).value().text, "42");

    EXPECT_EQ(metric->instance(2).instance_id(), "note_box");
    ASSERT_TRUE(metric->instance(2).value().text.has_value());
    EXPECT_EQ(*metric->instance(2).value().text, "");
}

// ===========================================================================
// 輸入變更後 refresh() 更新對應實例（pull 模型：呼叫端判定該讀新值時呼叫）。
// ===========================================================================
TEST(UserTextInputProvider, RefreshPicksUpChangedInputText) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement search_box(metrics);
    search_box.set_text("first");

    UserTextInputProvider provider;
    provider.bind("search_box", "Search Box", search_box);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    ASSERT_TRUE(metric->instance(0).value().text.has_value());
    EXPECT_EQ(*metric->instance(0).value().text, "first");

    // 使用者繼續輸入：輸入框內容變更（不透過本提供者，本提供者非致動器）。
    search_box.insert(" edit");
    // 尚未 refresh：指標仍反映舊值（pull 模型，非即時推播）。
    EXPECT_EQ(*metric->instance(0).value().text, "first");

    provider.refresh();
    ASSERT_TRUE(metric->instance(0).value().text.has_value());
    EXPECT_EQ(*metric->instance(0).value().text, "first edit");

    // 再次變更為完全不同內容（含清空)，refresh 後亦如實反映。
    search_box.set_text("");
    provider.refresh();
    ASSERT_TRUE(metric->instance(0).value().text.has_value());
    EXPECT_TRUE(metric->instance(0).value().valid);
    EXPECT_EQ(*metric->instance(0).value().text, "");
}

// register_metrics 尚未呼叫時 refresh() 為 no-op（不崩）。
TEST(UserTextInputProvider, RefreshBeforeRegisterIsNoOp) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement box(metrics);
    box.set_text("x");

    UserTextInputProvider provider;
    provider.bind("box", "Box", box);
    provider.refresh();  // 未曾 register_metrics()：no-op，不崩
    EXPECT_EQ(provider.bound_count(), 1u);
}

// ===========================================================================
// unbind：既有實例設為 unknown、不再隨 refresh 更新；rebind 後恢復更新。
// ===========================================================================
TEST(UserTextInputProvider, UnbindMarksInstanceUnknownAndStopsUpdating) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement box(metrics);
    box.set_text("before");

    UserTextInputProvider provider;
    provider.bind("box", "Box", box);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    ASSERT_TRUE(metric->instance(0).value().valid);

    provider.unbind("box");
    EXPECT_EQ(provider.bound_count(), 0u);
    EXPECT_FALSE(metric->instance(0).value().valid);  // 已解除綁定 → 無讀值

    // 之後輸入框繼續變更、呼叫 refresh()：已 unbind 者不受影響，維持 unknown。
    box.set_text("after");
    provider.refresh();
    EXPECT_FALSE(metric->instance(0).value().valid);

    // rebind 後，refresh 恢復更新（讀回目前值）。
    provider.bind("box", "Box", box);
    EXPECT_TRUE(metric->instance(0).value().valid);
    ASSERT_TRUE(metric->instance(0).value().text.has_value());
    EXPECT_EQ(*metric->instance(0).value().text, "after");
}

// unbind 一個不存在的 key 為 no-op（不崩）。
TEST(UserTextInputProvider, UnbindUnknownKeyIsNoOp) {
    UserTextInputProvider provider;
    provider.unbind("does_not_exist");
    EXPECT_EQ(provider.bound_count(), 0u);
}

// ===========================================================================
// 無時序歷史（history_capacity=0，同 E2-12 靜態欄位慣例）。
// ===========================================================================
TEST(UserTextInputProvider, InstancesHaveNoHistory) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement box(metrics);
    box.set_text("v");

    UserTextInputProvider provider;
    provider.bind("box", "Box", box);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);

    const auto& h = metric->instance(0).history();
    EXPECT_EQ(h.capacity(), 0u);
    EXPECT_TRUE(h.empty());
}

// ===========================================================================
// 範圍 = unbounded（文字值無值域）。
// ===========================================================================
TEST(UserTextInputProvider, MetricRangeIsUnbounded) {
    MetricRegistry registry;
    UserTextInputProvider provider;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    const auto r = metric->range();
    EXPECT_FALSE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_FALSE(r.is_bounded());
}

// ===========================================================================
// 消費者範式：新增指標 = 新增提供者、消費者只走 E2-01 registry（不觸及具體型別）。
// ===========================================================================
TEST(UserTextInputProvider, ConsumerWalksViaAbstractContractOnly) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement a(metrics);
    a.set_text("a");
    TextInputElement b(metrics);
    b.set_text("bb");

    UserTextInputProvider provider;
    provider.bind("a", "A", a);
    provider.bind("b", "B", b);

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 一個「掛件」風格的消費者：只認得 E2-01 的 Metric，數輸入框數量。
    std::size_t total = 0;
    for (const auto& m : registry.all()) {
        total += m->instance_count();  // 全程無 sysinfo 型別
    }
    EXPECT_EQ(total, 2u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫既有）。
TEST(UserTextInputProvider, DuplicateRegistrationRejected) {
    FixedFontMetrics metrics = MakeMetrics();
    TextInputElement box(metrics);
    box.set_text("one");

    MetricRegistry registry;
    UserTextInputProvider p1;
    p1.bind("box", "Box", box);
    UserTextInputProvider p2;  // 無輸入框

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);  // 同 id "input.text" 被拒
    auto metric = registry.get("input.text");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 1u);  // 既有指標未被覆寫：仍為 p1 的一個輸入框
}

}  // namespace
