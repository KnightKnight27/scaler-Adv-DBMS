#include "parser.hpp"

#include <stdexcept>

const Token& Parser::expect(Tok t, const char* what) {
    if (!check(t)) throw std::runtime_error(std::string("parse error: expected ") + what +
                                            " but got '" + peek().text + "'");
    return advance();
}

Statement Parser::parse() {
    Statement stmt;
    switch (peek().type) {
        case Tok::CREATE: stmt = parse_create(); break;
        case Tok::INSERT: stmt = parse_insert(); break;
        case Tok::SELECT: stmt = parse_select(); break;
        case Tok::DELETE: stmt = parse_delete(); break;
        default: throw std::runtime_error("parse error: expected a statement keyword");
    }
    match(Tok::SEMI);  // optional trailing ';'
    if (!check(Tok::END)) throw std::runtime_error("parse error: trailing tokens after statement");
    return stmt;
}

// IDENT, or IDENT '.' IDENT (qualified). On the unqualified form `table` stays "".
void Parser::read_colref(std::string& table, std::string& name) {
    std::string first = expect(Tok::IDENT, "column name").text;
    if (match(Tok::DOT)) {
        table = first;
        name  = expect(Tok::IDENT, "column name after '.'").text;
    } else {
        table.clear();
        name = first;
    }
}

Value Parser::parse_literal_value() {
    if (check(Tok::NUMBER)) return Value{std::stoi(advance().text)};
    if (check(Tok::STRING)) return Value{advance().text};
    throw std::runtime_error("parse error: expected a literal value");
}

CreateStmt Parser::parse_create() {
    expect(Tok::CREATE, "CREATE");
    expect(Tok::TABLE, "TABLE");
    CreateStmt s;
    s.table = expect(Tok::IDENT, "table name").text;
    expect(Tok::LPAREN, "'('");
    s.pk_col = 0;
    bool pk_set = false;
    do {
        Column col;
        col.name = expect(Tok::IDENT, "column name").text;
        if (match(Tok::INT_KW))       col.type = ColumnType::INT;
        else if (match(Tok::TEXT_KW)) col.type = ColumnType::TEXT;
        else throw std::runtime_error("parse error: expected INT or TEXT");
        if (match(Tok::PRIMARY)) {     // optional inline "PRIMARY KEY"
            expect(Tok::KEY, "KEY");
            s.pk_col = static_cast<int>(s.schema.columns.size());
            pk_set = true;
        }
        s.schema.columns.push_back(col);
    } while (match(Tok::COMMA));
    expect(Tok::RPAREN, "')'");
    (void)pk_set;  // if none given, pk defaults to column 0
    return s;
}

InsertStmt Parser::parse_insert() {
    expect(Tok::INSERT, "INSERT");
    expect(Tok::INTO, "INTO");
    InsertStmt s;
    s.table = expect(Tok::IDENT, "table name").text;
    expect(Tok::VALUES, "VALUES");
    expect(Tok::LPAREN, "'('");
    do {
        s.values.push_back(parse_literal_value());
    } while (match(Tok::COMMA));
    expect(Tok::RPAREN, "')'");
    return s;
}

SelectStmt Parser::parse_select() {
    expect(Tok::SELECT, "SELECT");
    SelectStmt s;
    if (match(Tok::STAR)) {
        s.star = true;
    } else {
        do {
            SelectCol c;
            read_colref(c.table, c.name);
            s.columns.push_back(c);
        } while (match(Tok::COMMA));
    }
    expect(Tok::FROM, "FROM");
    s.from = expect(Tok::IDENT, "table name").text;

    if (match(Tok::JOIN)) {
        s.has_join = true;
        s.join_table = expect(Tok::IDENT, "join table name").text;
        expect(Tok::ON, "ON");
        read_colref(s.jl_table, s.jl_col);
        expect(Tok::EQ, "'=' in join condition");
        read_colref(s.jr_table, s.jr_col);
    }

    if (match(Tok::WHERE)) s.where = parse_or();
    return s;
}

DeleteStmt Parser::parse_delete() {
    expect(Tok::DELETE, "DELETE");
    expect(Tok::FROM, "FROM");
    DeleteStmt s;
    s.table = expect(Tok::IDENT, "table name").text;
    if (match(Tok::WHERE)) s.where = parse_or();
    return s;
}

// ── expression grammar ──────────────────────────────────────────────────────

std::unique_ptr<Expr> Parser::parse_or() {
    auto left = parse_and();
    while (match(Tok::OR)) {
        auto node = std::make_unique<BinaryExpr>();
        node->op = "OR";
        node->left = std::move(left);
        node->right = parse_and();
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_and() {
    auto left = parse_cmp();
    while (match(Tok::AND)) {
        auto node = std::make_unique<BinaryExpr>();
        node->op = "AND";
        node->left = std::move(left);
        node->right = parse_cmp();
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_cmp() {
    auto left = parse_primary();
    std::string op;
    switch (peek().type) {
        case Tok::EQ:  op = "=";  break;
        case Tok::NEQ: op = "!="; break;
        case Tok::LT:  op = "<";  break;
        case Tok::GT:  op = ">";  break;
        case Tok::LE:  op = "<="; break;
        case Tok::GE:  op = ">="; break;
        default: return left;  // no comparison operator: just the primary
    }
    advance();
    auto node = std::make_unique<BinaryExpr>();
    node->op = op;
    node->left = std::move(left);
    node->right = parse_primary();
    return node;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    if (match(Tok::LPAREN)) {
        auto e = parse_or();
        expect(Tok::RPAREN, "')'");
        return e;
    }
    if (check(Tok::NUMBER) || check(Tok::STRING)) {
        auto lit = std::make_unique<Literal>();
        lit->val = parse_literal_value();
        return lit;
    }
    if (check(Tok::IDENT)) {
        auto col = std::make_unique<ColumnRef>();
        read_colref(col->table, col->name);
        return col;
    }
    throw std::runtime_error("parse error: expected a column, literal, or '('");
}
