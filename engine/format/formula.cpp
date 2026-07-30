// E7-05 公式運算引擎 — 實作（平台中立 / engine 層）。見 formula.hpp 檔首語意。
//
// 架構：詞法切分（Tokenizer，帶位置）→ 遞迴下降解析 + 樹走求值（Parser）。
// 內部以受控例外 `EvalException` 承載 `EvalError`，於 `evaluate()` 邊界捕捉轉 failure
// （讓遞迴下降碼保持乾淨；對外仍為無例外的結果物件——沿用 E7-02 手法）。
// 無任何平台分支（`#ifdef` / win32 / cocoa）。
#include "formula.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ds::format {

// -----------------------------------------------------------------------------
// 結果物件工廠
// -----------------------------------------------------------------------------

EvalResult EvalResult::success(Value v) {
    EvalResult r;
    r.ok_ = true;
    r.value_ = std::move(v);
    return r;
}

EvalResult EvalResult::failure(EvalError e) {
    EvalResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

namespace {

// -----------------------------------------------------------------------------
// 內部：受控例外承載 EvalError（於 evaluate() 邊界捕捉轉 failure）
// -----------------------------------------------------------------------------

struct EvalException {
    EvalError err;
};

[[noreturn]] void fail(std::size_t position, std::string message) {
    throw EvalException{EvalError{position, std::move(message)}};
}

// -----------------------------------------------------------------------------
// 詞法切分
// -----------------------------------------------------------------------------

enum class Tok {
    End,
    Number,
    Ident,      // 識別字（變數名）
    True,
    False,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    LParen,
    RParen,
    Bang,       // !
    AndAnd,     // &&
    OrOr,       // ||
    EqEq,       // ==
    NotEq,      // !=
    Lt,         // <
    LtEq,       // <=
    Gt,         // >
    GtEq,       // >=
};

struct Token {
    Tok kind = Tok::End;
    std::size_t pos = 0;   // 0-based 起始欄。
    double num = 0.0;      // Number 用。
    bool integral = false; // Number 是否為整數字面量。
    std::string text;      // Ident 用。
};

bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }

class Tokenizer {
public:
    explicit Tokenizer(const std::string& src) : s_(src) {}

    // 切出全部 token（含結尾 End）。非法字元 → 語法錯誤（帶位置）。
    std::vector<Token> run() {
        std::vector<Token> out;
        for (;;) {
            skip_spaces();
            if (i_ >= s_.size()) {
                out.push_back(Token{Tok::End, i_, 0.0, false, {}});
                return out;
            }
            const std::size_t start = i_;
            const char c = s_[i_];
            if (is_digit(c) || (c == '.' && i_ + 1 < s_.size() && is_digit(s_[i_ + 1]))) {
                out.push_back(lex_number(start));
            } else if (is_ident_start(c)) {
                out.push_back(lex_ident(start));
            } else {
                out.push_back(lex_operator(start));
            }
        }
    }

private:
    void skip_spaces() {
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++i_;
            } else {
                break;
            }
        }
    }

    Token lex_number(std::size_t start) {
        bool integral = true;
        std::size_t j = i_;
        while (j < s_.size() && is_digit(s_[j])) ++j;
        if (j < s_.size() && s_[j] == '.') {
            integral = false;
            ++j;
            if (j >= s_.size() || !is_digit(s_[j])) {
                fail(start, "數字字面量小數點後缺少數字");
            }
            while (j < s_.size() && is_digit(s_[j])) ++j;
        }
        if (j < s_.size() && (s_[j] == 'e' || s_[j] == 'E')) {
            integral = false;
            ++j;
            if (j < s_.size() && (s_[j] == '+' || s_[j] == '-')) ++j;
            if (j >= s_.size() || !is_digit(s_[j])) {
                fail(start, "數字字面量指數部分格式錯誤");
            }
            while (j < s_.size() && is_digit(s_[j])) ++j;
        }
        const std::string lit = s_.substr(start, j - start);
        i_ = j;
        double v = 0.0;
        try {
            v = std::stod(lit);
        } catch (...) {
            fail(start, "無法解析的數字字面量：" + lit);
        }
        return Token{Tok::Number, start, v, integral, {}};
    }

    Token lex_ident(std::size_t start) {
        std::size_t j = i_;
        while (j < s_.size() && is_ident_char(s_[j])) ++j;
        std::string word = s_.substr(start, j - start);
        i_ = j;
        if (word == "true") return Token{Tok::True, start, 0.0, false, {}};
        if (word == "false") return Token{Tok::False, start, 0.0, false, {}};
        return Token{Tok::Ident, start, 0.0, false, std::move(word)};
    }

    Token lex_operator(std::size_t start) {
        const char c = s_[start];
        const char n = (start + 1 < s_.size()) ? s_[start + 1] : '\0';
        auto one = [&](Tok k) { i_ = start + 1; return Token{k, start, 0.0, false, {}}; };
        auto two = [&](Tok k) { i_ = start + 2; return Token{k, start, 0.0, false, {}}; };
        switch (c) {
            case '+': return one(Tok::Plus);
            case '-': return one(Tok::Minus);
            case '*': return one(Tok::Star);
            case '/': return one(Tok::Slash);
            case '%': return one(Tok::Percent);
            case '(': return one(Tok::LParen);
            case ')': return one(Tok::RParen);
            case '!': return (n == '=') ? two(Tok::NotEq) : one(Tok::Bang);
            case '<': return (n == '=') ? two(Tok::LtEq) : one(Tok::Lt);
            case '>': return (n == '=') ? two(Tok::GtEq) : one(Tok::Gt);
            case '=':
                if (n == '=') return two(Tok::EqEq);
                fail(start, "非預期字元 '='（比較請用 '=='）");
            case '&':
                if (n == '&') return two(Tok::AndAnd);
                fail(start, "非預期字元 '&'（邏輯且請用 '&&'）");
            case '|':
                if (n == '|') return two(Tok::OrOr);
                fail(start, "非預期字元 '|'（邏輯或請用 '||'）");
            default:
                fail(start, std::string("非預期字元 '") + c + "'");
        }
    }

    const std::string& s_;
    std::size_t i_ = 0;
};

