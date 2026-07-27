// E1-18 具名螢幕與每螢幕實例 — 契約測試（gtest）
//
// 驗證：具名列舉（ScreenId，非數字 index）、每螢幕獨立實例狀態、未知螢幕保守處理、
// null 期單一具名主螢幕、具名角色 / 具名相對錨點（非絕對座標 / 數字 z-order）、
// 重複 id 覆蓋、與 E1-17 每螢幕縮放搭配（PerScreen<double>）。
// 相位 1：只驗介面 + null（宣告式預設）行為，不含任何平台分支或絕對座標。
#include "named_screens.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::kernel::PerScreen;
using ds::kernel::Screen;
using ds::kernel::ScreenAnchor;
using ds::kernel::ScreenRegistry;
using ds::kernel::ScreenRole;

namespace {

// 預設拓撲（null 期）：單一具名主螢幕、角色 Primary、錨點 Center。不真的查詢 OS。
TEST(ScreenRegistry, DefaultsAreSingleNamedPrimary) {
    const ScreenRegistry reg = ScreenRegistry::defaults();
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_FALSE(reg.empty());
    EXPECT_TRUE(reg.is_known("screen.primary"));
    EXPECT_TRUE(reg.is_primary("screen.primary"));
    EXPECT_EQ(reg.anchor_of("screen.primary"), ScreenAnchor::Center);
    const Screen* p = reg.primary();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->id, "screen.primary");
    EXPECT_EQ(p->role, ScreenRole::Primary);
}

// 具名列舉：ids() 依宣告順序回傳具名 ScreenId，對外不暴露數字 index。
TEST(ScreenRegistry, EnumerationIsByNamedId) {
    ScreenRegistry reg(std::vector<Screen>{
        {"screen.laptop", "內建", ScreenRole::Primary, ScreenAnchor::Center},
        {"screen.hdmi", "外接右", ScreenRole::Secondary, ScreenAnchor::Right},
        {"screen.dp", "外接左", ScreenRole::Secondary, ScreenAnchor::Left},
    });
    const std::vector<std::string> ids = reg.ids();
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], "screen.laptop");
    EXPECT_EQ(ids[1], "screen.hdmi");
    EXPECT_EQ(ids[2], "screen.dp");
}

// 幾何以具名角色 + 具名相對錨點表達（非絕對座標 / 數字 z-order）。
TEST(ScreenRegistry, GeometryIsNamedRoleAndAnchor) {
    ScreenRegistry reg(std::vector<Screen>{
        {"screen.main", "主", ScreenRole::Primary, ScreenAnchor::Center},
        {"screen.above", "上方", ScreenRole::Secondary, ScreenAnchor::Above},
    });
    EXPECT_TRUE(reg.is_primary("screen.main"));
    EXPECT_FALSE(reg.is_primary("screen.above"));
    EXPECT_EQ(reg.anchor_of("screen.main"), ScreenAnchor::Center);
    EXPECT_EQ(reg.anchor_of("screen.above"), ScreenAnchor::Above);
    const Screen* s = reg.find("screen.above");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->role, ScreenRole::Secondary);
    EXPECT_EQ(s->anchor, ScreenAnchor::Above);
}

// primary()：第一個 Primary 勝出。
TEST(ScreenRegistry, PrimaryPicksFirstPrimaryRole) {
    ScreenRegistry reg(std::vector<Screen>{
        {"screen.a", "次", ScreenRole::Secondary, ScreenAnchor::Left},
        {"screen.b", "主", ScreenRole::Primary, ScreenAnchor::Center},
        {"screen.c", "另一主", ScreenRole::Primary, ScreenAnchor::Right},
    });
    const Screen* p = reg.primary();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->id, "screen.b");
}

// primary() 保守退回：無任何 Primary 角色時取第一個螢幕。
TEST(ScreenRegistry, PrimaryFallsBackToFirstWhenNoPrimaryRole) {
    ScreenRegistry reg(std::vector<Screen>{
        {"screen.x", "次1", ScreenRole::Secondary, ScreenAnchor::Left},
        {"screen.y", "次2", ScreenRole::Secondary, ScreenAnchor::Right},
    });
    const Screen* p = reg.primary();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->id, "screen.x");
}

// 未知螢幕（核心保守語意）：is_known false、find nullptr、is_primary false、
// anchor_of 回中性 Center。
TEST(ScreenRegistry, UnknownScreenIsConservative) {
    const ScreenRegistry reg = ScreenRegistry::defaults();
    EXPECT_FALSE(reg.is_known("screen.does-not-exist"));
    EXPECT_EQ(reg.find("screen.does-not-exist"), nullptr);
    EXPECT_FALSE(reg.is_primary("screen.does-not-exist"));
    EXPECT_EQ(reg.anchor_of("screen.does-not-exist"), ScreenAnchor::Center);
}

