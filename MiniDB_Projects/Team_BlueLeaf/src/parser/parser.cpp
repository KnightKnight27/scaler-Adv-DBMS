#include "parser/parser.h"

#include <cctype>

#include "common/exception.h"

namespace minidb {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
bool is_comparison(TokenType t) {
    return t == TokenType::EQ || t == TokenType::NEQ || t == TokenType::LT ||
           t == TokenType::GT || t == TokenType::LTE || t == TokenType::GTE;
}
std::string op_text(TokenType t) {
    switch (t) {
        case TokenType::EQ:  return "=";
        case TokenType::NEQ: return "!=";
        case TokenType::LT:  return "<";
        case TokenType::GT:  return ">";
        case TokenType::LTE: return "<=";
        case TokenType::GTE: return ">=";
        default:             return "?";
    }
}
} // namespace

const Token& Parser::expect(TokenType t, const char* what) {
    if (!check(t)) throw DBException(std::string("Parser: expected ") + what);
    return advance();
}

StmtPtr Parser::parse() {
    switch (peek().type) {
        case TokenType::CREATE: return parse_create();
        case TokenType::INSERT: return parse_insert();
        case TokenType::DELETE: return parse_delete();
        case TokenType::SELECT: return parse_select();
        default: throw DBException("Parser: expected a statement (CREATE/INSERT/DELETE/SELECT)");
    }
}

StmtPtr Parser::parse_create() {
    advance();  // CREATE
    expect(TokenType::TABLE, "TABLE");
    auto stmt = std::make_unique<CreateTableStmt>();
    stmt->table = expect(TokenType::IDENTIFIER, "table name").text;
    expect(TokenType::LPAREN, "(");

    for (;;) {
        std::string col = expect(TokenType::IDENTIFIER, "column name").text;
        Column c;
        c.name = col;
        if (match(TokenType::INT_TYPE)) { c.type = ValueType::INT; c.length = 8; }
        else if (match(TokenType::DOUBLE_TYPE)) { c.type = ValueType::DOUBLE; c.length = 8; }
        else if (match(TokenType::VARCHAR_TYPE)) {
            c.type = ValueType::VARCHAR; c.length = 255;
            if (match(TokenType::LPAREN)) {
                c.length = static_cast<std::uint16_t>(std::stoi(expect(TokenType::NUMBER, "length").text));
                expect(TokenType::RPAREN, ")");
            }
        } else {
            throw DBException("Parser: expected a column type for '" + col + "'");
        }
        if (match(TokenType::PRIMARY)) {
            expect(TokenType::KEY, "KEY");
            stmt->primary_key = static_cast<int>(stmt->columns.size());
        }
        stmt->columns.push_back(c);
        if (!match(TokenType::COMMA)) break;
    }
    expect(TokenType::RPAREN, ")");
    match(TokenType::SEMICOLON);
    return stmt;
}

StmtPtr Parser::parse_insert() {
    advance();  // INSERT
    expect(TokenType::INTO, "INTO");
    auto stmt = std::make_unique<InsertStmt>();
    stmt->table = expect(TokenType::IDENTIFIER, "table name").text;
    expect(TokenType::VALUES, "VALUES");

    for (;;) {
        expect(TokenType::LPAREN, "(");
        std::vector<Value> row;
        for (;;) {
            row.push_back(parse_value());
            if (!match(TokenType::COMMA)) break;
        }
        expect(TokenType::RPAREN, ")");
        stmt->rows.push_back(std::move(row));
        if (!match(TokenType::COMMA)) break;  // another (...) tuple
    }
    match(TokenType::SEMICOLON);
    return stmt;
}

StmtPtr Parser::parse_delete() {
    advance();  // DELETE
    expect(TokenType::FROM, "FROM");
    auto stmt = std::make_unique<DeleteStmt>();
    stmt->table = expect(TokenType::IDENTIFIER, "table name").text;
    if (match(TokenType::WHERE)) stmt->where = parse_expr();
    match(TokenType::SEMICOLON);
    return stmt;
}

