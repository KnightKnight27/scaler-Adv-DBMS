#pragma once

#include "types.h"
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>

// Query structure for simple SELECT statements.
struct SelectQuery {
    std::vector<std::string> columns;
    std::string table;
    std::string where_clause;
    std::string order_by;
    bool order_asc = true;
    int limit = -1;
};

class SelectParser {
public:
    explicit SelectParser(const std::vector<Token>& toks);
    SelectQuery parse();

private:
    std::vector<Token> tokens;
    size_t pos = 0;

    Token& current();
    const Token& current() const;
    Token consume(TokenType expected);
    Token consume();
    bool match(TokenType type);
    bool check(TokenType type) const;

    void parseSelect(SelectQuery& q);
    void parseFrom(SelectQuery& q);
    void parseWhere(SelectQuery& q);
    void parseOrderBy(SelectQuery& q);
    void parseLimit(SelectQuery& q);
};

inline SelectParser::SelectParser(const std::vector<Token>& toks) : tokens(toks) {}

inline SelectQuery SelectParser::parse() {
    SelectQuery q;

    if (current().type != TokenType::SELECT) {
        throw std::runtime_error("Expected SELECT keyword");
    }

    parseSelect(q);
    parseFrom(q);

    if (check(TokenType::WHERE)) parseWhere(q);
    if (check(TokenType::ORDER)) parseOrderBy(q);
    if (check(TokenType::LIMIT)) parseLimit(q);

    if (current().type != TokenType::END) {
        throw std::runtime_error("Unexpected token after query: " + current().text);
    }

    return q;
}

inline Token& SelectParser::current() {
    if (pos >= tokens.size()) return tokens.back();
    return tokens[pos];
}

inline const Token& SelectParser::current() const {
    if (pos >= tokens.size()) return tokens.back();
    return tokens[pos];
}

inline Token SelectParser::consume(TokenType expected) {
    if (current().type != expected) {
        throw std::runtime_error("Unexpected token: " + current().text);
    }
    return consume();
}

inline Token SelectParser::consume() {
    if (pos < tokens.size()) {
        return tokens[pos++];
    }
    throw std::runtime_error("Unexpected end of tokens");
}

inline bool SelectParser::match(TokenType type) {
    if (check(type)) {
        consume();
        return true;
    }
    return false;
}

inline bool SelectParser::check(TokenType type) const {
    return current().type == type;
}

inline void SelectParser::parseSelect(SelectQuery& q) {
    consume(TokenType::SELECT);

    if (match(TokenType::STAR)) {
        q.columns.clear();
        return;
    }

    while (true) {
        if (current().type != TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected column name, got " + current().text);
        }
        q.columns.push_back(current().text);
        consume();

        if (!match(TokenType::COMMA)) break;
    }
}

inline void SelectParser::parseFrom(SelectQuery& q) {
    consume(TokenType::FROM);

    if (current().type != TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected table name");
    }
    q.table = current().text;
    consume();
}

inline void SelectParser::parseWhere(SelectQuery& q) {
    consume(TokenType::WHERE);

    std::stringstream clause;
    while (pos < tokens.size() &&
           current().type != TokenType::ORDER &&
           current().type != TokenType::LIMIT &&
           current().type != TokenType::END) {
        if (!clause.str().empty()) clause << ' ';
        clause << current().text;
        consume();
    }

    q.where_clause = clause.str();
}

inline void SelectParser::parseOrderBy(SelectQuery& q) {
    consume(TokenType::ORDER);
    consume(TokenType::BY);

    if (current().type != TokenType::IDENTIFIER) {
        throw std::runtime_error("Expected column name after ORDER BY");
    }
    q.order_by = current().text;
    consume();

    if (match(TokenType::DESC)) {
        q.order_asc = false;
    } else {
        match(TokenType::ASC);
        q.order_asc = true;
    }
}

inline void SelectParser::parseLimit(SelectQuery& q) {
    consume(TokenType::LIMIT);

    if (current().type != TokenType::NUMBER) {
        throw std::runtime_error("Expected number after LIMIT");
    }

    q.limit = std::stoi(current().text);
    consume();
}
