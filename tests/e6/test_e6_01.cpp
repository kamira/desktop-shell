// E6-01 命令匯流排與分派 — 單元測試（gtest）
//
// 驗證擴充點 3「動作」契約：
//   註冊 → 分派 → 處理器被呼叫、參數傳遞、回傳值、未知命令不崩潰、重複註冊拒絕、
//   unregister、has_command、多命令獨立、空 id / 空處理器拒絕、值型別安全存取、
//   Command 打包分派、命令列舉、契約版本標記。
// 全程純邏輯，不依賴任何平台後端或真實副作用。
#include "command_bus.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using ds::command::Command;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::command::CommandValue;

namespace {

// -------- CommandValue：型別安全存取 --------

TEST(CommandValue, DefaultIsNull) {
    CommandValue v;
    EXPECT_TRUE(v.is_null());
    EXPECT_EQ(v.type(), CommandValue::Type::Null);
    EXPECT_FALSE(v.as_bool().has_value());
    EXPECT_FALSE(v.as_int().has_value());
    EXPECT_FALSE(v.as_double().has_value());
    EXPECT_FALSE(v.as_string().has_value());
}

TEST(CommandValue, HoldsEachTypeAndRejectsMismatch) {
    CommandValue b(true);
    EXPECT_EQ(b.type(), CommandValue::Type::Bool);
    ASSERT_TRUE(b.as_bool().has_value());
    EXPECT_TRUE(*b.as_bool());
    EXPECT_FALSE(b.as_int().has_value());  // 型別不符 -> nullopt，不 UB

    CommandValue i(std::int64_t{42});
    EXPECT_EQ(i.type(), CommandValue::Type::Int);
    ASSERT_TRUE(i.as_int().has_value());
    EXPECT_EQ(*i.as_int(), 42);
    EXPECT_FALSE(i.as_double().has_value());

    CommandValue d(3.5);
    EXPECT_EQ(d.type(), CommandValue::Type::Double);
    ASSERT_TRUE(d.as_double().has_value());
    EXPECT_DOUBLE_EQ(*d.as_double(), 3.5);

    CommandValue s(std::string("vol"));
    EXPECT_EQ(s.type(), CommandValue::Type::String);
    ASSERT_TRUE(s.as_string().has_value());
    EXPECT_EQ(*s.as_string(), "vol");

    CommandValue cs("lit");  // const char* -> string
    EXPECT_EQ(cs.type(), CommandValue::Type::String);
    ASSERT_TRUE(cs.as_string().has_value());
    EXPECT_EQ(*cs.as_string(), "lit");

    CommandValue ii(7);  // int -> int64
    EXPECT_EQ(ii.type(), CommandValue::Type::Int);
    EXPECT_EQ(*ii.as_int(), 7);
}

TEST(CommandValue, Equality) {
    EXPECT_EQ(CommandValue(1), CommandValue(1));
    EXPECT_NE(CommandValue(1), CommandValue(2));
    EXPECT_NE(CommandValue(1), CommandValue(std::string("1")));  // 型別不同
    EXPECT_NE(CommandValue(true), CommandValue(std::int64_t{1}));
    EXPECT_EQ(CommandValue(), CommandValue());
}

// -------- CommandArgs：具名參數字典 --------

TEST(CommandArgs, SetHasFindAndTypedGetters) {
    CommandArgs args;
    args.set("level", 60).set("muted", false).set("label", "master");

    EXPECT_EQ(args.size(), 3u);
    EXPECT_FALSE(args.empty());
    EXPECT_TRUE(args.has("level"));
    EXPECT_FALSE(args.has("missing"));

    ASSERT_TRUE(args.get_int("level").has_value());
    EXPECT_EQ(*args.get_int("level"), 60);
    ASSERT_TRUE(args.get_bool("muted").has_value());
    EXPECT_FALSE(*args.get_bool("muted"));
    ASSERT_TRUE(args.get_string("label").has_value());
    EXPECT_EQ(*args.get_string("label"), "master");

    // 缺鍵 / 型別不符 -> nullopt
    EXPECT_FALSE(args.get_int("missing").has_value());
    EXPECT_FALSE(args.get_string("level").has_value());
    EXPECT_EQ(args.find("missing"), nullptr);
    ASSERT_NE(args.find("level"), nullptr);
}

TEST(CommandArgs, SetOverwrites) {
    CommandArgs args;
    args.set("k", 1).set("k", 2);
    EXPECT_EQ(args.size(), 1u);
    EXPECT_EQ(*args.get_int("k"), 2);
}

// -------- CommandBus：註冊與分派 --------

TEST(CommandBus, RegisterThenDispatchCallsHandler) {
    CommandBus bus;
    int calls = 0;
    ASSERT_TRUE(bus.register_command("noop", [&](const CommandArgs&) {
        ++calls;
        return CommandResult::make_ok();
    }));
    EXPECT_TRUE(bus.has_command("noop"));
    EXPECT_EQ(bus.command_count(), 1u);

    CommandResult r = bus.dispatch("noop");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.status, CommandStatus::Ok);
    EXPECT_EQ(calls, 1);
}

TEST(CommandBus, DispatchPassesArgsToHandler) {
    CommandBus bus;
    std::int64_t seen = -1;
    bus.register_command("set_volume", [&](const CommandArgs& a) {
        seen = a.get_int("level").value_or(-1);
        return CommandResult::make_ok();
    });

    CommandArgs args;
    args.set("level", 73);
    EXPECT_TRUE(bus.dispatch("set_volume", args).ok());
    EXPECT_EQ(seen, 73);
}

