// E12-02 多語系 — 實作（engine 層 / 平台中立純邏輯）
//
// 無 `#ifdef`、無平台分支、無真實 locale / ICU / OS API：翻譯資料全由呼叫端注入
// （`add_translation` / `add_plural`），本層只做查詢 + 回退 + 佔位替換。
#include "locale_catalog.hpp"

#include <stdexcept>
#include <utility>

namespace ds::common {

// ---------------------------------------------------------------------------
// 自由函式
// ---------------------------------------------------------------------------
const char* to_string(TranslationStatus status) noexcept {
    switch (status) {
        case TranslationStatus::Exact:          return "exact";
        case TranslationStatus::LocaleFallback:  return "locale_fallback";
        case TranslationStatus::KeyFallback:     return "key_fallback";
    }
    return "unknown";  // 不可達；防禦性
}

std::string substitute_placeholders(const std::string& text, const TranslationArgs& args) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        if (text[i] == '{') {
            const std::size_t close = text.find('}', i + 1);
            if (close == std::string::npos) {
                // 未閉合的 '{'：視為 literal 字元，原樣輸出剩餘內容。
                out.append(text, i, n - i);
                break;
            }
            const std::string name = text.substr(i + 1, close - i - 1);
            const auto it = args.find(name);
            if (it != args.end()) {
                out += it->second;
            } else {
                // 未提供的佔位符原樣保留（可見原則：漏傳參數一望即知，非靜默消失）。
                out.append(text, i, close - i + 1);
            }
            i = close + 1;
        } else {
            out += text[i];
            ++i;
        }
    }
    return out;
}

namespace {
std::string wrap_missing(const MessageKey& key) {
    // 缺鍵回退的可見標記：形如 "[missing:key]"，避免在畫面上與正常文案混淆。
    return "[missing:" + key + "]";
}
}  // namespace

// ---------------------------------------------------------------------------
// LocaleCatalog
// ---------------------------------------------------------------------------
LocaleCatalog::LocaleCatalog(LocaleId default_locale)
    : default_locale_(std::move(default_locale)), current_locale_(default_locale_) {
    if (default_locale_.empty()) {
        throw std::invalid_argument("LocaleCatalog: default_locale 不可為空字串");
    }
}

LocaleCatalog& LocaleCatalog::add_translation(const LocaleId& locale, const MessageKey& key,
                                              const std::string& text) {
    if (locale.empty()) {
        throw std::invalid_argument("LocaleCatalog::add_translation: locale 不可為空字串");
    }
    if (key.empty()) {
        throw std::invalid_argument("LocaleCatalog::add_translation: key 不可為空字串");
    }
    translations_[locale][key] = text;
    return *this;
}

LocaleCatalog& LocaleCatalog::add_plural(const LocaleId& locale, const MessageKey& key,
                                         const std::string& singular_text,
                                         const std::string& plural_text) {
    if (locale.empty()) {
        throw std::invalid_argument("LocaleCatalog::add_plural: locale 不可為空字串");
    }
    if (key.empty()) {
        throw std::invalid_argument("LocaleCatalog::add_plural: key 不可為空字串");
    }
    plurals_[locale][key] = std::make_pair(singular_text, plural_text);
    return *this;
}

void LocaleCatalog::set_locale(const LocaleId& locale) {
    if (locale.empty()) {
        throw std::invalid_argument("LocaleCatalog::set_locale: locale 不可為空字串");
    }
    current_locale_ = locale;
}

bool LocaleCatalog::has_key(const LocaleId& locale, const MessageKey& key) const {
    const auto lit = translations_.find(locale);
    if (lit == translations_.end()) {
        return false;
    }
    return lit->second.find(key) != lit->second.end();
}

bool LocaleCatalog::has_key(const MessageKey& key) const {
    return has_key(current_locale_, key);
}

TranslationResult LocaleCatalog::translate_detailed(const MessageKey& key,
                                                    const TranslationArgs& args) const {
    if (key.empty()) {
        throw std::invalid_argument("LocaleCatalog::translate: key 不可為空字串");
    }

    // 第一層：current_locale 直接命中。
    {
        const auto lit = translations_.find(current_locale_);
        if (lit != translations_.end()) {
            const auto kit = lit->second.find(key);
            if (kit != lit->second.end()) {
                TranslationResult r;
                r.text = substitute_placeholders(kit->second, args);
                r.status = TranslationStatus::Exact;
                r.locale_used = current_locale_;
                return r;
            }
        }
    }

    // 第二層：退而使用 default_locale（current_locale 本身就是 default_locale 時，
    // 上一層已含蓋，不會誤判為「回退」）。
    if (current_locale_ != default_locale_) {
        const auto lit = translations_.find(default_locale_);
        if (lit != translations_.end()) {
            const auto kit = lit->second.find(key);
            if (kit != lit->second.end()) {
                TranslationResult r;
                r.text = substitute_placeholders(kit->second, args);
                r.status = TranslationStatus::LocaleFallback;
                r.locale_used = default_locale_;
                return r;
            }
        }
    }

    // 第三層：兩層皆缺 → 回退為可見標記包裹的鍵本身（不靜默、不擲例外）。
    TranslationResult r;
    r.text = wrap_missing(key);
    r.status = TranslationStatus::KeyFallback;
    r.locale_used.clear();
    return r;
}

std::string LocaleCatalog::translate(const MessageKey& key, const TranslationArgs& args) const {
    return translate_detailed(key, args).text;
}

std::string LocaleCatalog::translate_plural(const MessageKey& key, long count,
                                            TranslationArgs args) const {
    if (key.empty()) {
        throw std::invalid_argument("LocaleCatalog::translate_plural: key 不可為空字串");
    }
    if (args.find("count") == args.end()) {
        args["count"] = std::to_string(count);
    }

    const bool is_singular = (count == 1);

    auto lookup_plural = [&](const LocaleId& locale) -> const std::string* {
        const auto lit = plurals_.find(locale);
        if (lit == plurals_.end()) {
            return nullptr;
        }
        const auto kit = lit->second.find(key);
        if (kit == lit->second.end()) {
            return nullptr;
        }
        return is_singular ? &kit->second.first : &kit->second.second;
    };

    // 第一層：current_locale。
    if (const std::string* text = lookup_plural(current_locale_)) {
        return substitute_placeholders(*text, args);
    }
    // 第二層：default_locale（回退）。
    if (current_locale_ != default_locale_) {
        if (const std::string* text = lookup_plural(default_locale_)) {
            return substitute_placeholders(*text, args);
        }
    }
    // 第三層：兩層皆缺 → 與 translate() 一致的可見缺鍵標記。
    return wrap_missing(key);
}

}  // namespace ds::common
