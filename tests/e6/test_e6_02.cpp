// E6-02 動作註冊表與前綴/模糊分派 — 單元測試（gtest）
//
// 驗證擴充點 3「動作」查詢層契約：
//   精確比對、前綴比對（vol → volume.set）、子字串比對、模糊比對（編輯距離容錯）、
//   相關度排序（精確 > 前綴 > 子字串 > 模糊）、歧義回多候選（不亂猜）、
//   無比對明確回報、經 E6-01 CommandBus dispatch_best 實際執行、參數傳遞、
//   sync_from(bus) 同步、模糊上限設定、註冊表與 bus 漂移之誠實回報、契約版本標記。
// 全程純邏輯，不依賴任何平台後端或真實副作用。
#include "action_registry.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::command::ActionRegistry;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::command::CommandValue;
using ds::command::DispatchOutcome;
using ds::command::Match;
using ds::command::MatchKind;
using ds::command::ResolveStatus;

namespace {

// 建一個帶常見命令的註冊表，供多個測試共用。
ActionRegistry make_registry() {
    ActionRegistry reg;
    reg.add("volume.set", "Set volume");
    reg.add("volume.get", "Get volume");
    reg.add("power.sleep");
    reg.add("power.off");
    reg.add("brightness.set");
    return reg;
}

// -------- 註冊表基本操作 --------

TEST(ActionRegistry, AddContainsSizeIdsRemove) {
    ActionRegistry reg;
    EXPECT_TRUE(reg.empty());
    EXPECT_TRUE(reg.add("volume.set"));
    EXPECT_FALSE(reg.add("volume.set", "retitle"));  // 既有 id 更新標題回 false
    EXPECT_TRUE(reg.add("power.off"));
    EXPECT_EQ(reg.size(), 2u);
    EXPECT_TRUE(reg.contains("volume.set"));
    EXPECT_FALSE(reg.contains("nope"));

    auto ids = reg.ids();  // 字典序
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], "power.off");
    EXPECT_EQ(ids[1], "volume.set");

    EXPECT_TRUE(reg.remove("power.off"));
    EXPECT_FALSE(reg.remove("power.off"));
    EXPECT_EQ(reg.size(), 1u);
}

TEST(ActionRegistry, EmptyIdRejected) {
    ActionRegistry reg;
    EXPECT_FALSE(reg.add(""));
    EXPECT_EQ(reg.size(), 0u);
}

// -------- 精確比對 --------

TEST(Resolve, ExactMatchWinsAndIsUnique) {
    auto reg = make_registry();
    auto m = reg.resolve("volume.set");
    ASSERT_FALSE(m.empty());
    EXPECT_EQ(m.front().id, "volume.set");
    EXPECT_EQ(m.front().kind, MatchKind::Exact);
    // 精確命中相關度嚴格高於任何其他候選。
    for (std::size_t i = 1; i < m.size(); ++i) {
        EXPECT_LT(m[i].score, m.front().score);
        EXPECT_NE(m[i].kind, MatchKind::Exact);
    }
}

// -------- 前綴比對（規格範例：vol → volume.*） --------

TEST(Resolve, PrefixMatch) {
    auto reg = make_registry();
    auto m = reg.resolve("bright");  // 唯一前綴 → brightness.set
    ASSERT_FALSE(m.empty());
    EXPECT_EQ(m.front().id, "brightness.set");
    EXPECT_EQ(m.front().kind, MatchKind::Prefix);
}

TEST(Resolve, PrefixRanksAboveFuzzy) {
    ActionRegistry reg;
    reg.add("volume.set");   // 前綴命中 "vol"
    reg.add("value.get");    // 對 "vol" 為模糊（value vs vol，距離 2）
    auto m = reg.resolve("vol");
    ASSERT_GE(m.size(), 1u);
    EXPECT_EQ(m.front().id, "volume.set");
    EXPECT_EQ(m.front().kind, MatchKind::Prefix);
}

// -------- 子字串比對 --------

TEST(Resolve, SubstringMatch) {
    ActionRegistry reg;
    reg.add("audio.volume.set");  // "volume" 在中段，非前綴
    auto m = reg.resolve("volume");
    ASSERT_FALSE(m.empty());
    EXPECT_EQ(m.front().id, "audio.volume.set");
    EXPECT_EQ(m.front().kind, MatchKind::Substring);
}

