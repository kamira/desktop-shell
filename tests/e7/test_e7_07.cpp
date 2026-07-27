// tests/e7/test_e7_07.cpp — E7-07 熱重載 契約測試
//
// 驗證宣告式格式文件的熱重載生命週期（engine 層 / 平台中立）：
//   1. 初次載入：poll 首次即讀取來源、解析、當前值生效。
//   2. 內容變更 → 重載成功 → 新值生效（差異式套用）。
//   3. 重載解析失敗 → **保留舊有有效值** + 以 E7-06 產生定位到行的診斷（不靜默）。
//   4. 成功 / 失敗回呼：各於對應事件觸發、且僅在該事件觸發。
//   5. 無變更不重載：revision 未變則 Unchanged、不觸發回呼；相同內容寫入不算變更。
//   6. 邊界：載入前無 Document、初次即失敗則無舊值可留、強制 reload 忽略 revision、
//      成功後舊診斷清空、失敗後同 revision 不重試。

#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "document.hpp"     // E7-01
#include "visibility.hpp"   // E7-06
#include "hot_reload.hpp"   // E7-07

using namespace ds::format;

namespace {

// 有效文件 A：root 含 name=alpha, width=800。
const std::string kValidA =
    "format_version: 1.0\n"
    "name: alpha\n"
    "width: 800\n";

// 有效文件 B：root 含 name=beta, width=1024。
const std::string kValidB =
    "format_version: 1.0\n"
    "name: beta\n"
    "width: 1024\n";

// 無效文件：重複 key 'name'（E7-01 會拒絕並回報行號）。
const std::string kInvalidDup =
    "format_version: 1.0\n"
    "name: alpha\n"
    "name: dup\n";

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. 初次載入
// -----------------------------------------------------------------------------

TEST(E7_07_InitialLoad, PollLoadsAndActivatesValue) {
    MemorySource src(kValidA);
    HotReloader r(src);

    EXPECT_FALSE(r.has_document());  // 載入前無值

    ReloadResult res = r.poll();

    EXPECT_EQ(res.status, ReloadStatus::Loaded);
    EXPECT_TRUE(res.reloaded());
    EXPECT_TRUE(res.has_document);
    EXPECT_TRUE(res.ok());
    ASSERT_TRUE(r.has_document());
    EXPECT_EQ(r.root().at("name").as_string(), "alpha");
    EXPECT_EQ(r.root().at("width").as_int(), 800);
    EXPECT_EQ(r.document().format_version, (FormatVersion{1, 0}));
}

TEST(E7_07_InitialLoad, DocumentThrowsBeforeAnyLoad) {
    MemorySource src(kValidA);
    HotReloader r(src);
    EXPECT_THROW(r.document(), std::runtime_error);
    EXPECT_THROW(r.root(), std::runtime_error);
}

// -----------------------------------------------------------------------------
// 2. 內容變更 → 重載成功 → 新值生效
// -----------------------------------------------------------------------------

TEST(E7_07_Reload, ContentChangeReloadsNewValue) {
    MemorySource src(kValidA);
    HotReloader r(src);

    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);
    EXPECT_EQ(r.root().at("name").as_string(), "alpha");

    src.set_content(kValidB);  // 外部來源變更
    ReloadResult res = r.poll();

    EXPECT_EQ(res.status, ReloadStatus::Loaded);
    EXPECT_EQ(r.root().at("name").as_string(), "beta");
    EXPECT_EQ(r.root().at("width").as_int(), 1024);
}

// -----------------------------------------------------------------------------
// 3. 重載解析失敗 → 保留舊值 + 產生診斷（不靜默）
// -----------------------------------------------------------------------------

TEST(E7_07_Failure, ReloadFailureKeepsOldValidValue) {
    MemorySource src(kValidA);
    HotReloader r(src);
    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    src.set_content(kInvalidDup);  // 變成壞內容
    ReloadResult res = r.poll();

    EXPECT_EQ(res.status, ReloadStatus::Failed);
    EXPECT_FALSE(res.ok());
    EXPECT_TRUE(res.has_document);          // 仍持有舊值
    ASSERT_TRUE(r.has_document());
    EXPECT_EQ(r.root().at("name").as_string(), "alpha");  // 舊值未被壞輸入覆寫
}

TEST(E7_07_Failure, ReloadFailureProducesLocatedDiagnostic) {
    MemorySource src(kValidA);
    HotReloader r(src);
    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    src.set_content(kInvalidDup);
    ReloadResult res = r.poll();

    ASSERT_FALSE(res.diagnostics.empty());          // 不靜默
    EXPECT_EQ(res.diagnostics.front().severity, Severity::Error);
    EXPECT_TRUE(contains(res.report, "error:"));    // E7-06 呈現
    EXPECT_TRUE(contains(res.report, "line"));      // 定位到行
    EXPECT_TRUE(contains(res.report, "name"));      // 保留原訊息（重複 key 'name'）

    // 事後仍可查最近失敗診斷。
    EXPECT_FALSE(r.last_diagnostics().empty());
    EXPECT_EQ(r.last_report(), res.report);
}

