// E9-01 統一套件格式定義 — 契約測試（gtest）
//
// 驗證：套件結構解析（manifest 區段 + 內容清單）、結構完整性驗證、缺 manifest 報錯（含定位）、
// 內容清單各項讀出、logical_path 合法性、重複路徑/缺冒號/空 kind 定位、manifest 錯誤原樣傳遞、
// validate_package 對記憶體內 Package 的獨立驗證。平台中立：不含任何平台分支。
#include "package.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::package::FormatVersion;
using ds::package::is_valid_logical_path;
using ds::package::Manifest;
using ds::package::Package;
using ds::package::PackageEntry;
using ds::package::PackageResult;
using ds::package::parse_package;
using ds::package::validate_package;

namespace {

// 解析一份完整套件：manifest 欄位 + 內容清單各項正確讀出。
TEST(Package, ParsesFullPackage) {
    const std::string text =
        "# 套件描述\n"
        "format_version: 1.0\n"
        "name: com.example.hello\n"
        "requires: host.tray_icon\n"
        "permissions: fs.read\n"
        "---\n"
        "# 內容清單\n"
        "asset: icons/tray.png\n"
        "code: main.wasm\n"
        "data: config/default.txt\n";

    const PackageResult r = parse_package(text);
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    const Package& p = r.package();

    // manifest 由 E9-02 解析（複用，不重造）。
    EXPECT_EQ(p.manifest.name, "com.example.hello");
    EXPECT_EQ(p.manifest.format_version.major, 1);
    EXPECT_EQ(p.manifest.format_version.minor, 0);
    ASSERT_EQ(p.manifest.required_capabilities.size(), 1u);
    EXPECT_EQ(p.manifest.required_capabilities[0], "host.tray_icon");

    // 內容清單。
    ASSERT_EQ(p.entries.size(), 3u);
    EXPECT_EQ(p.entries[0].kind, "asset");
    EXPECT_EQ(p.entries[0].logical_path, "icons/tray.png");
    EXPECT_EQ(p.entries[1].kind, "code");
    EXPECT_EQ(p.entries[1].logical_path, "main.wasm");
    EXPECT_EQ(p.entries[2].kind, "data");
    EXPECT_EQ(p.entries[2].logical_path, "config/default.txt");
}

// 僅含 manifest（無分隔線、無內容清單）：合法，entries 為空。
TEST(Package, ManifestOnlyPackageHasEmptyInventory) {
    const PackageResult r = parse_package("format_version: 1.0\nname: minimal\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_EQ(r.package().manifest.name, "minimal");
    EXPECT_TRUE(r.package().entries.empty());
}

// 有分隔線但內容清單為空：合法，entries 為空。
TEST(Package, DelimiterWithEmptyInventoryIsValid) {
    const PackageResult r = parse_package(
        "format_version: 1.0\nname: x\n---\n# 只有註解\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_TRUE(r.package().entries.empty());
}

// 缺 manifest（分隔線前無實質內容，只有內容清單）：報錯並定位到分隔線。
TEST(Package, MissingManifestSectionIsError) {
    const PackageResult r = parse_package(
        "# 沒有 manifest\n"
        "---\n"
        "asset: icons/tray.png\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);  // 分隔線在第 2 行。
    EXPECT_NE(r.error().message.find("manifest"), std::string::npos);
}

// 完全空白/註解的文字：缺 manifest（無分隔線 → line 0）。
TEST(Package, EmptyTextIsMissingManifest) {
    const PackageResult r = parse_package("\n# 空\n   \n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 0u);
    EXPECT_NE(r.error().message.find("manifest"), std::string::npos);
}

// manifest 區段本身錯誤（缺 name）：由 E9-02 偵測，錯誤原樣傳出。
TEST(Package, ManifestErrorIsPropagated) {
    const PackageResult r = parse_package(
        "format_version: 1.0\n---\nasset: a.png\n");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("name"), std::string::npos);
}

// manifest 區段版本無法解析：錯誤含行號，且行號對齊絕對行（區段在頂部）。
TEST(Package, ManifestVersionErrorKeepsAbsoluteLine) {
    const PackageResult r = parse_package(
        "format_version: abc\nname: x\n---\nasset: a.png\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 1u);
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos);
}

// 內容清單行缺 ':'：報錯並定位到該絕對行。
TEST(Package, ContentLineWithoutColonIsError) {
    const PackageResult r = parse_package(
        "format_version: 1.0\nname: x\n---\nasset icons/tray.png\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 4u);
}

// 內容清單 kind 為空：報錯並定位。
TEST(Package, EmptyKindIsError) {
    const PackageResult r = parse_package(
        "format_version: 1.0\nname: x\n---\n: icons/tray.png\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 4u);
    EXPECT_NE(r.error().message.find("kind"), std::string::npos);
}

// 內容清單 logical_path 為空：報錯並定位。
TEST(Package, EmptyPathIsError) {
    const PackageResult r = parse_package(
        "format_version: 1.0\nname: x\n---\nasset:   \n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 4u);
    EXPECT_NE(r.error().message.find("logical_path"), std::string::npos);
}

// 非法 logical_path（絕對路徑 / '..' 逃逸）：報錯並定位。
TEST(Package, InvalidLogicalPathIsError) {
    const PackageResult abs = parse_package(
        "format_version: 1.0\nname: x\n---\nasset: /etc/passwd\n");
    ASSERT_FALSE(abs.ok());
    EXPECT_EQ(abs.error().line, 4u);

    const PackageResult esc = parse_package(
        "format_version: 1.0\nname: x\n---\ncode: ../evil.wasm\n");
    ASSERT_FALSE(esc.ok());
    EXPECT_EQ(esc.error().line, 4u);
}

// 重複 logical_path：報錯並定位到第二次出現處。
TEST(Package, DuplicateLogicalPathIsError) {
    const PackageResult r = parse_package(
        "format_version: 1.0\nname: x\n---\nasset: a.png\ncode: a.png\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 5u);
    EXPECT_NE(r.error().message.find("重複"), std::string::npos);
}

// is_valid_logical_path 的邊界語意。
TEST(Package, LogicalPathValidityRules) {
    EXPECT_TRUE(is_valid_logical_path("main.wasm"));
    EXPECT_TRUE(is_valid_logical_path("icons/tray.png"));
    EXPECT_TRUE(is_valid_logical_path("a/b/c.txt"));
    EXPECT_FALSE(is_valid_logical_path(""));            // 空
    EXPECT_FALSE(is_valid_logical_path("/abs.txt"));    // 絕對
    EXPECT_FALSE(is_valid_logical_path("../up.txt"));   // 逃逸
    EXPECT_FALSE(is_valid_logical_path("a/../b"));      // 中段逃逸
    EXPECT_FALSE(is_valid_logical_path("a//b"));        // 空區段
    EXPECT_FALSE(is_valid_logical_path("a/"));          // 結尾空區段
}

// validate_package：對記憶體內構成的合法 Package 通過。
TEST(Package, ValidateAcceptsWellFormedPackage) {
    Package p;
    p.manifest.name = "com.example.ok";
    p.manifest.format_version = FormatVersion{1, 0};
    p.entries.push_back(PackageEntry{"asset", "icons/a.png"});
    p.entries.push_back(PackageEntry{"code", "main.wasm"});

    const PackageResult r = validate_package(p);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
}

// validate_package：manifest name 空 → 無效（line 0）。
TEST(Package, ValidateRejectsEmptyManifestName) {
    Package p;
    p.manifest.name = "";
    p.manifest.format_version = FormatVersion{1, 0};
    const PackageResult r = validate_package(p);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 0u);
    EXPECT_NE(r.error().message.find("manifest"), std::string::npos);
}

// validate_package：manifest 版本不相容 → 無效。
TEST(Package, ValidateRejectsIncompatibleFormatVersion) {
    Package p;
    p.manifest.name = "com.example.future";
    p.manifest.format_version = FormatVersion{2, 0};
    const PackageResult r = validate_package(p);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos);
}

// validate_package：內容清單重複路徑 → 無效。
TEST(Package, ValidateRejectsDuplicatePaths) {
    Package p;
    p.manifest.name = "com.example.dup";
    p.manifest.format_version = FormatVersion{1, 0};
    p.entries.push_back(PackageEntry{"asset", "dup.bin"});
    p.entries.push_back(PackageEntry{"data", "dup.bin"});
    const PackageResult r = validate_package(p);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("重複"), std::string::npos);
}

// 內容清單值只切第一個冒號：logical_path 內含 ':' 仍屬合法字元（本格式只在 '/' 上分段）。
TEST(Package, ContentValueSplitsOnFirstColonOnly) {
    const PackageResult r = parse_package(
        "format_version: 1.0\nname: x\n---\nasset: dir/a:b.png\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    ASSERT_EQ(r.package().entries.size(), 1u);
    EXPECT_EQ(r.package().entries[0].logical_path, "dir/a:b.png");
}

}  // namespace
