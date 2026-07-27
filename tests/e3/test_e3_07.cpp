// E3-07 剪貼簿寫入致動器 — gtest 契約測試。
//
// 覆蓋：三個具名命令註冊到 E6-01 匯流排、寫入文字經 dispatch 分派、多資料型別
// （text / html / image）、清空剪貼簿、null 後端狀態一致（read 驗證）、
// 空 / 缺 / 型別錯 / 無效必填參數回 Failed（不崩潰、不改後端狀態）、未知命令回 NotFound、
// unregister、直接呼叫處理器、型別 ↔ 字串轉換、契約版本標記。
#include "clipboard_write_actuator.hpp"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using ds::actuators::ClipboardContentType;
using ds::actuators::ClipboardData;
using ds::actuators::ClipboardBackend;
using ds::actuators::ClipboardWriteActuator;
using ds::actuators::NullClipboardBackend;
using ds::actuators::clipboard_type_from_string;
using ds::actuators::clipboard_type_name;
using ds::actuators::kCmdClipboardClear;
using ds::actuators::kCmdClipboardRead;
using ds::actuators::kCmdClipboardWrite;
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandStatus;

namespace {

// 便捷：讀取一個成功結果的字串回傳值（寫入 / 讀取的內容）。
std::string result_content(const ds::command::CommandResult& r) {
    EXPECT_TRUE(r.value.as_string().has_value());
    return r.value.as_string().value_or(std::string{"<none>"});
}

}  // namespace

// ---------------------------------------------------------------------------
// 命令註冊到匯流排
// ---------------------------------------------------------------------------
TEST(E3_07_Register, RegistersAllThreeCommands) {
    CommandBus bus;
    ClipboardWriteActuator actuator;  // 預設綁 NullClipboardBackend
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_TRUE(bus.has_command(kCmdClipboardWrite));
    EXPECT_TRUE(bus.has_command(kCmdClipboardClear));
    EXPECT_TRUE(bus.has_command(kCmdClipboardRead));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_07_Register, DuplicateRegistrationRollsBackAndFails) {
    CommandBus bus;
    ClipboardWriteActuator a1;
    ClipboardWriteActuator a2;
    ASSERT_TRUE(a1.register_on(bus));
    // 第二個致動器要掛同名命令：E6-01 不覆蓋 → register_on 應回滾並回 false，
    // 且不得改動已註冊的三個命令（仍是 a1 的）。
    EXPECT_FALSE(a2.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(3));
}

TEST(E3_07_Register, NullBackendActuatorCannotRegister) {
    CommandBus bus;
    ClipboardWriteActuator actuator{std::shared_ptr<ClipboardBackend>{}};
    EXPECT_FALSE(actuator.register_on(bus));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
}

TEST(E3_07_Register, UnregisterRemovesAllThree) {
    CommandBus bus;
    ClipboardWriteActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(3));
    EXPECT_EQ(bus.command_count(), static_cast<std::size_t>(0));
    // 再 unregister 一次：已無命令，回 0。
    EXPECT_EQ(actuator.unregister_from(bus), static_cast<std::size_t>(0));
}

// ---------------------------------------------------------------------------
// 寫入文字經 E6-01 分派 + null 後端狀態一致（read 驗證）
// ---------------------------------------------------------------------------
TEST(E3_07_Write, WriteTextViaBusUpdatesBackend) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto result = bus.dispatch(kCmdClipboardWrite, CommandArgs{}.set("content", std::string{"hello"}));
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommandStatus::Ok);
    EXPECT_EQ(result_content(result), std::string{"hello"});
    EXPECT_EQ(result.message, std::string{"text"});  // 預設型別
    // null 後端狀態一致：read 應得同值同型別。
    EXPECT_EQ(backend->read(), (ClipboardData{ClipboardContentType::PlainText, "hello"}));
    EXPECT_EQ(actuator.current_data().content, std::string{"hello"});
}

TEST(E3_07_Write, ReadReflectsPreviouslyWrittenContent) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdClipboardWrite, CommandArgs{}.set("content", std::string{"copied text"}));
    auto got = bus.dispatch(kCmdClipboardRead);
    EXPECT_TRUE(got.ok());
    EXPECT_EQ(result_content(got), std::string{"copied text"});
    EXPECT_EQ(got.message, std::string{"text"});
}

TEST(E3_07_Write, ExplicitTextTypeIsAccepted) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdClipboardWrite,
                          CommandArgs{}.set("content", std::string{"plain"}).set("type", std::string{"text"}));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(backend->read().type, ClipboardContentType::PlainText);
}