// -----------------------------------------------------------------------------
// 數值 / 真值輔助
// -----------------------------------------------------------------------------

// 從 Number Value 取雙精度值。非 Number → 型別錯誤（帶位置）。
double num_of(const Value& v, std::size_t pos, const char* ctx) {
    if (!v.is_number()) {
        fail(pos, std::string("運算需要數值，但 ") + ctx + " 不是數值型別");
    }
    return v.as_number();
}

// 真值判斷：Bool 原樣；Number 非零為真。其餘型別 → 型別錯誤。
bool truth_of(const Value& v, std::size_t pos) {
    if (v.is_bool()) return v.as_bool();
    if (v.is_number()) return v.as_number() != 0.0;
    fail(pos, "邏輯運算需要布林或數值運算元");
}

// 依整數旗標建構 Number Value。
Value make_number(double v, bool integral) {
    return Value::number(v, integral);
}

// -----------------------------------------------------------------------------
// 遞迴下降解析 + 樹走求值
// -----------------------------------------------------------------------------
//
// 文法（低優先序 → 高）：
//   or       := and   ( "||" and )*
//   and      := equal ( "&&" equal )*
//   equal    := comp  ( ("=="|"!=") comp )*
//   comp     := add   ( ("<"|"<="|">"|">=") add )*
//   add      := mul   ( ("+"|"-") mul )*
//   mul      := unary ( ("*"|"/"|"%") unary )*
//   unary    := ("+"|"-"|"!") unary | primary
//   primary  := Number | True | False | Ident | "(" or ")"
class Parser {
public:
    Parser(std::vector<Token> toks, const VariableScope* scope)
        : toks_(std::move(toks)), scope_(scope) {}

