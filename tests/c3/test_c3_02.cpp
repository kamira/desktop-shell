// tests/c3/test_c3_02.cpp — C3-02 角色對話本（gtest）
//
// 涵蓋：建構（舞台指示成功掛上 bus / 撞名時 ready() 為 false）、load_script（E8-02 腳本定義,
// 空腳本拒絕）、start（推進到第一句台詞並經 C1-03 氣球顯示、未 load_script 即 start、重複 start
// 未 reset、speaker 未載入）、advance（逐句經 C1-03 氣球、腳本執行到底轉 Finished、未 start 即
// advance、已 Finished 後再 advance）、current_line / is_finished / is_active 查詢、E8-02
// 條件分支 / 跳轉（if goto 命中跳過中間台詞）、E8-03 舞台指示（`say "show hero"` 不算一句台詞、
// 經 CommandBus 真正分派、`advance()` 略過繼續找下一句真正台詞）、腳本執行期錯誤（未知指令 /
// 跳轉到未知標籤，經 ScriptError 回報且視為終止）、reset（收掉氣球、捨棄執行進度、腳本定義保留
// 可重新 start）、tick（透傳 C1-03 存活倒數，跨過 ttl 自動收掉氣球）、具名結果字串（NFR-02）。
//
// 本檔同時 #include "dialogue_book.hpp"（不直接引入 E1-14 / E1-02 標頭，見其上游 C1-03 說明）
// 與 "portrait_profile.hpp"（C1-02，需要真實 PortraitProfile 以 load_portrait 建立測試用
// speaker）——兩者可安全共存於同一翻譯單元，理由與 tests/c1/test_c1_03.cpp 相同：C1-03 已把
// E1-14 的實際串接 pimpl 隔離、把 C1-02 的實際串接橋接隔離，本檔並未引入 E1-02
// input_strategy.hpp，E8-02 / E8-03 的上游鏈（E7 系列 / E6-01 / E4-06）亦不涉及該 HitResult
// 命名碰撞（既有上游命名碰撞，見 content/profiles/c1_03/balloon_profile.hpp 說明）。
#include "dialogue_book.hpp"

#include <gtest/gtest.h>

#include <string>

#include "portrait_profile.hpp"  // C1-02（上游，可讀不可改）：PortraitProfile（測試用 speaker）

using ds::content::DialogueBook;
using ds::content::DialogueStatus;

using ds::command::CommandBus;
using ds::render::SurfaceSwitcher;
using ds::render::FixedFontMetrics;

using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;

using ds::profiles::PortraitProfile;
using ds::profiles::PortraitStatus;

using ds::script::Script;
using ds::script::Step;

namespace {

// 就地建構一個已載入的測試用角色（PortraitProfile 刪除複製 / 移動建構子，故一律就地建構，
// 同 tests/c1/test_c1_03.cpp 慣例）。
struct TestRig {
    NullKernelBackend backend{alpha_capable_matrix()};
    bool backend_initialized_ = backend.init();  // CHG-20260803-11：成員依宣告順序初始化，故此行在其後成員建構前完成（K-007 對齊）
    LayerStack layers{CapabilityMatrix::defaults()};
    CommandBus bus;
    SurfaceSwitcher surfaces;
    FixedFontMetrics metrics{1.0, 10.0};
};

}  // namespace

// -----------------------------------------------------------------------------
// 建構 / 舞台指示註冊
// -----------------------------------------------------------------------------

TEST(DialogueBook, ConstructedReadyWithNoScript) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    EXPECT_TRUE(book.ready());
    EXPECT_TRUE(book.registration_error().empty());
    EXPECT_TRUE(book.current_line().empty());
    EXPECT_FALSE(book.is_finished());
    EXPECT_FALSE(book.is_active());
    EXPECT_FALSE(book.is_showing());
}

TEST(DialogueBook, SecondBookOnSameBusFailsReadyAndRejectsLoadStart) {
    TestRig rig;
    DialogueBook first("book.a", rig.metrics, rig.bus, rig.surfaces);
    ASSERT_TRUE(first.ready());

    DialogueBook second("book.b", rig.metrics, rig.bus, rig.surfaces);  // 撞名：五個舞台指示已被佔用
    EXPECT_FALSE(second.ready());
    EXPECT_FALSE(second.registration_error().empty());

    Script script;
    script.add(Step::say("Hi"));
    EXPECT_EQ(second.load_script(script), DialogueStatus::Ok);  // load_script 本身不檢查 ready()

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    EXPECT_EQ(second.start(speaker, 5), DialogueStatus::Invalid);  // !ready() -> Invalid
}

// -----------------------------------------------------------------------------
// load_script — 空腳本拒絕
// -----------------------------------------------------------------------------

TEST(LoadScript, EmptyScriptRejected) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script empty;
    EXPECT_EQ(book.load_script(empty), DialogueStatus::Invalid);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    EXPECT_EQ(book.start(speaker, 5), DialogueStatus::Invalid);  // 未成功 load_script
}