StmtPtr Parser::parse_select() {
    advance();  // SELECT
    auto stmt = std::make_unique<SelectStmt>();
    parse_select_list(*stmt);

    expect(TokenType::FROM, "FROM");
    stmt->from_table = expect(TokenType::IDENTIFIER, "table name").text;
    if (check(TokenType::IDENTIFIER)) stmt->from_alias = advance().text;

    if (match(TokenType::JOIN)) {
        stmt->join_table = expect(TokenType::IDENTIFIER, "join table").text;
        if (check(TokenType::IDENTIFIER)) stmt->join_alias = advance().text;
        expect(TokenType::ON, "ON");
        stmt->join_on = parse_expr();
    }
    if (match(TokenType::WHERE)) stmt->where = parse_expr();
    if (match(TokenType::GROUP)) {
        expect(TokenType::BY, "BY");
        for (;;) {
            stmt->group_by.push_back(parse_column_name());
            if (!match(TokenType::COMMA)) break;
        }
    }
    match(TokenType::SEMICOLON);
    return stmt;
}

void Parser::parse_select_list(SelectStmt& stmt) {
    if (match(TokenType::STAR)) { stmt.columns.push_back("*"); return; }
    for (;;) {
        TokenType t = peek().type;
        if (t == TokenType::COUNT || t == TokenType::SUM || t == TokenType::AVG ||
            t == TokenType::MIN || t == TokenType::MAX) {
            AggCall agg;
            agg.func = upper(advance().text);
            expect(TokenType::LPAREN, "(");
            agg.column = match(TokenType::STAR) ? "*" : parse_column_name();
            expect(TokenType::RPAREN, ")");
            stmt.aggregates.push_back(std::move(agg));
        } else {
            stmt.columns.push_back(parse_column_name());
        }
        if (!match(TokenType::COMMA)) break;
    }
}

std::string Parser::parse_column_name() {
    std::string name = expect(TokenType::IDENTIFIER, "column name").text;
    if (match(TokenType::DOT)) {
        std::string col = expect(TokenType::IDENTIFIER, "column after '.'").text;
        return name + "." + col;
    }
    return name;
}

Value Parser::parse_value() {
    if (check(TokenType::NUMBER)) {
        std::string t = advance().text;
        if (t.find('.') != std::string::npos) return Value{std::stod(t)};
        return Value{static_cast<std::int64_t>(std::stoll(t))};
    }
    if (check(TokenType::STRING)) return Value{advance().text};
    throw DBException("Parser: expected a literal value");
}

ExprPtr Parser::parse_expr() { return parse_or(); }

ExprPtr Parser::parse_or() {
    ExprPtr left = parse_and();
    while (match(TokenType::OR)) {
        ExprPtr right = parse_and();
        left = std::make_unique<BinaryExpr>("OR", std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parse_and() {
    ExprPtr left = parse_comparison();
    while (match(TokenType::AND)) {
        ExprPtr right = parse_comparison();
        left = std::make_unique<BinaryExpr>("AND", std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parse_comparison() {
    ExprPtr left = parse_primary();
    if (is_comparison(peek().type)) {
        std::string op = op_text(peek().type);
        advance();
        ExprPtr right = parse_primary();
        return std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parse_primary() {
    if (match(TokenType::LPAREN)) {
        ExprPtr e = parse_expr();
        expect(TokenType::RPAREN, ")");
        return e;
    }
    if (check(TokenType::NUMBER) || check(TokenType::STRING)) {
        return std::make_unique<LiteralExpr>(parse_value());
    }
    if (check(TokenType::IDENTIFIER)) {
        std::string name = advance().text;
        if (match(TokenType::DOT)) {
            std::string col = expect(TokenType::IDENTIFIER, "column after '.'").text;
            return std::make_unique<ColumnExpr>(name, col);
        }
        return std::make_unique<ColumnExpr>("", name);
    }
    throw DBException("Parser: expected an expression");
}

} // namespace minidb
