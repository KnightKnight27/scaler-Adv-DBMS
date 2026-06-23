#include "parser/lexer.h"

#include <cctype>
#include <unordered_map>

#include "common/exception.h"

namespace minidb {

namespace {
// Keyword lookup, keyed by upper-cased text.
const std::unordered_map<std::string, TokenType>& keywords() {
    static const std::unordered_map<std::string, TokenType> kw = {
        {"SELECT", TokenType::SELECT}, {"FROM", TokenType::FROM},
        {"WHERE", TokenType::WHERE},   {"INSERT", TokenType::INSERT},
        {"INTO", TokenType::INTO},     {"VALUES", TokenType::VALUES},
        {"DELETE", TokenType::DELETE}, {"CREATE", TokenType::CREATE},
        {"TABLE", TokenType::TABLE},   {"JOIN", TokenType::JOIN},
        {"ON", TokenType::ON},         {"AND", TokenType::AND},
        {"OR", TokenType::OR},         {"GROUP", TokenType::GROUP},
        {"BY", TokenType::BY},         {"COUNT", TokenType::COUNT},
        {"SUM", TokenType::SUM},       {"AVG", TokenType::AVG},
        {"MIN", TokenType::MIN},       {"MAX", TokenType::MAX},
        {"INT", TokenType::INT_TYPE},  {"INTEGER", TokenType::INT_TYPE},
        {"VARCHAR", TokenType::VARCHAR_TYPE}, {"TEXT", TokenType::VARCHAR_TYPE},
        {"DOUBLE", TokenType::DOUBLE_TYPE},   {"FLOAT", TokenType::DOUBLE_TYPE},
        {"PRIMARY", TokenType::PRIMARY}, {"KEY", TokenType::KEY},
    };
    return kw;
}

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    std::size_t i = 0, n = sql_.size();

    auto push = [&](TokenType t, std::string text = "") { out.push_back({t, std::move(text)}); };

    while (i < n) {
        char c = sql_[i];

        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        // identifiers / keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(sql_[i])) || sql_[i] == '_')) ++i;
            std::string word = sql_.substr(start, i - start);
            auto it = keywords().find(to_upper(word));
            if (it != keywords().end()) push(it->second, word);
            else push(TokenType::IDENTIFIER, word);
            continue;
        }

        // numbers (integer or decimal)
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < n && std::isdigit(static_cast<unsigned char>(sql_[i + 1])))) {
            std::size_t start = i;
            if (sql_[i] == '-') ++i;
            while (i < n && (std::isdigit(static_cast<unsigned char>(sql_[i])) || sql_[i] == '.')) ++i;
            push(TokenType::NUMBER, sql_.substr(start, i - start));
            continue;
        }

        // single-quoted string literal
        if (c == '\'') {
            ++i;
            std::string s;
            while (i < n && sql_[i] != '\'') s += sql_[i++];
            if (i >= n) throw DBException("Lexer: unterminated string literal");
            ++i;  // closing quote
            push(TokenType::STRING, s);
            continue;
        }

        // operators / punctuation
        switch (c) {
            case '*': push(TokenType::STAR); ++i; break;
            case ',': push(TokenType::COMMA); ++i; break;
            case '(': push(TokenType::LPAREN); ++i; break;
            case ')': push(TokenType::RPAREN); ++i; break;
            case ';': push(TokenType::SEMICOLON); ++i; break;
            case '.': push(TokenType::DOT); ++i; break;
            case '=': push(TokenType::EQ); ++i; break;
            case '<':
                if (i + 1 < n && sql_[i + 1] == '=') { push(TokenType::LTE); i += 2; }
                else if (i + 1 < n && sql_[i + 1] == '>') { push(TokenType::NEQ); i += 2; }
                else { push(TokenType::LT); ++i; }
                break;
            case '>':
                if (i + 1 < n && sql_[i + 1] == '=') { push(TokenType::GTE); i += 2; }
                else { push(TokenType::GT); ++i; }
                break;
            case '!':
                if (i + 1 < n && sql_[i + 1] == '=') { push(TokenType::NEQ); i += 2; }
                else throw DBException("Lexer: unexpected '!'");
                break;
            default:
                throw DBException(std::string("Lexer: unexpected character '") + c + "'");
        }
    }

    push(TokenType::END);
    return out;
}

} // namespace minidb