TEST(E7_07_Failure, InitialLoadFailureLeavesNoDocument) {
    MemorySource src(kInvalidDup);  // 一開始就壞
    HotReloader r(src);

    ReloadResult res = r.poll();

    EXPECT_EQ(res.status, ReloadStatus::Failed);
    EXPECT_FALSE(res.has_document);         // 沒有舊值可留
    EXPECT_FALSE(r.has_document());
    EXPECT_THROW(r.document(), std::runtime_error);
    EXPECT_FALSE(res.diagnostics.empty());  // 仍產生診斷，不靜默
}

TEST(E7_07_Failure, DiagnosticsClearedAfterRecovery) {
    MemorySource src(kValidA);
    HotReloader r(src);
    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    src.set_content(kInvalidDup);
    ASSERT_EQ(r.poll().status, ReloadStatus::Failed);
    ASSERT_FALSE(r.last_diagnostics().empty());

    src.set_content(kValidB);  // 修好
    ReloadResult res = r.poll();
    EXPECT_EQ(res.status, ReloadStatus::Loaded);
    EXPECT_TRUE(r.last_diagnostics().empty());  // 舊診斷清空
    EXPECT_TRUE(r.last_report().empty());
    EXPECT_EQ(r.root().at("name").as_string(), "beta");
}

// -----------------------------------------------------------------------------
// 4. 成功 / 失敗回呼
// -----------------------------------------------------------------------------

TEST(E7_07_Callbacks, SuccessCallbackFiresOnReloadOnly) {
    MemorySource src(kValidA);
    HotReloader r(src);

    int success = 0;
    std::string last_name;
    r.on_reload([&](const Document& d) {
        ++success;
        last_name = d.root.at("name").as_string();
    });

    r.poll();  // 初次載入 → 觸發
    EXPECT_EQ(success, 1);
    EXPECT_EQ(last_name, "alpha");

    r.poll();  // 無變更 → 不觸發
    EXPECT_EQ(success, 1);

    src.set_content(kValidB);
    r.poll();  // 變更成功 → 觸發
    EXPECT_EQ(success, 2);
    EXPECT_EQ(last_name, "beta");
}

TEST(E7_07_Callbacks, ErrorCallbackFiresOnFailureOnly) {
    MemorySource src(kValidA);
    HotReloader r(src);

    int errors = 0;
    int successes = 0;
    std::string captured_report;
    r.on_reload([&](const Document&) { ++successes; });
    r.on_error([&](const std::vector<Diagnostic>& diags, const std::string& report) {
        ++errors;
        EXPECT_FALSE(diags.empty());
        captured_report = report;
    });

    r.poll();  // 成功
    EXPECT_EQ(errors, 0);
    EXPECT_EQ(successes, 1);

    src.set_content(kInvalidDup);
    r.poll();  // 失敗 → 觸發 error 回呼、不觸發 success
    EXPECT_EQ(errors, 1);
    EXPECT_EQ(successes, 1);
    EXPECT_TRUE(contains(captured_report, "error:"));
}

// -----------------------------------------------------------------------------
// 5. 無變更不重載
// -----------------------------------------------------------------------------

TEST(E7_07_NoChange, UnchangedRevisionDoesNotReload) {
    MemorySource src(kValidA);
    HotReloader r(src);

    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    ReloadResult res = r.poll();
    EXPECT_EQ(res.status, ReloadStatus::Unchanged);
    EXPECT_TRUE(res.unchanged());
    EXPECT_TRUE(res.has_document);
    EXPECT_TRUE(res.diagnostics.empty());
}

TEST(E7_07_NoChange, SettingSameContentIsNotAChange) {
    MemorySource src(kValidA);
    HotReloader r(src);
    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    const std::uint64_t rev_before = src.revision();
    src.set_content(kValidA);  // 內容相同
    EXPECT_EQ(src.revision(), rev_before);        // revision 不變
    EXPECT_EQ(r.poll().status, ReloadStatus::Unchanged);
}

TEST(E7_07_NoChange, FailedReloadDoesNotRetrySameRevision) {
    MemorySource src(kValidA);
    HotReloader r(src);
    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    src.set_content(kInvalidDup);
    ASSERT_EQ(r.poll().status, ReloadStatus::Failed);

    // 同一壞 revision：下次 poll 不再重試（Unchanged），但仍保有舊值。
    ReloadResult res = r.poll();
    EXPECT_EQ(res.status, ReloadStatus::Unchanged);
    EXPECT_TRUE(res.has_document);
    EXPECT_EQ(r.root().at("name").as_string(), "alpha");
}

// -----------------------------------------------------------------------------
// 6. 強制重載
// -----------------------------------------------------------------------------

TEST(E7_07_ForceReload, ReloadIgnoresRevisionAndRefires) {
    MemorySource src(kValidA);
    HotReloader r(src);
    ASSERT_EQ(r.poll().status, ReloadStatus::Loaded);

    int success = 0;
    r.on_reload([&](const Document&) { ++success; });

    ReloadResult res = r.reload();  // 無 revision 變更也強制重解析
    EXPECT_EQ(res.status, ReloadStatus::Loaded);
    EXPECT_EQ(success, 1);

    // 之後 poll 應視為未變更（reload 已同步 revision 追蹤）。
    EXPECT_EQ(r.poll().status, ReloadStatus::Unchanged);
}
