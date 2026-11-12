// E9-11 權限授予狀態與決策記錄 — 契約測試（GoogleTest）
//
// 本測試組盯住驗收標準的四件事：
//   1. **每套件每權限三態**（未決／已授予／已拒絕）—— 且「未決 ≠ 已拒絕」在**查詢介面**上
//      真的可區分（具名三態枚舉的意義在決策流程：分得出「從沒問過」與「問過被拒」）。
//   2. **持久化** —— 「重啟」比照 E9-10：後端在**外層** scope、狀態物件在**內層** scope 解構
//      後以**新物件**讀同一個存活後端。**刻意不寫**「對同一個還活著的物件再查一次」。
//   3. **組出 E8-04 `HostEnvironment`，不修改 E8-04** —— 只呼叫其公開介面。
//   4. 『拒絕 → 載入回 `MissingPermission` **且 `missing` 附缺項**』與
//      『已授予 → 撤銷 → **重啟後**載入被擋』，且**不得靜默降級**。
//
// 關於「不得靜默降級成載入成功但功能沒作用」：這條有兩個方向，兩邊都測。
//   - 負向：被擋時要擋得明白（status **且** missing 的**內容與順序**，不只 status）。
//   - 正向：沒被擋時功能要**真的在** —— 一個回 `Ok` 但其實沒生效的載入必須被抓到，
//     所以正向案例不只斷言「沒被擋」，還斷言註冊入口**真的被呼叫過**、能力**真的進了
//     載入器的能力表**、且 provider 是該模組（見 `E9_11_Gate` 的
//     `GrantingEveryPermissionActuallyLoadsAndRegistersTheCapability`）。
//
// 平台中立：不含任何平台分支、不接真實檔案 I/O。
#include "permission_store.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "module_loader.hpp"  // E8-04：ModuleLoader / Module / LoadStatus（可讀不可改）

namespace {

using ds::ext::CapabilityKind;
using ds::ext::LoadResult;
using ds::ext::LoadStatus;
using ds::ext::Module;
using ds::ext::ModuleContext;
using ds::ext::ModuleLoader;
using ds::package::compose_host_environment;
using ds::package::deserialize_permission_records;
using ds::package::evaluate_permissions;
using ds::package::InstalledPackageStore;
using ds::package::kPermissionLedgerFormat;
using ds::package::MemoryInstallStorage;
using ds::package::MemoryPermissionStorage;
using ds::package::Package;
using ds::package::parse_package;
using ds::package::parse_permission_decision;
using ds::package::PermissionDecision;
using ds::package::PermissionGate;
using ds::package::PermissionLedger;
using ds::package::PermissionPrompt;
using ds::package::PermissionRecord;
using ds::package::permissions_awaiting_decision;
using ds::package::serialize_permission_records;
using ds::package::to_string;

// --- 測試前置（不是被測對象）-------------------------------------------------------

// 一份最小合法套件描述。requires / permissions 可注入（逗號分隔，E9-02 文字格式）。
std::string package_text(const std::string& name, const std::string& requires_csv,
                         const std::string& permissions_csv) {
    std::string text = "format_version: 1.0\nname: " + name + "\nversion: 0.1.0\n";
    if (!requires_csv.empty()) text += "requires: " + requires_csv + "\n";
    if (!permissions_csv.empty()) text += "permissions: " + permissions_csv + "\n";
    text += "---\nasset: icons/tray.png\n";
    return text;
}

Package make_package(const std::string& name, const std::string& requires_csv = "",
                     const std::string& permissions_csv = "fs.read, net.connect") {
    const auto r = parse_package(package_text(name, requires_csv, permissions_csv));
    if (!r.ok()) {
        throw std::runtime_error("測試前置失敗：套件描述無法解析 —— " + r.error().message);
    }
    return r.package();
}

// 一個會記錄「註冊入口有沒有真的被呼叫」的假模組。
// 這個計數是正向側的證據：一個回 Ok 但其實沒跑起來的載入，`registered_calls` 會是 0。
struct SpyModule {
    int register_calls = 0;
    int unload_calls = 0;

    Module make(const Package& pkg, std::string sensor_id = "cpu.usage") {
        Module m;
        m.manifest = pkg.manifest;
        m.on_register = [this, sensor_id](ModuleContext& ctx) {
            ++register_calls;
            return ctx.provide_sensor(sensor_id);
        };
        m.on_unload = [this]() { ++unload_calls; };
        return m;
    }
};

PermissionRecord rec(const std::string& pkg, const std::string& perm, PermissionDecision d) {
    return PermissionRecord{pkg, perm, d};
}

// 把 `HostEnvironment` 與 `ModuleLoader` 綁在同一個物件上，讓 env **活得比 loader 久**。
//
// 這不是風格偏好：E8-04 的 `ModuleLoader` 以 **const 參考**持有 host
// （`module_loader.hpp`：`explicit ModuleLoader(const HostEnvironment& host) : host_(host) {}`），
// 所以 `ModuleLoader loader(compose_host_environment(...));` 會讓它抓住一個在該敘述結束就被
// 銷毀的臨時物件 —— 懸空參考（UB）。本單元的 `compose_host_environment()` 是**按值回傳**的
// 工廠函式，正好湊成這個組合，故一律經此工具綁定，不直接把臨時物件餵給載入器。
struct GatedLoader {
    ds::ext::HostEnvironment env;  // 宣告在前 → 先初始化，且與 loader 同生命週期
    ModuleLoader loader;

