// tests/e7/test_e7_08.cpp — E7-08 設定遷移與版本相容 契約測試（gtest）
//
// 涵蓋：單步遷移、多步鏈式遷移、已是最新版 no-op、無遷移路徑報錯、版本過新報錯、
// 遷移後版本標記正確；並補：非法步驟拒絕、transform 實際改變結構、Document 重載、
// has_path 可達性、最短鏈選取、分支鏈。
//
// 消費上游 E7-01 的 Value / FormatVersion / Document（可讀不可改）。

#include "gtest/gtest.h"

#include "migration.hpp"

using ds::format::Document;
using ds::format::FormatVersion;
using ds::format::MigrateStatus;
using ds::format::MigrationRegistry;
using ds::format::Value;

namespace {

// 便捷建構 FormatVersion（避免在 EXPECT_* 巨集引數內出現 braced-init 的逗號）。
FormatVersion fv(int major, int minor) { return FormatVersion{major, minor}; }

// 以有序成員建一個設定 map。
Value make_config(std::vector<Value::Member> members) {
    return Value::map(std::move(members));
}

// 回傳一個「在 root map 尾端附加 (key,val) 字串成員」的 transform。
std::function<Value(const Value&)> add_key(std::string key, std::string val) {
    return [key, val](const Value& in) -> Value {
        std::vector<Value::Member> members = in.as_map();  // 複製既有成員（保序）
        members.emplace_back(key, Value::string(val));
        return Value::map(std::move(members));
    };
}

}  // namespace

// --- 版本序關係 helper ----------------------------------------------------------

TEST(MigrationVersion, LessOrders) {
    EXPECT_TRUE(ds::format::version_less(FormatVersion{1, 0}, FormatVersion{1, 1}));
    EXPECT_TRUE(ds::format::version_less(FormatVersion{1, 9}, FormatVersion{2, 0}));
    EXPECT_FALSE(ds::format::version_less(FormatVersion{2, 0}, FormatVersion{1, 9}));
    EXPECT_FALSE(ds::format::version_less(FormatVersion{1, 0}, FormatVersion{1, 0}));
    EXPECT_EQ(ds::format::version_to_string(FormatVersion{2, 3}), std::string("2.3"));
}

// --- 註冊契約 -------------------------------------------------------------------

TEST(MigrationRegistry, RejectsNonAscendingStep) {
    MigrationRegistry reg;
    // from == to：非嚴格上升，拒絕。
    EXPECT_FALSE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 0}, nullptr));
    // from > to：降級，拒絕。
    EXPECT_FALSE(reg.add(FormatVersion{2, 0}, FormatVersion{1, 0}, nullptr));
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(0));
    // 合法步驟：接受。
    EXPECT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, nullptr));
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(1));
}

// --- 單步遷移 -------------------------------------------------------------------

TEST(MigrationRegistry, SingleStepMigration) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1},
                        add_key("theme", "light")));

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 0}, FormatVersion{1, 1});

    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.migrated());
    // 版本標記正確。
    EXPECT_TRUE(r.version() == fv(1, 1));
    // 結構已升級：新增 theme。
    ASSERT_TRUE(r.value().is_map());
    ASSERT_TRUE(r.value().contains("theme"));
    EXPECT_EQ(r.value().at("theme").as_string(), std::string("light"));
    // 原欄位保留。
    ASSERT_TRUE(r.value().contains("name"));
    EXPECT_EQ(r.value().at("name").as_string(), std::string("app"));
}

// --- 多步鏈式遷移 ---------------------------------------------------------------

TEST(MigrationRegistry, MultiStepChain) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, add_key("a", "1")));
    ASSERT_TRUE(reg.add(FormatVersion{1, 1}, FormatVersion{1, 2}, add_key("b", "2")));
    ASSERT_TRUE(reg.add(FormatVersion{1, 2}, FormatVersion{2, 0}, add_key("c", "3")));

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 0}, FormatVersion{2, 0});

    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.migrated());
    EXPECT_TRUE(r.version() == fv(2, 0));
    // 三步依序套用：a、b、c 全在。
    ASSERT_TRUE(r.value().is_map());
    EXPECT_TRUE(r.value().contains("a"));
    EXPECT_TRUE(r.value().contains("b"));
    EXPECT_TRUE(r.value().contains("c"));
    // 套用順序（尾端附加）：name, a, b, c。
    ASSERT_EQ(r.value().size(), static_cast<std::size_t>(4));
    EXPECT_EQ(r.value().keys()[0], std::string("name"));
    EXPECT_EQ(r.value().keys()[1], std::string("a"));
    EXPECT_EQ(r.value().keys()[2], std::string("b"));
    EXPECT_EQ(r.value().keys()[3], std::string("c"));
}

// --- 已是最新版 no-op -----------------------------------------------------------

TEST(MigrationRegistry, AlreadyCurrentIsNoOp) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, add_key("x", "y")));

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 1}, FormatVersion{1, 1});

    ASSERT_TRUE(r.ok());
    // no-op：未套用任何步驟。
    EXPECT_FALSE(r.migrated());
    EXPECT_TRUE(r.version() == fv(1, 1));
    // 值原封不動（未新增 x）。
    ASSERT_TRUE(r.value().is_map());
    EXPECT_FALSE(r.value().contains("x"));
    EXPECT_EQ(r.value().size(), static_cast<std::size_t>(1));
}

