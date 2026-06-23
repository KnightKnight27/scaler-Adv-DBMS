//
// lexer.cpp
// ---------------------------------------------------------------------------
// Implementation of the lexer declared in lexer.h.
// ---------------------------------------------------------------------------

#include "lexer.h"
#include <cctype>
#include <stdexcept>

const char *tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::SELECT:     return "SELECT";
        case TokenType::FROM:       return "FROM";
        case TokenType::WHERE:      return "WHERE";
        case TokenType::AND:        return "AND";
        case TokenType::OR:         return "OR";
        case TokenType::NOT:        return "NOT";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::NUMBER:     return "NUMBER";
        case TokenType::STRING:     return "STRING";
        case TokenType::EQ:         return "EQ";
        case TokenType::NEQ:        return "NEQ";
        case TokenType::GT:         return "GT";
        case TokenType::LT:         return "LT";
        case TokenType::GTE:        return "GTE";
        case TokenType::LTE:        return "LTE";
        case TokenType::PLUS:       return "PLUS";
        case TokenType::MINUS:      return "MINUS";
        case TokenType::STAR:       return "STAR";
        case TokenType::SLASH:      return "SLASH";
        case TokenType::LPAREN:     return "LPAREN";
        case TokenType::RPAREN:     return "RPAREN";
        case TokenType::COMMA:      return "COMMA";
        case TokenType::END:        return "END";
    }
    return "?";
}

Lexer::Lexer(std::string sql) : input(std::move(sql)) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    size_t pos = 0;
    const size_t n = input.size();

    while (pos < n) {
        char c = input[pos];

        // 1. skip whitespace
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++pos;
            continue;
        }

        // 2. identifiers & keywords: [A-Za-z_][A-Za-z0-9_]*
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string word;
            while (pos < n &&
                   (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_')) {
                word += input[pos++];
            }
            // Keyword matching is case-sensitive (uppercase) to match the repo
            // style; AND/OR/NOT are reserved words, everything else is an id.
            if      (word == "SELECT") tokens.push_back({TokenType::SELECT, word});
            else if (word == "FROM")   tokens.push_back({TokenType::FROM, word});
            else if (word == "WHERE")  tokens.push_back({TokenType::WHERE, word});
            else if (word == "AND")    tokens.push_back({TokenType::AND, word});
            else if (word == "OR")     tokens.push_back({TokenType::OR, word});
            else if (word == "NOT")    tokens.push_back({TokenType::NOT, word});
            else                       tokens.push_back({TokenType::IDENTIFIER, word});
            continue;
        }

        // 3. integer literals
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string num;
            while (pos < n && std::isdigit(static_cast<unsigned char>(input[pos]))) {
                num += input[pos++];
            }
            tokens.push_back({TokenType::NUMBER, num});
            continue;
        }

        // 4. 'single-quoted' string literals
        if (c == '\'') {
            ++pos;                       // skip opening quote
            std::string str;
            while (pos < n && input[pos] != '\'') {
                str += input[pos++];
            }
            if (pos >= n) throw std::runtime_error("Unterminated string literal");
            ++pos;                       // skip closing quote
            tokens.push_back({TokenType::STRING, str});
            continue;
        }

        // 5. multi-char and single-char operators / punctuation.
        //    We peek at the next char to disambiguate >=, <=, != .
        char next = (pos + 1 < n) ? input[pos + 1] : '\0';
        switch (c) {
            case '=':
                tokens.push_back({TokenType::EQ, "="}); pos += 1; break;
            case '!':
                if (next == '=') { tokens.push_back({TokenType::NEQ, "!="}); pos += 2; }
                else throw std::runtime_error("Unexpected '!' (did you mean '!=' ?)");
                break;
            case '>':
                if (next == '=') { tokens.push_back({TokenType::GTE, ">="}); pos += 2; }
                else             { tokens.push_back({TokenType::GT,  ">"});  pos += 1; }
                break;
            case '<':
                if (next == '=') { tokens.push_back({TokenType::LTE, "<="}); pos += 2; }
                else             { tokens.push_back({TokenType::LT,  "<"});  pos += 1; }
                break;
            case '+': tokens.push_back({TokenType::PLUS,   "+"}); pos += 1; break;
            case '-': tokens.push_back({TokenType::MINUS,  "-"}); pos += 1; break;
            case '*': tokens.push_back({TokenType::STAR,   "*"}); pos += 1; break;
            case '/': tokens.push_back({TokenType::SLASH,  "/"}); pos += 1; break;
            case '(': tokens.push_back({TokenType::LPAREN, "("}); pos += 1; break;
            case ')': tokens.push_back({TokenType::RPAREN, ")"}); pos += 1; break;
            case ',': tokens.push_back({TokenType::COMMA,  ","}); pos += 1; break;
            default:
                throw std::runtime_error(std::string("Unexpected character: '") + c + "'");
        }
    }

    tokens.push_back({TokenType::END, ""});
    return tokens;
}