TEST(LoadScript, ValidScriptAccepted) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello"));
    EXPECT_EQ(book.load_script(script), DialogueStatus::Ok);
}

// -----------------------------------------------------------------------------
// start — 推進到第一句台詞，經 C1-03 氣球顯示
// -----------------------------------------------------------------------------

TEST(Start, ShowsFirstLineThroughBalloon) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello")).add(Step::say("World"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "Hello");
    EXPECT_TRUE(book.is_showing());
    EXPECT_TRUE(book.is_active());
    EXPECT_FALSE(book.is_finished());
}

TEST(Start, WithoutLoadScriptIsInvalid) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    EXPECT_EQ(book.start(speaker, 5), DialogueStatus::Invalid);
}

TEST(Start, StartedTwiceWithoutResetIsInvalid) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello")).add(Step::say("World"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 10), DialogueStatus::Ok);

    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Invalid);  // 未 reset() 前不靜默重播
    EXPECT_EQ(book.current_line(), "Hello");                     // 未被覆寫
}

TEST(Start, UnloadedSpeakerRejectedButScriptPositionAdvances) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);  // 未 load_portrait
    EXPECT_EQ(book.start(speaker, 5), DialogueStatus::Invalid);
    EXPECT_FALSE(book.is_showing());
    EXPECT_EQ(book.current_line(), "Hello");  // 腳本已找到這句台詞（見標頭說明的既有取捨）
}

// -----------------------------------------------------------------------------
// advance — 逐句經 C1-03 氣球顯示；腳本執行到底轉 Finished
// -----------------------------------------------------------------------------

TEST(Advance, MovesToNextLineThroughBalloon) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello")).add(Step::say("World"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    ASSERT_EQ(book.current_line(), "Hello");

    EXPECT_EQ(book.advance(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "World");
    EXPECT_TRUE(book.is_showing());
    EXPECT_FALSE(book.is_finished());
}

TEST(Advance, PastLastLineReturnsFinishedAndClearsLine) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello")).add(Step::say("World"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    ASSERT_EQ(book.advance(speaker, 10), DialogueStatus::Ok);

    EXPECT_EQ(book.advance(speaker, 10), DialogueStatus::Finished);
    EXPECT_TRUE(book.current_line().empty());
    EXPECT_TRUE(book.is_finished());
    EXPECT_FALSE(book.is_showing());
    EXPECT_FALSE(book.is_active());
}

TEST(Advance, WithoutStartIsInvalid) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);
    Script script;
    script.add(Step::say("Hello"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    EXPECT_EQ(book.advance(speaker, 5), DialogueStatus::Invalid);
}

TEST(Advance, AfterFinishedIsInvalid) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);
    Script script;
    script.add(Step::say("Hello"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 5), DialogueStatus::Ok);
    ASSERT_EQ(book.advance(speaker, 5), DialogueStatus::Finished);

    EXPECT_EQ(book.advance(speaker, 5), DialogueStatus::Invalid);  // 已 Finished，不靜默重推進
}

// -----------------------------------------------------------------------------
// E8-02 條件分支 / 跳轉
// -----------------------------------------------------------------------------

TEST(Branch, ConditionalGotoSkipsInterveningLine) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::set("x", "1"))
        .add(Step::if_goto("x > 0", "positive"))
        .add(Step::say("skip-me"))
        .add(Step::label("positive"))
        .add(Step::say("took-branch"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "took-branch");  // "skip-me" 被條件跳轉略過

    EXPECT_EQ(book.advance(speaker, 10), DialogueStatus::Finished);
}

TEST(Branch, UnconditionalGotoJumpsForward) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::goto_label("target"))
        .add(Step::say("unreachable"))
        .add(Step::label("target"))
        .add(Step::say("reached"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "reached");
}

// -----------------------------------------------------------------------------
// E8-03 舞台指示 —— 不算一句台詞，經 CommandBus 真正分派
// -----------------------------------------------------------------------------

TEST(Presentation, DirectiveLineIsDispatchedNotShownAsDialogue) {
    TestRig rig;
    ASSERT_EQ(rig.surfaces.register_surface("main"), ds::render::SwitchStatus::Ok);
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("show hero"))
        .add(Step::say("switch_surface main"))
        .add(Step::say("Hello there"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "Hello there");  // 兩個舞台指示都被略過，非台詞

    EXPECT_TRUE(book.presentation().is_visible("hero"));   // show 真的經 CommandBus 分派
    EXPECT_EQ(rig.surfaces.current(), "main");              // switch_surface 真的切換

    EXPECT_EQ(book.advance(speaker, 10), DialogueStatus::Finished);
}

TEST(Presentation, WaitAndTransitionDirectivesRecordedAndSkipped) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("transition fade sceneA sceneB 0.5"))
        .add(Step::say("wait 0.25"))
        .add(Step::say("Line one"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "Line one");
    ASSERT_EQ(book.presentation().transitions().size(), 1u);
    EXPECT_EQ(book.presentation().transitions()[0].kind, "fade");
    ASSERT_EQ(book.presentation().waits().size(), 1u);
    EXPECT_DOUBLE_EQ(book.presentation().waits()[0].seconds, 0.25);
}

