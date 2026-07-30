// E7-10 字串處理 — 實作（平台中立 / engine 層）。見 strings.hpp 檔首語意。
//
// 架構：一組純函式，輸入 / 輸出以 E7-01 `Value` 表達、結果 / 錯誤沿用 E7-05
// `EvalResult` / `EvalError`。內部以受控例外 `StrException` 承載 `EvalError`，於每個
// 公開函式邊界由 `guard()` 捕捉轉 failure（讓內部碼保持乾淨、對外仍為無例外的結果物件
// ——沿用 E7-05 手法）。UTF-8 碼位運算不依賴任何平台 API；無任何平台分支
// （`#ifdef` / win32 / cocoa）。
#include "strings.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ds::format {
namespace strings {

namespace {

// -----------------------------------------------------------------------------
// 內部：受控例外承載 EvalError（於公開函式邊界由 guard() 捕捉轉 failure）
// -----------------------------------------------------------------------------

struct StrException {
    EvalError err;
};

[[noreturn]] void fail(std::string message) {
    // 字串運算錯誤無「運算式欄位置」語意；position 統一為 0（對齊 E7-05「非特定位置 = 0」）。
    throw StrException{EvalError{0, std::move(message)}};
}

// 於邊界捕捉 StrException 轉 failure；成功則包成 success。
template <class F>
EvalResult guard(F&& f) {
    try {
        return EvalResult::success(f());
    } catch (const StrException& ex) {
        return EvalResult::failure(ex.err);
    }
}

// -----------------------------------------------------------------------------
// 型別守衛（不靜默：型別 / 索引不符一律明確報錯）
// -----------------------------------------------------------------------------

const std::string& as_str(const Value& v, const char* ctx) {
    if (!v.is_string()) {
        fail(std::string("字串運算需要字串，但 ") + ctx + " 不是字串型別");
    }
    return v.as_string();
}

std::int64_t as_index(const Value& v, const char* ctx) {
    if (!v.is_integer()) {
        fail(std::string("字串運算需要整數，但 ") + ctx + " 不是整數型別");
    }
    return v.as_int();
}

// -----------------------------------------------------------------------------
// UTF-8 碼位邊界
// -----------------------------------------------------------------------------

// 回傳每個碼位的起始位元組位移，並在尾端補上總長度（sentinel）。
// 故碼位數 = 回傳大小 - 1；碼位 i 佔 [off[i], off[i+1]) 位元組。
// 遇非法 UTF-8 前導 / 續接位元組時，寬容地將該位元組視為單一單位（不崩潰、不吞資料）。
std::vector<std::size_t> cp_offsets(const std::string& s) {
    std::vector<std::size_t> off;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        off.push_back(i);
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t adv = 1;
        if (c < 0x80) {
            adv = 1;
        } else if ((c >> 5) == 0x6) {
            adv = 2;
        } else if ((c >> 4) == 0xE) {
            adv = 3;
        } else if ((c >> 3) == 0x1E) {
            adv = 4;
        } else {
            adv = 1;  // 非法前導位元組：視為單一單位。
        }
        if (adv > 1) {
            if (i + adv > n) {
                adv = 1;  // 尾端截斷：退回單一單位。
            } else {
                for (std::size_t k = 1; k < adv; ++k) {
                    if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                        adv = 1;  // 續接位元組非法：退回單一單位。
                        break;
                    }
                }
            }
        }
        i += adv;
    }
    off.push_back(n);
    return off;
}

// 碼位數。
std::size_t cp_count(const std::string& s) { return cp_offsets(s).size() - 1; }

// -----------------------------------------------------------------------------
// 值字串化（供 format 使用；容器無法字串化 → 型別錯誤）
// -----------------------------------------------------------------------------

std::string trim_double(double v) {
    std::string t = std::to_string(v);
    // std::to_string(double) 常帶尾零（如 "3.140000"）；修去尾零與孤立小數點以求決定性輸出。
    if (t.find('.') != std::string::npos) {
        std::size_t end = t.size();
        while (end > 0 && t[end - 1] == '0') --end;
        if (end > 0 && t[end - 1] == '.') --end;
        t.erase(end);
    }
    return t;
}

std::string stringify(const Value& v, const char* ctx) {
    switch (v.type()) {
        case ValueType::Null:
            return "null";
        case ValueType::Bool:
            return v.as_bool() ? "true" : "false";
        case ValueType::Number:
            return v.is_integer() ? std::to_string(v.as_int()) : trim_double(v.as_number());
        case ValueType::String:
            return v.as_string();
        case ValueType::List:
        case ValueType::Map:
            fail(std::string("無法將容器型別（List / Map）字串化：") + ctx);
    }
    fail(std::string("未知的 Value 型別：") + ctx);  // 不可達；滿足所有路徑有回值。
}

}  // namespace