// 空拓撲：任何查詢皆保守回 nullptr / false / 中性。
TEST(ScreenRegistry, EmptyTopologyQueriesAreSafe) {
    ScreenRegistry reg(std::vector<Screen>{});
    EXPECT_EQ(reg.size(), 0u);
    EXPECT_TRUE(reg.empty());
    EXPECT_TRUE(reg.ids().empty());
    EXPECT_FALSE(reg.is_known("anything"));
    EXPECT_EQ(reg.primary(), nullptr);
    EXPECT_FALSE(reg.is_primary("anything"));
    EXPECT_EQ(reg.anchor_of("anything"), ScreenAnchor::Center);
}

// 重複 id：後定義者為準（後端可覆寫先前宣告）。
TEST(ScreenRegistry, DuplicateIdLastWins) {
    ScreenRegistry reg(std::vector<Screen>{
        {"screen.p", "first 次 左", ScreenRole::Secondary, ScreenAnchor::Left},
        {"screen.p", "second 主 中", ScreenRole::Primary, ScreenAnchor::Center},
    });
    const Screen* s = reg.find("screen.p");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->description, "second 主 中");
    EXPECT_TRUE(reg.is_primary("screen.p"));
    EXPECT_EQ(reg.anchor_of("screen.p"), ScreenAnchor::Center);
}

// 每螢幕獨立實例狀態：各具名螢幕的狀態彼此獨立、互不影響。
TEST(PerScreenState, PerScreenInstancesAreIndependent) {
    PerScreen<int> state;
    state.set("screen.a", 10);
    state.set("screen.b", 20);
    state.set("screen.c", 30);
    EXPECT_EQ(state.size(), 3u);
    EXPECT_TRUE(state.has("screen.a"));
    ASSERT_NE(state.find("screen.b"), nullptr);
    EXPECT_EQ(*state.find("screen.b"), 20);
    // 改動一個螢幕的狀態，不影響其他螢幕。
    state.set("screen.b", 200);
    EXPECT_EQ(*state.find("screen.b"), 200);
    EXPECT_EQ(*state.find("screen.a"), 10);
    EXPECT_EQ(*state.find("screen.c"), 30);
}

// 每螢幕實例：未知螢幕保守（find nullptr、has false、get_or 回 fallback）。
TEST(PerScreenState, UnknownScreenIsConservative) {
    PerScreen<int> state;
    state.set("screen.a", 7);
    EXPECT_FALSE(state.has("screen.zzz"));
    EXPECT_EQ(state.find("screen.zzz"), nullptr);
    EXPECT_EQ(state.get_or("screen.zzz", -1), -1);  // fallback
    EXPECT_EQ(state.get_or("screen.a", -1), 7);     // 已知回實際值
}

// 每螢幕實例：重複 set 覆蓋、erase 移除、clear 清空。
TEST(PerScreenState, SetOverwritesEraseAndClear) {
    PerScreen<std::string> state;
    state.set("screen.a", "one");
    state.set("screen.a", "two");  // 覆蓋
    EXPECT_EQ(state.size(), 1u);
    ASSERT_NE(state.find("screen.a"), nullptr);
    EXPECT_EQ(*state.find("screen.a"), "two");

    state.set("screen.b", "b");
    EXPECT_TRUE(state.erase("screen.a"));
    EXPECT_FALSE(state.erase("screen.a"));  // 已不存在
    EXPECT_FALSE(state.has("screen.a"));
    EXPECT_EQ(state.size(), 1u);

    state.clear();
    EXPECT_TRUE(state.empty());
}

// 與 E1-17 每螢幕縮放搭配：以 PerScreen<double> 為各具名螢幕保存縮放係數。
// 各螢幕縮放獨立，未知螢幕保守回中性 1.0。
TEST(PerScreenState, PairsWithPerScreenScaleFactors) {
    ScreenRegistry reg(std::vector<Screen>{
        {"screen.laptop", "內建 1x", ScreenRole::Primary, ScreenAnchor::Center},
        {"screen.retina", "外接 2x", ScreenRole::Secondary, ScreenAnchor::Right},
    });
    PerScreen<double> scale;
    for (const auto& id : reg.ids()) {
        scale.set(id, 1.0);  // 先鋪中性預設
    }
    scale.set("screen.retina", 2.0);  // 覆寫該螢幕縮放

    EXPECT_DOUBLE_EQ(scale.get_or("screen.laptop", 1.0), 1.0);
    EXPECT_DOUBLE_EQ(scale.get_or("screen.retina", 1.0), 2.0);
    // 未在拓撲 / 未設定的螢幕：保守回 fallback（中性 1.0）。
    EXPECT_DOUBLE_EQ(scale.get_or("screen.unknown", 1.0), 1.0);
}

}  // namespace
