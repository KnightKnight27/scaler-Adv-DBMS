#pragma once
// ---------------------------------------------------------------------------
// lexer.h - turns a raw SQL string into a flat list of tokens.
//
// This is the same idea as the Lab 5 tokenizer, extended to cover the keywords
// and punctuation MiniDB's grammar needs. The lexer does no validation beyond
// recognising token shapes; deciding whether the tokens form a legal statement
// is the parser's job.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>

namespace minidb {

enum class Tok {
    // keywords
    CREATE, TABLE, INSERT, INTO, VALUES, SELECT, FROM, WHERE, JOIN, ON,
    DELETE, AND, OR, INT, TEXT, PRIMARY, KEY, BEGIN, COMMIT, ABORT, EXPLAIN,
    // literals / names
    IDENT, NUMBER, STRING,
    // punctuation / operators
    LPAREN, RPAREN, COMMA, SEMI, STAR, DOT,
    EQ, NEQ, LT, GT, LE, GE,
    END
};

struct Token {
    Tok         type;
    std::string text;
};

class Lexer {
public:
    explicit Lexer(std::string sql) : s_(std::move(sql)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        while (true) {
            skip_ws();
            if (i_ >= s_.size()) { out.push_back({Tok::END, ""}); break; }
            char c = s_[i_];
            if (std::isalpha((unsigned char)c) || c == '_') { out.push_back(ident()); }
            else if (std::isdigit((unsigned char)c) ||
                     (c == '-' && i_ + 1 < s_.size() && std::isdigit((unsigned char)s_[i_+1]))) {
                out.push_back(number());
            }
            else if (c == '\'') { out.push_back(string_lit()); }
            else { out.push_back(punct()); }
        }
        return out;
    }

private:
    void skip_ws() { while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) i_++; }

    Token ident() {
        size_t start = i_;
        while (i_ < s_.size() && (std::isalnum((unsigned char)s_[i_]) || s_[i_] == '_')) i_++;
        std::string w = s_.substr(start, i_ - start);
        std::string up; for (char c : w) up.push_back((char)std::toupper((unsigned char)c));
        if (up == "CREATE")  return {Tok::CREATE, w};
        if (up == "TABLE")   return {Tok::TABLE, w};
        if (up == "INSERT")  return {Tok::INSERT, w};
        if (up == "INTO")    return {Tok::INTO, w};
        if (up == "VALUES")  return {Tok::VALUES, w};
        if (up == "SELECT")  return {Tok::SELECT, w};
        if (up == "FROM")    return {Tok::FROM, w};
        if (up == "WHERE")   return {Tok::WHERE, w};
        if (up == "JOIN")    return {Tok::JOIN, w};
        if (up == "ON")      return {Tok::ON, w};
        if (up == "DELETE")  return {Tok::DELETE, w};
        if (up == "AND")     return {Tok::AND, w};
        if (up == "OR")      return {Tok::OR, w};
        if (up == "INT")     return {Tok::INT, w};
        if (up == "TEXT")    return {Tok::TEXT, w};
        if (up == "PRIMARY") return {Tok::PRIMARY, w};
        if (up == "KEY")     return {Tok::KEY, w};
        if (up == "BEGIN")   return {Tok::BEGIN, w};
        if (up == "COMMIT")  return {Tok::COMMIT, w};
        if (up == "ABORT" || up == "ROLLBACK") return {Tok::ABORT, w};
        if (up == "EXPLAIN") return {Tok::EXPLAIN, w};
        return {Tok::IDENT, w};
    }

    Token number() {
        size_t start = i_;
        if (s_[i_] == '-') i_++;
        while (i_ < s_.size() && std::isdigit((unsigned char)s_[i_])) i_++;
        return {Tok::NUMBER, s_.substr(start, i_ - start)};
    }

    Token string_lit() {
        i_++; // opening quote
        size_t start = i_;
        while (i_ < s_.size() && s_[i_] != '\'') i_++;
        std::string v = s_.substr(start, i_ - start);
        if (i_ < s_.size()) i_++; // closing quote
        return {Tok::STRING, v};
    }

    Token punct() {
        char c = s_[i_];
        switch (c) {
            case '(': i_++; return {Tok::LPAREN, "("};
            case ')': i_++; return {Tok::RPAREN, ")"};
            case ',': i_++; return {Tok::COMMA, ","};
            case ';': i_++; return {Tok::SEMI, ";"};
            case '*': i_++; return {Tok::STAR, "*"};
            case '.': i_++; return {Tok::DOT, "."};
            case '=': i_++; return {Tok::EQ, "="};
            case '<':
                i_++;
                if (i_ < s_.size() && s_[i_] == '=') { i_++; return {Tok::LE, "<="}; }
                if (i_ < s_.size() && s_[i_] == '>') { i_++; return {Tok::NEQ, "<>"}; }
                return {Tok::LT, "<"};
            case '>':
                i_++;
                if (i_ < s_.size() && s_[i_] == '=') { i_++; return {Tok::GE, ">="}; }
                return {Tok::GT, ">"};
            case '!':
                i_++;
                if (i_ < s_.size() && s_[i_] == '=') { i_++; return {Tok::NEQ, "!="}; }
                throw std::runtime_error("unexpected '!'");
        }
        throw std::runtime_error(std::string("unexpected character '") + c + "'");
    }

    std::string s_;
    size_t      i_ = 0;
};

} // namespace minidb
