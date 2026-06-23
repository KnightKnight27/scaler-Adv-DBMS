#include "parser/parser.h"
#include <stdexcept>

namespace minidb {

SQLParser::SQLParser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Token SQLParser::Current() {
    if (pos_ < tokens_.size()) {
        return tokens_[pos_];
    }
    return {TokenType::END, ""};
}

Token SQLParser::Consume(TokenType expected) {
    Token t = Current();
    if (t.type != expected) {
        throw std::runtime_error("Parser error: Expected token type, but got: " + t.text);
    }
    pos_++;
    return t;
}

SQLSelectStatement SQLParser::ParseSelect() {
    SQLSelectStatement stmt;

    Consume(TokenType::SELECT);

    // Parse projection list (e.g. *, or id, name, age)
    if (Current().type == TokenType::STAR) {
        Consume(TokenType::STAR);
        stmt.projection_cols.push_back("*");
    } else {
        while (true) {
            Token col = Consume(TokenType::IDENTIFIER);
            stmt.projection_cols.push_back(col.text);
            if (Current().type == TokenType::COMMA) {
                Consume(TokenType::COMMA);
            } else {
                break;
            }
        }
    }

    Consume(TokenType::FROM);
    Token table = Consume(TokenType::IDENTIFIER);
    stmt.table_name = table.text;

    // Parse optional WHERE clause
    if (Current().type == TokenType::WHERE) {
        Consume(TokenType::WHERE);

        bool has_paren = false;
        if (Current().type == TokenType::LPAREN) {
            Consume(TokenType::LPAREN);
            has_paren = true;
        }

        Token col = Consume(TokenType::IDENTIFIER);
        stmt.filter_col = col.text;

        Token op = Current();
        if (op.type == TokenType::EQUALS || op.type == TokenType::GT || op.type == TokenType::LT) {
            stmt.filter_op = op.text;
            pos_++;
        } else {
            throw std::runtime_error("Parser error: Expected operator (=, >, <) but got: " + op.text);
        }

        Token val = Consume(TokenType::NUMBER);
        stmt.filter_val = std::stoi(val.text);

        if (has_paren) {
            Consume(TokenType::RPAREN);
        }
    }

    return stmt;
}

} // namespace minidb
