#include "parser/parser.h"
#include <stdexcept>

// ============================================================
// Expr factory helpers
// ============================================================

ExprPtr Expr::MakeLiteral(const Value& v) {
    auto e = std::make_shared<Expr>();
    e->type = LITERAL; e->value = v; return e;
}
ExprPtr Expr::MakeColumnRef(const std::string& table, const std::string& col) {
    auto e = std::make_shared<Expr>();
    e->type = COLUMN_REF; e->table_name = table; e->column_name = col; return e;
}
ExprPtr Expr::MakeCompare(const std::string& op, ExprPtr l, ExprPtr r) {
    auto e = std::make_shared<Expr>();
    e->type = COMPARE; e->op = op; e->left = l; e->right = r; return e;
}
ExprPtr Expr::MakeAnd(ExprPtr l, ExprPtr r) {
    auto e = std::make_shared<Expr>();
    e->type = AND_EXPR; e->left = l; e->right = r; return e;
}

// ============================================================
// Parser — token navigation helpers
// ============================================================

Parser::Parser(const std::vector<Token>& tokens) : tokens_(tokens), pos_(0) {}

const Token& Parser::Current() const { return tokens_[pos_]; }
const Token& Parser::Peek() const { return tokens_[pos_]; }

bool Parser::Check(TokenType type) const { return Current().type == type; }

bool Parser::Match(TokenType type) {
    if (Check(type)) { pos_++; return true; }
    return false;
}

Token Parser::Consume(TokenType expected, const std::string& err_msg) {
    if (Check(expected)) { return tokens_[pos_++]; }
    throw std::runtime_error("Parse error: " + err_msg +
                             " (got '" + Current().value + "' at pos " +
                             std::to_string(Current().position) + ")");
}

// ============================================================
// Parse — dispatch to the right statement parser
// ============================================================

Statement Parser::Parse() {
    if (Check(TokenType::CREATE))     return ParseCreateTable();
    if (Check(TokenType::INSERT))     return ParseInsert();
    if (Check(TokenType::SELECT))     return ParseSelect();
    if (Check(TokenType::DELETE))     return ParseDelete();
    if (Check(TokenType::UPDATE))     return ParseUpdate();
    throw std::runtime_error("Parse error: expected SQL statement, got '" + Current().value + "'");
}

// ============================================================
// CREATE TABLE name (col1 TYPE, col2 TYPE, ..., PRIMARY KEY(col))
// ============================================================

Statement Parser::ParseCreateTable() {
    Statement stmt; stmt.type = StmtType::CREATE_TABLE;
    auto& ct = stmt.create_table;

    Consume(TokenType::CREATE, "expected CREATE");
    Consume(TokenType::TABLE, "expected TABLE");
    ct.table_name = Consume(TokenType::IDENTIFIER, "expected table name").value;
    Consume(TokenType::LPAREN, "expected '('");

    while (!Check(TokenType::RPAREN) && !Check(TokenType::END_OF_INPUT)) {
        // Check for PRIMARY KEY(col)
        if (Check(TokenType::PRIMARY)) {
            Consume(TokenType::PRIMARY, "");
            Consume(TokenType::KEY, "expected KEY");
            Consume(TokenType::LPAREN, "expected '('");
            std::string pk_col = Consume(TokenType::IDENTIFIER, "expected PK column").value;
            Consume(TokenType::RPAREN, "expected ')'");
            // Find the column index
            for (int i = 0; i < (int)ct.columns.size(); i++) {
                if (ct.columns[i].name == pk_col) { ct.pk_index = i; break; }
            }
        } else {
            // Regular column: name TYPE[(length)]
            Column col;
            col.name = Consume(TokenType::IDENTIFIER, "expected column name").value;
            col.type = ParseDataType(col.max_length);
            ct.columns.push_back(col);
        }
        if (!Match(TokenType::COMMA)) break;
    }
    Consume(TokenType::RPAREN, "expected ')'");
    Match(TokenType::SEMICOLON);
    return stmt;
}

