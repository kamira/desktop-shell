// E12-02 多語系 — 契約測試（gtest）
//
// 涵蓋：註冊/查詢翻譯、語系切換、缺翻譯回退(預設語系/鍵本身)、參數化訊息佔位替換、
// 缺鍵處理、多語系並存、複數形式。
// 平台中立純邏輯：翻譯資料全由測試注入，無真實 locale / ICU / OS API。
#include "locale_catalog.hpp"

#include <gtest/gtest.h>

using ds::common::LocaleCatalog;
using ds::common::TranslationArgs;
using ds::common::TranslationStatus;
using ds::common::substitute_placeholders;
using ds::common::to_string;

namespace {

// --- 建構 / 契約驗證 --------------------------------------------------------

TEST(Construction, DefaultLocaleBecomesInitialCurrentLocale) {
    LocaleCatalog cat("en");
    EXPECT_EQ("en", cat.default_locale());
    EXPECT_EQ("en", cat.current_locale());
}

TEST(Construction, EmptyDefaultLocaleThrows) {
    EXPECT_THROW(LocaleCatalog(""), std::invalid_argument);
}

TEST(Construction, AddTranslationRejectsEmptyLocaleOrKey) {
    LocaleCatalog cat("en");
    EXPECT_THROW(cat.add_translation("", "key", "text"), std::invalid_argument);
    EXPECT_THROW(cat.add_translation("en", "", "text"), std::invalid_argument);
}

TEST(Construction, SetLocaleRejectsEmptyString) {
    LocaleCatalog cat("en");
    EXPECT_THROW(cat.set_locale(""), std::invalid_argument);
}

TEST(Construction, TranslateRejectsEmptyKey) {
    LocaleCatalog cat("en");
    EXPECT_THROW(cat.translate(""), std::invalid_argument);
}

// --- 註冊 / 查詢翻譯 --------------------------------------------------------

TEST(Registration, AddThenTranslateReturnsRegisteredText) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "app.title", "Desktop Shell");
    EXPECT_EQ("Desktop Shell", cat.translate("app.title"));
}

TEST(Registration, AddTranslationIsChainable) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "a", "A").add_translation("en", "b", "B");
    EXPECT_EQ("A", cat.translate("a"));
    EXPECT_EQ("B", cat.translate("b"));
}

TEST(Registration, AddTranslationOverwritesExisting) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "k", "first");
    cat.add_translation("en", "k", "second");
    EXPECT_EQ("second", cat.translate("k"));
}

TEST(Registration, HasKeyReflectsRegisteredState) {
    LocaleCatalog cat("en");
    EXPECT_FALSE(cat.has_key("en", "k"));
    cat.add_translation("en", "k", "v");
    EXPECT_TRUE(cat.has_key("en", "k"));
    EXPECT_TRUE(cat.has_key("k"));  // 便捷重載 = 查 current_locale()
    EXPECT_FALSE(cat.has_key("fr", "k"));  // 未註冊語系
}

// --- 語系切換 ---------------------------------------------------------------

TEST(LocaleSwitch, SetLocaleChangesTranslationSource) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "greet", "Hello");
    cat.add_translation("zh-Hant", "greet", "你好");

    EXPECT_EQ("Hello", cat.translate("greet"));
    cat.set_locale("zh-Hant");
    EXPECT_EQ("zh-Hant", cat.current_locale());
    EXPECT_EQ("你好", cat.translate("greet"));
}

TEST(LocaleSwitch, SwitchingToUnregisteredLocaleSucceeds) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "greet", "Hello");
    // 切換本身永不失敗；ja 尚無任何翻譯，查詢時自然回退（見下方 Fallback 測試）。
    EXPECT_NO_THROW(cat.set_locale("ja"));
    EXPECT_EQ("ja", cat.current_locale());
}

// --- 多語系並存 --------------------------------------------------------------

