// E2-23 檔案系統列舉 — 測試（gtest）
//
// 覆蓋：列舉目錄項目、檔/目錄類型區分、中繼欄位（大小/修改時間）、巢狀路徑、
// 無效路徑報錯（不靜默）、非目錄報錯、權限錯報錯、null / 假來源、stat 中繼、
// 經 E2-01 provider 掛成指標並由抽象介面走訪、重複註冊保守拒絕。
// 相位 1：只驗介面 + 記憶體內假來源行為，不含任何平台分支、不接真實檔案系統。
#include "fs_enum.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::sysinfo::DirEntry;
using ds::sysinfo::DirEntryType;
using ds::sysinfo::FileSystemEnumProvider;
using ds::sysinfo::FileSystemSource;
using ds::sysinfo::FsError;
using ds::sysinfo::ListResult;
using ds::sysinfo::NullFileSystemSource;
using ds::sysinfo::StatResult;

namespace {

// 建一棵有代表性的記憶體目錄樹：
//   /docs/            (dir)
//   /docs/readme.txt  (file, 12 bytes, mtime 1000)
//   /docs/guide.md    (file, 34 bytes, mtime 2000)
//   /docs/img/        (dir, mtime 3000)
//   /bin/tool         (file)  ← 佐證巢狀且不同分支
std::shared_ptr<NullFileSystemSource> makeTree() {
    auto src = std::make_shared<NullFileSystemSource>();
    src->add_file("/docs/readme.txt", 12, 1000);
    src->add_file("/docs/guide.md", 34, 2000);
    src->add_dir("/docs/img", 3000);
    src->add_file("/bin/tool", 99, 4000);
    return src;
}

// ===========================================================================
// 列舉目錄項目 + 決定性順序
// ===========================================================================
TEST(NullFileSystemSource, ListsDirectoryEntries) {
    auto src = makeTree();
    ListResult r = src->list("/docs");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.error, FsError::None);
    // /docs 下三項：guide.md, img, readme.txt（map 字典序，決定性）。
    ASSERT_EQ(r.entries.size(), 3u);
    EXPECT_EQ(r.entries[0].name, "guide.md");
    EXPECT_EQ(r.entries[1].name, "img");
    EXPECT_EQ(r.entries[2].name, "readme.txt");
}

// 尾端斜線 / 前導斜線寫法皆等價（路徑正規化）。
TEST(NullFileSystemSource, PathNormalizationEquivalent) {
    auto src = makeTree();
    EXPECT_EQ(src->list("/docs").entries.size(), 3u);
    EXPECT_EQ(src->list("/docs/").entries.size(), 3u);
    EXPECT_EQ(src->list("docs").entries.size(), 3u);
    EXPECT_EQ(src->list("//docs//").entries.size(), 3u);
}

// 根目錄列舉：頂層兩個目錄 bin, docs。
TEST(NullFileSystemSource, ListsRoot) {
    auto src = makeTree();
    for (const auto& p : {std::string(""), std::string("/")}) {
        ListResult r = src->list(p);
        ASSERT_TRUE(r.ok());
        ASSERT_EQ(r.entries.size(), 2u);
        EXPECT_EQ(r.entries[0].name, "bin");
        EXPECT_EQ(r.entries[1].name, "docs");
    }
}

// ===========================================================================
// 檔 / 目錄類型區分
// ===========================================================================
TEST(NullFileSystemSource, DistinguishesFileAndDirectory) {
    auto src = makeTree();
    ListResult r = src->list("/docs");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.entries.size(), 3u);
    // guide.md = 檔、img = 目錄、readme.txt = 檔。
    EXPECT_EQ(r.entries[0].type, DirEntryType::File);
    EXPECT_TRUE(r.entries[0].is_file());
    EXPECT_EQ(r.entries[1].type, DirEntryType::Directory);
    EXPECT_TRUE(r.entries[1].is_directory());
    EXPECT_EQ(r.entries[2].type, DirEntryType::File);
    EXPECT_TRUE(r.entries[2].is_file());
}

