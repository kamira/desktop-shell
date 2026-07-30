// E12-02 多語系 — 契約 + 純邏輯翻譯目錄（engine 層 / 子系統 common / 平台中立）
//
// 語意：**多語系 / 國際化（i18n）**——具名訊息鍵（`MessageKey`）→ 各語系（`LocaleId`）
// 翻譯字串的查詢、當前語系切換、缺翻譯回退、參數化訊息（佔位替換）、複數形式（可選）。
// 供文字元件（E4-01 排版）在顯示前把具名鍵解析為在地化文字。
//
// 分層約束（engine 層）：
//   - **平台中立、純邏輯**：無 `#ifdef`、無平台分支、無真實 locale / ICU / OS API。語系與翻譯
//     字串一律由呼叫端以 `add_translation` 靜態注入（未來相位可由資源檔載入，但載入邏輯不在
//     本單元——本單元只管「查詢 + 回退 + 佔位替換」）。
//   - 相依 E4-01（`target_link_libraries(e12_02 PUBLIC e4_01)`）：本單元產出的在地化文字
//     即 E4-01 `TextLayout::layout()` 的輸入字串；E4-01 本身不知道多語系，翻譯在排版之前完成。
//
// 缺鍵 / 缺語系回退（明確可見，不靜默崩潰）：
//   - `current_locale` 缺此鍵 → 退而使用 `default_locale` 的翻譯（`LocaleFallback`）。
//   - `default_locale` 也缺此鍵 → 退而回傳**以可見標記包裹的鍵本身**（`KeyFallback`，
//     形如 `[missing:key]`），讓遺漏在畫面上一望即知，而非靜默顯示空白或吞掉鍵名混入正常文案。
//   - `translate()` 系列**絕不擲例外**：查無翻譯是預期可回退的情境，非程式錯誤；
//     只有明顯的程式呼叫錯誤（如空鍵字串）才視為契約違反。
//
// 佔位替換：訊息文字內 `{name}` 會被 `args` 中對應鍵的值取代；`args` 未提供的佔位符**原樣
// 保留**（同樣是「可見」原則——未替換的 `{name}` 比默默消失更容易被發現是漏傳參數）。
#ifndef DS_ENGINE_E12_02_LOCALE_CATALOG_HPP
#define DS_ENGINE_E12_02_LOCALE_CATALOG_HPP

#include <map>
#include <string>

namespace ds::common {

// 語系識別碼（純字串身分，如 "en"、"zh-Hant"、"ja"；本單元不驗證真實 BCP-47 語法，
// 純粹作對照表的鍵——平台中立、無真實 locale API）。
using LocaleId = std::string;

// 具名訊息鍵（如 "app.title"、"dialog.confirm"）。
using MessageKey = std::string;

// 佔位替換參數：名稱 → 替換值。以 std::map 求穩定疊代順序（診斷 / 測試可預期）。
using TranslationArgs = std::map<std::string, std::string>;

// 翻譯回退狀態（供呼叫端診斷 / 記錄；`translate()` 簡便版只回文字，細節版回本狀態）。
enum class TranslationStatus {
    Exact,           // current_locale 直接命中此鍵。
    LocaleFallback,  // current_locale 缺此鍵，改用 default_locale 的翻譯。
    KeyFallback,     // default_locale 亦缺此鍵，回退為可見標記包裹的鍵本身。
};

// 診斷用穩定字串（"exact" / "locale_fallback" / "key_fallback"）。
const char* to_string(TranslationStatus status) noexcept;

// 翻譯查詢的完整結果（細節版 `translate_detailed` 的回傳型別）。
struct TranslationResult {
    std::string text;                            // 已完成佔位替換的最終顯示文字。
    TranslationStatus status = TranslationStatus::Exact;
    LocaleId locale_used;                         // 實際命中翻譯的語系；KeyFallback 時為空字串。
};

// ---------------------------------------------------------------------------
// LocaleCatalog：多語系翻譯目錄。
// ---------------------------------------------------------------------------
// 用法：以 `add_translation` 逐語系逐鍵註冊翻譯字串；以 `set_locale` 切換當前語系；
// 以 `translate(key, args)` 取得已代入參數的在地化文字（缺翻譯自動回退，見上）。
// 純邏輯、可注入、無 I/O：翻譯資料全由呼叫端傳入，完全可單元測試。
class LocaleCatalog {
public:
    // default_locale：查無 current_locale 翻譯時的第一層回退目標；亦是初始 current_locale。
    // 空字串 default_locale → std::invalid_argument（契約違反：目錄至少要有一個基準語系）。
    explicit LocaleCatalog(LocaleId default_locale = "en");