    Value parse() {
        if (peek().kind == Tok::End) {
            fail(0, "空運算式");
        }
        Value v = parse_or();
        if (peek().kind != Tok::End) {
            fail(peek().pos, "運算式結尾有非預期 token");
        }
        return v;
    }

private:
    const Token& peek() const { return toks_[idx_]; }
    const Token& advance() { return toks_[idx_++]; }
    bool match(Tok k) {
        if (peek().kind == k) { ++idx_; return true; }
        return false;
    }

    Value parse_or() {
        Value left = parse_and();
        while (peek().kind == Tok::OrOr) {
            const std::size_t p = advance().pos;
            const bool lb = truth_of(left, p);
            Value right = parse_and();
            const bool rb = truth_of(right, p);
            left = Value::boolean(lb || rb);
        }
        return left;
    }

    Value parse_and() {
        Value left = parse_equality();
        while (peek().kind == Tok::AndAnd) {
            const std::size_t p = advance().pos;
            const bool lb = truth_of(left, p);
            Value right = parse_equality();
            const bool rb = truth_of(right, p);
            left = Value::boolean(lb && rb);
        }
        return left;
    }

    Value parse_equality() {
        Value left = parse_comparison();
        for (;;) {
            const Tok k = peek().kind;
            if (k != Tok::EqEq && k != Tok::NotEq) break;
            const std::size_t p = advance().pos;
            Value right = parse_comparison();
            const bool eq = values_equal(left, right, p);
            left = Value::boolean(k == Tok::EqEq ? eq : !eq);
        }
        return left;
    }

    Value parse_comparison() {
        Value left = parse_additive();
        for (;;) {
            const Tok k = peek().kind;
            if (k != Tok::Lt && k != Tok::LtEq && k != Tok::Gt && k != Tok::GtEq) break;
            const std::size_t p = advance().pos;
            const double a = num_of(left, p, "左運算元");
            Value right = parse_additive();
            const double b = num_of(right, p, "右運算元");
            bool res = false;
            switch (k) {
                case Tok::Lt:   res = a < b; break;
                case Tok::LtEq: res = a <= b; break;
                case Tok::Gt:   res = a > b; break;
                case Tok::GtEq: res = a >= b; break;
                default: break;
            }
            left = Value::boolean(res);
        }
        return left;
    }

    Value parse_additive() {
        Value left = parse_multiplicative();
        for (;;) {
            const Tok k = peek().kind;
            if (k != Tok::Plus && k != Tok::Minus) break;
            const std::size_t p = advance().pos;
            const bool li = left.is_integer();
            const double a = num_of(left, p, "左運算元");
            Value right = parse_multiplicative();
            const bool ri = right.is_integer();
            const double b = num_of(right, p, "右運算元");
            const double r = (k == Tok::Plus) ? (a + b) : (a - b);
            left = make_number(r, li && ri);
        }
        return left;
    }

    Value parse_multiplicative() {
        Value left = parse_unary();
        for (;;) {
            const Tok k = peek().kind;
            if (k != Tok::Star && k != Tok::Slash && k != Tok::Percent) break;
            const std::size_t p = advance().pos;
            const bool li = left.is_integer();
            const double a = num_of(left, p, "左運算元");
            Value right = parse_unary();
            const bool ri = right.is_integer();
            const double b = num_of(right, p, "右運算元");
            if (k == Tok::Star) {
                left = make_number(a * b, li && ri);
            } else if (k == Tok::Slash) {
                if (b == 0.0) fail(p, "除以零");
                if (li && ri && std::fmod(a, b) == 0.0) {
                    left = make_number(a / b, /*integral=*/true);  // 整除 → 整數
                } else {
                    left = make_number(a / b, /*integral=*/false);
                }
            } else {  // Percent
                if (!li || !ri) fail(p, "取模 '%' 的兩運算元須為整數");
                if (b == 0.0) fail(p, "對零取模");
                const std::int64_t ia = static_cast<std::int64_t>(a);
                const std::int64_t ib = static_cast<std::int64_t>(b);
                left = Value::integer(ia % ib);
            }
        }
        return left;
    }

