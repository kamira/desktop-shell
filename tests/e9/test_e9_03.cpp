// E9-03 可互換元件組合 — 契約測試（gtest）
//
// 驗證：相容性檢查（provides required_kinds / 結構無效 / 空要求）、插槽註冊（唯一/非空）、
// 綁定相容元件、替換（swap）為另一相容元件、列舉候選（compatible_candidates）、
// 不相容綁定/替換明確報錯且狀態不變、空插槽狀態與 unbind、組合完整性驗證。
// 平台中立：不含任何平台分支。
#include "composition.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::package::check_compatibility;
using ds::package::CompatibilityResult;
using ds::package::ComponentSlot;
using ds::package::Composition;
using ds::package::Package;
using ds::package::parse_package;

namespace {

// 從套件描述文字建立一個結構完整的元件（Package）。測試前置，assert 解析成功。
Package make_component(const std::string& text) {
    const auto r = parse_package(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.package();
}

// 一個提供 asset + code 的合法元件。
Package renderer_v1() {
    return make_component(
        "format_version: 1.0\n"
        "name: com.example.renderer.v1\n"
        "---\n"
        "asset: icons/main.png\n"
        "code: renderer.wasm\n");
}

// 另一個同樣提供 asset + code 的合法元件（可互換替代品）。
Package renderer_v2() {
    return make_component(
        "format_version: 1.0\n"
        "name: com.example.renderer.v2\n"
        "---\n"
        "asset: icons/alt.png\n"
        "code: renderer2.wasm\n");
}

// 只提供 asset、不提供 code 的元件（對「需要 code」的插槽不相容）。
Package asset_only() {
    return make_component(
        "format_version: 1.0\n"
        "name: com.example.assetonly\n"
        "---\n"
        "asset: icons/only.png\n");
}

ComponentSlot renderer_slot() {
    return ComponentSlot{"renderer", {"asset", "code"}};
}

// ── 相容性檢查 ───────────────────────────────────────────────────────────────

TEST(Compatibility, ComponentProvidingAllRequiredKindsIsCompatible) {
    const CompatibilityResult cr = check_compatibility(renderer_slot(), renderer_v1());
    EXPECT_TRUE(cr.compatible);
    EXPECT_TRUE(static_cast<bool>(cr));
    EXPECT_TRUE(cr.reason.empty());
}

TEST(Compatibility, MissingRequiredKindIsIncompatibleWithReason) {
    const CompatibilityResult cr = check_compatibility(renderer_slot(), asset_only());
    EXPECT_FALSE(cr.compatible);
    // 不靜默：原因需指出缺少的 kind。
    EXPECT_NE(cr.reason.find("code"), std::string::npos) << cr.reason;
}

TEST(Compatibility, SlotWithNoRequiredKindsAcceptsAnyValidComponent) {
    const ComponentSlot open_slot{"any", {}};
    EXPECT_TRUE(check_compatibility(open_slot, asset_only()).compatible);
    EXPECT_TRUE(check_compatibility(open_slot, renderer_v1()).compatible);
}

TEST(Compatibility, StructurallyInvalidComponentIsIncompatible) {
    // 手工構造一個結構無效的元件：name 為空（違反 E9-01 validate_package）。
    Package broken;  // 預設建構：manifest.name 為空。
    const CompatibilityResult cr = check_compatibility(ComponentSlot{"s", {}}, broken);
    EXPECT_FALSE(cr.compatible);
    EXPECT_NE(cr.reason.find("結構無效"), std::string::npos) << cr.reason;
}

// ── 插槽註冊 ────────────────────────────────────────────────────────────────

TEST(Composition, AddSlotRejectsEmptyAndDuplicateId) {
    Composition comp;
    EXPECT_TRUE(comp.add_slot(ComponentSlot{"a", {}}));
    EXPECT_FALSE(comp.add_slot(ComponentSlot{"", {}}));   // 空 id
    EXPECT_FALSE(comp.add_slot(ComponentSlot{"a", {}}));  // 重複 id
    ASSERT_EQ(comp.slot_ids().size(), 1u);
    EXPECT_EQ(comp.slot_ids()[0], "a");
}

// ── 空插槽狀態 ──────────────────────────────────────────────────────────────

TEST(Composition, NewSlotStartsEmpty) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    EXPECT_TRUE(comp.has_slot("renderer"));
    EXPECT_FALSE(comp.is_bound("renderer"));
    EXPECT_EQ(comp.bound_component("renderer"), nullptr);
    // 未知插槽查詢一律安全。
    EXPECT_FALSE(comp.has_slot("nope"));
    EXPECT_FALSE(comp.is_bound("nope"));
    EXPECT_EQ(comp.bound_component("nope"), nullptr);
}

// ── 綁定相容元件 ────────────────────────────────────────────────────────────

TEST(Composition, BindCompatibleComponentSucceeds) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));

    const auto r = comp.bind("renderer", renderer_v1());
    ASSERT_TRUE(r.ok) << r.message;
    EXPECT_TRUE(comp.is_bound("renderer"));
    const Package* p = comp.bound_component("renderer");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->manifest.name, "com.example.renderer.v1");
}