TEST(Resolve, ExactBeatsPrefixBeatsSubstring) {
    ActionRegistry reg;
    reg.add("set");             // 對 "set" 精確
    reg.add("set.value");       // 對 "set" 前綴
    reg.add("volume.set");      // 對 "set" 子字串
    auto m = reg.resolve("set");
    ASSERT_EQ(m.size(), 3u);
    EXPECT_EQ(m[0].kind, MatchKind::Exact);
    EXPECT_EQ(m[0].id, "set");
    EXPECT_EQ(m[1].kind, MatchKind::Prefix);
    EXPECT_EQ(m[1].id, "set.value");
    EXPECT_EQ(m[2].kind, MatchKind::Substring);
    EXPECT_EQ(m[2].id, "volume.set");
}

// -------- 模糊比對（編輯距離容錯） --------

TEST(Resolve, FuzzyMatchTypo) {
    auto reg = make_registry();
    // "volme" 是 "volume" 的錯字（漏一個 u）。對 id 段 "volume" 距離 1。
    auto m = reg.resolve("volme");
    ASSERT_FALSE(m.empty());
    // 兩個 volume.* 皆應以模糊命中，且最相近命令排前。
    EXPECT_EQ(m.front().kind, MatchKind::Fuzzy);
    EXPECT_EQ(m.front().distance, 1);
    bool found_volume = false;
    for (const auto& x : m) {
        if (x.id == "volume.set" || x.id == "volume.get") found_volume = true;
    }
    EXPECT_TRUE(found_volume);
}

TEST(Resolve, FuzzyRespectsMaxDistance) {
    ActionRegistry reg;
    reg.add("brightness.set");
    reg.set_fuzzy_max_distance(1);
    // "bxightness" 對 "brightness" 距離 1 → 命中。
    EXPECT_FALSE(reg.resolve("bxightness").empty());
    // 差太多（距離 > 1）→ 不命中。
    EXPECT_TRUE(reg.resolve("zzzzz").empty());

    reg.set_fuzzy_max_distance(0);  // 關閉模糊
    EXPECT_TRUE(reg.resolve("bxightness").empty());
    EXPECT_EQ(reg.fuzzy_max_distance(), 0);
}

TEST(Resolve, SmallerEditDistanceRanksHigher) {
    ActionRegistry reg;
    reg.add("power.off");   // 對 "pywer" 距離 1（o→y）於段 "power"
    reg.add("paper.off");   // 對 "pywer" 段 "paper" 距離 2
    reg.set_fuzzy_max_distance(2);
    auto m = reg.resolve("pywer");
    ASSERT_GE(m.size(), 2u);
    EXPECT_EQ(m.front().id, "power.off");
    EXPECT_LE(m.front().distance, m.back().distance);
}

// -------- 無比對明確回報 --------

TEST(Resolve, NoMatchReturnsEmpty) {
    auto reg = make_registry();
    EXPECT_TRUE(reg.resolve("qwxyz123").empty());
}

TEST(Resolve, EmptyQueryReturnsEmpty) {
    auto reg = make_registry();
    EXPECT_TRUE(reg.resolve("").empty());
}

// -------- 歧義：多候選回清單而非亂猜 --------

TEST(DispatchBest, AmbiguousReturnsCandidatesWithoutDispatch) {
    CommandBus bus;
    int calls = 0;
    bus.register_command("volume.set", [&](const CommandArgs&) { ++calls; return CommandResult::make_ok(); });
    bus.register_command("volume.get", [&](const CommandArgs&) { ++calls; return CommandResult::make_ok(); });

    ActionRegistry reg;
    reg.sync_from(bus);

    // "volume" 為兩者共同前綴，並列最佳 → 歧義。
    auto out = reg.dispatch_best(bus, "volume");
    EXPECT_EQ(out.status, ResolveStatus::Ambiguous);
    EXPECT_TRUE(out.ambiguous());
    ASSERT_EQ(out.candidates.size(), 2u);
    // 並列候選同分。
    EXPECT_EQ(out.candidates[0].score, out.candidates[1].score);
    // 歧義**不分派**——處理器一次都不該被呼叫。
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(out.chosen.empty());
}

// -------- 經 E6-01 CommandBus dispatch_best 實際執行 --------

