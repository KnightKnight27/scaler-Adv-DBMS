// ============================================================================
//  parser.hpp — Recursive-descent parser: tokens -> Statement (AST).
//
//  Same technique as Lab 7: one function per grammar rule, so the call stack
//  mirrors the grammar and operator precedence falls out of the nesting
//  (parse_or -> parse_and -> parse_compare). The parser only checks SYNTAX;
//  semantic checks (does the table exist? right column count?) happen later in
//  the executor/catalog, where the schema is known.
// ============================================================================
#pragma once

#include "ast.hpp"
#include "lexer.hpp"

#include <stdexcept>

namespace minidb {

class Parser {
public:
    explicit Parser(const std::string& sql) {
        toks_ = Lexer(sql).tokenize();
    }

    Statement parse() {
        Statement st;
        switch (cur().kind) {
            case Tok::Create: st.kind = Statement::Kind::Create; st.create = parse_create(); break;
            case Tok::Insert: st.kind = Statement::Kind::Insert; st.insert = parse_insert(); break;
            case Tok::Select: st.kind = Statement::Kind::Select; st.select = parse_select(); break;
            case Tok::Delete: st.kind = Statement::Kind::Delete; st.del    = parse_delete(); break;
            default: throw std::runtime_error("parse error: expected a statement keyword");
        }
        expect(Tok::End, "trailing tokens after statement");
        return st;
    }

private:
    // ---- token cursor helpers ----------------------------------------------
    const Token& cur() const { return toks_[pos_]; }
    const Token& advance() { return toks_[pos_++]; }
    bool accept(Tok k) { if (cur().kind == k) { ++pos_; return true; } return false; }
    const Token& expect(Tok k, const char* what) {
        if (cur().kind != k) throw std::runtime_error(std::string("parse error: expected ") + what);
        return advance();
    }

    // CREATE TABLE t (name TYPE, ...)
    CreateStmt parse_create() {
        expect(Tok::Create, "CREATE");
        expect(Tok::Table, "TABLE");
        CreateStmt c;
        c.table = expect(Tok::Ident, "table name").text;
        expect(Tok::LParen, "(");
        do {
            ColumnDef col;
            col.name = expect(Tok::Ident, "column name").text;
            std::string ty = expect(Tok::Ident, "column type").text;
            col.type = parse_type(ty);
            c.columns.push_back(col);
        } while (accept(Tok::Comma));
        expect(Tok::RParen, ")");
        return c;
    }

    // INSERT INTO t [(c1, c2)] VALUES (v1, v2)
    InsertStmt parse_insert() {
        expect(Tok::Insert, "INSERT");
        expect(Tok::Into, "INTO");
        InsertStmt ins;
        ins.table = expect(Tok::Ident, "table name").text;
        if (accept(Tok::LParen)) {                  // optional column list
            do { ins.columns.push_back(expect(Tok::Ident, "column").text); }
            while (accept(Tok::Comma));
            expect(Tok::RParen, ")");
        }
        expect(Tok::Values, "VALUES");
        expect(Tok::LParen, "(");
        do { ins.values.push_back(parse_literal()); } while (accept(Tok::Comma));
        expect(Tok::RParen, ")");
        return ins;
    }

    // SELECT cols FROM t [JOIN u ON a.x = b.y] [WHERE pred]
    SelectStmt parse_select() {
        expect(Tok::Select, "SELECT");
        SelectStmt sel;
        if (accept(Tok::Star)) {
            sel.columns.push_back("*");
        } else {
            do { sel.columns.push_back(parse_colref()); } while (accept(Tok::Comma));
        }
        expect(Tok::From, "FROM");
        sel.table = expect(Tok::Ident, "table name").text;
        if (accept(Tok::Join)) {
            sel.join.present = true;
            sel.join.table = expect(Tok::Ident, "joined table").text;
            expect(Tok::On, "ON");
            sel.join.left_col  = parse_colref();
            expect(Tok::Eq, "= (only equi-join supported)");
            sel.join.right_col = parse_colref();
        }
        if (accept(Tok::Where)) sel.where = parse_or();
        return sel;
    }

    // DELETE FROM t [WHERE pred]
    DeleteStmt parse_delete() {
        expect(Tok::Delete, "DELETE");
        expect(Tok::From, "FROM");
        DeleteStmt d;
        d.table = expect(Tok::Ident, "table name").text;
        if (accept(Tok::Where)) d.where = parse_or();
        return d;
    }

    // ---- predicate grammar (precedence: OR < AND < compare) -----------------
    std::unique_ptr<Expr> parse_or() {
        auto e = parse_and();
        while (accept(Tok::Or)) e = Expr::Logic(Expr::Kind::Or, std::move(e), parse_and());
        return e;
    }
    std::unique_ptr<Expr> parse_and() {
        auto e = parse_compare();
        while (accept(Tok::And)) e = Expr::Logic(Expr::Kind::And, std::move(e), parse_compare());
        return e;
    }
    std::unique_ptr<Expr> parse_compare() {
        if (accept(Tok::LParen)) { auto e = parse_or(); expect(Tok::RParen, ")"); return e; }
        std::string col = parse_colref();
        CmpOp op = parse_cmp_op();
        Value v  = parse_literal();
        return Expr::Cmp(std::move(col), op, std::move(v));
    }

    // a column reference, possibly qualified: ident [ . ident ]
    std::string parse_colref() {
        std::string name = expect(Tok::Ident, "column").text;
        if (accept(Tok::Dot)) { name += "."; name += expect(Tok::Ident, "column").text; }
        return name;
    }

    CmpOp parse_cmp_op() {
        switch (advance().kind) {
            case Tok::Eq: return CmpOp::EQ;
            case Tok::Ne: return CmpOp::NE;
            case Tok::Lt: return CmpOp::LT;
            case Tok::Le: return CmpOp::LE;
            case Tok::Gt: return CmpOp::GT;
            case Tok::Ge: return CmpOp::GE;
            default: throw std::runtime_error("parse error: expected comparison operator");
        }
    }

    Value parse_literal() {
        const Token& t = advance();
        if (t.kind == Tok::Number) return Value::Int(std::stoll(t.text));
        if (t.kind == Tok::String) return Value::Text(t.text);
        throw std::runtime_error("parse error: expected a literal value");
    }

    static ColType parse_type(const std::string& ty) {
        std::string u;
        for (char c : ty) u.push_back((char)std::toupper((unsigned char)c));
        if (u == "INT" || u == "INTEGER") return ColType::INT;
        if (u == "TEXT" || u == "VARCHAR" || u == "STRING") return ColType::TEXT;
        throw std::runtime_error("parse error: unknown column type '" + ty + "'");
    }

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

}  // namespace minidb