// ===========================================================================
// 中繼欄位：大小 / 修改時間
// ===========================================================================
TEST(NullFileSystemSource, CarriesMetadataFields) {
    auto src = makeTree();
    ListResult r = src->list("/docs");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.entries.size(), 3u);
    // readme.txt: size 12, mtime 1000（第 3 項）。
    EXPECT_EQ(r.entries[2].name, "readme.txt");
    EXPECT_EQ(r.entries[2].size, static_cast<std::uint64_t>(12));
    EXPECT_EQ(r.entries[2].mtime, static_cast<std::int64_t>(1000));
    // guide.md: size 34, mtime 2000（第 1 項）。
    EXPECT_EQ(r.entries[0].size, static_cast<std::uint64_t>(34));
    EXPECT_EQ(r.entries[0].mtime, static_cast<std::int64_t>(2000));
    // 目錄 img: mtime 3000、size 慣例 0。
    EXPECT_EQ(r.entries[1].mtime, static_cast<std::int64_t>(3000));
    EXPECT_EQ(r.entries[1].size, static_cast<std::uint64_t>(0));
}

// ===========================================================================
// 巢狀路徑：深入子目錄列舉
// ===========================================================================
TEST(NullFileSystemSource, NestedPathEnumeration) {
    auto src = std::make_shared<NullFileSystemSource>();
    src->add_file("/a/b/c/deep.txt", 5, 42);
    src->add_dir("/a/b/c/sub");

    // 中間目錄自動建立且可列舉。
    ListResult ra = src->list("/a");
    ASSERT_TRUE(ra.ok());
    ASSERT_EQ(ra.entries.size(), 1u);
    EXPECT_EQ(ra.entries[0].name, "b");
    EXPECT_EQ(ra.entries[0].type, DirEntryType::Directory);

    // 最深層：deep.txt (file) 與 sub (dir)。
    ListResult rc = src->list("/a/b/c");
    ASSERT_TRUE(rc.ok());
    ASSERT_EQ(rc.entries.size(), 2u);
    EXPECT_EQ(rc.entries[0].name, "deep.txt");
    EXPECT_EQ(rc.entries[0].type, DirEntryType::File);
    EXPECT_EQ(rc.entries[0].size, static_cast<std::uint64_t>(5));
    EXPECT_EQ(rc.entries[1].name, "sub");
    EXPECT_EQ(rc.entries[1].type, DirEntryType::Directory);
}

// 空目錄：ok 且 entries 為空（可與「路徑錯誤」分辨）。
TEST(NullFileSystemSource, EmptyDirectoryIsOkNotError) {
    auto src = std::make_shared<NullFileSystemSource>();
    src->add_dir("/empty");
    ListResult r = src->list("/empty");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.error, FsError::None);
    EXPECT_TRUE(r.entries.empty());
}

// ===========================================================================
// 無效路徑 / 非目錄 / 權限錯：明確回報，不靜默
// ===========================================================================
TEST(NullFileSystemSource, InvalidPathReportsNotFound) {
    auto src = makeTree();
    ListResult r = src->list("/does/not/exist");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, FsError::NotFound);
    EXPECT_TRUE(r.entries.empty());  // 錯誤時不魚目混珠回資料
}

TEST(NullFileSystemSource, ListOnFileReportsNotADirectory) {
    auto src = makeTree();
    // readme.txt 是檔案，對它 list 應明確報「非目錄」，而非回空清單。
    ListResult r = src->list("/docs/readme.txt");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, FsError::NotADirectory);
}

TEST(NullFileSystemSource, PermissionDeniedReported) {
    auto src = makeTree();
    src->set_permission_denied("/docs");
    ListResult r = src->list("/docs");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error, FsError::PermissionDenied);
    EXPECT_TRUE(r.entries.empty());

    // 解除後恢復正常。
    src->set_permission_denied("/docs", false);
    EXPECT_TRUE(src->list("/docs").ok());
}

