#include "parser/parser.h"

#include <stdexcept>

namespace minidb {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Statement Parser::Parse() {
    if (Current().type == TokenType::SELECT) {
        Statement stmt;
        stmt.type = StatementType::SELECT;
        stmt.select = ParseSelect();
        return stmt;
    }
    if (Current().type == TokenType::INSERT) {
        Statement stmt;
        stmt.type = StatementType::INSERT;
        stmt.insert = ParseInsert();
        return stmt;
    }
    if (Current().type == TokenType::DELETE) {
        Statement stmt;
        stmt.type = StatementType::DELETE;
        stmt.delete_stmt = ParseDelete();
        return stmt;
    }
    throw std::runtime_error("Expected SELECT, INSERT, or DELETE");
}

SelectStatement Parser::ParseSelect() {
    Consume(TokenType::SELECT);
    const std::string column = Consume(TokenType::IDENTIFIER).text;
    Consume(TokenType::FROM);
    const std::string table = Consume(TokenType::IDENTIFIER).text;

    SelectStatement stmt;
    stmt.column = column;
    stmt.table = table;

    if (Current().type == TokenType::JOIN) {
        Consume(TokenType::JOIN);
        stmt.join = std::make_unique<JoinClause>();
        stmt.join->table = Consume(TokenType::IDENTIFIER).text;
        Consume(TokenType::ON);
        stmt.join->left_column = Consume(TokenType::IDENTIFIER).text;
        Consume(TokenType::EQ);
        stmt.join->right_column = Consume(TokenType::IDENTIFIER).text;
    }

    if (Current().type == TokenType::WHERE) {
        Consume(TokenType::WHERE);
        stmt.where = ParseExpression();
    }

    return stmt;
}

InsertStatement Parser::ParseInsert() {
    Consume(TokenType::INSERT);
    Consume(TokenType::INTO);
    InsertStatement stmt;
    stmt.table = Consume(TokenType::IDENTIFIER).text;

    if (Current().type == TokenType::LPAREN) {
        Consume(TokenType::LPAREN);
        stmt.columns.push_back(Consume(TokenType::IDENTIFIER).text);
        while (Current().type == TokenType::COMMA) {
            Consume(TokenType::COMMA);
            stmt.columns.push_back(Consume(TokenType::IDENTIFIER).text);
        }
        Consume(TokenType::RPAREN);
    }

    Consume(TokenType::VALUES);
    Consume(TokenType::LPAREN);
    stmt.values.push_back(ParseValue());
    while (Current().type == TokenType::COMMA) {
        Consume(TokenType::COMMA);
        stmt.values.push_back(ParseValue());
    }
    Consume(TokenType::RPAREN);

    return stmt;
}

DeleteStatement Parser::ParseDelete() {
    Consume(TokenType::DELETE);
    Consume(TokenType::FROM);
    DeleteStatement stmt;
    stmt.table = Consume(TokenType::IDENTIFIER).text;
    if (Current().type == TokenType::WHERE) {
        Consume(TokenType::WHERE);
        stmt.where = ParseExpression();
    }
    return stmt;
}

std::unique_ptr<Expr> Parser::ParseExpression() {
    auto left = ParseAndExpression();
    while (Current().type == TokenType::OR) {
        Consume(TokenType::OR);
        auto right = ParseAndExpression();
        left = std::make_unique<BinaryExpr>("OR", std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParseAndExpression() {
    auto left = ParsePrimary();
    while (Current().type == TokenType::AND) {
        Consume(TokenType::AND);
        auto right = ParsePrimary();
        left = std::make_unique<BinaryExpr>("AND", std::move(left), std::move(right));
    }
    return left;
}

std::unique_ptr<Expr> Parser::ParsePrimary() {
    if (Current().type == TokenType::LPAREN) {
        Consume(TokenType::LPAREN);
        auto expr = ParseExpression();
        Consume(TokenType::RPAREN);
        return expr;
    }
    return ParseCondition();
}

std::unique_ptr<Expr> Parser::ParseCondition() {
    const std::string col = Consume(TokenType::IDENTIFIER).text;
    auto left = std::make_unique<ColumnRefExpr>(col);

    std::string op;
    if (Current().type == TokenType::GT) {
        op = ">";
        Consume(TokenType::GT);
    } else if (Current().type == TokenType::LT) {
        op = "<";
        Consume(TokenType::LT);
    } else if (Current().type == TokenType::EQ) {
        op = "=";
        Consume(TokenType::EQ);
    } else {
        throw std::runtime_error("Expected >, <, or =");
    }

    auto right = std::make_unique<LiteralExpr>(ParseValue());
    return std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
}

Value Parser::ParseValue() {
    if (Current().type == TokenType::NUMBER) {
        Value value;
        value.type = ValueType::INT;
        value.int_val = std::stoll(Consume(TokenType::NUMBER).text);
        return value;
    }
    if (Current().type == TokenType::STRING) {
        Value value;
        value.type = ValueType::STRING;
        value.str_val = Consume(TokenType::STRING).text;
        return value;
    }
    throw std::runtime_error("Expected number or string literal");
}

Token& Parser::Current() {
    return tokens_[pos_];
}

Token Parser::Consume(TokenType expected) {
    if (Current().type != expected) {
        throw std::runtime_error("Unexpected token in SQL");
    }
    return tokens_[pos_++];
}

bool Parser::Match(TokenType type) const {
    return tokens_[pos_].type == type;
}

}  // namespace minidb
