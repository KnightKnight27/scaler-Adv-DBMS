#include "parser.h"
#include <stdexcept>

DbParser::DbParser(std::vector<Token> toks) : tokens(std::move(toks)) {}

Token DbParser::peek() const {
    if (pos >= tokens.size()) return {TokenType::END, ""};
    return tokens[pos];
}

Token DbParser::advance() {
    if (pos >= tokens.size()) return {TokenType::END, ""};
    return tokens[pos++];
}

bool DbParser::match(TokenType type) {
    if (peek().type == type) {
        advance();
        return true;
    }
    return false;
}

Token DbParser::consume(TokenType type, const std::string& errorMsg) {
    if (peek().type == type) {
        return advance();
    }
    throw std::runtime_error(errorMsg + " (Got: " + peek().text + ")");
}

bool DbParser::isAtEnd() const {
    return peek().type == TokenType::END;
}

SelectStatement DbParser::parseSelect() {
    consume(TokenType::SELECT, "Expected 'SELECT'");

    SelectStatement stmt;

    if (match(TokenType::STAR)) {
        // SELECT * -> empty columns vector represents SELECT *
    } else {
        do {
            Token colTok = consume(TokenType::IDENTIFIER, "Expected column name");
            stmt.columns.push_back(colTok.text);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::FROM, "Expected 'FROM'");
    stmt.tableName = consume(TokenType::IDENTIFIER, "Expected table name").text;

    if (match(TokenType::WHERE)) {
        stmt.whereFilter = parseExpression();
    }

    if (match(TokenType::ORDER)) {
        consume(TokenType::BY, "Expected 'BY' after 'ORDER'");
        stmt.orderByColumn = consume(TokenType::IDENTIFIER, "Expected order by column name").text;
        if (match(TokenType::DESC)) {
            stmt.orderByAsc = false;
        } else if (match(TokenType::ASC)) {
            stmt.orderByAsc = true;
        }
    }

    if (match(TokenType::LIMIT)) {
        Token limitTok = consume(TokenType::NUMBER, "Expected number after 'LIMIT'");
        stmt.limit = std::stoi(limitTok.text);
    }

    return stmt;
}

Expression* DbParser::parseExpression() {
    return parseOr();
}

Expression* DbParser::parseOr() {
    Expression* left = parseAnd();
    while (match(TokenType::OR)) {
        Expression* right = parseAnd();
        left = new BinaryExpression("OR", left, right);
    }
    return left;
}

Expression* DbParser::parseAnd() {
    Expression* left = parseComparison();
    while (match(TokenType::AND)) {
        Expression* right = parseComparison();
        left = new BinaryExpression("AND", left, right);
    }
    return left;
}

Expression* DbParser::parseComparison() {
    Expression* left = parsePrimary();

    Token opTok = peek();
    if (opTok.type == TokenType::GT || opTok.type == TokenType::GE ||
        opTok.type == TokenType::LT || opTok.type == TokenType::LE ||
        opTok.type == TokenType::EQ || opTok.type == TokenType::NE) {
        advance(); // consume operator
        Expression* right = parsePrimary();
        left = new BinaryExpression(opTok.text, left, right);
    }

    return left;
}

Expression* DbParser::parsePrimary() {
    Token tok = peek();
    if (match(TokenType::LPAREN)) {
        Expression* expr = parseExpression();
        consume(TokenType::RPAREN, "Expected ')'");
        return expr;
    }

    if (match(TokenType::NUMBER)) {
        // Parse float or int
        if (tok.text.find('.') != std::string::npos) {
            return new Literal(std::stod(tok.text));
        } else {
            return new Literal(std::stoi(tok.text));
        }
    }

    if (match(TokenType::STRING)) {
        return new Literal(tok.text);
    }

    if (match(TokenType::IDENTIFIER)) {
        return new ColumnRef(tok.text);
    }

    throw std::runtime_error("Unexpected token in expression: " + tok.text);
}
