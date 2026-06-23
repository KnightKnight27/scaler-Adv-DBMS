#include "parser/lexer.h"

#include <cctype>
#include <unordered_map>

namespace minidb {

bool Lexer::AtEnd() const { return pos_ >= source_.size(); }

char Lexer::Peek() const { return AtEnd() ? '\0' : source_[pos_]; }

char Lexer::PeekNext() const {
    return (pos_ + 1 >= source_.size()) ? '\0' : source_[pos_ + 1];
}

char Lexer::Advance() { return source_[pos_++]; }

TokenType Lexer::KeywordType(const std::string& upper) {
    static const std::unordered_map<std::string, TokenType> kKeywords = {
        {"SELECT", TokenType::KW_SELECT},     {"FROM", TokenType::KW_FROM},
        {"WHERE", TokenType::KW_WHERE},       {"INSERT", TokenType::KW_INSERT},
        {"INTO", TokenType::KW_INTO},         {"VALUES", TokenType::KW_VALUES},
        {"CREATE", TokenType::KW_CREATE},     {"TABLE", TokenType::KW_TABLE},
        {"DELETE", TokenType::KW_DELETE},     {"JOIN", TokenType::KW_JOIN},
        {"ON", TokenType::KW_ON},             {"AND", TokenType::KW_AND},
        {"OR", TokenType::KW_OR},             {"NOT", TokenType::KW_NOT},
        {"BEGIN", TokenType::KW_BEGIN},       {"COMMIT", TokenType::KW_COMMIT},
        {"ROLLBACK", TokenType::KW_ROLLBACK}, {"INDEX", TokenType::KW_INDEX},
        {"INT", TokenType::KW_INT},           {"INTEGER", TokenType::KW_INTEGER},
        {"VARCHAR", TokenType::KW_VARCHAR},   {"EXIT", TokenType::KW_EXIT},
        {"QUIT", TokenType::KW_EXIT},
    };
    auto it = kKeywords.find(upper);
    return it == kKeywords.end() ? TokenType::IDENTIFIER : it->second;
}

Token Lexer::LexIdentifierOrKeyword() {
    size_t start = pos_;
    while (!AtEnd() && (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
        Advance();
    }
    std::string text = source_.substr(start, pos_ - start);

    std::string upper = text;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    TokenType type = KeywordType(upper);
    // For keywords we normalize the lexeme to upper-case so downstream code is
    // case-insensitive; identifiers keep their original casing.
    return Token(type, type == TokenType::IDENTIFIER ? text : upper, start);
}

Token Lexer::LexNumber() {
    size_t start = pos_;
    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) Advance();
    return Token(TokenType::INTEGER_LITERAL, source_.substr(start, pos_ - start), start);
}

Token Lexer::LexString(char quote) {
    size_t start = pos_;
    Advance(); // consume opening quote
    std::string value;
    while (!AtEnd() && Peek() != quote) {
        // Support doubled-quote escaping inside string literals ('it''s').
        if (Peek() == quote && PeekNext() == quote) {
            value += quote;
            Advance();
            Advance();
            continue;
        }
        value += Advance();
    }
    if (AtEnd()) {
        return Token(TokenType::INVALID, "Unterminated string literal", start);
    }
    Advance(); // consume closing quote
    return Token(TokenType::STRING_LITERAL, value, start);
}

Token Lexer::LexOperatorOrPunct() {
    size_t start = pos_;
    char c = Advance();
    switch (c) {
        case '(': return Token(TokenType::LPAREN, "(", start);
        case ')': return Token(TokenType::RPAREN, ")", start);
        case ',': return Token(TokenType::COMMA, ",", start);
        case ';': return Token(TokenType::SEMICOLON, ";", start);
        case '.': return Token(TokenType::DOT, ".", start);
        case '*': return Token(TokenType::STAR, "*", start);
        case '=': return Token(TokenType::EQUAL, "=", start);
        case '<':
            if (Peek() == '=') { Advance(); return Token(TokenType::LESS_EQUAL, "<=", start); }
            if (Peek() == '>') { Advance(); return Token(TokenType::NOT_EQUAL, "<>", start); }
            return Token(TokenType::LESS, "<", start);
        case '>':
            if (Peek() == '=') { Advance(); return Token(TokenType::GREATER_EQUAL, ">=", start); }
            return Token(TokenType::GREATER, ">", start);
        case '!':
            if (Peek() == '=') { Advance(); return Token(TokenType::NOT_EQUAL, "!=", start); }
            return Token(TokenType::INVALID, "!", start);
        default:
            return Token(TokenType::INVALID, std::string(1, c), start);
    }
}

std::vector<Token> Lexer::Tokenize() {
    std::vector<Token> tokens;
    while (!AtEnd()) {
        char c = Peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            Advance();
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(LexIdentifierOrKeyword());
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(LexNumber());
        } else if (c == '\'' || c == '"') {
            tokens.push_back(LexString(c));
        } else {
            tokens.push_back(LexOperatorOrPunct());
        }
    }
    tokens.emplace_back(TokenType::END_OF_FILE, "", pos_);
    return tokens;
}

} // namespace minidb
