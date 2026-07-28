// E9-04 主題切換 — 契約測試（gtest）
//
// 驗證：註冊 / 列舉主題（註冊順序、空名稱拒絕、重複拒絕）、切換主題、當前主題查詢
// （無當前主題 throw、切換後可查、current_name）、主題變更通知（切換觸發、帶新資料、
// 多回呼、切到同一主題不重複觸發）、查無主題明確報錯（switch 未知失敗且當前不變、
// find_theme/has_theme）、切回（A→B→A 每次真變更各觸發一次）、與 E7-07 熱重載整合
// （theme_from_document + set_theme 於當前主題自動重新套用並通知）、與 E9-03 可互換組合
// 整合（切換主題即切換其元件組合）。平台中立：不含任何平台分支。
#include "theme.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "composition.hpp"  // E9-03：Composition / ComponentSlot
#include "document.hpp"     // E7-01：parse / Document / Value
#include "hot_reload.hpp"   // E7-07：MemorySource / HotReloader
#include "package.hpp"      // E9-01：parse_package / Package

using ds::format::Document;
using ds::format::HotReloader;
using ds::format::MemorySource;
using ds::format::parse;
using ds::package::ComponentSlot;
using ds::package::Composition;
using ds::package::Package;
using ds::package::parse_package;
using ds::package::theme_from_document;
using ds::package::ThemeData;
using ds::package::ThemeManager;

namespace {

// 由套件描述文字建立一個結構完整的元件（Package）。
Package make_component(const std::string& text) {
    const auto r = parse_package(text);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return r.package();
}

// 一個只提供 asset 的合法元件（供「需要 asset」的插槽相容）。
Package icon_pkg(const std::string& name, const std::string& path) {
    return make_component(
        "format_version: 1.0\n"
        "name: " + name + "\n"
        "---\n"
        "asset: " + path + "\n");
}

// 一個帶顏色/樣式宣告式屬性表的主題（透過 E7-01 解析出 Document 再轉 ThemeData）。
ThemeData doc_theme(const std::string& name, const std::string& body) {
    const auto r = parse("format_version: 1.0\n" + body);
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    return theme_from_document(name, r.document());
}

// 一個帶可互換元件組合的主題：組合含一個 "icons" 插槽（需 asset），綁定指定元件。
ThemeData comp_theme(const std::string& name, const Package& icons) {
    ThemeData t;
    t.name = name;
    Composition c;
    EXPECT_TRUE(c.add_slot(ComponentSlot{"icons", {"asset"}}).ok);
    EXPECT_TRUE(c.bind("icons", icons).ok);
    t.components = c;
    return t;
}

}  // namespace

// ----------------------------------------------------------------------------
// 註冊 / 列舉
// ----------------------------------------------------------------------------

TEST(ThemeRegister, RegistersAndListsInOrder) {
    ThemeManager mgr;
    EXPECT_TRUE(mgr.register_theme("dark", {}).ok);
    EXPECT_TRUE(mgr.register_theme("light", {}).ok);
    EXPECT_TRUE(mgr.register_theme("solarized", {}).ok);

    EXPECT_EQ(mgr.size(), 3u);
    const std::vector<std::string> expected{"dark", "light", "solarized"};
    EXPECT_EQ(mgr.list_themes(), expected);
    EXPECT_TRUE(mgr.has_theme("light"));
    EXPECT_FALSE(mgr.has_theme("nope"));
}

TEST(ThemeRegister, EmptyNameRejected) {
    ThemeManager mgr;
    const auto r = mgr.register_theme("", {});
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.message.empty());  // 明確報錯，不靜默。
    EXPECT_EQ(mgr.size(), 0u);
}

TEST(ThemeRegister, DuplicateRejected) {
    ThemeManager mgr;
    EXPECT_TRUE(mgr.register_theme("dark", {}).ok);
    const auto r = mgr.register_theme("dark", {});
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(mgr.size(), 1u);  // 未新增第二筆。
}

TEST(ThemeRegister, RegisterAuthoritativeName) {
    // data.name 與註冊鍵不符時，以註冊鍵為準。
    ThemeManager mgr;
    ThemeData d;
    d.name = "WRONG";
    EXPECT_TRUE(mgr.register_theme("dark", d).ok);
    ASSERT_NE(mgr.find_theme("dark"), nullptr);
    EXPECT_EQ(mgr.find_theme("dark")->name, "dark");
    EXPECT_FALSE(mgr.has_theme("WRONG"));
}

