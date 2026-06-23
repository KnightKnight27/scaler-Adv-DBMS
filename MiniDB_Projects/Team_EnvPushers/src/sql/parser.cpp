#include "sql/parser.hpp"

#include <algorithm>
#include <stdexcept>

namespace minidb {

Parser::Parser(const std::string& sql) {
    Lexer lex(sql);
    toks_ = lex.tokenize();
}

std::string Parser::upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

const Token& Parser::peek(int n) const {
    size_t i = pos_ + n;
    return i < toks_.size() ? toks_[i] : toks_.back();  // back() is END
}

bool Parser::match(TokKind k) {
    if (check(k)) { pos_++; return true; }
    return false;
}

Token Parser::expect(TokKind k, const char* what) {
    if (!check(k)) throw std::runtime_error(std::string("parse error: expected ") + what);
    return advance();
}

bool Parser::is_kw(const std::string& kw) const {
    return cur().kind == TokKind::IDENT && upper(cur().text) == kw;
}

bool Parser::match_kw(const std::string& kw) {
    if (is_kw(kw)) { pos_++; return true; }
    return false;
}

void Parser::expect_kw(const std::string& kw) {
    if (!match_kw(kw)) throw std::runtime_error("parse error: expected keyword " + kw);
}

std::unique_ptr<Statement> Parser::parse() {
    if (match_kw("CREATE")) return parse_create();
    if (match_kw("INSERT")) return parse_insert();
    if (match_kw("SELECT")) return parse_select();
    if (match_kw("DELETE")) return parse_delete();
    if (match_kw("UPDATE")) return parse_update();
    if (match_kw("BEGIN"))  return std::make_unique<TxnStmt>(StmtKind::BEGIN);
    if (match_kw("COMMIT")) return std::make_unique<TxnStmt>(StmtKind::COMMIT);
    if (match_kw("ABORT") || match_kw("ROLLBACK"))
        return std::make_unique<TxnStmt>(StmtKind::ABORT);
    throw std::runtime_error("parse error: unknown statement");
}

std::unique_ptr<Statement> Parser::parse_create() {
    expect_kw("TABLE");
    auto stmt = std::make_unique<CreateTableStmt>();
    stmt->table = expect(TokKind::IDENT, "table name").text;
    expect(TokKind::LPAREN, "(");
    do {
        Column col;
        col.name = expect(TokKind::IDENT, "column name").text;
        std::string ty = upper(expect(TokKind::IDENT, "column type").text);
        if (ty == "INT" || ty == "INTEGER") col.type = TypeId::INTEGER;
        else if (ty == "TEXT" || ty == "VARCHAR" || ty == "STRING") col.type = TypeId::TEXT;
        else throw std::runtime_error("parse error: unknown type " + ty);
        // VARCHAR(n) optional length, ignored
        if (match(TokKind::LPAREN)) { expect(TokKind::INT_LIT, "length"); expect(TokKind::RPAREN, ")"); }
        if (match_kw("PRIMARY")) { expect_kw("KEY"); col.is_primary_key = true; }
        stmt->columns.push_back(col);
    } while (match(TokKind::COMMA));
    expect(TokKind::RPAREN, ")");
    match(TokKind::SEMICOLON);
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_insert() {
    expect_kw("INTO");
    auto stmt = std::make_unique<InsertStmt>();
    stmt->table = expect(TokKind::IDENT, "table name").text;
    if (match(TokKind::LPAREN)) {
        do { stmt->columns.push_back(expect(TokKind::IDENT, "column").text); }
        while (match(TokKind::COMMA));
        expect(TokKind::RPAREN, ")");
    }
    expect_kw("VALUES");
    do {
        expect(TokKind::LPAREN, "(");
        std::vector<Value> row;
        do { row.push_back(parse_value()); } while (match(TokKind::COMMA));
        expect(TokKind::RPAREN, ")");
        stmt->rows.push_back(std::move(row));
    } while (match(TokKind::COMMA));
    match(TokKind::SEMICOLON);
    return stmt;
}

Value Parser::parse_value() {
    if (check(TokKind::INT_LIT)) return Value::Int(advance().int_val);
    if (check(TokKind::STR_LIT)) return Value::Text(advance().text);
    if (is_kw("NULL")) { advance(); return Value::Null(); }
    throw std::runtime_error("parse error: expected literal value");
}

SelectItem Parser::parse_select_item() {
    SelectItem item;
    if (check(TokKind::STAR)) { advance(); item.is_star = true; return item; }

    // aggregate?  COUNT(*) / SUM(col) / ...
    if (cur().kind == TokKind::IDENT) {
        std::string fn = upper(cur().text);
        AggFunc af = AggFunc::NONE;
        if (fn == "COUNT") af = AggFunc::COUNT;
        else if (fn == "SUM") af = AggFunc::SUM;
        else if (fn == "MIN") af = AggFunc::MIN;
        else if (fn == "MAX") af = AggFunc::MAX;
        else if (fn == "AVG") af = AggFunc::AVG;
        if (af != AggFunc::NONE && peek().kind == TokKind::LPAREN) {
            advance();  // function name
            advance();  // (
            item.agg = af;
            if (match(TokKind::STAR)) item.agg_star = true;
            else {
                std::string c1 = expect(TokKind::IDENT, "aggregate arg").text;
                if (match(TokKind::DOT)) { item.table = c1; item.column = expect(TokKind::IDENT, "column").text; }
                else item.column = c1;
            }
            expect(TokKind::RPAREN, ")");
            item.alias = fn + "_" + (item.agg_star ? "star" : item.column);
            if (match_kw("AS")) item.alias = expect(TokKind::IDENT, "alias").text;
            return item;
        }
    }

    // plain column, optionally table-qualified
    std::string first = expect(TokKind::IDENT, "column").text;
    if (match(TokKind::DOT)) { item.table = first; item.column = expect(TokKind::IDENT, "column").text; }
    else item.column = first;
    item.alias = item.column;
    if (match_kw("AS")) item.alias = expect(TokKind::IDENT, "alias").text;
    return item;
}

TableRef Parser::parse_table_ref() {
    TableRef ref;
    ref.name = expect(TokKind::IDENT, "table name").text;
    if (match_kw("AS")) ref.alias = expect(TokKind::IDENT, "alias").text;
    else if (cur().kind == TokKind::IDENT && !is_kw("JOIN") && !is_kw("ON") &&
             !is_kw("WHERE") && !is_kw("GROUP") && !is_kw("ORDER") && !is_kw("INNER"))
        ref.alias = advance().text;  // implicit alias: FROM t x
    if (ref.alias.empty()) ref.alias = ref.name;
    return ref;
}

std::unique_ptr<Statement> Parser::parse_select() {
    auto stmt = std::make_unique<SelectStmt>();
    do { stmt->items.push_back(parse_select_item()); } while (match(TokKind::COMMA));

    expect_kw("FROM");
    stmt->from = parse_table_ref();

    // JOIN clauses (also accept comma-separated cross joins)
    while (is_kw("JOIN") || is_kw("INNER") || check(TokKind::COMMA)) {
        JoinClause jc;
        if (match(TokKind::COMMA)) {
            jc.table = parse_table_ref();
            jc.on = nullptr;  // condition (if any) comes from WHERE
        } else {
            match_kw("INNER");
            expect_kw("JOIN");
            jc.table = parse_table_ref();
            if (match_kw("ON")) jc.on = parse_expr();
        }
        stmt->joins.push_back(std::move(jc));
    }

    if (match_kw("WHERE")) stmt->where = parse_expr();

    if (match_kw("GROUP")) {
        expect_kw("BY");
        do {
            std::string t, c;
            std::string first = expect(TokKind::IDENT, "group column").text;
            if (match(TokKind::DOT)) { t = first; c = expect(TokKind::IDENT, "column").text; }
            else c = first;
            stmt->group_by.emplace_back(t, c);
        } while (match(TokKind::COMMA));
    }

    if (match_kw("ORDER")) {
        expect_kw("BY");
        stmt->has_order_by = true;
        std::string first = expect(TokKind::IDENT, "order column").text;
        if (match(TokKind::DOT)) { stmt->order_by.table = first; stmt->order_by.column = expect(TokKind::IDENT, "column").text; }
        else stmt->order_by.column = first;
        if (match_kw("DESC")) stmt->order_by.desc = true;
        else match_kw("ASC");
    }

    match(TokKind::SEMICOLON);
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_delete() {
    expect_kw("FROM");
    auto stmt = std::make_unique<DeleteStmt>();
    stmt->table = expect(TokKind::IDENT, "table name").text;
    if (match_kw("WHERE")) stmt->where = parse_expr();
    match(TokKind::SEMICOLON);
    return stmt;
}

std::unique_ptr<Statement> Parser::parse_update() {
    auto stmt = std::make_unique<UpdateStmt>();
    stmt->table = expect(TokKind::IDENT, "table name").text;
    expect_kw("SET");
    do {
        std::string col = expect(TokKind::IDENT, "column").text;
        expect(TokKind::EQ, "=");
        stmt->assignments.emplace_back(col, parse_value());
    } while (match(TokKind::COMMA));
    if (match_kw("WHERE")) stmt->where = parse_expr();
    match(TokKind::SEMICOLON);
    return stmt;
}

// ---- expressions ----------------------------------------------------------
ExprPtr Parser::parse_expr() {
    ExprPtr left = parse_and();
    while (match_kw("OR")) left = Expr::Binary(BinOp::OR, left, parse_and());
    return left;
}

ExprPtr Parser::parse_and() {
    ExprPtr left = parse_comparison();
    while (match_kw("AND")) left = Expr::Binary(BinOp::AND, left, parse_comparison());
    return left;
}

ExprPtr Parser::parse_comparison() {
    ExprPtr left = parse_primary();
    BinOp op;
    switch (cur().kind) {
        case TokKind::EQ: op = BinOp::EQ; break;
        case TokKind::NE: op = BinOp::NE; break;
        case TokKind::LT: op = BinOp::LT; break;
        case TokKind::LE: op = BinOp::LE; break;
        case TokKind::GT: op = BinOp::GT; break;
        case TokKind::GE: op = BinOp::GE; break;
        default: return left;  // not a comparison
    }
    advance();
    return Expr::Binary(op, left, parse_primary());
}

ExprPtr Parser::parse_primary() {
    if (match(TokKind::LPAREN)) {
        ExprPtr e = parse_expr();
        expect(TokKind::RPAREN, ")");
        return e;
    }
    if (check(TokKind::INT_LIT)) return Expr::Literal(Value::Int(advance().int_val));
    if (check(TokKind::STR_LIT)) return Expr::Literal(Value::Text(advance().text));
    if (is_kw("NULL")) { advance(); return Expr::Literal(Value::Null()); }
    if (check(TokKind::IDENT)) {
        std::string first = advance().text;
        if (match(TokKind::DOT))
            return Expr::Column(first, expect(TokKind::IDENT, "column").text);
        return Expr::Column("", first);
    }
    throw std::runtime_error("parse error: expected expression");
}

}  // namespace minidb
