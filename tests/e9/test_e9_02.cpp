// E9-02 manifest — 契約測試（gtest）
//
// 驗證：解析有效 manifest、requires/permissions 正確讀出、缺必填欄位報錯（含定位）、
// 版本欄位解析、格式不相容偵測、未知/重複/缺冒號的行報錯、註解與空行處理。
// 平台中立：不含任何平台分支。
#include "manifest.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::package::FormatVersion;
using ds::package::is_format_compatible;
using ds::package::kSupportedFormat;
using ds::package::Manifest;
using ds::package::parse_manifest;
using ds::package::ParseResult;

namespace {

// 解析一份完整有效的 manifest：所有欄位與 requires/permissions 正確讀出。
TEST(Manifest, ParsesValidManifest) {
    const std::string text =
        "# 範例 manifest\n"
        "format_version: 1.0\n"
        "name: com.example.hello\n"
        "version: 0.1.0\n"
        "description: 範例模組\n"
        "requires: host.tray_icon, host.global_hotkey\n"
        "permissions: fs.read, net.connect\n";

    const ParseResult r = parse_manifest(text);
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    const Manifest& m = r.manifest();

    EXPECT_EQ(m.format_version.major, 1);
    EXPECT_EQ(m.format_version.minor, 0);
    EXPECT_EQ(m.name, "com.example.hello");
    EXPECT_EQ(m.version, "0.1.0");
    EXPECT_EQ(m.description, "範例模組");

    ASSERT_EQ(m.required_capabilities.size(), 2u);
    EXPECT_EQ(m.required_capabilities[0], "host.tray_icon");
    EXPECT_EQ(m.required_capabilities[1], "host.global_hotkey");

    ASSERT_EQ(m.permissions.size(), 2u);
    EXPECT_EQ(m.permissions[0], "fs.read");
    EXPECT_EQ(m.permissions[1], "net.connect");
}

// 僅必填欄位（format_version + name）即可成功；選填清單為空。
TEST(Manifest, MinimalManifestIsValid) {
    const ParseResult r = parse_manifest("format_version: 1.0\nname: minimal\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_EQ(r.manifest().name, "minimal");
    EXPECT_TRUE(r.manifest().required_capabilities.empty());
    EXPECT_TRUE(r.manifest().permissions.empty());
}

// 缺 format_version：報錯（非特定行 → line == 0）。
TEST(Manifest, MissingFormatVersionIsError) {
    const ParseResult r = parse_manifest("name: only.name\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 0u);
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos);
}

// 缺 name：報錯。
TEST(Manifest, MissingNameIsError) {
    const ParseResult r = parse_manifest("format_version: 1.0\n");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("name"), std::string::npos);
}

// 格式版本不相容（主版號較高）：偵測並報錯，不得靜默通過。
TEST(Manifest, IncompatibleMajorVersionIsError) {
    const ParseResult r = parse_manifest("format_version: 2.0\nname: future\n");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("不相容"), std::string::npos);
}

// 次版號高於支援上限（同主版）：亦視為不相容（host 無法理解更新的欄位）。
TEST(Manifest, HigherMinorVersionIsIncompatible) {
    const ParseResult r = parse_manifest("format_version: 1.5\nname: newer\n");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("不相容"), std::string::npos);
}

// is_format_compatible 的邊界語意。
TEST(Manifest, FormatCompatibilityRules) {
    EXPECT_TRUE(is_format_compatible(FormatVersion{1, 0}));
    EXPECT_TRUE(is_format_compatible(FormatVersion{1, 0}, FormatVersion{1, 3}));  // 舊 manifest 於新 host
    EXPECT_FALSE(is_format_compatible(FormatVersion{1, 4}, FormatVersion{1, 3}));  // 新 manifest 於舊 host
    EXPECT_FALSE(is_format_compatible(FormatVersion{2, 0}));  // 主版不同
    EXPECT_FALSE(is_format_compatible(FormatVersion{0, 9}));  // 主版不同
    EXPECT_EQ(kSupportedFormat.major, 1);
}

// 格式版本無法解析：報錯並定位到該行。
TEST(Manifest, UnparseableVersionIsError) {
    const ParseResult r = parse_manifest("format_version: abc\nname: x\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 1u);
    EXPECT_NE(r.error().message.find("format_version"), std::string::npos);
}

// "1.2.3" 非 major.minor：拒絕。
TEST(Manifest, ThreePartVersionIsRejected) {
    const ParseResult r = parse_manifest("format_version: 1.2.3\nname: x\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 1u);
}

// 未知欄位：明確報錯並定位（不得靜默忽略）。
TEST(Manifest, UnknownKeyIsError) {
    const ParseResult r =
        parse_manifest("format_version: 1.0\nname: x\nbogus_key: whatever\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 3u);
    EXPECT_NE(r.error().message.find("bogus_key"), std::string::npos);
}

// 缺 ':' 的行：報錯並定位。
TEST(Manifest, LineWithoutColonIsError) {
    const ParseResult r = parse_manifest("format_version: 1.0\nname x\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 2u);
}

// 重複 key：報錯並定位到第二次出現處。
TEST(Manifest, DuplicateKeyIsError) {
    const ParseResult r =
        parse_manifest("format_version: 1.0\nname: a\nname: b\n");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().line, 3u);
    EXPECT_NE(r.error().message.find("name"), std::string::npos);
}

// requires/permissions 的逗號清單：去空白、略過空項、單項亦可。
TEST(Manifest, ListParsingTrimsAndSkipsEmpties) {
    const ParseResult r = parse_manifest(
        "format_version: 1.0\n"
        "name: x\n"
        "requires:   cap.one ,, cap.two ,  \n"
        "permissions: perm.only\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    const Manifest& m = r.manifest();
    ASSERT_EQ(m.required_capabilities.size(), 2u);
    EXPECT_EQ(m.required_capabilities[0], "cap.one");
    EXPECT_EQ(m.required_capabilities[1], "cap.two");
    ASSERT_EQ(m.permissions.size(), 1u);
    EXPECT_EQ(m.permissions[0], "perm.only");
}

// 空的 requires/permissions 值：得到空清單（非錯誤）。
TEST(Manifest, EmptyListValuesYieldEmptyVectors) {
    const ParseResult r = parse_manifest(
        "format_version: 1.0\nname: x\nrequires:\npermissions:  \n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_TRUE(r.manifest().required_capabilities.empty());
    EXPECT_TRUE(r.manifest().permissions.empty());
}

// value 內含 ':'（只切第一個冒號）。
TEST(Manifest, ValueMayContainColon) {
    const ParseResult r = parse_manifest(
        "format_version: 1.0\nname: x\ndescription: a:b:c\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_EQ(r.manifest().description, "a:b:c");
}

// 註解與空行被忽略，不影響解析。
TEST(Manifest, CommentsAndBlankLinesIgnored) {
    const ParseResult r = parse_manifest(
        "\n"
        "# 這是註解\n"
        "   \n"
        "format_version: 1.0\n"
        "   # 縮排註解\n"
        "name: x\n");
    ASSERT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    EXPECT_EQ(r.manifest().name, "x");
}

}  // namespace