TEST(MultiLocale, ThreeLocalesCoexistIndependently) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "app.title", "Desktop Shell");
    cat.add_translation("zh-Hant", "app.title", "桌面殼層");
    cat.add_translation("ja", "app.title", "デスクトップシェル");

    EXPECT_TRUE(cat.has_key("en", "app.title"));
    EXPECT_TRUE(cat.has_key("zh-Hant", "app.title"));
    EXPECT_TRUE(cat.has_key("ja", "app.title"));

    cat.set_locale("ja");
    EXPECT_EQ("デスクトップシェル", cat.translate("app.title"));
    cat.set_locale("zh-Hant");
    EXPECT_EQ("桌面殼層", cat.translate("app.title"));
    cat.set_locale("en");
    EXPECT_EQ("Desktop Shell", cat.translate("app.title"));
}

// --- 缺翻譯回退：預設語系 ----------------------------------------------------

TEST(Fallback, MissingInCurrentLocaleFallsBackToDefaultLocale) {
    LocaleCatalog cat("en");  // default = current = en
    cat.add_translation("en", "app.title", "Desktop Shell");
    cat.set_locale("fr");  // fr 尚未註冊 app.title

    EXPECT_EQ("Desktop Shell", cat.translate("app.title"));

    const auto detail = cat.translate_detailed("app.title");
    EXPECT_EQ(TranslationStatus::LocaleFallback, detail.status);
    EXPECT_EQ("en", detail.locale_used);
    EXPECT_EQ("Desktop Shell", detail.text);
}

TEST(Fallback, ExactHitDoesNotReportAsFallback) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "k", "v");
    const auto detail = cat.translate_detailed("k");
    EXPECT_EQ(TranslationStatus::Exact, detail.status);
    EXPECT_EQ("en", detail.locale_used);
}

TEST(Fallback, CurrentLocaleEqualsDefaultLocaleIsExactNotFallback) {
    // current_locale == default_locale 時，命中不應被誤判為 LocaleFallback。
    LocaleCatalog cat("en");
    cat.add_translation("en", "k", "v");
    const auto detail = cat.translate_detailed("k");
    EXPECT_EQ(TranslationStatus::Exact, detail.status);
}

// --- 缺翻譯回退：鍵本身（可見標記，不靜默）-----------------------------------

TEST(Fallback, MissingEverywhereFallsBackToVisibleKeyMarker) {
    LocaleCatalog cat("en");
    cat.set_locale("fr");
    // "app.unknown" 在 en（default）與 fr（current）皆未註冊。
    const std::string result = cat.translate("app.unknown");
    EXPECT_EQ("[missing:app.unknown]", result);

    const auto detail = cat.translate_detailed("app.unknown");
    EXPECT_EQ(TranslationStatus::KeyFallback, detail.status);
    EXPECT_TRUE(detail.locale_used.empty());
}

TEST(Fallback, KeyFallbackNeverThrowsEvenForCompletelyUnknownLocaleAndKey) {
    LocaleCatalog cat("en");
    cat.set_locale("xx-Unregistered");
    EXPECT_NO_THROW({
        const std::string r = cat.translate("totally.unknown.key");
        EXPECT_EQ("[missing:totally.unknown.key]", r);
    });
}

// --- 缺鍵處理（has_key 的存在性語意不含回退）---------------------------------

TEST(MissingKeyHandling, HasKeyIsExactNotFallbackAware) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "k", "v");
    cat.set_locale("fr");
    // fr 沒有 "k"：has_key(current) 應為 false，即使 translate() 會回退成功。
    EXPECT_FALSE(cat.has_key("k"));
    EXPECT_EQ("v", cat.translate("k"));  // 回退成功
}

// --- 參數化訊息（佔位替換）---------------------------------------------------

TEST(Placeholders, SingleNamedPlaceholderSubstituted) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "greet.name", "Hello, {name}!");
    EXPECT_EQ("Hello, Ada!", cat.translate("greet.name", {{"name", "Ada"}}));
}