// ===========================================================================
// stat：單一路徑中繼
// ===========================================================================
TEST(NullFileSystemSource, StatFileReturnsMetadata) {
    auto src = makeTree();
    StatResult s = src->stat("/docs/guide.md");
    ASSERT_TRUE(s.ok());
    EXPECT_EQ(s.entry.name, "guide.md");
    EXPECT_EQ(s.entry.type, DirEntryType::File);
    EXPECT_EQ(s.entry.size, static_cast<std::uint64_t>(34));
    EXPECT_EQ(s.entry.mtime, static_cast<std::int64_t>(2000));
}

TEST(NullFileSystemSource, StatDirectoryAndRoot) {
    auto src = makeTree();
    StatResult d = src->stat("/docs");
    ASSERT_TRUE(d.ok());
    EXPECT_EQ(d.entry.name, "docs");
    EXPECT_EQ(d.entry.type, DirEntryType::Directory);

    StatResult root = src->stat("/");
    ASSERT_TRUE(root.ok());
    EXPECT_EQ(root.entry.name, "/");
    EXPECT_EQ(root.entry.type, DirEntryType::Directory);
}

TEST(NullFileSystemSource, StatMissingReportsNotFound) {
    auto src = makeTree();
    StatResult s = src->stat("/nope");
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.error, FsError::NotFound);
}

// ===========================================================================
// null / 空來源
// ===========================================================================
TEST(NullFileSystemSource, EmptySourceRootIsEmptyDir) {
    NullFileSystemSource src;
    // 空來源：根為空目錄（ok、無項目）。
    ListResult r = src.list("/");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.entries.empty());
}

TEST(NullFileSystemSource, ClearResetsTree) {
    auto src = makeTree();
    ASSERT_EQ(src->list("/").entries.size(), 2u);
    src->clear();
    EXPECT_TRUE(src->list("/").entries.empty());
    EXPECT_EQ(src->list("/docs").error, FsError::NotFound);
}

// ===========================================================================
// DirEntry 相等性 / to_string
// ===========================================================================
TEST(DirEntry, EqualityAndHelpers) {
    DirEntry a{"x", DirEntryType::File, 10, 5};
    DirEntry b{"x", DirEntryType::File, 10, 5};
    DirEntry c{"x", DirEntryType::Directory, 10, 5};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_TRUE(a.is_file());
    EXPECT_TRUE(c.is_directory());
    EXPECT_STREQ(ds::sysinfo::to_string(DirEntryType::File), "file");
    EXPECT_STREQ(ds::sysinfo::to_string(DirEntryType::Directory), "dir");
    EXPECT_STREQ(ds::sysinfo::to_string(FsError::NotFound), "not_found");
    EXPECT_STREQ(ds::sysinfo::to_string(FsError::PermissionDenied), "permission_denied");
}

// ===========================================================================
// 經 E2-01 provider：把目錄項目掛成指標
// ===========================================================================
TEST(FileSystemEnumProvider, ProviderIdIsStable) {
    FileSystemEnumProvider p{std::make_shared<NullFileSystemSource>(), "/"};
    EXPECT_EQ(p.provider_id(), "sysinfo.fs");
    EXPECT_EQ(std::string(FileSystemEnumProvider::kMetricId), "fs.entries");
    EXPECT_EQ(p.path(), "/");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(FileSystemEnumProvider, IsAMetricProvider) {
    auto p = std::make_shared<FileSystemEnumProvider>(makeTree(), "/docs");
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.fs");
}

// 註冊到 E2-01 registry：掛上一個指標，各子項目為可列舉實例。
TEST(FileSystemEnumProvider, RegistersEntriesAsInstances) {
    MetricRegistry registry;
    FileSystemEnumProvider provider{makeTree(), "/docs"};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(registry.contains("fs.entries"));
    EXPECT_EQ(provider.last_error(), FsError::None);

    auto metric = registry.get("fs.entries");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->name(), "Filesystem Entries");
    EXPECT_EQ(metric->unit(), "");
    // 「數量」= 實例數 = /docs 下三項。
    EXPECT_EQ(metric->instance_count(), 3u);
    // 「清單」= 依序列舉實例（決定性），全程只用 E2-01 抽象介面。
    EXPECT_EQ(metric->instance(0).instance_id(), "guide.md");
    EXPECT_EQ(metric->instance(1).instance_id(), "img");
    EXPECT_EQ(metric->instance(2).instance_id(), "readme.txt");
}