DataType Parser::ParseDataType(int& max_len) {
    max_len = 0;
    if (Match(TokenType::INT_TYPE)) return DataType::INT;
    if (Match(TokenType::BOOL_TYPE)) return DataType::BOOL;
    if (Match(TokenType::VARCHAR_TYPE)) {
        Consume(TokenType::LPAREN, "expected '(' after VARCHAR");
        max_len = std::stoi(Consume(TokenType::INT_LITERAL, "expected length").value);
        Consume(TokenType::RPAREN, "expected ')'");
        return DataType::VARCHAR;
    }
    throw std::runtime_error("Parse error: expected data type, got '" + Current().value + "'");
}

// ============================================================
// INSERT INTO table VALUES (v1, v2, ...)
// ============================================================

Statement Parser::ParseInsert() {
    Statement stmt; stmt.type = StmtType::INSERT;
    auto& ins = stmt.insert;

    Consume(TokenType::INSERT, "");
    Consume(TokenType::INTO, "expected INTO");
    ins.table_name = Consume(TokenType::IDENTIFIER, "expected table name").value;
    Consume(TokenType::VALUES, "expected VALUES");
    Consume(TokenType::LPAREN, "expected '('");

    while (!Check(TokenType::RPAREN) && !Check(TokenType::END_OF_INPUT)) {
        ins.values.push_back(ParseLiteralValue());
        if (!Match(TokenType::COMMA)) break;
    }
    Consume(TokenType::RPAREN, "expected ')'");
    Match(TokenType::SEMICOLON);
    return stmt;
}

Value Parser::ParseLiteralValue() {
    if (Match(TokenType::INT_LITERAL)) return Value::Int(std::stoi(tokens_[pos_ - 1].value));
    if (Match(TokenType::STRING_LITERAL)) return Value::Varchar(tokens_[pos_ - 1].value);
    if (Match(TokenType::TRUE_LIT)) return Value::Bool(true);
    if (Match(TokenType::FALSE_LIT)) return Value::Bool(false);
    if (Match(TokenType::NULL_LIT)) return Value::Null(DataType::INT);
    // Handle negative numbers
    if (Check(TokenType::IDENTIFIER) && Current().value == "-") {
        pos_++;
        return Value::Int(-std::stoi(Consume(TokenType::INT_LITERAL, "expected number").value));
    }
    throw std::runtime_error("Parse error: expected literal value, got '" + Current().value + "'");
}

// ============================================================
// SELECT [*|cols] FROM table [JOIN table ON cond] [WHERE cond]
// ============================================================

Statement Parser::ParseSelect() {
    Statement stmt; stmt.type = StmtType::SELECT;
    auto& sel = stmt.select;

    Consume(TokenType::SELECT, "");

    // Column list or *
    if (Match(TokenType::STAR)) {
        sel.select_all = true;
    } else {
        sel.select_all = false;
        do {
            sel.columns.push_back(ParseColumnRef());
        } while (Match(TokenType::COMMA));
    }

    Consume(TokenType::FROM, "expected FROM");
    sel.table_name = Consume(TokenType::IDENTIFIER, "expected table name").value;

    // Optional JOIN
    if (Match(TokenType::JOIN)) {
        JoinClause join;
        join.table_name = Consume(TokenType::IDENTIFIER, "expected join table name").value;
        Consume(TokenType::ON, "expected ON");
        join.left_col = ParseColumnRef();
        Consume(TokenType::EQ, "expected '=' in JOIN ON");
        join.right_col = ParseColumnRef();
        sel.join = join;
    }

    // Optional WHERE
    if (Match(TokenType::WHERE)) {
        sel.where_clause = ParseExpr();
    }

    Match(TokenType::SEMICOLON);
    return stmt;
}

ColumnRef Parser::ParseColumnRef() {
    ColumnRef ref;
    ref.column = Consume(TokenType::IDENTIFIER, "expected column name").value;
    if (Match(TokenType::DOT)) {
        ref.table = ref.column;  // first part was table name
        ref.column = Consume(TokenType::IDENTIFIER, "expected column name after '.'").value;
    }
    return ref;
}

// ============================================================
// DELETE FROM table [WHERE cond]
// ============================================================