TEST(Composition, BindUnknownSlotFails) {
    Composition comp;
    const auto r = comp.bind("ghost", renderer_v1());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("不存在"), std::string::npos) << r.message;
}

TEST(Composition, BindIncompatibleComponentFailsAndSlotStaysEmpty) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    const auto r = comp.bind("renderer", asset_only());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("code"), std::string::npos) << r.message;
    // 狀態不變：仍為空。
    EXPECT_FALSE(comp.is_bound("renderer"));
}

TEST(Composition, BindOnAlreadyBoundSlotFails) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    ASSERT_TRUE(comp.bind("renderer", renderer_v1()).ok);
    const auto r = comp.bind("renderer", renderer_v2());  // 應改用 swap
    EXPECT_FALSE(r.ok);
    // 原綁定保留。
    ASSERT_NE(comp.bound_component("renderer"), nullptr);
    EXPECT_EQ(comp.bound_component("renderer")->manifest.name, "com.example.renderer.v1");
}

// ── 替換（swap）───────────────────────────────────────────────────────────────

TEST(Composition, SwapReplacesBoundComponentWithCompatibleOne) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    ASSERT_TRUE(comp.bind("renderer", renderer_v1()).ok);

    const auto r = comp.swap("renderer", renderer_v2());
    ASSERT_TRUE(r.ok) << r.message;
    ASSERT_NE(comp.bound_component("renderer"), nullptr);
    EXPECT_EQ(comp.bound_component("renderer")->manifest.name, "com.example.renderer.v2");
}

TEST(Composition, SwapWithIncompatibleComponentFailsAndKeepsOriginal) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    ASSERT_TRUE(comp.bind("renderer", renderer_v1()).ok);

    const auto r = comp.swap("renderer", asset_only());
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("code"), std::string::npos) << r.message;
    // 不靜默、不半途破壞：原綁定保留。
    ASSERT_NE(comp.bound_component("renderer"), nullptr);
    EXPECT_EQ(comp.bound_component("renderer")->manifest.name, "com.example.renderer.v1");
}

TEST(Composition, SwapOnEmptySlotFails) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    const auto r = comp.swap("renderer", renderer_v1());  // 首次綁定應用 bind
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(comp.is_bound("renderer"));
}

// ── unbind / 回到空狀態 ───────────────────────────────────────────────────────

TEST(Composition, UnbindReturnsSlotToEmpty) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    ASSERT_TRUE(comp.bind("renderer", renderer_v1()).ok);
    ASSERT_TRUE(comp.is_bound("renderer"));

    ASSERT_TRUE(comp.unbind("renderer").ok);
    EXPECT_FALSE(comp.is_bound("renderer"));
    EXPECT_EQ(comp.bound_component("renderer"), nullptr);

    // 重複 unbind 明確報錯（不靜默）。
    EXPECT_FALSE(comp.unbind("renderer").ok);
    // unbind 後可重新以任一相容元件綁定（可互換）。
    EXPECT_TRUE(comp.bind("renderer", renderer_v2()).ok);
}

// ── 列舉候選 ────────────────────────────────────────────────────────────────

TEST(Composition, CompatibleCandidatesReturnsMatchingIndicesInOrder) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));

    const std::vector<Package> candidates = {
        renderer_v1(),  // 0: 相容
        asset_only(),   // 1: 不相容（缺 code）
        renderer_v2(),  // 2: 相容
    };
    const std::vector<std::size_t> hits = comp.compatible_candidates("renderer", candidates);
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], 0u);
    EXPECT_EQ(hits[1], 2u);
}

TEST(Composition, CompatibleCandidatesForUnknownSlotIsEmpty) {
    Composition comp;
    const std::vector<Package> candidates = {renderer_v1()};
    EXPECT_TRUE(comp.compatible_candidates("ghost", candidates).empty());
}

// ── 組合完整性驗證 ──────────────────────────────────────────────────────────

TEST(Composition, ValidateFailsWhenSlotUnbound) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    ASSERT_TRUE(comp.add_slot(ComponentSlot{"storage", {}}));
    ASSERT_TRUE(comp.bind("renderer", renderer_v1()).ok);

    const auto r = comp.validate();
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.message.find("storage"), std::string::npos) << r.message;
}

TEST(Composition, ValidateSucceedsWhenAllSlotsBoundAndCompatible) {
    Composition comp;
    ASSERT_TRUE(comp.add_slot(renderer_slot()));
    ASSERT_TRUE(comp.add_slot(ComponentSlot{"storage", {}}));
    ASSERT_TRUE(comp.bind("renderer", renderer_v1()).ok);
    ASSERT_TRUE(comp.bind("storage", asset_only()).ok);  // storage 無 kind 要求

    EXPECT_TRUE(comp.validate().ok);
}

TEST(Composition, EmptyCompositionIsValid) {
    Composition comp;
    EXPECT_TRUE(comp.validate().ok);
}

}  // namespace