// -----------------------------------------------------------------------------
// 基本查詢 / 轉換
// -----------------------------------------------------------------------------

EvalResult length(const Value& s) {
    return guard([&] {
        const std::string& str = as_str(s, "length 的引數");
        return Value::integer(static_cast<std::int64_t>(cp_count(str)));
    });
}

EvalResult upper(const Value& s) {
    return guard([&] {
        std::string out = as_str(s, "upper 的引數");
        for (char& ch : out) {
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        }
        return Value::string(std::move(out));
    });
}

EvalResult lower(const Value& s) {
    return guard([&] {
        std::string out = as_str(s, "lower 的引數");
        for (char& ch : out) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        return Value::string(std::move(out));
    });
}

EvalResult trim(const Value& s) {
    return guard([&] {
        const std::string& str = as_str(s, "trim 的引數");
        auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        std::size_t a = 0, b = str.size();
        while (a < b && ws(str[a])) ++a;
        while (b > a && ws(str[b - 1])) --b;
        return Value::string(str.substr(a, b - a));
    });
}

EvalResult concat(const std::vector<Value>& parts) {
    return guard([&] {
        std::string out;
        for (std::size_t k = 0; k < parts.size(); ++k) {
            out += as_str(parts[k], "concat 的引數");
        }
        return Value::string(std::move(out));
    });
}

// -----------------------------------------------------------------------------
// 子字串（碼位單位；越界明確報錯）
// -----------------------------------------------------------------------------

EvalResult substring(const Value& s, const Value& start) {
    return guard([&] {
        const std::string& str = as_str(s, "substring 的字串引數");
        const std::int64_t st = as_index(start, "substring 的起點");
        const std::vector<std::size_t> off = cp_offsets(str);
        const std::int64_t m = static_cast<std::int64_t>(off.size()) - 1;
        if (st < 0 || st > m) {
            fail("substring 起點越界：start=" + std::to_string(st) +
                 "，長度=" + std::to_string(m));
        }
        const std::size_t begin = off[static_cast<std::size_t>(st)];
        return Value::string(str.substr(begin, off[static_cast<std::size_t>(m)] - begin));
    });
}

EvalResult substring(const Value& s, const Value& start, const Value& count) {
    return guard([&] {
        const std::string& str = as_str(s, "substring 的字串引數");
        const std::int64_t st = as_index(start, "substring 的起點");
        const std::int64_t cnt = as_index(count, "substring 的長度");
        const std::vector<std::size_t> off = cp_offsets(str);
        const std::int64_t m = static_cast<std::int64_t>(off.size()) - 1;
        if (st < 0 || st > m) {
            fail("substring 起點越界：start=" + std::to_string(st) +
                 "，長度=" + std::to_string(m));
        }
        if (cnt < 0) {
            fail("substring 長度不可為負：count=" + std::to_string(cnt));
        }
        if (st + cnt > m) {
            fail("substring 範圍越界：start=" + std::to_string(st) +
                 " + count=" + std::to_string(cnt) + " 超過長度 " + std::to_string(m));
        }
        const std::size_t begin = off[static_cast<std::size_t>(st)];
        const std::size_t end = off[static_cast<std::size_t>(st + cnt)];
        return Value::string(str.substr(begin, end - begin));
    });
}

// -----------------------------------------------------------------------------
// 替換 / 切分 / 連接
// -----------------------------------------------------------------------------

EvalResult replace(const Value& s, const Value& from, const Value& to) {
    return guard([&] {
        const std::string& str = as_str(s, "replace 的字串引數");
        const std::string& f = as_str(from, "replace 的來源子字串");
        const std::string& t = as_str(to, "replace 的替換字串");
        if (f.empty()) {
            fail("replace 的來源子字串不可為空字串");
        }
        std::string out;
        std::size_t pos = 0;
        for (;;) {
            const std::size_t found = str.find(f, pos);
            if (found == std::string::npos) {
                out.append(str, pos, std::string::npos);
                break;
            }
            out.append(str, pos, found - pos);
            out += t;
            pos = found + f.size();
        }
        return Value::string(std::move(out));
    });
}