    explicit GatedLoader(ds::ext::HostEnvironment e) : env(std::move(e)), loader(env) {}
};

// =============================================================================
// 1. 三態：未決 ≠ 已授予 ≠ 已拒絕，且在查詢介面上可區分
// =============================================================================

// 這是本單元存在的理由之一：若用一個 bool 表示，「從沒問過」與「問過被拒」會塌成同一個狀態，
// `CHG-20260829-05` 的「Q-C 權限在安裝時問」就實作不出來（不知道該問誰）。
TEST(E9_11_Tristate, UndecidedIsDistinguishableFromDeniedInTheQueryInterface) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);

    // 從沒問過。
    EXPECT_EQ(ledger.decision("com.example.hello", "fs.read"), PermissionDecision::Undecided);
    EXPECT_FALSE(ledger.is_decided("com.example.hello", "fs.read"));

    // 問過被拒。
    ASSERT_TRUE(ledger.deny("com.example.hello", "net.connect"));
    EXPECT_EQ(ledger.decision("com.example.hello", "net.connect"), PermissionDecision::Denied);
    EXPECT_TRUE(ledger.is_decided("com.example.hello", "net.connect"));

    // 兩者**不相等**——這一行就是「未決 ≠ 已拒絕」的釘子。
    EXPECT_NE(ledger.decision("com.example.hello", "fs.read"),
              ledger.decision("com.example.hello", "net.connect"));

    // 授予是第三態，與前兩者都不同。
    ASSERT_TRUE(ledger.grant("com.example.hello", "ui.overlay"));
    EXPECT_EQ(ledger.decision("com.example.hello", "ui.overlay"), PermissionDecision::Granted);
    EXPECT_NE(ledger.decision("com.example.hello", "ui.overlay"),
              ledger.decision("com.example.hello", "net.connect"));
    EXPECT_NE(ledger.decision("com.example.hello", "ui.overlay"),
              ledger.decision("com.example.hello", "fs.read"));
}

// 閘門評估也必須把兩種「被扣住」的原因分開回報（否則呼叫端無從判斷該不該問）。
TEST(E9_11_Tristate, GateReportsUndecidedAndDeniedAsSeparateReasons) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.hello", "", "fs.read, net.connect, ui.overlay");

    ledger.grant("com.example.hello", "fs.read");
    ledger.deny("com.example.hello", "net.connect");
    // ui.overlay 刻意不碰 → 未決。

    const PermissionGate gate = evaluate_permissions(ledger, pkg);
    EXPECT_FALSE(gate.loadable);
    EXPECT_EQ(gate.granted, (std::vector<std::string>{"fs.read"}));
    EXPECT_EQ(gate.denied, (std::vector<std::string>{"net.connect"}));
    EXPECT_EQ(gate.undecided, (std::vector<std::string>{"ui.overlay"}));
    // withheld 依 manifest 宣告順序（不是「未決清單接上已拒絕清單」）。
    EXPECT_EQ(gate.withheld, (std::vector<std::string>{"net.connect", "ui.overlay"}));
    EXPECT_TRUE(gate.needs_prompt());  // 還有沒問過的 → 該問

    // 把未決那一項也拒絕掉之後：仍然擋，但**不再需要詢問**（問過被拒，不要再問）。
    ledger.deny("com.example.hello", "ui.overlay");
    const PermissionGate after = evaluate_permissions(ledger, pkg);
    EXPECT_FALSE(after.loadable);
    EXPECT_TRUE(after.undecided.empty());
    EXPECT_FALSE(after.needs_prompt());
    EXPECT_EQ(after.denied, (std::vector<std::string>{"net.connect", "ui.overlay"}));
}

// 撤銷回到**未決**（可以再問），而不是變成已拒絕（不要再問）——三態的第三條邊。
TEST(E9_11_Tristate, RevokeReturnsToUndecidedRatherThanDenied) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);

    ASSERT_TRUE(ledger.grant("com.example.hello", "fs.read"));
    ASSERT_TRUE(ledger.revoke("com.example.hello", "fs.read"));
    EXPECT_EQ(ledger.decision("com.example.hello", "fs.read"), PermissionDecision::Undecided);
    EXPECT_NE(ledger.decision("com.example.hello", "fs.read"), PermissionDecision::Denied);
    EXPECT_FALSE(ledger.is_decided("com.example.hello", "fs.read"));

    // 撤銷一個本來就沒有決定的權限 → false（不是錯誤，也不留紀錄）。
    EXPECT_FALSE(ledger.revoke("com.example.hello", "fs.read"));
    EXPECT_EQ(ledger.size(), 0u);
}

TEST(E9_11_Tristate, DecisionLiteralsRoundTripAndUnknownLiteralIsRejected) {
    EXPECT_STREQ(to_string(PermissionDecision::Undecided), "undecided");
    EXPECT_STREQ(to_string(PermissionDecision::Granted), "granted");
    EXPECT_STREQ(to_string(PermissionDecision::Denied), "denied");

    PermissionDecision d = PermissionDecision::Granted;
    ASSERT_TRUE(parse_permission_decision("denied", d));
    EXPECT_EQ(d, PermissionDecision::Denied);
    ASSERT_TRUE(parse_permission_decision("undecided", d));
    EXPECT_EQ(d, PermissionDecision::Undecided);
    ASSERT_TRUE(parse_permission_decision("granted", d));
    EXPECT_EQ(d, PermissionDecision::Granted);

    // 無法辨識 → false，且**不改動 out**（不靜默當成某一態）。
    EXPECT_FALSE(parse_permission_decision("GRANTED", d));
    EXPECT_FALSE(parse_permission_decision("yes", d));
    EXPECT_FALSE(parse_permission_decision("", d));
    EXPECT_EQ(d, PermissionDecision::Granted);
}

