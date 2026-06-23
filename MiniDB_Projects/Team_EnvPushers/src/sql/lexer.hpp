// Hand-written lexer: turns a SQL string into a token stream.
#pragma once

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace minidb {

enum class TokKind {
    IDENT, INT_LIT, STR_LIT,
    LPAREN, RPAREN, COMMA, STAR, SEMICOLON, DOT,
    EQ, NE, LT, LE, GT, GE,
    END
};

struct Token {
    TokKind kind;
    std::string text;     // identifier / keyword / string contents
    int64_t int_val = 0;  // for INT_LIT
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        while (true) {
            Token t = next();
            out.push_back(t);
            if (t.kind == TokKind::END) break;
        }
        return out;
    }

private:
    Token next() {
        skip_ws();
        if (pos_ >= src_.size()) return {TokKind::END, ""};
        char c = src_[pos_];

        if (std::isalpha((unsigned char)c) || c == '_') return ident();
        if (std::isdigit((unsigned char)c)) return number();
        if (c == '-' && pos_ + 1 < src_.size() && std::isdigit((unsigned char)src_[pos_ + 1]))
            return number();
        if (c == '\'' || c == '"') return string_lit(c);

        pos_++;
        switch (c) {
            case '(': return {TokKind::LPAREN, "("};
            case ')': return {TokKind::RPAREN, ")"};
            case ',': return {TokKind::COMMA, ","};
            case '*': return {TokKind::STAR, "*"};
            case ';': return {TokKind::SEMICOLON, ";"};
            case '.': return {TokKind::DOT, "."};
            case '=': return {TokKind::EQ, "="};
            case '<':
                if (peek() == '=') { pos_++; return {TokKind::LE, "<="}; }
                if (peek() == '>') { pos_++; return {TokKind::NE, "<>"}; }
                return {TokKind::LT, "<"};
            case '>':
                if (peek() == '=') { pos_++; return {TokKind::GE, ">="}; }
                return {TokKind::GT, ">"};
            case '!':
                if (peek() == '=') { pos_++; return {TokKind::NE, "!="}; }
                break;
        }
        throw std::runtime_error(std::string("lexer: unexpected character '") + c + "'");
    }

    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    void skip_ws() {
        while (pos_ < src_.size() && std::isspace((unsigned char)src_[pos_])) pos_++;
    }

    Token ident() {
        size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isalnum((unsigned char)src_[pos_]) || src_[pos_] == '_'))
            pos_++;
        return {TokKind::IDENT, src_.substr(start, pos_ - start)};
    }

    Token number() {
        size_t start = pos_;
        if (src_[pos_] == '-') pos_++;
        while (pos_ < src_.size() && std::isdigit((unsigned char)src_[pos_])) pos_++;
        Token t{TokKind::INT_LIT, src_.substr(start, pos_ - start)};
        t.int_val = std::stoll(t.text);
        return t;
    }

    Token string_lit(char quote) {
        pos_++;  // opening quote
        std::string s;
        while (pos_ < src_.size() && src_[pos_] != quote) {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) { pos_++; }
            s.push_back(src_[pos_++]);
        }
        if (pos_ >= src_.size()) throw std::runtime_error("lexer: unterminated string");
        pos_++;  // closing quote
        return {TokKind::STR_LIT, s};
    }

    std::string src_;
    size_t pos_ = 0;
};

}  // namespace minidb
