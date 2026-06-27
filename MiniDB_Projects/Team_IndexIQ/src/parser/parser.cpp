#include "parser/parser.h"
#include <stdexcept>

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

Token Parser::consume(TokenType expected) {
    if (cur().type != expected)
        throw std::runtime_error("Parse error: unexpected token '" + cur().text + "'");
    return tokens_[pos_++];
}

Statement Parser::parse() {
    if (match(TokenType::EXPLAIN)) return parse_select(true);
    if (check(TokenType::SELECT))  { pos_++; return parse_select(false); }
    if (check(TokenType::INSERT))  { pos_++; return parse_insert(); }
    if (check(TokenType::DELETE))  { pos_++; return parse_delete(); }
    if (check(TokenType::CREATE))  { pos_++; return parse_create(); }
    if (match(TokenType::BEGIN))   return BeginStmt{};
    if (match(TokenType::COMMIT))  return CommitStmt{};
    if (match(TokenType::ROLLBACK))return RollbackStmt{};
    throw std::runtime_error("Unknown statement: " + cur().text);
}

Statement Parser::parse_select(bool explain) {
    if (explain) consume(TokenType::SELECT);

    SelectStmt stmt;
    stmt.explain = explain;

    if (match(TokenType::STAR)) {
        stmt.cols = {"*"};
    } else {
        stmt.cols.push_back(consume(TokenType::IDENTIFIER).text);
        while (match(TokenType::COMMA))
            stmt.cols.push_back(consume(TokenType::IDENTIFIER).text);
    }

    consume(TokenType::FROM);
    stmt.table = consume(TokenType::IDENTIFIER).text;

    if (match(TokenType::JOIN)) {
        stmt.join_table = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::ON);
        std::string lt = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::DOT);
        std::string lc = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::EQ);
        std::string rt = consume(TokenType::IDENTIFIER).text;
        consume(TokenType::DOT);
        std::string rc = consume(TokenType::IDENTIFIER).text;
        stmt.join_left_col  = lt + "." + lc;
        stmt.join_right_col = rt + "." + rc;
    }

    if (match(TokenType::WHERE))
        stmt.where = parse_expr();

    return stmt;
}

Statement Parser::parse_insert() {
    consume(TokenType::INTO);
    InsertStmt stmt;
    stmt.table = consume(TokenType::IDENTIFIER).text;
    consume(TokenType::VALUES);
    consume(TokenType::LPAREN);
    auto read_val = [&]() -> std::string {
        if (check(TokenType::NUMBER))     return consume(TokenType::NUMBER).text;
        if (check(TokenType::STRING))     return consume(TokenType::STRING).text;
        if (check(TokenType::IDENTIFIER)) return consume(TokenType::IDENTIFIER).text;
        throw std::runtime_error("Expected value in INSERT");
    };
    stmt.values.push_back(read_val());
    while (match(TokenType::COMMA)) stmt.values.push_back(read_val());
    consume(TokenType::RPAREN);
    return stmt;
}

Statement Parser::parse_delete() {
    consume(TokenType::FROM);
    DeleteStmt stmt;
    stmt.table = consume(TokenType::IDENTIFIER).text;
    if (match(TokenType::WHERE)) stmt.where = parse_expr();
    return stmt;
}

Statement Parser::parse_create() {
    consume(TokenType::TABLE);
    CreateStmt stmt;
    stmt.table = consume(TokenType::IDENTIFIER).text;
    consume(TokenType::LPAREN);
    auto read_col = [&]() {
        ColDef c;
        c.name = consume(TokenType::IDENTIFIER).text;
        c.type = consume(TokenType::IDENTIFIER).text;
        for (char& ch : c.type) ch = std::toupper(ch);
        stmt.columns.push_back(c);
    };
    read_col();
    while (match(TokenType::COMMA)) read_col();
    consume(TokenType::RPAREN);
    return stmt;
}

Expression* Parser::parse_expr() {
    auto* left = parse_and();
    while (match(TokenType::OR)) {
        auto* right = parse_and();
        left = new BinaryExpr("OR", left, right);
    }
    return left;
}

Expression* Parser::parse_and() {
    auto* left = parse_comparison();
    while (match(TokenType::AND)) {
        auto* right = parse_comparison();
        left = new BinaryExpr("AND", left, right);
    }
    return left;
}

Expression* Parser::parse_comparison() {
    auto* left = parse_primary();
    std::string op;
    if      (match(TokenType::EQ))  op = "=";
    else if (match(TokenType::NEQ)) op = "!=";
    else if (match(TokenType::GT))  op = ">";
    else if (match(TokenType::LT))  op = "<";
    else if (match(TokenType::GTE)) op = ">=";
    else if (match(TokenType::LTE)) op = "<=";
    else return left;
    auto* right = parse_primary();
    return new BinaryExpr(op, left, right);
}

Expression* Parser::parse_primary() {
    if (check(TokenType::NUMBER))
        return new Literal(consume(TokenType::NUMBER).text);
    if (check(TokenType::STRING))
        return new Literal(consume(TokenType::STRING).text);
    if (check(TokenType::IDENTIFIER)) {
        std::string name = consume(TokenType::IDENTIFIER).text;
        if (match(TokenType::DOT)) {
            std::string col = consume(TokenType::IDENTIFIER).text;
            return new ColumnRef(name, col);
        }
        return new ColumnRef(name);
    }
    if (match(TokenType::LPAREN)) {
        auto* e = parse_expr();
        consume(TokenType::RPAREN);
        return e;
    }
    throw std::runtime_error("Expected expression, got: " + cur().text);
}