    Value parse_unary() {
        const Tok k = peek().kind;
        if (k == Tok::Plus || k == Tok::Minus || k == Tok::Bang) {
            const std::size_t p = advance().pos;
            Value operand = parse_unary();
            if (k == Tok::Bang) {
                return Value::boolean(!truth_of(operand, p));
            }
            const bool integral = operand.is_integer();
            const double v = num_of(operand, p, "運算元");
            return make_number(k == Tok::Minus ? -v : v, integral);
        }
        return parse_primary();
    }

    Value parse_primary() {
        const Token& t = peek();
        switch (t.kind) {
            case Tok::Number: {
                advance();
                return make_number(t.num, t.integral);
            }
            case Tok::True:  advance(); return Value::boolean(true);
            case Tok::False: advance(); return Value::boolean(false);
            case Tok::Ident: {
                advance();
                if (scope_ == nullptr) {
                    fail(t.pos, "未定義變數 '" + t.text + "'（無變數作用域）");
                }
                const Value* v = scope_->find(t.text);
                if (v == nullptr) {
                    fail(t.pos, "未定義變數 '" + t.text + "'");
                }
                return *v;
            }
            case Tok::LParen: {
                advance();
                Value v = parse_or();
                if (!match(Tok::RParen)) {
                    fail(peek().pos, "缺少對應的右括號 ')'");
                }
                return v;
            }
            case Tok::RParen:
                fail(t.pos, "非預期的右括號 ')'");
            case Tok::End:
                fail(t.pos, "非預期的運算式結尾（缺少運算元）");
            default:
                fail(t.pos, "非預期 token（缺少運算元）");
        }
    }

    // == / != 相等：數值比較值；兩布林比較布林；否則型別錯誤。
    bool values_equal(const Value& a, const Value& b, std::size_t p) {
        if (a.is_number() && b.is_number()) return a.as_number() == b.as_number();
        if (a.is_bool() && b.is_bool()) return a.as_bool() == b.as_bool();
        fail(p, "'==' / '!=' 只支援數值或布林運算元");
    }

    std::vector<Token> toks_;
    const VariableScope* scope_ = nullptr;
    std::size_t idx_ = 0;
};

}  // namespace

// -----------------------------------------------------------------------------
// 公式標記辨識
// -----------------------------------------------------------------------------

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

bool is_formula(const std::string& s) {
    const std::string t = trim(s);
    if (t.empty()) return false;
    if (t[0] == '=') return true;
    if (t.size() >= 3 && t[0] == '$' && t[1] == '{' && t.back() == '}') return true;
    return false;
}

std::string formula_body(const std::string& s) {
    std::string t = trim(s);
    if (!t.empty() && t[0] == '=') {
        return trim(t.substr(1));
    }
    if (t.size() >= 3 && t[0] == '$' && t[1] == '{' && t.back() == '}') {
        return trim(t.substr(2, t.size() - 3));
    }
    return t;
}

// -----------------------------------------------------------------------------
// 求值入口
// -----------------------------------------------------------------------------

EvalResult Evaluator::evaluate(const std::string& expr) const {
    try {
        const std::string body = formula_body(expr);
        Tokenizer tok(body);
        std::vector<Token> toks = tok.run();
        Parser parser(std::move(toks), scope_);
        return EvalResult::success(parser.parse());
    } catch (const EvalException& ex) {
        return EvalResult::failure(ex.err);
    }
}

EvalResult evaluate(const std::string& expr, const VariableScope& scope) {
    return Evaluator(scope).evaluate(expr);
}

EvalResult evaluate(const std::string& expr) {
    return Evaluator().evaluate(expr);
}

}  // namespace ds::format