TEST(E9_11_Tristate, RepeatedIdenticalDecisionReportsNoChange) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    EXPECT_TRUE(ledger.grant("com.example.hello", "fs.read"));
    EXPECT_FALSE(ledger.grant("com.example.hello", "fs.read"));  // 狀態未變
    EXPECT_TRUE(ledger.deny("com.example.hello", "fs.read"));    // 改變了（授予 → 拒絕）
    EXPECT_FALSE(ledger.deny("com.example.hello", "fs.read"));
}

TEST(E9_11_Tristate, EmptyIdentifiersAreRejectedRatherThanStored) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    EXPECT_THROW(ledger.grant("", "fs.read"), std::runtime_error);
    EXPECT_THROW(ledger.grant("com.example.hello", ""), std::runtime_error);
    EXPECT_THROW(ledger.deny("", ""), std::runtime_error);
    EXPECT_THROW(ledger.revoke("", "fs.read"), std::runtime_error);
    EXPECT_THROW(ledger.revoke_package(""), std::runtime_error);
    EXPECT_EQ(ledger.size(), 0u);
}

// =============================================================================
// 2. 格式：沿用 E7-01 文字格式 + E7-12 寫回，不發明新格式
// =============================================================================

TEST(E9_11_Format, SerializedTextIsAValidE7_01DocumentCarryingFormatVersion) {
    const std::string text = serialize_permission_records(
        {rec("com.example.hello", "fs.read", PermissionDecision::Granted)});

    const auto parsed = ds::format::parse(text);
    ASSERT_TRUE(parsed.ok()) << "行 " << parsed.error().line << "：" << parsed.error().message;
    EXPECT_EQ(parsed.document().format_version, kPermissionLedgerFormat);
    ASSERT_TRUE(parsed.document().root.is_map());
    ASSERT_NE(parsed.document().root.find("decisions"), nullptr);
}

TEST(E9_11_Format, RecordsRoundTripThroughTextUnchanged) {
    const std::vector<PermissionRecord> before{
        rec("com.example.alpha", "fs.read", PermissionDecision::Granted),
        rec("com.example.alpha", "net.connect", PermissionDecision::Denied),
        rec("com.example.beta", "ui.overlay", PermissionDecision::Granted),
    };
    const std::vector<PermissionRecord> after =
        deserialize_permission_records(serialize_permission_records(before));
    EXPECT_EQ(after, before);
}

// E7-12 已知限制 1（空容器不 round-trip 為同型：`key:` 下無縮排塌成 Null）。
TEST(E9_11_Format, EmptyLedgerRoundTripsAsEmptyRatherThanFailing) {
    const std::string text = serialize_permission_records({});
    EXPECT_TRUE(deserialize_permission_records(text).empty());
    // 缺鍵亦然（視為尚未做過任何決定，不是錯誤）。
    EXPECT_TRUE(deserialize_permission_records("format_version: 1.0\n").empty());
}

// E7-12 已知限制 2（map 鍵不支援引號）：故套件名與權限名都放在**值**的位置。
// E9-02 對 name 只要求非空、未禁止 ':'，所以這不是理論風險。
TEST(E9_11_Format, IdentifiersContainingColonsSurviveBecauseTheyAreValuesNotMapKeys) {
    const std::vector<PermissionRecord> before{
        rec("weird: package", "odd: permission", PermissionDecision::Denied),
    };
    const std::vector<PermissionRecord> after =
        deserialize_permission_records(serialize_permission_records(before));
    EXPECT_EQ(after, before);
}

TEST(E9_11_Format, WritingAnUndecidedRecordIsRejected) {
    // 未決 = 沒有紀錄。寫出一筆「未決紀錄」是語意矛盾 → 明確拒絕，而非靜默略過。
    EXPECT_THROW(serialize_permission_records(
                     {rec("com.example.hello", "fs.read", PermissionDecision::Undecided)}),
                 std::runtime_error);
}

TEST(E9_11_Format, DuplicateAndEmptyIdentifiersAreRejectedOnSerialize) {
    EXPECT_THROW(serialize_permission_records(
                     {rec("com.example.hello", "fs.read", PermissionDecision::Granted),
                      rec("com.example.hello", "fs.read", PermissionDecision::Denied)}),
                 std::runtime_error);
    EXPECT_THROW(serialize_permission_records({rec("", "fs.read", PermissionDecision::Granted)}),
                 std::runtime_error);
    EXPECT_THROW(
        serialize_permission_records({rec("com.example.hello", "", PermissionDecision::Granted)}),
        std::runtime_error);
}

