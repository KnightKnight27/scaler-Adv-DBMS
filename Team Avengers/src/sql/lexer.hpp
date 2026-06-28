// ============================================================================
//  lexer.hpp — Tokenizer. Splits a SQL string into a flat token stream.
//
//  This is the Lab 7 lexer generalised: more keywords (CREATE/INSERT/DELETE/
//  JOIN/...), string literals in single quotes, and the '*' and ',' '.' '('
//  ')' punctuation the fuller grammar needs. Keywords are matched
//  case-insensitively; identifiers and string contents keep their original
//  case.
// ============================================================================
#pragma once

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace minidb {

enum class Tok {
    // keywords
    Create, Table, Insert, Into, Values, Select, From, Where,
    Delete, Join, On, And, Or,
    // literals / names
    Ident, Number, String,
    // operators
    Eq, Ne, Lt, Le, Gt, Ge,
    // punctuation
    LParen, RParen, Comma, Dot, Star,
    End
};

struct Token {
    Tok kind;
    std::string text;   // raw lexeme (identifier name, number digits, string body)
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (std::isspace((unsigned char)c)) { ++i_; continue; }

            // identifier or keyword
            if (std::isalpha((unsigned char)c) || c == '_') {
                size_t j = i_;
                while (j < s_.size() && (std::isalnum((unsigned char)s_[j]) || s_[j] == '_')) ++j;
                std::string w = s_.substr(i_, j - i_);
                i_ = j;
                out.push_back({keyword_or_ident(w), w});
                continue;
            }
            // number
            if (std::isdigit((unsigned char)c) ||
                (c == '-' && i_ + 1 < s_.size() && std::isdigit((unsigned char)s_[i_ + 1]))) {
                size_t j = i_ + (c == '-' ? 1 : 0);
                while (j < s_.size() && std::isdigit((unsigned char)s_[j])) ++j;
                out.push_back({Tok::Number, s_.substr(i_, j - i_)});
                i_ = j;
                continue;
            }
            // single-quoted string literal: 'abc'
            if (c == '\'') {
                size_t j = i_ + 1;
                std::string body;
                while (j < s_.size() && s_[j] != '\'') body.push_back(s_[j++]);
                if (j >= s_.size()) throw std::runtime_error("lexer: unterminated string literal");
                i_ = j + 1;
                out.push_back({Tok::String, body});
                continue;
            }
            // two-char operators: != <= >=
            if (c == '!' && peek(1) == '=') { out.push_back({Tok::Ne, "!="}); i_ += 2; continue; }
            if (c == '<' && peek(1) == '=') { out.push_back({Tok::Le, "<="}); i_ += 2; continue; }
            if (c == '>' && peek(1) == '=') { out.push_back({Tok::Ge, ">="}); i_ += 2; continue; }
            // single-char tokens
            switch (c) {
                case '=': out.push_back({Tok::Eq, "="}); break;
                case '<': out.push_back({Tok::Lt, "<"}); break;
                case '>': out.push_back({Tok::Gt, ">"}); break;
                case '(': out.push_back({Tok::LParen, "("}); break;
                case ')': out.push_back({Tok::RParen, ")"}); break;
                case ',': out.push_back({Tok::Comma, ","}); break;
                case '.': out.push_back({Tok::Dot, "."}); break;
                case '*': out.push_back({Tok::Star, "*"}); break;
                case ';': break;  // statement terminator: ignore
                default:  throw std::runtime_error(std::string("lexer: unexpected char '") + c + "'");
            }
            ++i_;
        }
        out.push_back({Tok::End, ""});
        return out;
    }

private:
    char peek(size_t k) const { return i_ + k < s_.size() ? s_[i_ + k] : '\0'; }

    static Tok keyword_or_ident(const std::string& w) {
        std::string u;
        for (char ch : w) u.push_back((char)std::toupper((unsigned char)ch));
        if (u == "CREATE") return Tok::Create;
        if (u == "TABLE")  return Tok::Table;
        if (u == "INSERT") return Tok::Insert;
        if (u == "INTO")   return Tok::Into;
        if (u == "VALUES") return Tok::Values;
        if (u == "SELECT") return Tok::Select;
        if (u == "FROM")   return Tok::From;
        if (u == "WHERE")  return Tok::Where;
        if (u == "DELETE") return Tok::Delete;
        if (u == "JOIN")   return Tok::Join;
        if (u == "ON")     return Tok::On;
        if (u == "AND")    return Tok::And;
        if (u == "OR")     return Tok::Or;
        return Tok::Ident;
    }

    const std::string& s_;
    size_t i_ = 0;
};

}  // namespace minidb