// --- 無遷移路徑報錯 -------------------------------------------------------------

TEST(MigrationRegistry, NoPathIsError) {
    MigrationRegistry reg;
    // 只註冊 1.0->1.1；由 1.1 到 2.0 無邊。
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, nullptr));

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 1}, FormatVersion{2, 0});

    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(r.error().status == MigrateStatus::NoPath);
    EXPECT_TRUE(r.error().from == fv(1, 1));
    EXPECT_TRUE(r.error().target == fv(2, 0));
    EXPECT_FALSE(r.error().message.empty());
}

TEST(MigrationRegistry, BrokenChainInMiddleIsNoPath) {
    MigrationRegistry reg;
    // 1.0->1.1 與 1.2->2.0 存在，但 1.1->1.2 缺，鏈斷。
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, nullptr));
    ASSERT_TRUE(reg.add(FormatVersion{1, 2}, FormatVersion{2, 0}, nullptr));

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 0}, FormatVersion{2, 0});
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(r.error().status == MigrateStatus::NoPath);
}

// --- 版本過新報錯 ---------------------------------------------------------------

TEST(MigrationRegistry, TooNewIsError) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{2, 0}, nullptr));

    Value cfg = make_config({{"name", Value::string("app")}});
    // 文件是 2.0，卻要求降級到 1.0：過新。
    auto r = reg.migrate(cfg, FormatVersion{2, 0}, FormatVersion{1, 0});

    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(r.error().status == MigrateStatus::TooNew);
    EXPECT_TRUE(r.error().from == fv(2, 0));
    EXPECT_TRUE(r.error().target == fv(1, 0));
    EXPECT_FALSE(r.error().message.empty());
}

// --- has_path 可達性 ------------------------------------------------------------

TEST(MigrationRegistry, HasPathReachability) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, nullptr));
    ASSERT_TRUE(reg.add(FormatVersion{1, 1}, FormatVersion{2, 0}, nullptr));

    EXPECT_TRUE(reg.has_path(FormatVersion{1, 0}, FormatVersion{2, 0}));
    EXPECT_TRUE(reg.has_path(FormatVersion{1, 1}, FormatVersion{2, 0}));
    // 相同版本：可達（空鏈）。
    EXPECT_TRUE(reg.has_path(FormatVersion{2, 0}, FormatVersion{2, 0}));
    // 無邊抵達 3.0。
    EXPECT_FALSE(reg.has_path(FormatVersion{1, 0}, FormatVersion{3, 0}));
    // 降級不可達。
    EXPECT_FALSE(reg.has_path(FormatVersion{2, 0}, FormatVersion{1, 0}));
}

// --- 最短鏈選取（存在直達捷徑時優先） -------------------------------------------

TEST(MigrationRegistry, PrefersShortestChain) {
    MigrationRegistry reg;
    // 長鏈：1.0->1.1->1.2->2.0（各加一個 key）
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, add_key("s1", "x")));
    ASSERT_TRUE(reg.add(FormatVersion{1, 1}, FormatVersion{1, 2}, add_key("s2", "x")));
    ASSERT_TRUE(reg.add(FormatVersion{1, 2}, FormatVersion{2, 0}, add_key("s3", "x")));
    // 捷徑：1.0->2.0 直達（只加一個 key）
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{2, 0}, add_key("direct", "x")));

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 0}, FormatVersion{2, 0});
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.version() == fv(2, 0));
    // BFS 取最短：只走捷徑，故只有 direct，無 s1/s2/s3。
    EXPECT_TRUE(r.value().contains("direct"));
    EXPECT_FALSE(r.value().contains("s1"));
    EXPECT_FALSE(r.value().contains("s2"));
    EXPECT_FALSE(r.value().contains("s3"));
}

// --- Document 重載 --------------------------------------------------------------

TEST(MigrationRegistry, MigrateDocumentOverload) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, add_key("added", "yes")));

    Document doc;
    doc.format_version = FormatVersion{1, 0};
    doc.root = make_config({{"name", Value::string("app")}});

    auto r = reg.migrate(doc, FormatVersion{1, 1});
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.migrated());
    EXPECT_TRUE(r.version() == fv(1, 1));
    ASSERT_TRUE(r.value().contains("added"));
    EXPECT_EQ(r.value().at("added").as_string(), std::string("yes"));
}

// --- 空 transform：僅升版本標記，內容不變 --------------------------------------

TEST(MigrationRegistry, NullTransformOnlyBumpsVersion) {
    MigrationRegistry reg;
    ASSERT_TRUE(reg.add(FormatVersion{1, 0}, FormatVersion{1, 1}, nullptr));  // 恆等

    Value cfg = make_config({{"name", Value::string("app")}});
    auto r = reg.migrate(cfg, FormatVersion{1, 0}, FormatVersion{1, 1});
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.migrated());
    EXPECT_TRUE(r.version() == fv(1, 1));
    // 內容不變。
    EXPECT_TRUE(r.value() == cfg);
}