TEST(E9_11_Format, CorruptOrStructurallyInvalidTextThrowsWithLocatableMessage) {
    EXPECT_THROW(deserialize_permission_records("format_version: 1.0\n  : broken\n"),
                 std::runtime_error);
    // 根節點非 Map。
    EXPECT_THROW(deserialize_permission_records("format_version: 1.0\n- a\n- b\n"),
                 std::runtime_error);
    // decisions 非清單。
    EXPECT_THROW(deserialize_permission_records("format_version: 1.0\ndecisions: nope\n"),
                 std::runtime_error);
    // 缺 permission 欄位。
    EXPECT_THROW(deserialize_permission_records("format_version: 1.0\ndecisions:\n  -\n"
                                                "    package: com.example.hello\n"
                                                "    decision: granted\n"),
                 std::runtime_error);
    // 無法辨識的 decision 字面值 —— 絕不靜默當成某一態。
    EXPECT_THROW(deserialize_permission_records("format_version: 1.0\ndecisions:\n  -\n"
                                                "    package: com.example.hello\n"
                                                "    permission: fs.read\n"
                                                "    decision: maybe\n"),
                 std::runtime_error);
    // 文件內重複的 (套件, 權限)。
    EXPECT_THROW(deserialize_permission_records("format_version: 1.0\ndecisions:\n  -\n"
                                                "    package: com.example.hello\n"
                                                "    permission: fs.read\n"
                                                "    decision: granted\n  -\n"
                                                "    package: com.example.hello\n"
                                                "    permission: fs.read\n"
                                                "    decision: denied\n"),
                 std::runtime_error);
}

// 人工編輯過的檔案可能出現字面 `undecided`：它不帶任何資訊，接受並略過（不是錯誤）。
TEST(E9_11_Format, ExplicitUndecidedRowIsAcceptedAndCarriesNoDecision) {
    const std::vector<PermissionRecord> parsed =
        deserialize_permission_records("format_version: 1.0\ndecisions:\n  -\n"
                                       "    package: com.example.hello\n"
                                       "    permission: fs.read\n"
                                       "    decision: undecided\n  -\n"
                                       "    package: com.example.hello\n"
                                       "    permission: net.connect\n"
                                       "    decision: denied\n");
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].permission_id, "net.connect");
    EXPECT_EQ(parsed[0].decision, PermissionDecision::Denied);
}

// =============================================================================
// 3. 持久化與「重啟」（後端存活、狀態物件解構後重新建構）
// =============================================================================

TEST(E9_11_Restart, GrantsAndDenialsSurviveTheLedgerObjectBeingDestroyedAndRebuilt) {
    MemoryPermissionStorage storage;  // ← 後端存活於外層 scope（相位 1 的「跨重啟」媒介）

    {
        PermissionLedger before_restart(storage);
        ASSERT_TRUE(before_restart.grant("com.example.alpha", "fs.read"));
        ASSERT_TRUE(before_restart.deny("com.example.alpha", "net.connect"));
        ASSERT_TRUE(before_restart.grant("com.example.beta", "ui.overlay"));
    }  // ← ledger 解構：此行之後，記憶體索引已經不存在了

    // 全新的帳本物件，指向同一個後端 —— 這就是「重啟後」。
    const PermissionLedger after_restart(storage);
    EXPECT_EQ(after_restart.size(), 3u);
    EXPECT_EQ(after_restart.decision("com.example.alpha", "fs.read"),
              PermissionDecision::Granted);
    // 「已拒絕」活過重啟**也**是重點：若它退回未決，下一次安裝流程會重新詢問已被拒絕的權限。
    EXPECT_EQ(after_restart.decision("com.example.alpha", "net.connect"),
              PermissionDecision::Denied);
    EXPECT_EQ(after_restart.decision("com.example.beta", "ui.overlay"),
              PermissionDecision::Granted);
    // 從沒被決定過的仍然是未決（不是被重啟「順便」變成某一態）。
    EXPECT_EQ(after_restart.decision("com.example.beta", "fs.read"),
              PermissionDecision::Undecided);
    EXPECT_EQ(after_restart.packages(),
              (std::vector<std::string>{"com.example.alpha", "com.example.beta"}));
}

// **撤銷也要落盤**：否則「已授予 → 撤銷 → 重啟 → 又變成已授予」就是另一種靜默降級。
TEST(E9_11_Restart, RevokedGrantDoesNotComeBackAfterRestart) {
    MemoryPermissionStorage storage;
    {
        PermissionLedger ledger(storage);
        ASSERT_TRUE(ledger.grant("com.example.hello", "fs.read"));
    }
    {
        PermissionLedger after_first_restart(storage);  // 重啟 1：授予確實落盤了
        ASSERT_EQ(after_first_restart.decision("com.example.hello", "fs.read"),
                  PermissionDecision::Granted);
        ASSERT_TRUE(after_first_restart.revoke("com.example.hello", "fs.read"));
    }
    const PermissionLedger after_second_restart(storage);  // 重啟 2：撤銷也落盤了
    EXPECT_EQ(after_second_restart.decision("com.example.hello", "fs.read"),
              PermissionDecision::Undecided);
    EXPECT_EQ(after_second_restart.size(), 0u);
}