TEST(DispatchBest, PrefixResolvesAndDispatchesThroughBus) {
    CommandBus bus;
    std::string seen_arg;
    bus.register_command("brightness.set", [&](const CommandArgs& a) {
        if (auto v = a.get_string("level")) seen_arg = *v;
        return CommandResult::make_ok(CommandValue(std::string("done")));
    });

    ActionRegistry reg;
    reg.sync_from(bus);

    CommandArgs args;
    args.set("level", CommandValue(std::string("hi")));
    // "bright" 唯一前綴 → brightness.set，經 bus 實際執行，參數透傳。
    auto out = reg.dispatch_best(bus, "bright", args);
    ASSERT_EQ(out.status, ResolveStatus::Dispatched);
    EXPECT_TRUE(out.dispatched());
    EXPECT_EQ(out.chosen, "brightness.set");
    EXPECT_EQ(out.result.status, CommandStatus::Ok);
    ASSERT_TRUE(out.result.value.as_string().has_value());
    EXPECT_EQ(*out.result.value.as_string(), "done");
    EXPECT_EQ(seen_arg, "hi");  // 參數確實傳到處理器
}

TEST(DispatchBest, FuzzyTypoResolvesAndDispatches) {
    CommandBus bus;
    int calls = 0;
    bus.register_command("brightness.set", [&](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok();
    });
    ActionRegistry reg;
    reg.sync_from(bus);

    // "brihtness" 為 "brightness" 錯字（漏 g，距離 1）→ 唯一候選 → 分派執行。
    auto out = reg.dispatch_best(bus, "brihtness");
    ASSERT_EQ(out.status, ResolveStatus::Dispatched);
    EXPECT_EQ(out.chosen, "brightness.set");
    EXPECT_EQ(calls, 1);
}

TEST(DispatchBest, NoMatchReportsNotFoundAndDoesNotDispatch) {
    CommandBus bus;
    int calls = 0;
    bus.register_command("volume.set", [&](const CommandArgs&) { ++calls; return CommandResult::make_ok(); });
    ActionRegistry reg;
    reg.sync_from(bus);

    auto out = reg.dispatch_best(bus, "zzzzzzzz");
    EXPECT_EQ(out.status, ResolveStatus::NotFound);
    EXPECT_TRUE(out.not_found());
    EXPECT_TRUE(out.candidates.empty());
    EXPECT_EQ(calls, 0);
}

// 註冊表與 bus 漂移：候選在註冊表但 bus 未註冊 → 誠實回 bus 的結構化 NotFound。
TEST(DispatchBest, RegistryBusDriftSurfacesBusNotFound) {
    CommandBus bus;  // 空 bus，未註冊任何命令
    ActionRegistry reg;
    reg.add("brightness.set");  // 只在註冊表

    auto out = reg.dispatch_best(bus, "bright");
    // 註冊表解析出唯一候選並嘗試分派；bus 無此命令 → 分派結果為 NotFound。
    EXPECT_EQ(out.status, ResolveStatus::Dispatched);
    EXPECT_EQ(out.chosen, "brightness.set");
    EXPECT_EQ(out.result.status, CommandStatus::NotFound);  // 不靜默
}

// -------- sync_from 同步 --------

TEST(SyncFrom, ImportsBusCommandIds) {
    CommandBus bus;
    bus.register_command("a.one", [](const CommandArgs&) { return CommandResult::make_ok(); });
    bus.register_command("a.two", [](const CommandArgs&) { return CommandResult::make_ok(); });

    ActionRegistry reg;
    EXPECT_EQ(reg.sync_from(bus), 2u);
    EXPECT_EQ(reg.sync_from(bus), 0u);  // 冪等：重複同步不重加
    EXPECT_TRUE(reg.contains("a.one"));
    EXPECT_TRUE(reg.contains("a.two"));
}

// -------- best() 便捷 --------

TEST(Best, ReturnsTopMatchOrFalse) {
    auto reg = make_registry();
    Match m;
    EXPECT_TRUE(reg.best("brightness.set", m));
    EXPECT_EQ(m.id, "brightness.set");
    EXPECT_EQ(m.kind, MatchKind::Exact);
    EXPECT_FALSE(reg.best("nope-nope", m));
}

// -------- 契約版本標記 --------

TEST(Contract, VersionSymbol) {
    EXPECT_STREQ(ds::command::action_registry_contract_version(), "e6_02/1.0.0");
}

}  // namespace
