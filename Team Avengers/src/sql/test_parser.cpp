// M2 test — SQL parser. Verifies each statement type parses into the expected
// AST shape, that operator precedence (AND binds tighter than OR) is correct,
// and that malformed input is rejected.
#include "parser.hpp"
#include <cassert>
#include <cstdio>

using namespace minidb;

int main() {
    // CREATE
    {
        Statement s = Parser("CREATE TABLE users (id INT, name TEXT, age INT)").parse();
        assert(s.kind == Statement::Kind::Create);
        assert(s.create.table == "users");
        assert(s.create.columns.size() == 3);
        assert(s.create.columns[1].name == "name" && s.create.columns[1].type == ColType::TEXT);
    }
    // INSERT with explicit columns + a string literal
    {
        Statement s = Parser("INSERT INTO users (id, name, age) VALUES (1, 'alice', 30)").parse();
        assert(s.kind == Statement::Kind::Insert);
        assert(s.insert.values.size() == 3);
        assert(s.insert.values[1].type == ColType::TEXT && s.insert.values[1].s == "alice");
        assert(s.insert.values[2].i == 30);
    }
    // SELECT * with a JOIN
    {
        Statement s = Parser(
            "SELECT * FROM users JOIN orders ON users.id = orders.uid").parse();
        assert(s.kind == Statement::Kind::Select);
        assert(s.select.columns.size() == 1 && s.select.columns[0] == "*");
        assert(s.select.join.present);
        assert(s.select.join.left_col == "users.id" && s.select.join.right_col == "orders.uid");
    }
    // WHERE precedence:  a=1 OR b=2 AND c=3   parses as  a=1 OR (b=2 AND c=3)
    {
        Statement s = Parser("SELECT id FROM t WHERE a = 1 OR b = 2 AND c = 3").parse();
        Expr* root = s.select.where.get();
        assert(root->kind == Expr::Kind::Or);
        assert(root->lhs->kind == Expr::Kind::Compare);     // a = 1
        assert(root->rhs->kind == Expr::Kind::And);          // (b=2 AND c=3)
        std::printf("[M2] WHERE precedence: OR binds looser than AND  OK\n");
    }
    // DELETE with a range predicate
    {
        Statement s = Parser("DELETE FROM users WHERE age >= 65").parse();
        assert(s.kind == Statement::Kind::Delete);
        assert(s.del.where->op == CmpOp::GE && s.del.where->literal.i == 65);
    }
    // malformed input must throw
    {
        bool threw = false;
        try { Parser("SELECT FROM").parse(); } catch (const std::exception&) { threw = true; }
        assert(threw && "missing column list should be a parse error");
    }

    std::printf("[M2] SQL parser: ALL CHECKS PASSED\n");
    return 0;
}