TEST(E9_11_Restart, RevokingAWholePackageSurvivesRestart) {
    MemoryPermissionStorage storage;
    {
        PermissionLedger ledger(storage);
        ledger.grant("com.example.alpha", "fs.read");
        ledger.deny("com.example.alpha", "net.connect");
        ledger.grant("com.example.beta", "fs.read");
        EXPECT_EQ(ledger.revoke_package("com.example.alpha"), 2u);
        EXPECT_EQ(ledger.revoke_package("com.example.absent"), 0u);
    }
    const PermissionLedger after(storage);
    EXPECT_EQ(after.size(), 1u);
    EXPECT_EQ(after.decision("com.example.alpha", "fs.read"), PermissionDecision::Undecided);
    EXPECT_EQ(after.decision("com.example.alpha", "net.connect"), PermissionDecision::Undecided);
    EXPECT_EQ(after.decision("com.example.beta", "fs.read"), PermissionDecision::Granted);
}

TEST(E9_11_Store, FirstStartupIsNotAnErrorButCorruptBackendThrows) {
    MemoryPermissionStorage storage;
    EXPECT_EQ(PermissionLedger(storage).size(), 0u);  // 後端尚無 slot = 初次啟動

    storage.put("permissions", "format_version: 1.0\ndecisions: nope\n");
    // 絕不靜默當成空帳本——那會把所有「已拒絕」悄悄變回「未決」。
    // （`(void)` 不可省：`EXPECT_THROW(PermissionLedger(storage), …)` 會被當成變數宣告
    //   `PermissionLedger storage;`——most vexing parse，編譯就先失敗。）
    EXPECT_THROW((void)PermissionLedger(storage), std::runtime_error);
}

TEST(E9_11_Store, EmptySlotNameIsRejected) {
    MemoryPermissionStorage storage;
    EXPECT_THROW((void)PermissionLedger(storage, ""), std::runtime_error);
}

TEST(E9_11_Store, SeparateSlotsAreIndependentOnTheSameBackend) {
    MemoryPermissionStorage storage;
    {
        PermissionLedger a(storage, "profile.a");
        a.grant("com.example.hello", "fs.read");
        PermissionLedger b(storage, "profile.b");
        b.deny("com.example.hello", "fs.read");
    }
    EXPECT_EQ(PermissionLedger(storage, "profile.a").decision("com.example.hello", "fs.read"),
              PermissionDecision::Granted);
    EXPECT_EQ(PermissionLedger(storage, "profile.b").decision("com.example.hello", "fs.read"),
              PermissionDecision::Denied);
}

TEST(E9_11_Store, EveryDecisionIsWrittenThroughImmediately) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    EXPECT_TRUE(ledger.serialized().empty());  // 尚未有任何決定

    ledger.grant("com.example.hello", "fs.read");
    const std::string after_grant = ledger.serialized();
    ASSERT_FALSE(after_grant.empty());
    EXPECT_EQ(deserialize_permission_records(after_grant),
              (std::vector<PermissionRecord>{
                  rec("com.example.hello", "fs.read", PermissionDecision::Granted)}));

    ledger.revoke("com.example.hello", "fs.read");  // 撤銷同樣立即落盤
    EXPECT_TRUE(deserialize_permission_records(ledger.serialized()).empty());
}

TEST(E9_11_Store, ReloadPicksUpExternalBackendChanges) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    ledger.grant("com.example.hello", "fs.read");

    storage.put("permissions", serialize_permission_records(
                                   {rec("com.example.hello", "fs.read",
                                        PermissionDecision::Denied)}));
    EXPECT_EQ(ledger.decision("com.example.hello", "fs.read"), PermissionDecision::Granted);
    ledger.reload();
    EXPECT_EQ(ledger.decision("com.example.hello", "fs.read"), PermissionDecision::Denied);
}

TEST(E9_11_Store, RecordsAreListedInLexicographicOrder) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    ledger.grant("com.example.beta", "ui.overlay");
    ledger.deny("com.example.alpha", "net.connect");
    ledger.grant("com.example.alpha", "fs.read");

    const std::vector<PermissionRecord> all = ledger.records();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].package_id, "com.example.alpha");
    EXPECT_EQ(all[0].permission_id, "fs.read");
    EXPECT_EQ(all[1].permission_id, "net.connect");
    EXPECT_EQ(all[2].package_id, "com.example.beta");

    const std::vector<PermissionRecord> alpha = ledger.records_for("com.example.alpha");
    ASSERT_EQ(alpha.size(), 2u);
    EXPECT_EQ(alpha[0].permission_id, "fs.read");
    EXPECT_TRUE(ledger.records_for("com.example.absent").empty());
}

// =============================================================================
// 4. 投影成 E8-04 HostEnvironment + 載入閘控（驗收標準的兩條明文）
// =============================================================================

// 投影方向：只有 Granted 進集合；Undecided 與 Denied **都不進**。
TEST(E9_11_Projection, OnlyGrantedPermissionsReachTheHostEnvironment) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.hello", "", "fs.read, net.connect, ui.overlay");

    ledger.grant("com.example.hello", "fs.read");
    ledger.deny("com.example.hello", "net.connect");
    // ui.overlay → 未決

    const ds::ext::HostEnvironment env = compose_host_environment(ledger, pkg, {"host.tray_icon"});
    EXPECT_TRUE(env.grants_permission("fs.read"));
    EXPECT_FALSE(env.grants_permission("net.connect"));  // 已拒絕 → 不授予
    // **未決絕不等於預設授予**（樂觀預設 = 靜默降級，方向相反的同一個錯）。
    EXPECT_FALSE(env.grants_permission("ui.overlay"));
    // 能力（requires）由 host 自身決定，不是權限決策的一部分。
    EXPECT_TRUE(env.has_capability("host.tray_icon"));
    EXPECT_FALSE(env.has_capability("host.global_hotkey"));
}