// -----------------------------------------------------------------------------
// 腳本執行期錯誤（不靜默，視為終止）
// -----------------------------------------------------------------------------

TEST(ScriptErrorCase, UnknownCommandReportsErrorAndTerminates) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step("bogus", "", ""));  // 未知 opcode（不用工廠函式，直接建構）
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(book.start(speaker, 5), DialogueStatus::ScriptError);
    EXPECT_EQ(book.last_error().step, 0u);
    EXPECT_TRUE(book.is_finished());  // 視為終止：需 reset() 才能重新開始
    EXPECT_FALSE(book.is_active());

    // 錯誤後再 advance：因已視為 Finished，不靜默地回 Invalid（而非默默假裝仍可推進）。
    EXPECT_EQ(book.advance(speaker, 5), DialogueStatus::Invalid);
}

TEST(ScriptErrorCase, GotoUnknownLabelReportsError) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("first")).add(Step::goto_label("nowhere"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);

    ASSERT_EQ(book.start(speaker, 5), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "first");

    EXPECT_EQ(book.advance(speaker, 5), DialogueStatus::ScriptError);
    EXPECT_EQ(book.last_error().step, 1u);
    EXPECT_TRUE(book.is_finished());
}

// -----------------------------------------------------------------------------
// reset — 收掉氣球、捨棄執行進度；腳本定義保留，可重新 start()
// -----------------------------------------------------------------------------

TEST(Reset, DiscardsProgressButKeepsScriptForRestart) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hello")).add(Step::say("World"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    ASSERT_EQ(book.advance(speaker, 10), DialogueStatus::Ok);
    ASSERT_EQ(book.current_line(), "World");

    book.reset();
    EXPECT_TRUE(book.current_line().empty());
    EXPECT_FALSE(book.is_showing());
    EXPECT_FALSE(book.is_active());
    EXPECT_FALSE(book.is_finished());

    // 腳本定義仍在（未重新 load_script）：可重新從頭 start()。
    EXPECT_EQ(book.start(speaker, 10), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "Hello");
}

TEST(Reset, AfterScriptErrorAllowsRestart) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step("bogus", "", ""));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 5), DialogueStatus::ScriptError);

    book.reset();
    EXPECT_FALSE(book.is_finished());
    EXPECT_EQ(book.start(speaker, 5), DialogueStatus::ScriptError);  // 同一份壞腳本再跑仍錯誤
}

TEST(Reset, OnFreshBookIsSafeNoOp) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);
    book.reset();  // 從未 start 過：安全 no-op，不崩潰
    EXPECT_FALSE(book.is_active());
}

// -----------------------------------------------------------------------------
// tick — 透傳 C1-03 存活倒數 / 逐字進度
// -----------------------------------------------------------------------------

TEST(Tick, AdvancesBalloonAndAutoDismissesOnTtlElapsed) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);

    Script script;
    script.add(Step::say("Hi")).add(Step::say("Bye"));
    ASSERT_EQ(book.load_script(script), DialogueStatus::Ok);

    PortraitProfile speaker("portrait.miku", rig.backend, rig.layers);
    ASSERT_EQ(speaker.load_portrait(ds::format::Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(book.start(speaker, 3), DialogueStatus::Ok);
    EXPECT_TRUE(book.is_showing());

    book.tick(2);
    EXPECT_TRUE(book.is_showing());  // 尚未跨過 ttl

    book.tick(1);  // 累計推進 3 tick，跨過 ttl -> 氣球自動消失
    EXPECT_FALSE(book.is_showing());
    EXPECT_TRUE(book.is_active());       // 對話本本身仍在播放中（腳本未結束）
    EXPECT_EQ(book.current_line(), "Hi");  // 台詞內容不受氣球自動消失影響

    // 仍可正常推進到下一句。
    EXPECT_EQ(book.advance(speaker, 5), DialogueStatus::Ok);
    EXPECT_EQ(book.current_line(), "Bye");
}

TEST(Tick, WhileNotShowingIsNoOp) {
    TestRig rig;
    DialogueBook book("book.a", rig.metrics, rig.bus, rig.surfaces);
    book.tick(5);  // 從未顯示過：no-op，不崩潰
    EXPECT_FALSE(book.is_showing());
}

// -----------------------------------------------------------------------------
// 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(NamedResults, ToStringCoversAllStatuses) {
    EXPECT_EQ(std::string(ds::content::to_string(DialogueStatus::Ok)), "Ok");
    EXPECT_EQ(std::string(ds::content::to_string(DialogueStatus::Invalid)), "Invalid");
    EXPECT_EQ(std::string(ds::content::to_string(DialogueStatus::ScriptError)), "ScriptError");
    EXPECT_EQ(std::string(ds::content::to_string(DialogueStatus::Finished)), "Finished");
}