// ----------------------------------------------------------------------------
// 切換 / 當前主題查詢
// ----------------------------------------------------------------------------

TEST(ThemeCurrent, NoCurrentThrows) {
    ThemeManager mgr;
    EXPECT_FALSE(mgr.has_current());
    EXPECT_THROW(mgr.current(), std::runtime_error);
    EXPECT_THROW(mgr.current_name(), std::runtime_error);
}

TEST(ThemeSwitch, SwitchSetsCurrent) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", doc_theme("dark", "bg: black\n")).ok);
    ASSERT_TRUE(mgr.register_theme("light", doc_theme("light", "bg: white\n")).ok);

    ASSERT_TRUE(mgr.switch_to("dark").ok);
    ASSERT_TRUE(mgr.has_current());
    EXPECT_EQ(mgr.current_name(), "dark");
    ASSERT_TRUE(mgr.current().attributes.is_map());
    EXPECT_EQ(mgr.current().attributes.at("bg").as_string(), "black");
}

TEST(ThemeSwitch, UnknownThemeFailsAndKeepsCurrent) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", {}).ok);
    ASSERT_TRUE(mgr.switch_to("dark").ok);

    const auto r = mgr.switch_to("ghost");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.message.empty());          // 查無主題明確報錯，不靜默。
    EXPECT_EQ(mgr.current_name(), "dark");    // 當前主題不變。
}

TEST(ThemeQuery, FindUnknownReturnsNullptr) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", {}).ok);
    EXPECT_NE(mgr.find_theme("dark"), nullptr);
    EXPECT_EQ(mgr.find_theme("ghost"), nullptr);
}

// ----------------------------------------------------------------------------
// 主題變更通知
// ----------------------------------------------------------------------------

TEST(ThemeNotify, SwitchTriggersCallbackWithNewData) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", doc_theme("dark", "bg: black\n")).ok);
    ASSERT_TRUE(mgr.register_theme("light", doc_theme("light", "bg: white\n")).ok);

    int fired = 0;
    std::string last_name;
    std::string last_bg;
    mgr.on_theme_change([&](const ThemeData& t) {
        ++fired;
        last_name = t.name;
        last_bg = t.attributes.at("bg").as_string();
    });

    ASSERT_TRUE(mgr.switch_to("light").ok);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(last_name, "light");
    EXPECT_EQ(last_bg, "white");
}

TEST(ThemeNotify, MultipleCallbacksAllFire) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", {}).ok);
    int a = 0, b = 0;
    mgr.on_theme_change([&](const ThemeData&) { ++a; });
    mgr.on_theme_change([&](const ThemeData&) { ++b; });
    ASSERT_TRUE(mgr.switch_to("dark").ok);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(ThemeNotify, SwitchToSameIsNoopNoNotify) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", {}).ok);
    ASSERT_TRUE(mgr.switch_to("dark").ok);

    int fired = 0;
    mgr.on_theme_change([&](const ThemeData&) { ++fired; });
    const auto r = mgr.switch_to("dark");  // 已是當前主題。
    EXPECT_TRUE(r.ok);                       // no-op 仍算成功。
    EXPECT_EQ(fired, 0);                      // 不重複觸發。
}

TEST(ThemeNotify, SwitchBackTriggersEachRealChange) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", {}).ok);
    ASSERT_TRUE(mgr.register_theme("light", {}).ok);

    std::vector<std::string> seen;
    mgr.on_theme_change([&](const ThemeData& t) { seen.push_back(t.name); });

    ASSERT_TRUE(mgr.switch_to("dark").ok);
    ASSERT_TRUE(mgr.switch_to("light").ok);
    ASSERT_TRUE(mgr.switch_to("dark").ok);  // 切回。

    const std::vector<std::string> expected{"dark", "light", "dark"};
    EXPECT_EQ(seen, expected);
}

// ----------------------------------------------------------------------------
// 與 E7-07 熱重載整合
// ----------------------------------------------------------------------------

