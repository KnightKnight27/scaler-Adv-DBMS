#include "sql/SqlParser.h"

#include <stdexcept>
#include <utility>

namespace sql {

namespace {

CompOp toCompOp(const std::string& sym) {
    if (sym == "=" || sym == "==") return CompOp::Eq;
    if (sym == "!=" || sym == "<>") return CompOp::Ne;
    if (sym == "<") return CompOp::Lt;
    if (sym == "<=") return CompOp::Le;
    if (sym == ">") return CompOp::Gt;
    if (sym == ">=") return CompOp::Ge;
    throw std::runtime_error("unknown comparison operator: " + sym);
}

}  // namespace

SqlParser::SqlParser(std::vector<SqlToken> tokens) : tokens_(std::move(tokens)) {}

const SqlToken& SqlParser::peek() const { return tokens_[pos_]; }

const SqlToken& SqlParser::advance() {
    const SqlToken& tok = tokens_[pos_];
    if (peek().type != SqlTokenType::End) ++pos_;
    return tok;
}

bool SqlParser::check(SqlTokenType type) const { return peek().type == type; }

bool SqlParser::match(SqlTokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

const SqlToken& SqlParser::expect(SqlTokenType type, const std::string& what) {
    if (!check(type)) {
        throw std::runtime_error("syntax error: expected " + what + " but found '" + peek().text + "'");
    }
    return advance();
}

SelectStatement SqlParser::parse() {
    SelectStatement stmt;
    expect(SqlTokenType::Select, "SELECT");
    parseProjection(stmt);
    expect(SqlTokenType::From, "FROM");
    stmt.table = expect(SqlTokenType::Identifier, "table name").text;
    if (match(SqlTokenType::Where)) {
        stmt.where = parseOr();
    }
    expect(SqlTokenType::End, "end of query");
    return stmt;
}

void SqlParser::parseProjection(SelectStatement& stmt) {
    if (match(SqlTokenType::Star)) {
        stmt.selectAll = true;
        return;
    }
    stmt.columns.push_back(expect(SqlTokenType::Identifier, "column name").text);
    while (match(SqlTokenType::Comma)) {
        stmt.columns.push_back(expect(SqlTokenType::Identifier, "column name").text);
    }
}

BoolExprPtr SqlParser::parseOr() {
    BoolExprPtr expr = parseAnd();
    while (match(SqlTokenType::Or)) {
        BoolExprPtr rhs = parseAnd();
        expr = std::make_unique<Logical>(LogicOp::Or, std::move(expr), std::move(rhs));
    }
    return expr;
}

BoolExprPtr SqlParser::parseAnd() {
    BoolExprPtr expr = parsePrimary();
    while (match(SqlTokenType::And)) {
        BoolExprPtr rhs = parsePrimary();
        expr = std::make_unique<Logical>(LogicOp::And, std::move(expr), std::move(rhs));
    }
    return expr;
}

BoolExprPtr SqlParser::parsePrimary() {
    if (match(SqlTokenType::LeftParen)) {
        BoolExprPtr expr = parseOr();
        expect(SqlTokenType::RightParen, "')'");
        return expr;
    }
    return parseComparison();
}

BoolExprPtr SqlParser::parseComparison() {
    std::string column = expect(SqlTokenType::Identifier, "column name").text;
    const SqlToken& opTok = expect(SqlTokenType::Comparison, "comparison operator");
    CompOp op = toCompOp(opTok.text);

    if (check(SqlTokenType::Number)) {
        double n = advance().number;
        return std::make_unique<Comparison>(std::move(column), op, Value{n});
    }
    if (check(SqlTokenType::String)) {
        std::string s = advance().text;
        return std::make_unique<Comparison>(std::move(column), op, Value{s});
    }
    throw std::runtime_error("syntax error: expected a number or string literal after comparison");
}

}  // namespace sql