// 每個實例值 = 存在(1.0) + 類型文字（"file"/"dir"）。
TEST(FileSystemEnumProvider, InstanceCarriesPresenceAndType) {
    MetricRegistry registry;
    FileSystemEnumProvider provider{makeTree(), "/docs"};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("fs.entries");
    ASSERT_NE(metric, nullptr);

    // img 為目錄。
    const auto vimg = metric->instance(1).value();
    EXPECT_TRUE(vimg.valid);
    EXPECT_DOUBLE_EQ(vimg.number, 1.0);
    ASSERT_TRUE(vimg.text.has_value());
    EXPECT_EQ(*vimg.text, "dir");

    // readme.txt 為檔案。
    const auto vfile = metric->instance(2).value();
    ASSERT_TRUE(vfile.text.has_value());
    EXPECT_EQ(*vfile.text, "file");

    // 範圍 = at_least(0)。
    const auto r = metric->range();
    EXPECT_TRUE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_DOUBLE_EQ(*r.min, 0.0);
}

// find_instance 依名尋得（E2-01 便利查詢，走抽象介面）。
TEST(FileSystemEnumProvider, FindInstanceByName) {
    MetricRegistry registry;
    FileSystemEnumProvider provider{makeTree(), "/docs"};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("fs.entries");
    ASSERT_NE(metric, nullptr);
    const auto* guide = metric->find_instance("guide.md");
    ASSERT_NE(guide, nullptr);
    EXPECT_EQ(guide->label(), "guide.md");
    EXPECT_EQ(metric->find_instance("nope.txt"), nullptr);
}

// 無效路徑 → 仍掛上空指標，但 last_error 明確回報（不靜默、不崩）。
TEST(FileSystemEnumProvider, InvalidPathRegistersEmptyMetricWithError) {
    MetricRegistry registry;
    FileSystemEnumProvider provider{makeTree(), "/does/not/exist"};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    EXPECT_EQ(provider.last_error(), FsError::NotFound);
    auto metric = registry.get("fs.entries");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 0u);  // 空目錄與錯誤都是 0 實例，但 last_error 可分辨
}

// 權限錯 → 空指標 + last_error == PermissionDenied。
TEST(FileSystemEnumProvider, PermissionErrorSurfacedViaLastError) {
    auto src = makeTree();
    src->set_permission_denied("/docs");
    MetricRegistry registry;
    FileSystemEnumProvider provider{src, "/docs"};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    EXPECT_EQ(provider.last_error(), FsError::PermissionDenied);
    EXPECT_EQ(registry.get("fs.entries")->instance_count(), 0u);
}

// source 為 null 亦保守不崩：掛上空指標、last_error == NotFound。
TEST(FileSystemEnumProvider, NullSourcePointerIsSafe) {
    MetricRegistry registry;
    FileSystemEnumProvider provider{nullptr, "/docs"};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    EXPECT_EQ(provider.last_error(), FsError::NotFound);
    auto metric = registry.get("fs.entries");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 0u);
}

// 消費者範式：掛件風格消費者只走 E2-01 registry，數目錄項目數，全程無 sysinfo / fs 型別。
TEST(FileSystemEnumProvider, ConsumerWalksViaAbstractContractOnly) {
    MetricRegistry registry;
    FileSystemEnumProvider provider{makeTree(), "/docs"};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    std::size_t total = 0;
    for (const auto& m : registry.all()) {
        total += m->instance_count();  // 全程無 sysinfo / fs 型別
    }
    EXPECT_EQ(total, 3u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫）。
TEST(FileSystemEnumProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    FileSystemEnumProvider p1{makeTree(), "/docs"};              // 3 項
    FileSystemEnumProvider p2{makeTree(), "/bin"};               // 1 項
    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);                    // 同 id "fs.entries" 被拒
    auto metric = registry.get("fs.entries");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 3u);                     // 仍為 p1 的三個實例
}

}  // namespace