TEST(CommandBus, HandlerReturnValuePropagates) {
    CommandBus bus;
    bus.register_command("get_volume", [](const CommandArgs&) {
        return CommandResult::make_ok(CommandValue(std::int64_t{55}));
    });
    CommandResult r = bus.dispatch("get_volume");
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.value.as_int().has_value());
    EXPECT_EQ(*r.value.as_int(), 55);
}

TEST(CommandBus, HandlerFailurePropagates) {
    CommandBus bus;
    bus.register_command("risky", [](const CommandArgs&) {
        return CommandResult::make_failed("device busy");
    });
    CommandResult r = bus.dispatch("risky");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(r.message, "device busy");
}

TEST(CommandBus, UnknownCommandReturnsNotFoundAndDoesNotCrash) {
    CommandBus bus;
    CommandResult r = bus.dispatch("ghost");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.status, CommandStatus::NotFound);
    EXPECT_NE(r.message.find("ghost"), std::string::npos);
    // 帶參數的未知命令同樣安全
    CommandArgs a;
    a.set("x", 1);
    EXPECT_EQ(bus.dispatch("ghost", a).status, CommandStatus::NotFound);
}

TEST(CommandBus, DuplicateRegistrationRejectedNoOverwrite) {
    CommandBus bus;
    ASSERT_TRUE(bus.register_command("act", [](const CommandArgs&) {
        return CommandResult::make_ok(CommandValue(std::string("first")));
    }));
    // 重複 id：拒絕，且不覆蓋原處理器
    EXPECT_FALSE(bus.register_command("act", [](const CommandArgs&) {
        return CommandResult::make_ok(CommandValue(std::string("second")));
    }));
    EXPECT_EQ(bus.command_count(), 1u);
    CommandResult r = bus.dispatch("act");
    ASSERT_TRUE(r.value.as_string().has_value());
    EXPECT_EQ(*r.value.as_string(), "first");  // 仍是第一個
}

TEST(CommandBus, RejectsEmptyIdAndNullHandler) {
    CommandBus bus;
    EXPECT_FALSE(bus.register_command("", [](const CommandArgs&) {
        return CommandResult::make_ok();
    }));
    EXPECT_FALSE(bus.register_command("x", nullptr));
    EXPECT_EQ(bus.command_count(), 0u);
}

TEST(CommandBus, UnregisterRemovesCommand) {
    CommandBus bus;
    bus.register_command("temp", [](const CommandArgs&) { return CommandResult::make_ok(); });
    EXPECT_TRUE(bus.has_command("temp"));

    EXPECT_TRUE(bus.unregister("temp"));
    EXPECT_FALSE(bus.has_command("temp"));
    EXPECT_EQ(bus.command_count(), 0u);
    EXPECT_EQ(bus.dispatch("temp").status, CommandStatus::NotFound);

    // 再次 unregister 未知 id -> false
    EXPECT_FALSE(bus.unregister("temp"));
}

TEST(CommandBus, ReRegisterAfterUnregisterSucceeds) {
    CommandBus bus;
    bus.register_command("a", [](const CommandArgs&) { return CommandResult::make_ok(); });
    bus.unregister("a");
    EXPECT_TRUE(bus.register_command("a", [](const CommandArgs&) {
        return CommandResult::make_ok(CommandValue(std::string("v2")));
    }));
    EXPECT_EQ(*bus.dispatch("a").value.as_string(), "v2");
}

TEST(CommandBus, MultipleCommandsAreIndependent) {
    CommandBus bus;
    bus.register_command("up", [](const CommandArgs&) {
        return CommandResult::make_ok(CommandValue(std::string("up")));
    });
    bus.register_command("down", [](const CommandArgs&) {
        return CommandResult::make_ok(CommandValue(std::string("down")));
    });
    EXPECT_EQ(bus.command_count(), 2u);
    EXPECT_EQ(*bus.dispatch("up").value.as_string(), "up");
    EXPECT_EQ(*bus.dispatch("down").value.as_string(), "down");
}

TEST(CommandBus, DispatchWithPackagedCommand) {
    CommandBus bus;
    std::int64_t seen = -1;
    bus.register_command("set", [&](const CommandArgs& a) {
        seen = a.get_int("v").value_or(-1);
        return CommandResult::make_ok();
    });
    Command cmd;
    cmd.id = "set";
    cmd.args.set("v", 99);
    EXPECT_TRUE(bus.dispatch(cmd).ok());
    EXPECT_EQ(seen, 99);
}

TEST(CommandBus, CommandIdsAreListedSorted) {
    CommandBus bus;
    bus.register_command("charlie", [](const CommandArgs&) { return CommandResult::make_ok(); });
    bus.register_command("alpha", [](const CommandArgs&) { return CommandResult::make_ok(); });
    bus.register_command("bravo", [](const CommandArgs&) { return CommandResult::make_ok(); });
    std::vector<std::string> ids = bus.command_ids();
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], "alpha");
    EXPECT_EQ(ids[1], "bravo");
    EXPECT_EQ(ids[2], "charlie");
}

TEST(CommandBus, EmptyBusHasNoCommands) {
    CommandBus bus;
    EXPECT_EQ(bus.command_count(), 0u);
    EXPECT_FALSE(bus.has_command("anything"));
    EXPECT_TRUE(bus.command_ids().empty());
}

// -------- 契約版本標記 --------

TEST(Contract, VersionTagPresent) {
    EXPECT_STREQ(ds::command::contract_version(), "e6_01/1.0.0");
}

}  // namespace
