#pragma once
// ---------------------------------------------------------------------------
// parser.h - a hand-written recursive-descent parser.
//
// It consumes the token stream from the lexer and produces a Statement (ast.h).
// The WHERE / ON grammar is the classic precedence ladder:
//     or  ->  and  ->  comparison  ->  ( expr ) | operand OP operand
// which is the structured equivalent of the Shunting-Yard approach from Lab 5.
// ---------------------------------------------------------------------------
#include "lexer.h"
#include "ast.h"
#include <stdexcept>

namespace minidb {

class Parser {
public:
    explicit Parser(std::string sql) {
        Lexer lx(std::move(sql));
        toks_ = lx.tokenize();
    }

    Statement parse() {
        Statement st;
        bool explain = false;
        if (peek().type == Tok::EXPLAIN) { advance(); explain = true; }

        switch (peek().type) {
            case Tok::CREATE: st.type = StmtType::Create; st.create = parse_create(); break;
            case Tok::INSERT: st.type = StmtType::Insert; st.insert = parse_insert(); break;
            case Tok::SELECT: st.type = StmtType::Select; st.select = parse_select(); st.select.explain = explain; break;
            case Tok::DELETE: st.type = StmtType::Delete; st.del = parse_delete(); break;
            case Tok::BEGIN:  advance(); st.type = StmtType::Begin;  break;
            case Tok::COMMIT: advance(); st.type = StmtType::Commit; break;
            case Tok::ABORT:  advance(); st.type = StmtType::Abort;  break;
            default: throw std::runtime_error("unsupported or empty statement");
        }
        return st;
    }

private:
    CreateStmt parse_create() {
        expect(Tok::CREATE); expect(Tok::TABLE);
        CreateStmt c;
        c.table = expect(Tok::IDENT).text;
        expect(Tok::LPAREN);
        while (true) {
            Column col;
            col.name = expect(Tok::IDENT).text;
            if (peek().type == Tok::INT)  { advance(); col.type = Type::INT; }
            else if (peek().type == Tok::TEXT) { advance(); col.type = Type::TEXT; }
            else throw std::runtime_error("expected column type INT or TEXT");
            if (peek().type == Tok::PRIMARY) { advance(); expect(Tok::KEY); col.is_primary_key = true; }
            c.schema.push_back(col);
            if (peek().type == Tok::COMMA) { advance(); continue; }
            break;
        }
        expect(Tok::RPAREN);
        return c;
    }

    InsertStmt parse_insert() {
        expect(Tok::INSERT); expect(Tok::INTO);
        InsertStmt ins;
        ins.table = expect(Tok::IDENT).text;
        expect(Tok::VALUES); expect(Tok::LPAREN);
        while (true) {
            if (peek().type == Tok::NUMBER)      ins.values.push_back((int64_t)std::stoll(advance().text));
            else if (peek().type == Tok::STRING) ins.values.push_back(advance().text);
            else throw std::runtime_error("expected a literal value");
            if (peek().type == Tok::COMMA) { advance(); continue; }
            break;
        }
        expect(Tok::RPAREN);
        return ins;
    }

    SelectStmt parse_select() {
        expect(Tok::SELECT);
        SelectStmt s;
        if (peek().type == Tok::STAR) { advance(); /* columns empty == * */ }
        else {
            while (true) {
                s.columns.push_back(parse_colref_name());
                if (peek().type == Tok::COMMA) { advance(); continue; }
                break;
            }
        }
        expect(Tok::FROM);
        s.table = expect(Tok::IDENT).text;
        if (peek().type == Tok::JOIN) {
            advance();
            s.has_join = true;
            s.join_table = expect(Tok::IDENT).text;
            expect(Tok::ON);
            s.join_cond = parse_comparison();
        }
        if (peek().type == Tok::WHERE) { advance(); s.where = parse_expr(); }
        return s;
    }

    DeleteStmt parse_delete() {
        expect(Tok::DELETE); expect(Tok::FROM);
        DeleteStmt d;
        d.table = expect(Tok::IDENT).text;
        if (peek().type == Tok::WHERE) { advance(); d.where = parse_expr(); }
        return d;
    }

    // ---- expression grammar ----
    ExprPtr parse_expr() { return parse_or(); }

    ExprPtr parse_or() {
        ExprPtr left = parse_and();
        while (peek().type == Tok::OR) { advance(); left = Expr::bin("OR", left, parse_and()); }
        return left;
    }

    ExprPtr parse_and() {
        ExprPtr left = parse_primary();
        while (peek().type == Tok::AND) { advance(); left = Expr::bin("AND", left, parse_primary()); }
        return left;
    }

    ExprPtr parse_primary() {
        if (peek().type == Tok::LPAREN) {
            advance(); ExprPtr e = parse_expr(); expect(Tok::RPAREN); return e;
        }
        return parse_comparison();
    }

    ExprPtr parse_comparison() {
        ExprPtr left = parse_operand();
        std::string op = comparison_op();
        ExprPtr right = parse_operand();
        return Expr::bin(op, left, right);
    }

    ExprPtr parse_operand() {
        if (peek().type == Tok::NUMBER) return Expr::lit((int64_t)std::stoll(advance().text));
        if (peek().type == Tok::STRING) return Expr::lit(advance().text);
        // column reference, optionally table.column
        std::string a = expect(Tok::IDENT).text;
        if (peek().type == Tok::DOT) { advance(); std::string b = expect(Tok::IDENT).text; return Expr::col(a, b); }
        return Expr::col("", a);
    }

    std::string parse_colref_name() {
        std::string a = expect(Tok::IDENT).text;
        if (peek().type == Tok::DOT) { advance(); std::string b = expect(Tok::IDENT).text; return a + "." + b; }
        return a;
    }

    std::string comparison_op() {
        switch (peek().type) {
            case Tok::EQ:  advance(); return "=";
            case Tok::NEQ: advance(); return "!=";
            case Tok::LT:  advance(); return "<";
            case Tok::GT:  advance(); return ">";
            case Tok::LE:  advance(); return "<=";
            case Tok::GE:  advance(); return ">=";
            default: throw std::runtime_error("expected a comparison operator");
        }
    }

    // ---- token helpers ----
    const Token& peek() const { return toks_[pos_]; }
    Token advance() { return toks_[pos_++]; }
    Token expect(Tok t) {
        if (peek().type != t) throw std::runtime_error("unexpected token '" + peek().text + "'");
        return advance();
    }

    std::vector<Token> toks_;
    size_t pos_ = 0;
};

} // namespace minidb