TEST(ThemeHotReload, DocumentBecomesThemeAttributes) {
    MemorySource src(
        "format_version: 1.0\n"
        "name: dark\n"
        "colors:\n"
        "  background: \"#101010\"\n");
    HotReloader reloader(src);
    const auto r = reloader.poll();
    ASSERT_TRUE(r.reloaded());

    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", theme_from_document("dark", reloader.document())).ok);
    ASSERT_TRUE(mgr.switch_to("dark").ok);

    const auto& attrs = mgr.current().attributes;
    ASSERT_TRUE(attrs.is_map());
    ASSERT_TRUE(attrs.contains("colors"));
    EXPECT_EQ(attrs.at("colors").at("background").as_string(), "#101010");
}

TEST(ThemeHotReload, ReloadReAppliesCurrentThemeAndNotifies) {
    MemorySource src(
        "format_version: 1.0\n"
        "name: dark\n"
        "colors:\n"
        "  background: \"#101010\"\n");
    HotReloader reloader(src);
    ASSERT_TRUE(reloader.poll().reloaded());

    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", theme_from_document("dark", reloader.document())).ok);
    ASSERT_TRUE(mgr.switch_to("dark").ok);

    int fired = 0;
    std::string applied_bg;
    mgr.on_theme_change([&](const ThemeData& t) {
        ++fired;
        applied_bg = t.attributes.at("colors").at("background").as_string();
    });

    // 外部來源變更 → 熱重載出新文件 → set_theme 更新「當前」主題 → 自動重新套用並通知。
    src.set_content(
        "format_version: 1.0\n"
        "name: dark\n"
        "colors:\n"
        "  background: \"#202020\"\n");
    const auto r2 = reloader.poll();
    ASSERT_TRUE(r2.reloaded());
    ASSERT_TRUE(mgr.set_theme("dark", theme_from_document("dark", reloader.document())).ok);

    EXPECT_EQ(fired, 1);
    EXPECT_EQ(applied_bg, "#202020");
    EXPECT_EQ(mgr.current().attributes.at("colors").at("background").as_string(), "#202020");
}

TEST(ThemeHotReload, SetThemeOnNonCurrentDoesNotNotify) {
    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", doc_theme("dark", "bg: black\n")).ok);
    ASSERT_TRUE(mgr.register_theme("light", doc_theme("light", "bg: white\n")).ok);
    ASSERT_TRUE(mgr.switch_to("dark").ok);

    int fired = 0;
    mgr.on_theme_change([&](const ThemeData&) { ++fired; });

    // 更新的是非當前主題 → 不通知；且 set_theme 對未存在名稱為新增（upsert）。
    ASSERT_TRUE(mgr.set_theme("light", doc_theme("light", "bg: gray\n")).ok);
    ASSERT_TRUE(mgr.set_theme("hicontrast", doc_theme("hicontrast", "bg: pure\n")).ok);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(mgr.size(), 3u);  // dark / light / hicontrast。
    EXPECT_EQ(mgr.find_theme("light")->attributes.at("bg").as_string(), "gray");
}

// ----------------------------------------------------------------------------
// 與 E9-03 可互換組合整合
// ----------------------------------------------------------------------------

TEST(ThemeComposition, SwitchingThemeSwitchesComponentSet) {
    const Package dark_icons = icon_pkg("com.example.icons.dark", "icons/dark.png");
    const Package light_icons = icon_pkg("com.example.icons.light", "icons/light.png");

    ThemeManager mgr;
    ASSERT_TRUE(mgr.register_theme("dark", comp_theme("dark", dark_icons)).ok);
    ASSERT_TRUE(mgr.register_theme("light", comp_theme("light", light_icons)).ok);

    // 相依端於變更回呼中讀取當前主題的組合並套用其綁定元件。
    std::string applied_icon;
    mgr.on_theme_change([&](const ThemeData& t) {
        const Package* p = t.components.bound_component("icons");
        applied_icon = (p != nullptr) ? p->manifest.name : std::string{"<none>"};
    });

    ASSERT_TRUE(mgr.switch_to("dark").ok);
    EXPECT_EQ(applied_icon, "com.example.icons.dark");

    ASSERT_TRUE(mgr.switch_to("light").ok);
    EXPECT_EQ(applied_icon, "com.example.icons.light");

    // 也可直接自當前主題讀取組合。
    const Package* cur = mgr.current().components.bound_component("icons");
    ASSERT_NE(cur, nullptr);
    EXPECT_EQ(cur->manifest.name, "com.example.icons.light");
}