// 驗收明文 1：『拒絕 → 載入回 MissingPermission 且 missing 附缺項』。
// **只斷言 status 不夠** —— missing 的內容也釘住，否則呼叫端不知道缺什麼、無法去問使用者。
TEST(E9_11_Gate, DeniedPermissionMakesLoadFailAsMissingPermissionWithTheMissingItemListed) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.hello", "", "fs.read, net.connect");

    ledger.grant("com.example.hello", "fs.read");
    ledger.deny("com.example.hello", "net.connect");  // ← 被拒的就是這一項

    const ds::ext::HostEnvironment env = compose_host_environment(ledger, pkg);
    GatedLoader gated(env);  // env 複製進 gated，生命週期與 loader 綁定（見 GatedLoader）
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg));

    EXPECT_EQ(r.status, LoadStatus::MissingPermission);
    EXPECT_FALSE(r.ok());
    // missing 的**內容**：恰好是被扣住的那一項，不是空清單、也不是全部權限。
    EXPECT_EQ(r.missing, (std::vector<std::string>{"net.connect"}));
    EXPECT_FALSE(r.message.empty());
    // 且與本單元的閘門評估逐項一致（呼叫端兩邊看到的是同一份缺項）。
    EXPECT_EQ(r.missing, evaluate_permissions(ledger, pkg).withheld);

    // 不靜默降級：既然被擋，就**什麼都沒發生**——註冊入口沒被呼叫、能力沒進表、模組未載入。
    EXPECT_EQ(spy.register_calls, 0);
    EXPECT_FALSE(gated.loader.is_loaded("com.example.hello"));
    EXPECT_EQ(gated.loader.capability_count(), 0u);
    EXPECT_FALSE(gated.loader.provides(CapabilityKind::Sensor, "cpu.usage"));
}

// 未決在閘控上與已拒絕同行為（都擋），這正是有損投影的**安全**方向。
TEST(E9_11_Gate, UndecidedPermissionBlocksLoadingJustLikeADenialDoes) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.hello", "", "fs.read");
    // 刻意什麼都不決定：fs.read 處於未決。

    GatedLoader gated(compose_host_environment(ledger, pkg));
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg));

    EXPECT_EQ(r.status, LoadStatus::MissingPermission);
    EXPECT_EQ(r.missing, (std::vector<std::string>{"fs.read"}));
    EXPECT_EQ(spy.register_calls, 0);
    EXPECT_FALSE(gated.loader.is_loaded("com.example.hello"));

    // 而本單元仍分得出「這是未決，該去問」而非「問過被拒」。
    const PermissionGate gate = evaluate_permissions(ledger, pkg);
    // 「只有未決、沒有任何拒絕」也**不可載入**（未決不是預設授予）。
    EXPECT_FALSE(gate.loadable);
    EXPECT_TRUE(gate.needs_prompt());
    EXPECT_EQ(gate.undecided, (std::vector<std::string>{"fs.read"}));
    EXPECT_TRUE(gate.denied.empty());
}

// missing 的**順序**也釘住：未決與已拒絕交錯時，依 manifest 宣告順序，不分組。
TEST(E9_11_Gate, MissingListFollowsManifestDeclarationOrderAcrossBothWithholdingReasons) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg =
        make_package("com.example.hello", "", "a.denied, b.granted, c.undecided, d.denied");
    ledger.deny("com.example.hello", "a.denied");
    ledger.grant("com.example.hello", "b.granted");
    ledger.deny("com.example.hello", "d.denied");

    const PermissionGate gate = evaluate_permissions(ledger, pkg);
    EXPECT_FALSE(gate.loadable);
    EXPECT_EQ(gate.withheld,
              (std::vector<std::string>{"a.denied", "c.undecided", "d.denied"}));

    GatedLoader gated(compose_host_environment(ledger, pkg));
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg));
    EXPECT_EQ(r.status, LoadStatus::MissingPermission);
    EXPECT_EQ(r.missing, gate.withheld);  // 逐項同序
}

// manifest 重複宣告同一權限時的誠實邊界：withheld 去重，E8-04 的 missing 不去重。
// 同序、同集合，但 E8-04 會多出重複項——明確釘住，而不是假裝兩者永遠相等。
TEST(E9_11_Gate, DuplicateDeclarationIsDedupedInTheGateButNotInE8_04Missing) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.hello", "", "fs.read, fs.read");

    const PermissionGate gate = evaluate_permissions(ledger, pkg);
    EXPECT_FALSE(gate.loadable);
    EXPECT_EQ(gate.withheld, (std::vector<std::string>{"fs.read"}));
    EXPECT_EQ(gate.undecided, (std::vector<std::string>{"fs.read"}));

    GatedLoader gated(compose_host_environment(ledger, pkg));
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg));
    EXPECT_EQ(r.status, LoadStatus::MissingPermission);
    EXPECT_EQ(r.missing, (std::vector<std::string>{"fs.read", "fs.read"}));
}

