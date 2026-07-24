// E1-21 能力矩陣宣告檔 — 契約測試（gtest）
//
// 驗證：載入、查詢、optional 標記、未知能力保守回 false、自訂宣告建構、重複 id 覆蓋。
// 相位 1：只驗介面 + null（宣告式預設）行為，不含任何平台分支。
#include "capability_matrix.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;

namespace {

// 預設矩陣非空（真空防線：沒有宣告的能力矩陣無意義）。
TEST(CapabilityMatrix, DefaultsAreNonEmpty) {
    const CapabilityMatrix m = CapabilityMatrix::defaults();
    EXPECT_GT(m.size(), 0u);
    EXPECT_EQ(m.size(), m.all().size());
}

// 保證存在的能力：has() 為 true，且不標記為 optional。
TEST(CapabilityMatrix, MandatoryCapabilityIsAvailable) {
    const CapabilityMatrix m = CapabilityMatrix::defaults();
    EXPECT_TRUE(m.is_declared("render.paint"));
    EXPECT_TRUE(m.has("render.paint"));
    EXPECT_FALSE(m.is_optional("render.paint"));
}

// 可選能力：被宣告、標記 optional；相位 1 null 期預設不可用。
TEST(CapabilityMatrix, OptionalCapabilityDeclaredButUnavailableInNullPhase) {
    const CapabilityMatrix m = CapabilityMatrix::defaults();
    EXPECT_TRUE(m.is_declared("host.tray_icon"));
    EXPECT_TRUE(m.is_optional("host.tray_icon"));
    EXPECT_FALSE(m.has("host.tray_icon"));  // null 期保守：尚無真實後端探測
}

// 未知能力（NFR-03 核心語意）：一律保守回 false，永不誤放行。
TEST(CapabilityMatrix, UnknownCapabilityIsConservativelyFalse) {
    const CapabilityMatrix m = CapabilityMatrix::defaults();
    EXPECT_FALSE(m.is_declared("does.not.exist"));
    EXPECT_FALSE(m.has("does.not.exist"));
    EXPECT_FALSE(m.is_optional("does.not.exist"));
    EXPECT_EQ(m.find("does.not.exist"), nullptr);
}

// find() 對已宣告能力回傳指向正確欄位的指標。
TEST(CapabilityMatrix, FindReturnsDeclFields) {
    const CapabilityMatrix m = CapabilityMatrix::defaults();
    const CapabilityDecl* d = m.find("actuator.brightness");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->id, "actuator.brightness");
    EXPECT_TRUE(d->optional);
    EXPECT_FALSE(d->default_available);
    EXPECT_FALSE(d->description.empty());
}

// 由自訂宣告建構：has() 反映 default_available，與 optional 標記獨立。
TEST(CapabilityMatrix, ConstructFromCustomDecls) {
    CapabilityMatrix m(std::vector<CapabilityDecl>{
        {"feat.a", "always on", /*optional=*/false, /*default_available=*/true},
        {"feat.b", "optional but on", /*optional=*/true, /*default_available=*/true},
        {"feat.c", "optional off", /*optional=*/true, /*default_available=*/false},
    });
    EXPECT_EQ(m.size(), 3u);
    EXPECT_TRUE(m.has("feat.a"));
    EXPECT_TRUE(m.has("feat.b"));   // optional 但預設可用
    EXPECT_TRUE(m.is_optional("feat.b"));
    EXPECT_FALSE(m.has("feat.c"));
    EXPECT_TRUE(m.is_optional("feat.c"));
}

// 空矩陣：任何查詢皆保守回 false / nullptr。
TEST(CapabilityMatrix, EmptyMatrixQueriesAreSafe) {
    CapabilityMatrix m(std::vector<CapabilityDecl>{});
    EXPECT_EQ(m.size(), 0u);
    EXPECT_FALSE(m.has("anything"));
    EXPECT_FALSE(m.is_declared("anything"));
    EXPECT_EQ(m.find("anything"), nullptr);
}

// 重複 id：後定義者為準（後端可覆寫先前宣告的預設可用性）。
TEST(CapabilityMatrix, DuplicateIdLastWins) {
    CapabilityMatrix m(std::vector<CapabilityDecl>{
        {"dup", "first", /*optional=*/true, /*default_available=*/false},
        {"dup", "second", /*optional=*/false, /*default_available=*/true},
    });
    const CapabilityDecl* d = m.find("dup");
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->description, "second");
    EXPECT_TRUE(m.has("dup"));
    EXPECT_FALSE(m.is_optional("dup"));
}

}  // namespace
