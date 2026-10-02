/**
 * @file tokenizer.cpp
 * @brief memoria::script 脚本解释器 Tokenizer
 *
 * 纯 C++，零 ESP-IDF 依赖。
 * 完整实现：char stream → Token 序列。
 * Token 类型：Identifier / Number / String / Operator / Punctuation / Keyword / EOF
 *
 * 这是脚本解释器的第一阶段（最高风险组件）。
 * 必须作为独立库可单元测试。
 */

#include "tokenizer.hpp"

#include <cctype>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace memoria {
namespace script {

/* ---------- Tokenizer ---------- */
class Tokenizer {
public:
    explicit Tokenizer(std::string src) : src_(std::move(src)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        while (true) {
            skip_ws();
            if (pos_ >= src_.size()) { out.push_back({Token::Kind::Eof, "", line_, col_}); break; }
            char c = src_[pos_];
            if (std::isalpha((unsigned char)c) || c == '_') out.push_back(read_ident());
            else if (std::isdigit((unsigned char)c)) out.push_back(read_number());
            else if (c == '"') out.push_back(read_string());
            else out.push_back(read_op());
        }
        return out;
    }

private:
    std::string src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;

    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') { pos_++; col_++; }
            else if (c == '\n') { pos_++; line_++; col_ = 1; }
            else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_+1] == '/') {
                while (pos_ < src_.size() && src_[pos_] != '\n') pos_++;
            } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_+1] == '*') {
                pos_ += 2;
                while (pos_ + 1 < src_.size()) {
                    if (src_[pos_] == '*' && src_[pos_+1] == '/') { pos_ += 2; break; }
                    if (src_[pos_] == '\n') line_++;
                    pos_++;
                }
            } else break;
        }
    }

    Token read_ident() {
        size_t start = pos_; int l = line_, c = col_;
        while (pos_ < src_.size() &&
               (std::isalnum((unsigned char)src_[pos_]) || src_[pos_] == '_')) {
            pos_++; col_++;
        }
        std::string s = src_.substr(start, pos_ - start);
        Token::Kind k = ident_to_keyword(s);
        return {k, s, l, c};
    }

    Token read_number() {
        size_t start = pos_; int l = line_, c = col_;
        bool is_float = false;
        /* 数字字符集仅限 isdigit 与 '.'，防止将 + - e E _ 吞入数字，避免 `1+2` 被解析为单个 token */
        while (pos_ < src_.size() && (std::isdigit((unsigned char)src_[pos_]) || src_[pos_] == '.')) {
            if (src_[pos_] == '.') is_float = true;
            pos_++; col_++;
        }
        /* 科学计数法：仅当 e/E 之后跟 +/- 或数字才吸收，否则回退（1e 就是 1 和 e） */
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            size_t save = pos_;
            pos_++; col_++;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) { pos_++; col_++; }
            if (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) {
                while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) { pos_++; col_++; }
                is_float = true;
            } else {
                pos_ = save;   /* 不是科学计数法：回退到 e 之前 */
            }
        }
        std::string s = src_.substr(start, pos_ - start);
        return {is_float ? Token::Kind::FloatLit : Token::Kind::IntLit, s, l, c};
    }

    Token read_string() {
        int l = line_, c = col_;
        pos_++; col_++;   /* skip opening " */
        std::string buf;
        while (pos_ < src_.size() && src_[pos_] != '"') {
            char ch = src_[pos_];
            if (ch == '\\' && pos_ + 1 < src_.size()) {
                pos_++; col_++;
                char esc = src_[pos_];
                switch (esc) {
                    case 'n': buf += '\n'; break;
                    case 't': buf += '\t'; break;
                    case 'r': buf += '\r'; break;
                    case '\\': buf += '\\'; break;
                    case '"': buf += '"'; break;
                    default: buf += esc; break;
                }
            } else if (ch == '\n') line_++;
            else buf += ch;
            pos_++; col_++;
        }
        pos_++; col_++;
        return {Token::Kind::StringLit, buf, l, c};
    }

    Token read_op() {
        int l = line_, c = col_;
        char c1 = src_[pos_];
        pos_++; col_++;
        /* 多字符运算符 */
        if (pos_ < src_.size()) {
            char c2 = src_[pos_];
            auto two = [&](Token::Kind k, std::string s2) -> Token {
                pos_++; col_++;
                return {k, s2, l, c};
            };
            switch (c1) {
                case '+': if (c2 == '=') return two(Token::Kind::PlusAssign, "+=");
                          if (c2 == '+') { return two(Token::Kind::Inc, "++"); } break;
                case '-': if (c2 == '=') return two(Token::Kind::MinusAssign, "-=");
                          if (c2 == '-') return two(Token::Kind::Dec, "--");
                          if (c2 == '>') { return two(Token::Kind::Arrow, "->"); } break;
                case '*': if (c2 == '=') return two(Token::Kind::StarAssign, "*="); break;
                case '/': if (c2 == '=') return two(Token::Kind::SlashAssign, "/="); break;
                case '=': if (c2 == '=') return two(Token::Kind::Eq, "=="); break;
                case '!': if (c2 == '=') return two(Token::Kind::Neq, "!="); break;
                case '<': if (c2 == '=') return two(Token::Kind::Leq, "<="); break;
                case '>': if (c2 == '=') return two(Token::Kind::Geq, ">="); break;
                case '&': if (c2 == '&') return two(Token::Kind::And, "&&"); break;
                case '|': if (c2 == '|') return two(Token::Kind::Or, "||"); break;
            }
        }
        /* 单字符 */
        switch (c1) {
            case '+': return {Token::Kind::Plus, "+", l, c};
            case '-': return {Token::Kind::Minus, "-", l, c};
            case '*': return {Token::Kind::Star, "*", l, c};
            case '/': return {Token::Kind::Slash, "/", l, c};
            case '%': return {Token::Kind::Percent, "%", l, c};
            case '=': return {Token::Kind::Assign, "=", l, c};
            case '<': return {Token::Kind::Lt, "<", l, c};
            case '>': return {Token::Kind::Gt, ">", l, c};
            case '!': return {Token::Kind::Not, "!", l, c};
            case '(': return {Token::Kind::LParen, "(", l, c};
            case ')': return {Token::Kind::RParen, ")", l, c};
            case '{': return {Token::Kind::LBrace, "{", l, c};
            case '}': return {Token::Kind::RBrace, "}", l, c};
            case '[': return {Token::Kind::LBracket, "[", l, c};
            case ']': return {Token::Kind::RBracket, "]", l, c};
            case ';': return {Token::Kind::Semicolon, ";", l, c};
            case ',': return {Token::Kind::Comma, ",", l, c};
            case '.': return {Token::Kind::Dot, ".", l, c};
            case ':': return {Token::Kind::Colon, ":", l, c};
            case '?': return {Token::Kind::Question, "?", l, c};
        }
        /* 未知字符返回 Error token（原 Eof 会静默丢弃后续全部代码） */
        return {Token::Kind::Error, std::string(1, c1), l, c};
    }

    static Token::Kind ident_to_keyword(const std::string& s) {
        static const std::pair<std::string, Token::Kind> kw[] = {
            {"if", Token::Kind::If}, {"else", Token::Kind::Else},
            {"while", Token::Kind::While}, {"for", Token::Kind::For},
            {"return", Token::Kind::Return}, {"break", Token::Kind::Break},
            {"continue", Token::Kind::Continue}, {"true", Token::Kind::True},
            {"false", Token::Kind::False}, {"null", Token::Kind::Null},
            {"this", Token::Kind::This}, {"var", Token::Kind::Var},
            {"function", Token::Kind::Function}, {"new", Token::Kind::New},
            {"delete", Token::Kind::Delete}, {"import", Token::Kind::Import},
            {"include", Token::Kind::Include},
        };
        for (auto& p : kw) if (p.first == s) return p.second;
        return Token::Kind::Ident;
    }
};

/* ---------- 公开接口 ---------- */
std::vector<Token> tokenize(const std::string& src) {
    Tokenizer t(src);
    return t.tokenize();
}

} // namespace script
} // namespace memoria