EvalResult split(const Value& s, const Value& sep) {
    return guard([&] {
        const std::string& str = as_str(s, "split 的字串引數");
        const std::string& d = as_str(sep, "split 的分隔字串");
        std::vector<Value> parts;
        if (d.empty()) {
            // 空分隔 → 逐碼位切分。
            const std::vector<std::size_t> off = cp_offsets(str);
            for (std::size_t k = 0; k + 1 < off.size(); ++k) {
                parts.push_back(Value::string(str.substr(off[k], off[k + 1] - off[k])));
            }
        } else {
            std::size_t pos = 0;
            for (;;) {
                const std::size_t found = str.find(d, pos);
                if (found == std::string::npos) {
                    parts.push_back(Value::string(str.substr(pos)));
                    break;
                }
                parts.push_back(Value::string(str.substr(pos, found - pos)));
                pos = found + d.size();
            }
        }
        return Value::list(std::move(parts));
    });
}

EvalResult join(const Value& list, const Value& sep) {
    return guard([&] {
        if (!list.is_list()) {
            fail("join 的第一個引數須為清單（List）型別");
        }
        const std::string& d = as_str(sep, "join 的分隔字串");
        const std::vector<Value>& items = list.as_list();
        std::string out;
        for (std::size_t k = 0; k < items.size(); ++k) {
            if (k > 0) out += d;
            out += as_str(items[k], "join 的清單元素");
        }
        return Value::string(std::move(out));
    });
}

// -----------------------------------------------------------------------------
// 包含 / 前綴 / 後綴
// -----------------------------------------------------------------------------

EvalResult contains(const Value& s, const Value& sub) {
    return guard([&] {
        const std::string& str = as_str(s, "contains 的字串引數");
        const std::string& needle = as_str(sub, "contains 的子字串");
        return Value::boolean(str.find(needle) != std::string::npos);
    });
}

EvalResult starts_with(const Value& s, const Value& prefix) {
    return guard([&] {
        const std::string& str = as_str(s, "starts_with 的字串引數");
        const std::string& p = as_str(prefix, "starts_with 的前綴");
        const bool yes = str.size() >= p.size() && str.compare(0, p.size(), p) == 0;
        return Value::boolean(yes);
    });
}

EvalResult ends_with(const Value& s, const Value& suffix) {
    return guard([&] {
        const std::string& str = as_str(s, "ends_with 的字串引數");
        const std::string& p = as_str(suffix, "ends_with 的後綴");
        const bool yes =
            str.size() >= p.size() && str.compare(str.size() - p.size(), p.size(), p) == 0;
        return Value::boolean(yes);
    });
}

// -----------------------------------------------------------------------------
// 填充（碼位寬度；以 pad 循環填補）
// -----------------------------------------------------------------------------

namespace {

// 建構恰好 need 個碼位、由 pad 碼位循環取得的填充字串。pad 須非空。
std::string build_fill(const std::string& pad, std::size_t need) {
    const std::vector<std::size_t> poff = cp_offsets(pad);
    const std::size_t pcount = poff.size() - 1;  // >= 1（呼叫端保證非空）。
    std::string fill;
    for (std::size_t k = 0; k < need; ++k) {
        const std::size_t idx = k % pcount;
        fill.append(pad, poff[idx], poff[idx + 1] - poff[idx]);
    }
    return fill;
}

EvalResult pad_impl(const Value& s, const Value& width, const Value& pad, bool on_left,
                    const char* fn) {
    return guard([&] {
        const std::string& str = as_str(s, "pad 的字串引數");
        const std::int64_t w = as_index(width, "pad 的寬度");
        const std::string& p = as_str(pad, "pad 的填充字串");
        if (w < 0) {
            fail(std::string(fn) + " 的寬度不可為負：width=" + std::to_string(w));
        }
        if (p.empty()) {
            fail(std::string(fn) + " 的填充字串不可為空字串");
        }
        const std::size_t m = cp_count(str);
        if (m >= static_cast<std::size_t>(w)) {
            return Value::string(str);  // 已達 / 超過目標寬度：原樣回傳。
        }
        const std::string fill = build_fill(p, static_cast<std::size_t>(w) - m);
        return Value::string(on_left ? (fill + str) : (str + fill));
    });
}

}  // namespace

EvalResult pad_left(const Value& s, const Value& width, const Value& pad) {
    return pad_impl(s, width, pad, /*on_left=*/true, "pad_left");
}

EvalResult pad_right(const Value& s, const Value& width, const Value& pad) {
    return pad_impl(s, width, pad, /*on_left=*/false, "pad_right");
}

// -----------------------------------------------------------------------------
// 樣板組字
// -----------------------------------------------------------------------------