// 正向側：『沒被擋時功能要真的在』。
// 這個案例刻意不只斷言「沒被擋」——一個回 Ok 但其實沒生效的載入必須在這裡被抓到。
TEST(E9_11_Gate, GrantingEveryPermissionActuallyLoadsAndRegistersTheCapability) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg =
        make_package("com.example.hello", "host.tray_icon", "fs.read, net.connect");
    ledger.grant("com.example.hello", "fs.read");
    ledger.grant("com.example.hello", "net.connect");

    const PermissionGate gate = evaluate_permissions(ledger, pkg);
    ASSERT_TRUE(gate.loadable);
    ASSERT_TRUE(gate.withheld.empty());
    ASSERT_FALSE(gate.needs_prompt());

    const ds::ext::HostEnvironment env = compose_host_environment(ledger, pkg, {"host.tray_icon"});
    GatedLoader gated(env);  // env 複製進 gated，生命週期與 loader 綁定（見 GatedLoader）
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg, "cpu.usage"));

    ASSERT_EQ(r.status, LoadStatus::Ok) << r.message;
    EXPECT_TRUE(r.missing.empty());
    // ↓ 這四行才是「功能真的在」：註冊入口真的跑過、能力真的進了載入器的能力表、
    //   提供者是這個模組、且模組被追蹤為已載入。
    EXPECT_EQ(spy.register_calls, 1);
    EXPECT_TRUE(gated.loader.provides(CapabilityKind::Sensor, "cpu.usage"));
    EXPECT_EQ(gated.loader.provider_of(CapabilityKind::Sensor, "cpu.usage"), "com.example.hello");
    EXPECT_TRUE(gated.loader.is_loaded("com.example.hello"));
    EXPECT_EQ(gated.loader.capability_count(), 1u);

    // 卸載後能力確實離開能力表（正向路徑的對照，證明上面不是永遠為真的斷言）。
    EXPECT_TRUE(gated.loader.unload("com.example.hello"));
    EXPECT_EQ(spy.unload_calls, 1);
    EXPECT_FALSE(gated.loader.provides(CapabilityKind::Sensor, "cpu.usage"));
    EXPECT_EQ(gated.loader.capability_count(), 0u);
}

// 驗收明文 2：『已授予 → 撤銷 → **重啟後**載入被擋』。
// 重啟前**先證明它真的載入得起來且能力真的在**（否則「重啟後被擋」可能從頭到尾都沒作用）。
TEST(E9_11_Gate, GrantedThenRevokedThenRestartedLoadIsBlockedWithTheMissingItem) {
    MemoryPermissionStorage storage;  // ← 後端存活於外層 scope
    const Package pkg = make_package("com.example.hello", "", "fs.read");

    {
        PermissionLedger ledger(storage);
        ASSERT_TRUE(ledger.grant("com.example.hello", "fs.read"));

        // 已授予 → 載入成功，而且能力**真的**註冊了（正向側不是「只是沒被擋」）。
        GatedLoader gated(compose_host_environment(ledger, pkg));
        SpyModule spy;
        const LoadResult granted_load = gated.loader.load(spy.make(pkg));
        ASSERT_EQ(granted_load.status, LoadStatus::Ok) << granted_load.message;
        ASSERT_EQ(spy.register_calls, 1);
        ASSERT_TRUE(gated.loader.provides(CapabilityKind::Sensor, "cpu.usage"));

        // 撤銷。
        ASSERT_TRUE(ledger.revoke("com.example.hello", "fs.read"));
    }  // ← ledger 與 loader 全部解構：記憶體狀態已不存在

    // 重啟：全新帳本，只有後端可用。
    const PermissionLedger after_restart(storage);
    ASSERT_EQ(after_restart.decision("com.example.hello", "fs.read"),
              PermissionDecision::Undecided);  // 撤銷確實落盤了（沒有變回已授予）

    GatedLoader gated_after(compose_host_environment(after_restart, pkg));
    SpyModule spy_after;
    const LoadResult blocked = gated_after.loader.load(spy_after.make(pkg));

    EXPECT_EQ(blocked.status, LoadStatus::MissingPermission);
    EXPECT_EQ(blocked.missing, (std::vector<std::string>{"fs.read"}));
    EXPECT_FALSE(blocked.message.empty());
    // 不靜默降級：被擋就是什麼都沒發生。
    EXPECT_EQ(spy_after.register_calls, 0);
    EXPECT_FALSE(gated_after.loader.is_loaded("com.example.hello"));
    EXPECT_EQ(gated_after.loader.capability_count(), 0u);
    EXPECT_FALSE(gated_after.loader.provides(CapabilityKind::Sensor, "cpu.usage"));
}

// 拒絕也一樣活過重啟：重啟後仍然被擋，且仍然「不必再問」。
TEST(E9_11_Gate, DenialStillBlocksLoadingAfterRestartAndStillNeedsNoPrompt) {
    MemoryPermissionStorage storage;
    const Package pkg = make_package("com.example.hello", "", "fs.read");
    {
        PermissionLedger ledger(storage);
        ledger.deny("com.example.hello", "fs.read");
    }
    const PermissionLedger after(storage);
    EXPECT_EQ(after.decision("com.example.hello", "fs.read"), PermissionDecision::Denied);

    const PermissionGate gate = evaluate_permissions(after, pkg);
    EXPECT_FALSE(gate.loadable);
    EXPECT_FALSE(gate.needs_prompt());  // 問過被拒 → 不要再問
    EXPECT_EQ(gate.denied, (std::vector<std::string>{"fs.read"}));

    GatedLoader gated(compose_host_environment(after, pkg));
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg));
    EXPECT_EQ(r.status, LoadStatus::MissingPermission);
    EXPECT_EQ(r.missing, (std::vector<std::string>{"fs.read"}));
}

