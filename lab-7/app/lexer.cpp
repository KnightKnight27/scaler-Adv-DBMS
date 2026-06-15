#include "lexer.h"
#include <cctype>
#include <stdexcept>

Lexer::Lexer(const std::string& sql) : input(sql) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (pos < input.size()) {
        skipWhitespace();
        if (pos >= input.size()) break;
        
        char ch = current();
        
        if (std::isdigit(ch) || (ch == '.' && std::isdigit(peek()))) {
            tokens.push_back(readNumber());
        } else if (std::isalpha(ch) || ch == '_') {
            tokens.push_back(readIdentifierOrKeyword());
        } else if (ch == '\'' || ch == '"') {
            tokens.push_back(readString());
        } else if (ch == '(' || ch == ')' || ch == ',') {
            if (ch == '(') tokens.push_back(Token(TokenType::LPAREN, "("));
            else if (ch == ')') tokens.push_back(Token(TokenType::RPAREN, ")"));
            else if (ch == ',') tokens.push_back(Token(TokenType::COMMA, ","));
            advance();
        } else {
            tokens.push_back(readOperator());
        }
    }
    
    tokens.push_back(Token(TokenType::END, ""));
    return tokens;
}

char Lexer::current() {
    if (pos >= input.size()) return '\0';
    return input[pos];
}

char Lexer::peek(int offset) {
    if (pos + offset >= input.size()) return '\0';
    return input[pos + offset];
}

void Lexer::advance() {
    if (pos < input.size()) pos++;
}

void Lexer::skipWhitespace() {
    while (pos < input.size() && std::isspace(current())) {
        advance();
    }
}

Token Lexer::readNumber() {
    std::string num;
    while (pos < input.size() && (std::isdigit(current()) || current() == '.')) {
        num += current();
        advance();
    }
    return Token(TokenType::NUMBER, num);
}

Token Lexer::readIdentifierOrKeyword() {
    std::string word;
    while (pos < input.size() && (std::isalnum(current()) || current() == '_')) {
        word += current();
        advance();
    }
    TokenType type = getKeywordType(word);
    return Token(type, word);
}

Token Lexer::readString() {
    char quote = current();
    advance();
    std::string str;
    while (pos < input.size() && current() != quote) {
        str += current();
        advance();
    }
    if (pos < input.size()) advance();
    return Token(TokenType::STRING, str);
}

Token Lexer::readOperator() {
    char ch = current();
    
    if (pos + 1 < input.size()) {
        std::string two = std::string(1, ch) + peek();
        if (two == "<=") { advance(); advance(); return Token(TokenType::LE, "<="); }
        if (two == ">=") { advance(); advance(); return Token(TokenType::GE, ">="); }
        if (two == "!=") { advance(); advance(); return Token(TokenType::NE, "!="); }
        if (two == "==") { advance(); advance(); return Token(TokenType::EQ, "=="); }
        if (two == "&&") { advance(); advance(); return Token(TokenType::LOGICAL_AND, "&&"); }
        if (two == "||") { advance(); advance(); return Token(TokenType::LOGICAL_OR, "||"); }
    }
    
    advance();
    switch (ch) {
        case '+': return Token(TokenType::PLUS, "+");
        case '-': return Token(TokenType::MINUS, "-");
        case '*': return Token(TokenType::STAR, "*");
        case '/': return Token(TokenType::SLASH, "/");
        case '^': return Token(TokenType::CARET, "^");
        case '<': return Token(TokenType::LT, "<");
        case '>': return Token(TokenType::GT, ">");
        case '=': return Token(TokenType::EQ, "=");
        default: return Token(TokenType::UNKNOWN, std::string(1, ch));
    }
}

TokenType Lexer::getKeywordType(const std::string& word) {
    std::string upper = word;
    for (auto& c : upper) c = std::toupper(c);
    
    if (upper == "SELECT") return TokenType::SELECT;
    if (upper == "FROM") return TokenType::FROM;
    if (upper == "WHERE") return TokenType::WHERE;
    if (upper == "ORDER") return TokenType::ORDER;
    if (upper == "BY") return TokenType::BY;
    if (upper == "DESC") return TokenType::DESC;
    if (upper == "ASC") return TokenType::ASC;
    if (upper == "LIMIT") return TokenType::LIMIT;
    if (upper == "AND") return TokenType::AND;
    if (upper == "OR") return TokenType::OR;
    
    return TokenType::IDENTIFIER;
}