EvalResult format(const Value& tmpl, const std::vector<Value>& args) {
    return guard([&] {
        const std::string& t = as_str(tmpl, "format 的樣板");
        std::string out;
        const std::size_t n = t.size();
        std::size_t i = 0;
        while (i < n) {
            const char c = t[i];
            if (c == '{') {
                if (i + 1 < n && t[i + 1] == '{') {  // '{{' → 字面 '{'
                    out += '{';
                    i += 2;
                    continue;
                }
                std::size_t j = i + 1;
                std::string digits;
                while (j < n && t[j] != '}') {
                    digits += t[j];
                    ++j;
                }
                if (j >= n) {
                    fail("format 樣板有未閉合的 '{'");
                }
                if (digits.empty()) {
                    fail("format 佔位符 '{}' 缺少索引（需 '{i}'，i 為 0-based 整數）");
                }
                for (char d : digits) {
                    if (d < '0' || d > '9') {
                        fail("format 佔位符索引須為十進位整數：'{" + digits + "}'");
                    }
                }
                std::size_t idx = 0;
                try {
                    idx = static_cast<std::size_t>(std::stoull(digits));
                } catch (...) {
                    fail("format 佔位符索引超出可表示範圍：'{" + digits + "}'");
                }
                if (idx >= args.size()) {
                    fail("format 佔位符索引越界：{" + digits + "}，但僅提供 " +
                         std::to_string(args.size()) + " 個引數");
                }
                out += stringify(args[idx], "format 的引數");
                i = j + 1;
            } else if (c == '}') {
                if (i + 1 < n && t[i + 1] == '}') {  // '}}' → 字面 '}'
                    out += '}';
                    i += 2;
                    continue;
                }
                fail("format 樣板有落單的 '}'（字面 '}' 需寫成 '}}'）");
            } else {
                out += c;
                ++i;
            }
        }
        return Value::string(std::move(out));
    });
}

// -----------------------------------------------------------------------------
// 函式表（供上層以名稱分派接入）
// -----------------------------------------------------------------------------

namespace {

EvalResult arity_error(const char* fn, const std::string& want, std::size_t got) {
    return EvalResult::failure(
        EvalError{0, std::string(fn) + " 需要 " + want + " 個引數，但收到 " +
                         std::to_string(got)});
}

}  // namespace

const std::vector<std::pair<std::string, StringFn>>& function_table() {
    static const std::vector<std::pair<std::string, StringFn>> table = [] {
        std::vector<std::pair<std::string, StringFn>> t;
        auto add = [&t](const char* name, StringFn fn) {
            t.emplace_back(std::string(name), std::move(fn));
        };

        add("length", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 1) return arity_error("length", "1", a.size());
            return length(a[0]);
        });
        add("upper", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 1) return arity_error("upper", "1", a.size());
            return upper(a[0]);
        });
        add("lower", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 1) return arity_error("lower", "1", a.size());
            return lower(a[0]);
        });
        add("trim", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 1) return arity_error("trim", "1", a.size());
            return trim(a[0]);
        });
        add("concat", [](const std::vector<Value>& a) -> EvalResult {
            return concat(a);  // 變參：任意個數。
        });
        add("substring", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() == 2) return substring(a[0], a[1]);
            if (a.size() == 3) return substring(a[0], a[1], a[2]);
            return arity_error("substring", "2 或 3", a.size());
        });
        add("replace", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 3) return arity_error("replace", "3", a.size());
            return replace(a[0], a[1], a[2]);
        });
        add("split", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 2) return arity_error("split", "2", a.size());
            return split(a[0], a[1]);
        });
        add("join", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 2) return arity_error("join", "2", a.size());
            return join(a[0], a[1]);
        });
        add("contains", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 2) return arity_error("contains", "2", a.size());
            return contains(a[0], a[1]);
        });
        add("starts_with", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 2) return arity_error("starts_with", "2", a.size());
            return starts_with(a[0], a[1]);
        });
        add("ends_with", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 2) return arity_error("ends_with", "2", a.size());
            return ends_with(a[0], a[1]);
        });
        add("pad_left", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 3) return arity_error("pad_left", "3", a.size());
            return pad_left(a[0], a[1], a[2]);
        });
        add("pad_right", [](const std::vector<Value>& a) -> EvalResult {
            if (a.size() != 3) return arity_error("pad_right", "3", a.size());
            return pad_right(a[0], a[1], a[2]);
        });
        add("format", [](const std::vector<Value>& a) -> EvalResult {
            if (a.empty()) return arity_error("format", "至少 1", a.size());
            std::vector<Value> rest(a.begin() + 1, a.end());
            return format(a[0], rest);
        });
        return t;
    }();
    return table;
}

const StringFn* find_function(const std::string& name) {
    for (const auto& entry : function_table()) {
        if (entry.first == name) return &entry.second;
    }
    return nullptr;
}

}  // namespace strings
}  // namespace ds::format