    // -- 註冊 ------------------------------------------------------------

    // 註冊（或覆寫）一則翻譯：locale 下 key 對應 text。locale / key 為空字串 →
    // std::invalid_argument（契約違反，非「缺翻譯」的可回退情境）。回傳 *this 供鏈式呼叫。
    LocaleCatalog& add_translation(const LocaleId& locale, const MessageKey& key,
                                   const std::string& text);

    // 註冊複數形式（可選）：locale 下 key 的單數 / 複數文字。count == 1 用 singular_text，
    // 其餘（含 0、負值、>1）用 plural_text（簡化英式二態；不做完整 CLDR 複數規則——
    // 相位 1 契約單元，複雜複數規則留待未來相位視需求擴充）。
    LocaleCatalog& add_plural(const LocaleId& locale, const MessageKey& key,
                              const std::string& singular_text,
                              const std::string& plural_text);

    // -- 語系切換 ----------------------------------------------------------

    // 切換當前語系。locale 不必已有任何翻譯（切換本身永不失敗；查詢時自然回退）。
    // 空字串 → std::invalid_argument（契約違反）。
    void set_locale(const LocaleId& locale);
    const LocaleId& current_locale() const noexcept { return current_locale_; }
    const LocaleId& default_locale() const noexcept { return default_locale_; }

    // -- 查詢 ------------------------------------------------------------

    // 指定語系是否已註冊此鍵的翻譯（不含回退；純粹存在性查詢）。
    bool has_key(const LocaleId& locale, const MessageKey& key) const;
    // 便捷重載：對 current_locale() 查詢。
    bool has_key(const MessageKey& key) const;

    // -- 翻譯（不擲例外；缺翻譯回退，見檔頭說明）--------------------------

    // 簡便版：只回最終顯示文字（已代入 args 佔位）。
    std::string translate(const MessageKey& key, const TranslationArgs& args = {}) const;

    // 細節版：連同回退狀態 / 實際命中語系一併回傳，供診斷 / 記錄。
    TranslationResult translate_detailed(const MessageKey& key,
                                         const TranslationArgs& args = {}) const;

    // 複數形式翻譯：依 count 選單 / 複數文字（見 add_plural），再代入 args 佔位；
    // 若 args 未顯式提供 "count"，自動注入 count 的十進位字串表示供 {count} 佔位使用。
    // 缺翻譯回退規則與 translate() 相同（僅來源改查複數表）。
    std::string translate_plural(const MessageKey& key, long count,
                                 TranslationArgs args = {}) const;

private:
    LocaleId default_locale_;
    LocaleId current_locale_;
    std::map<LocaleId, std::map<MessageKey, std::string>> translations_;
    std::map<LocaleId, std::map<MessageKey, std::pair<std::string, std::string>>> plurals_;
};

// 佔位替換工具（供 translate 系列內部使用，亦對外公開便於測試 / 其他單元覆用）：
// 掃描 text 中的 `{name}` 記號，以 args[name] 取代；args 未含的 name 原樣保留（可見原則，
// 見檔頭）。未閉合的 `{` 視為 literal 字元（不擲例外——樣板語法錯誤不是本單元的驗證職責）。
std::string substitute_placeholders(const std::string& text, const TranslationArgs& args);

}  // namespace ds::common

#endif  // DS_ENGINE_E12_02_LOCALE_CATALOG_HPP
