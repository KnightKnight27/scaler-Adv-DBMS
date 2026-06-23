// sql/lexer.cpp — SQL tokenizer implementation.
//
// Scanning strategy:
//   1. Skip whitespace.
//   2. Peek at the next character to decide which sub-scanner to call.
//   3. Identifiers/keywords start with [a-zA-Z_].
//   4. Numbers start with a digit.
//   5. Strings start with a single quote.
//   6. Everything else is an operator or symbol (one or two characters).

#include "sql/lexer.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace minidb {

// ─── Keyword Table ───────────────────────────────────────────
// Maps upper-cased identifier text to the corresponding keyword TokenType.
static const std::unordered_map<std::string, TokenType> kKeywords = {
    {"SELECT",  TokenType::SELECT},
    {"FROM",    TokenType::FROM},
    {"WHERE",   TokenType::WHERE},
    {"INSERT",  TokenType::INSERT},
    {"INTO",    TokenType::INTO},
    {"VALUES",  TokenType::VALUES},
    {"DELETE",  TokenType::DELETE},
    {"CREATE",  TokenType::CREATE},
    {"TABLE",   TokenType::TABLE},
    {"JOIN",    TokenType::JOIN},
    {"INNER",   TokenType::INNER},
    {"LEFT",    TokenType::LEFT},
    {"ON",      TokenType::ON},
    {"AND",     TokenType::AND},
    {"OR",      TokenType::OR},
    {"NOT",     TokenType::NOT},
    {"PRIMARY", TokenType::PRIMARY},
    {"KEY",     TokenType::KEY},
    {"INT",     TokenType::INT_TYPE},
    {"FLOAT",   TokenType::FLOAT_TYPE},
    {"VARCHAR", TokenType::VARCHAR_TYPE},
    {"BOOL",    TokenType::BOOL_TYPE},
    {"TRUE",    TokenType::TRUE_KW},
    {"FALSE",   TokenType::FALSE_KW},
    {"NULL",    TokenType::NULL_KW},
    {"AS",      TokenType::AS},
};

// ─── Constructor ─────────────────────────────────────────────
Lexer::Lexer(const std::string& input) : input_(input), pos_(0) {}

// ─── Character-level helpers ─────────────────────────────────
char Lexer::peek() const {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_];
}

char Lexer::advance() {
    if (pos_ >= input_.size()) return '\0';
    return input_[pos_++];
}

void Lexer::skip_whitespace() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
        pos_++;
    }
}

// ─── Sub-scanners ────────────────────────────────────────────

Token Lexer::read_identifier_or_keyword() {
    int start = static_cast<int>(pos_);
    std::string text;
    // First character: [a-zA-Z_]
    text += advance();
    // Remaining: [a-zA-Z0-9_]
    while (pos_ < input_.size()) {
        char c = peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            text += advance();
        } else {
            break;
        }
    }

    // Case-insensitive keyword check: convert to uppercase.
    std::string upper = text;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char ch) { return std::toupper(ch); });

    auto it = kKeywords.find(upper);
    if (it != kKeywords.end()) {
        return Token{it->second, text, start};
    }
    return Token{TokenType::IDENTIFIER, text, start};
}

Token Lexer::read_number() {
    int start = static_cast<int>(pos_);
    std::string text;
    bool has_dot = false;

    while (pos_ < input_.size()) {
        char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            text += advance();
        } else if (c == '.' && !has_dot) {
            has_dot = true;
            text += advance();
        } else {
            break;
        }
    }

    TokenType tt = has_dot ? TokenType::FLOAT_LIT : TokenType::INTEGER_LIT;
    return Token{tt, text, start};
}

Token Lexer::read_string_literal() {
    int start = static_cast<int>(pos_);
    advance();  // consume opening '
    std::string text;

    while (pos_ < input_.size()) {
        char c = advance();
        if (c == '\'') {
            // Check for escaped quote ('')
            if (peek() == '\'') {
                text += '\'';
                advance();
            } else {
                // End of string literal.
                return Token{TokenType::STRING_LIT, text, start};
            }
        } else {
            text += c;
        }
    }
    throw std::runtime_error("Unterminated string literal at position " +
                             std::to_string(start));
}

// ─── Main tokenize loop ─────────────────────────────────────
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    while (true) {
        skip_whitespace();
        if (pos_ >= input_.size()) {
            tokens.push_back(Token{TokenType::END_OF_INPUT, "", static_cast<int>(pos_)});
            break;
        }

        char c = peek();
        int start = static_cast<int>(pos_);

        // Identifiers / keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(read_identifier_or_keyword());
            continue;
        }

        // Numbers
        if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(read_number());
            continue;
        }

        // String literals
        if (c == '\'') {
            tokens.push_back(read_string_literal());
            continue;
        }

        // Two-character operators
        if (c == '!' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenType::NEQ, "!=", start});
            continue;
        }
        if (c == '<' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenType::LTE, "<=", start});
            continue;
        }
        if (c == '>' && pos_ + 1 < input_.size() && input_[pos_ + 1] == '=') {
            advance(); advance();
            tokens.push_back(Token{TokenType::GTE, ">=", start});
            continue;
        }

        // Single-character tokens
        advance();
        switch (c) {
            case '*': tokens.push_back(Token{TokenType::STAR,      "*", start}); break;
            case ',': tokens.push_back(Token{TokenType::COMMA,     ",", start}); break;
            case '(': tokens.push_back(Token{TokenType::LPAREN,    "(", start}); break;
            case ')': tokens.push_back(Token{TokenType::RPAREN,    ")", start}); break;
            case ';': tokens.push_back(Token{TokenType::SEMICOLON, ";", start}); break;
            case '.': tokens.push_back(Token{TokenType::DOT,       ".", start}); break;
            case '=': tokens.push_back(Token{TokenType::EQ,        "=", start}); break;
            case '<': tokens.push_back(Token{TokenType::LT,        "<", start}); break;
            case '>': tokens.push_back(Token{TokenType::GT,        ">", start}); break;
            default:
                tokens.push_back(Token{TokenType::INVALID,
                                       std::string(1, c), start});
                break;
        }
    }

    return tokens;
}

}  // namespace minidb