// ---------------------------------------------------------------------------
// 多資料型別：純文字 / HTML / 影像參照
// ---------------------------------------------------------------------------
TEST(E3_07_MultiType, WriteHtml) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdClipboardWrite,
                          CommandArgs{}.set("content", std::string{"<b>bold</b>"}).set("type", std::string{"html"}));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.message, std::string{"html"});
    EXPECT_EQ(backend->read(), (ClipboardData{ClipboardContentType::Html, "<b>bold</b>"}));
}

TEST(E3_07_MultiType, WriteImageReference) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 影像型別：content 為影像參照（路徑 / 識別碼字串），非影像位元組。
    auto r = bus.dispatch(kCmdClipboardWrite,
                          CommandArgs{}.set("content", std::string{"asset://img/42"}).set("type", std::string{"image"}));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.message, std::string{"image"});
    EXPECT_EQ(backend->read(), (ClipboardData{ClipboardContentType::ImageRef, "asset://img/42"}));
}

TEST(E3_07_MultiType, LaterWriteReplacesEarlier) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdClipboardWrite, CommandArgs{}.set("content", std::string{"first"}));
    bus.dispatch(kCmdClipboardWrite,
                 CommandArgs{}.set("content", std::string{"<i>second</i>"}).set("type", std::string{"html"}));
    // 後寫覆蓋前寫（型別亦更新）。
    EXPECT_EQ(backend->read(), (ClipboardData{ClipboardContentType::Html, "<i>second</i>"}));
}

// ---------------------------------------------------------------------------
// 清空剪貼簿
// ---------------------------------------------------------------------------
TEST(E3_07_Clear, ClearEmptiesClipboard) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    bus.dispatch(kCmdClipboardWrite, CommandArgs{}.set("content", std::string{"to be cleared"}));
    EXPECT_FALSE(backend->read().empty());

    auto cleared = bus.dispatch(kCmdClipboardClear);
    EXPECT_TRUE(cleared.ok());
    EXPECT_EQ(cleared.message, std::string{"cleared"});
    // 清空後：Empty 型別 + 空內容。read 驗證一致。
    EXPECT_TRUE(backend->read().empty());
    EXPECT_EQ(backend->read(), ClipboardData::empty_data());

    auto after = bus.dispatch(kCmdClipboardRead);
    EXPECT_TRUE(after.ok());
    EXPECT_EQ(after.message, std::string{"empty"});
    EXPECT_EQ(result_content(after), std::string{});
}

// ---------------------------------------------------------------------------
// 空 / 缺 / 型別錯 / 無效必填參數 → Failed（不崩潰、不改後端狀態）
// ---------------------------------------------------------------------------
TEST(E3_07_Validation, WriteMissingContentFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>(
        ClipboardData{ClipboardContentType::PlainText, "sentinel"});
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    auto r = bus.dispatch(kCmdClipboardWrite, CommandArgs{});
    EXPECT_EQ(r.status, CommandStatus::Failed);
    // 後端狀態不變。
    EXPECT_EQ(backend->read().content, std::string{"sentinel"});
}

TEST(E3_07_Validation, WriteWrongContentTypeFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>(
        ClipboardData{ClipboardContentType::PlainText, "sentinel"});
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // content 給整數而非字串：has() 為真但 get_string 回 nullopt → Failed。
    auto r = bus.dispatch(kCmdClipboardWrite, CommandArgs{}.set("content", 123));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->read().content, std::string{"sentinel"});
}

TEST(E3_07_Validation, WriteEmptyContentFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>(
        ClipboardData{ClipboardContentType::PlainText, "sentinel"});
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // 空內容：寫入無效（清空應用 clipboard.clear）→ Failed，不改後端狀態。
    auto r = bus.dispatch(kCmdClipboardWrite, CommandArgs{}.set("content", std::string{}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->read().content, std::string{"sentinel"});
}

TEST(E3_07_Validation, WriteInvalidTypeFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>(
        ClipboardData{ClipboardContentType::PlainText, "sentinel"});
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // type 為未知字串 → Failed，不改後端狀態。
    auto r = bus.dispatch(kCmdClipboardWrite,
                          CommandArgs{}.set("content", std::string{"x"}).set("type", std::string{"rtf"}));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->read().content, std::string{"sentinel"});
}

TEST(E3_07_Validation, WriteWrongTypeParamTypeFails) {
    CommandBus bus;
    auto backend = std::make_shared<NullClipboardBackend>(
        ClipboardData{ClipboardContentType::PlainText, "sentinel"});
    ClipboardWriteActuator actuator{backend};
    ASSERT_TRUE(actuator.register_on(bus));

    // type 給整數而非字串 → Failed。
    auto r = bus.dispatch(kCmdClipboardWrite,
                          CommandArgs{}.set("content", std::string{"x"}).set("type", 7));
    EXPECT_EQ(r.status, CommandStatus::Failed);
    EXPECT_EQ(backend->read().content, std::string{"sentinel"});
}