Statement Parser::ParseDelete() {
    Statement stmt; stmt.type = StmtType::DELETE_STMT;
    Consume(TokenType::DELETE, "");
    Consume(TokenType::FROM, "expected FROM");
    stmt.delete_stmt.table_name = Consume(TokenType::IDENTIFIER, "expected table name").value;
    if (Match(TokenType::WHERE)) {
        stmt.delete_stmt.where_clause = ParseExpr();
    }
    Match(TokenType::SEMICOLON);
    return stmt;
}

// ============================================================
// UPDATE table SET col=val [, col=val] [WHERE cond]
// ============================================================

Statement Parser::ParseUpdate() {
    Statement stmt; stmt.type = StmtType::UPDATE;
    Consume(TokenType::UPDATE, "");
    stmt.update.table_name = Consume(TokenType::IDENTIFIER, "expected table name").value;
    Consume(TokenType::SET, "expected SET");

    do {
        std::string col = Consume(TokenType::IDENTIFIER, "expected column name").value;
        Consume(TokenType::EQ, "expected '='");
        Value val = ParseLiteralValue();
        stmt.update.assignments.push_back({col, val});
    } while (Match(TokenType::COMMA));

    if (Match(TokenType::WHERE)) {
        stmt.update.where_clause = ParseExpr();
    }
    Match(TokenType::SEMICOLON);
    return stmt;
}

// ============================================================
// Expression parsing (WHERE clause)
//   Expr     → AndExpr (OR AndExpr)*
//   AndExpr  → Compare (AND Compare)*
//   Compare  → Primary (op Primary)?
//   Primary  → literal | column_ref
// ============================================================

ExprPtr Parser::ParseExpr() {
    auto left = ParseAndExpr();
    while (Match(TokenType::OR)) {
        auto right = ParseAndExpr();
        auto or_expr = std::make_shared<Expr>();
        or_expr->type = Expr::OR_EXPR;
        or_expr->left = left; or_expr->right = right;
        left = or_expr;
    }
    return left;
}

ExprPtr Parser::ParseAndExpr() {
    auto left = ParseComparison();
    while (Match(TokenType::AND)) {
        auto right = ParseComparison();
        left = Expr::MakeAnd(left, right);
    }
    return left;
}

ExprPtr Parser::ParseComparison() {
    auto left = ParsePrimary();

    std::string op;
    if (Match(TokenType::EQ))  op = "=";
    else if (Match(TokenType::NEQ)) op = "!=";
    else if (Match(TokenType::LT))  op = "<";
    else if (Match(TokenType::GT))  op = ">";
    else if (Match(TokenType::LTE)) op = "<=";
    else if (Match(TokenType::GTE)) op = ">=";
    else return left;  // no operator → just return the primary

    auto right = ParsePrimary();
    return Expr::MakeCompare(op, left, right);
}

ExprPtr Parser::ParsePrimary() {
    // Literal values
    if (Check(TokenType::INT_LITERAL)) {
        int v = std::stoi(Current().value);
        pos_++;
        return Expr::MakeLiteral(Value::Int(v));
    }
    if (Check(TokenType::STRING_LITERAL)) {
        std::string v = Current().value;
        pos_++;
        return Expr::MakeLiteral(Value::Varchar(v));
    }
    if (Match(TokenType::TRUE_LIT)) return Expr::MakeLiteral(Value::Bool(true));
    if (Match(TokenType::FALSE_LIT)) return Expr::MakeLiteral(Value::Bool(false));
    if (Match(TokenType::NULL_LIT)) return Expr::MakeLiteral(Value::Null(DataType::INT));

    // Column reference: col or table.col
    if (Check(TokenType::IDENTIFIER)) {
        std::string first = Current().value;
        pos_++;
        if (Match(TokenType::DOT)) {
            std::string col = Consume(TokenType::IDENTIFIER, "expected column after '.'").value;
            return Expr::MakeColumnRef(first, col);
        }
        return Expr::MakeColumnRef("", first);
    }

    throw std::runtime_error("Parse error: expected expression, got '" + Current().value + "'");
}