TEST(Placeholders, MultiplePlaceholdersSubstituted) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "file.saved", "{file} saved to {dir}");
    TranslationArgs args{{"file", "notes.txt"}, {"dir", "/home"}};
    EXPECT_EQ("notes.txt saved to /home", cat.translate("file.saved", args));
}

TEST(Placeholders, UnprovidedPlaceholderLeftVisiblyIntact) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "greet.name", "Hello, {name}!");
    // args 未提供 "name"：原樣保留 "{name}"（可見，不靜默消失）。
    EXPECT_EQ("Hello, {name}!", cat.translate("greet.name"));
}

TEST(Placeholders, UnclosedBraceTreatedAsLiteral) {
    EXPECT_EQ("literal { brace", substitute_placeholders("literal { brace", {}));
}

TEST(Placeholders, NoPlaceholdersReturnsTextUnchanged) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "k", "plain text");
    EXPECT_EQ("plain text", cat.translate("k"));
}

TEST(Placeholders, SubstitutionAppliesAfterFallbackResolution) {
    LocaleCatalog cat("en");
    cat.add_translation("en", "greet.name", "Hello, {name}!");
    cat.set_locale("fr");  // fr 缺此鍵 → 回退 en，仍要代入 args
    EXPECT_EQ("Hello, Ada!", cat.translate("greet.name", {{"name", "Ada"}}));
}

// --- 複數形式（可選）---------------------------------------------------------

TEST(Plural, SingularWhenCountIsOne) {
    LocaleCatalog cat("en");
    cat.add_plural("en", "item.count", "{count} item", "{count} items");
    EXPECT_EQ("1 item", cat.translate_plural("item.count", 1));
}

TEST(Plural, PluralWhenCountIsNotOne) {
    LocaleCatalog cat("en");
    cat.add_plural("en", "item.count", "{count} item", "{count} items");
    EXPECT_EQ("0 items", cat.translate_plural("item.count", 0));
    EXPECT_EQ("2 items", cat.translate_plural("item.count", 2));
    EXPECT_EQ("5 items", cat.translate_plural("item.count", 5));
}

TEST(Plural, ExplicitCountArgOverridesAutoInjected) {
    LocaleCatalog cat("en");
    cat.add_plural("en", "item.count", "{count} item", "{count} items");
    // 顯式提供 "count" 時不被自動注入的值覆蓋。
    TranslationArgs args{{"count", "many"}};
    EXPECT_EQ("many items", cat.translate_plural("item.count", 2, args));
}

TEST(Plural, FallsBackToDefaultLocaleWhenMissingInCurrent) {
    LocaleCatalog cat("en");
    cat.add_plural("en", "item.count", "{count} item", "{count} items");
    cat.set_locale("fr");
    EXPECT_EQ("3 items", cat.translate_plural("item.count", 3));
}

TEST(Plural, MissingEverywhereFallsBackToVisibleKeyMarker) {
    LocaleCatalog cat("en");
    cat.set_locale("fr");
    EXPECT_EQ("[missing:item.count]", cat.translate_plural("item.count", 3));
}

TEST(Plural, MultiLocalePluralsCoexist) {
    LocaleCatalog cat("en");
    cat.add_plural("en", "item.count", "{count} item", "{count} items");
    cat.add_plural("zh-Hant", "item.count", "{count} 個項目", "{count} 個項目");
    cat.set_locale("zh-Hant");
    EXPECT_EQ("1 個項目", cat.translate_plural("item.count", 1));
    cat.set_locale("en");
    EXPECT_EQ("1 item", cat.translate_plural("item.count", 1));
}

// --- to_string 診斷 -----------------------------------------------------------

TEST(StatusString, StableDiagnostics) {
    EXPECT_STREQ("exact", to_string(TranslationStatus::Exact));
    EXPECT_STREQ("locale_fallback", to_string(TranslationStatus::LocaleFallback));
    EXPECT_STREQ("key_fallback", to_string(TranslationStatus::KeyFallback));
}

}  // namespace