// 權限授予與**能力**（requires）是兩件事：權限全授予但 host 不提供能力 → 仍擋，
// 但狀態是 MissingRequirement（E8-04 先驗 requires），不是 MissingPermission。
// 這一行保證本單元不會把兩種缺項混為一談。
TEST(E9_11_Gate, MissingHostCapabilityIsReportedSeparatelyFromMissingPermission) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.hello", "host.tray_icon", "fs.read");
    ledger.grant("com.example.hello", "fs.read");

    // 刻意不傳 host_capabilities。
    GatedLoader gated(compose_host_environment(ledger, pkg));
    SpyModule spy;
    const LoadResult r = gated.loader.load(spy.make(pkg));
    EXPECT_EQ(r.status, LoadStatus::MissingRequirement);
    EXPECT_EQ(r.missing, (std::vector<std::string>{"host.tray_icon"}));
    EXPECT_EQ(spy.register_calls, 0);
}

// 沒有宣告任何權限的套件：空集合即可載入（不是「因為沒權限所以被擋」）。
TEST(E9_11_Gate, PackageDeclaringNoPermissionsIsLoadableWithAnEmptyLedger) {
    MemoryPermissionStorage storage;
    const PermissionLedger ledger(storage);
    const Package pkg = make_package("com.example.plain", "", "");
    ASSERT_TRUE(pkg.manifest.permissions.empty());

    const PermissionGate gate = evaluate_permissions(ledger, pkg);
    EXPECT_TRUE(gate.loadable);
    EXPECT_FALSE(gate.needs_prompt());

    GatedLoader gated(compose_host_environment(ledger, pkg));
    SpyModule spy;
    EXPECT_EQ(gated.loader.load(spy.make(pkg)).status, LoadStatus::Ok);
    EXPECT_EQ(spy.register_calls, 1);
    EXPECT_TRUE(gated.loader.provides(CapabilityKind::Sensor, "cpu.usage"));
}

// 每套件獨立：對 alpha 的授予不會外溢到 beta（「每套件每權限」的那個「每套件」）。
TEST(E9_11_Gate, DecisionsAreScopedPerPackageAndDoNotLeakAcrossPackages) {
    MemoryPermissionStorage storage;
    PermissionLedger ledger(storage);
    const Package alpha = make_package("com.example.alpha", "", "fs.read");
    const Package beta = make_package("com.example.beta", "", "fs.read");
    ledger.grant("com.example.alpha", "fs.read");

    EXPECT_TRUE(evaluate_permissions(ledger, alpha).loadable);
    EXPECT_FALSE(evaluate_permissions(ledger, beta).loadable);
    EXPECT_TRUE(compose_host_environment(ledger, alpha).grants_permission("fs.read"));
    EXPECT_FALSE(compose_host_environment(ledger, beta).grants_permission("fs.read"));

    GatedLoader gated(compose_host_environment(ledger, beta));
    SpyModule spy;
    EXPECT_EQ(gated.loader.load(spy.make(beta)).status, LoadStatus::MissingPermission);
}

// =============================================================================
// 5. 與 E9-10 的接點：安裝時該問哪些權限（Q-C，CHG-20260829-05）
// =============================================================================

// 這是三態在**決策流程**上的用處：只問未決的，已拒絕的不再問、已授予的不必問。
// 全程不載入套件本體（只讀 E9-10 持久化的中繼資料）。
TEST(E9_11_Prompt, OnlyUndecidedPermissionsOfInstalledPackagesAreQueuedForPrompting) {
    MemoryInstallStorage install_backend;
    MemoryPermissionStorage permission_backend;

    {
        InstalledPackageStore installed(install_backend);
        ASSERT_TRUE(installed.record(make_package("com.example.beta", "", "fs.read")));
        ASSERT_TRUE(installed.record(
            make_package("com.example.alpha", "", "fs.read, net.connect, ui.overlay")));

        PermissionLedger ledger(permission_backend);
        ledger.grant("com.example.alpha", "fs.read");    // 已授予 → 不必問
        ledger.deny("com.example.alpha", "net.connect");  // 已拒絕 → 不要再問
    }  // ← 兩個狀態物件都解構

    // 重啟後（兩個後端都還活著）才決定要問什麼——這就是「安裝時問」在重啟後的續行。
    const InstalledPackageStore installed(install_backend);
    const PermissionLedger ledger(permission_backend);

    const std::vector<PermissionPrompt> prompts =
        permissions_awaiting_decision(installed, ledger);
    // 套件字典序（alpha 先），同套件內依 manifest 宣告順序。
    EXPECT_EQ(prompts, (std::vector<PermissionPrompt>{{"com.example.alpha", "ui.overlay"},
                                                      {"com.example.beta", "fs.read"}}));

    // 把剩下的都決定掉之後，就沒有要問的了（也不會重新冒出已拒絕的那一項）。
    PermissionLedger writable(permission_backend);
    writable.deny("com.example.alpha", "ui.overlay");
    writable.grant("com.example.beta", "fs.read");
    EXPECT_TRUE(permissions_awaiting_decision(installed, writable).empty());
}

}  // namespace