// ---------------------------------------------------------------------------
// 未知命令：匯流排回 NotFound（不崩潰）
// ---------------------------------------------------------------------------
TEST(E3_07_Dispatch, UnknownCommandReturnsNotFound) {
    CommandBus bus;
    ClipboardWriteActuator actuator;
    ASSERT_TRUE(actuator.register_on(bus));
    EXPECT_EQ(bus.dispatch("clipboard.nonexistent").status, CommandStatus::NotFound);
}

// ---------------------------------------------------------------------------
// null 後端狀態一致：一連串寫入 / 讀取 / 清空後仍一致
// ---------------------------------------------------------------------------
TEST(E3_07_NullBackend, SequenceRemainsConsistent) {
    NullClipboardBackend backend;
    EXPECT_TRUE(backend.read().empty());
    EXPECT_EQ(backend.state().type, ClipboardContentType::Empty);

    backend.write(ClipboardData{ClipboardContentType::PlainText, "a"});
    EXPECT_EQ(backend.read(), (ClipboardData{ClipboardContentType::PlainText, "a"}));

    backend.write(ClipboardData{ClipboardContentType::Html, "<p>b</p>"});
    EXPECT_EQ(backend.read(), (ClipboardData{ClipboardContentType::Html, "<p>b</p>"}));

    backend.clear();
    EXPECT_TRUE(backend.read().empty());
    EXPECT_EQ(backend.state(), ClipboardData::empty_data());
}

TEST(E3_07_NullBackend, InjectedInitialState) {
    NullClipboardBackend backend{ClipboardData{ClipboardContentType::ImageRef, "id://7"}};
    EXPECT_EQ(backend.read(), (ClipboardData{ClipboardContentType::ImageRef, "id://7"}));
    EXPECT_FALSE(backend.read().empty());
}

// ---------------------------------------------------------------------------
// 處理器可不經匯流排直接呼叫（語意等價）
// ---------------------------------------------------------------------------
TEST(E3_07_Handler, DirectHandlerCallWritesContent) {
    auto backend = std::make_shared<NullClipboardBackend>();
    ClipboardWriteActuator actuator{backend};
    auto r = actuator.handle_write(CommandArgs{}.set("content", std::string{"direct"}));
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(result_content(r), std::string{"direct"});
    EXPECT_EQ(backend->read().content, std::string{"direct"});
}

TEST(E3_07_Handler, NoBackendHandlersFail) {
    ClipboardWriteActuator actuator{std::shared_ptr<ClipboardBackend>{}};
    EXPECT_EQ(actuator.handle_read(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_write(CommandArgs{}.set("content", std::string{"x"})).status,
              CommandStatus::Failed);
    EXPECT_EQ(actuator.handle_clear(CommandArgs{}).status, CommandStatus::Failed);
    EXPECT_EQ(actuator.current_data(), ClipboardData::empty_data());
}

// ---------------------------------------------------------------------------
// 型別 ↔ 字串轉換
// ---------------------------------------------------------------------------
TEST(E3_07_TypeMapping, NameRoundTrip) {
    EXPECT_EQ(std::string{clipboard_type_name(ClipboardContentType::PlainText)}, std::string{"text"});
    EXPECT_EQ(std::string{clipboard_type_name(ClipboardContentType::Html)}, std::string{"html"});
    EXPECT_EQ(std::string{clipboard_type_name(ClipboardContentType::ImageRef)}, std::string{"image"});
    EXPECT_EQ(std::string{clipboard_type_name(ClipboardContentType::Empty)}, std::string{"empty"});
}

TEST(E3_07_TypeMapping, FromStringParsesKnownRejectsUnknown) {
    EXPECT_EQ(clipboard_type_from_string("text"), ClipboardContentType::PlainText);
    EXPECT_EQ(clipboard_type_from_string("html"), ClipboardContentType::Html);
    EXPECT_EQ(clipboard_type_from_string("image"), ClipboardContentType::ImageRef);
    // `empty` 不是可寫入型別（清空由 clipboard.clear 表達）；未知字串亦回 nullopt。
    EXPECT_FALSE(clipboard_type_from_string("empty").has_value());
    EXPECT_FALSE(clipboard_type_from_string("rtf").has_value());
    EXPECT_FALSE(clipboard_type_from_string("").has_value());
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(E3_07_Contract, VersionTag) {
    EXPECT_EQ(std::string{ds::actuators::clipboard_write_contract_version()},
              std::string{"e3_07/1.0.0"});
}
