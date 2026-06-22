#include "catch.hpp"

#include <string>

#include "parser/ast.h"
#include "parser/parser.h"
#include "catalog/value.h"

using namespace axiomdb;

// Small helpers to keep the assertions readable.
namespace {

// Parse and require success, returning the statement for further inspection.
StmtPtr parse_ok(const std::string& sql) {
  ParseResult r = parse_sql(sql);
  INFO("SQL: " << sql);
  INFO("error: " << r.error);
  REQUIRE(r.ok());
  REQUIRE(r.error.empty());
  return std::move(r.statement);
}

// Parse and require a graceful error (null statement + non-empty message).
void parse_err(const std::string& sql) {
  ParseResult r = parse_sql(sql);
  INFO("SQL: " << sql);
  CHECK_FALSE(r.ok());
  CHECK(r.statement == nullptr);
  CHECK_FALSE(r.error.empty());
}

}  // namespace

// ===========================================================================
// CREATE TABLE
// ===========================================================================

TEST_CASE("CREATE TABLE basic and primary key", "[parser]") {
  auto stmt = parse_ok("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)");
  REQUIRE(stmt->kind == StmtKind::CreateTable);
  auto* c = static_cast<CreateTableStmt*>(stmt.get());
  CHECK(c->table == "users");
  CHECK_FALSE(c->if_not_exists);
  REQUIRE(c->columns.size() == 2);
  CHECK(c->columns[0].name == "id");
  CHECK(c->columns[0].type == TypeId::Integer);
  CHECK(c->columns[0].primary_key);
  CHECK(c->columns[1].name == "name");
  CHECK(c->columns[1].type == TypeId::Varchar);
  CHECK_FALSE(c->columns[1].primary_key);
}

TEST_CASE("CREATE TABLE IF NOT EXISTS and all types", "[parser]") {
  auto stmt = parse_ok(
      "create table IF NOT EXISTS t (a INTEGER, b DOUBLE, c BOOLEAN, d TEXT)");
  auto* c = static_cast<CreateTableStmt*>(stmt.get());
  CHECK(c->if_not_exists);
  REQUIRE(c->columns.size() == 4);
  CHECK(c->columns[0].type == TypeId::Integer);
  CHECK(c->columns[1].type == TypeId::Double);
  CHECK(c->columns[2].type == TypeId::Boolean);
  CHECK(c->columns[3].type == TypeId::Varchar);
}

TEST_CASE("CREATE TABLE rejects unknown type and empty column list", "[parser]") {
  parse_err("CREATE TABLE t (id WIDGET)");
  parse_err("CREATE TABLE t ()");
  parse_err("CREATE TABLE t");
  parse_err("CREATE TABLE (id INT)");  // missing name
}

// ===========================================================================
// INSERT
// ===========================================================================

TEST_CASE("INSERT with explicit columns", "[parser]") {
  auto stmt = parse_ok("INSERT INTO t (a, b) VALUES (1, 'hi')");
  auto* ins = static_cast<InsertStmt*>(stmt.get());
  CHECK(ins->table == "t");
  REQUIRE(ins->columns.size() == 2);
  CHECK(ins->columns[0] == "a");
  CHECK(ins->columns[1] == "b");
  REQUIRE(ins->rows.size() == 1);
  REQUIRE(ins->rows[0].size() == 2);
}

TEST_CASE("INSERT multi-row without column list", "[parser]") {
  auto stmt = parse_ok("INSERT INTO t VALUES (1, 2), (3, 4), (5, 6)");
  auto* ins = static_cast<InsertStmt*>(stmt.get());
  CHECK(ins->columns.empty());
  REQUIRE(ins->rows.size() == 3);
  for (auto& row : ins->rows) REQUIRE(row.size() == 2);
  // First value of the first row is the integer literal 1.
  auto* lit = static_cast<LiteralExpr*>(ins->rows[0][0].get());
  REQUIRE(lit->kind == ExprKind::Literal);
  CHECK(lit->value.type() == TypeId::Integer);
  CHECK(lit->value.as_integer() == 1);
}

TEST_CASE("INSERT malformed", "[parser]") {
  parse_err("INSERT INTO t VALUES");
  parse_err("INSERT INTO t VALUES ()");        // empty tuple -> no expr
  parse_err("INSERT INTO t (a, b VALUES (1)");  // missing ')'
  parse_err("INSERT t VALUES (1)");             // missing INTO
}

// ===========================================================================
// SELECT shapes
// ===========================================================================

TEST_CASE("SELECT star", "[parser]") {
  auto stmt = parse_ok("SELECT * FROM t");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  REQUIRE(sel->items.size() == 1);
  CHECK(sel->items[0].star);
  CHECK(sel->items[0].star_table.empty());
  CHECK(sel->from.table == "t");
  CHECK(sel->from.alias.empty());
  CHECK(sel->joins.empty());
  CHECK(sel->where == nullptr);
}

TEST_CASE("SELECT qualified star and aliases", "[parser]") {
  auto stmt = parse_ok("SELECT u.*, u.id AS uid, name FROM users AS u");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  REQUIRE(sel->items.size() == 3);
  CHECK(sel->items[0].star);
  CHECK(sel->items[0].star_table == "u");
  CHECK_FALSE(sel->items[1].star);
  CHECK(sel->items[1].alias == "uid");
  auto* col = static_cast<ColumnRefExpr*>(sel->items[1].expr.get());
  CHECK(col->table == "u");
  CHECK(col->column == "id");
  // Unqualified column ref.
  auto* col2 = static_cast<ColumnRefExpr*>(sel->items[2].expr.get());
  CHECK(col2->table.empty());
  CHECK(col2->column == "name");
  CHECK(sel->from.alias == "u");
}

TEST_CASE("SELECT implicit table alias without AS", "[parser]") {
  auto stmt = parse_ok("SELECT x FROM t alias WHERE x > 0");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  CHECK(sel->from.table == "t");
  CHECK(sel->from.alias == "alias");
  REQUIRE(sel->where != nullptr);
}

TEST_CASE("SELECT with WHERE expression", "[parser]") {
  auto stmt = parse_ok("SELECT * FROM t WHERE a = 5 AND b > 3");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  REQUIRE(sel->where != nullptr);
  CHECK(expr_to_string(sel->where.get()) == "((a = 5) AND (b > 3))");
}

TEST_CASE("SELECT multi-join", "[parser]") {
  auto stmt = parse_ok(
      "SELECT a.x, b.y, c.z FROM a "
      "JOIN b ON a.id = b.aid "
      "INNER JOIN c AS cc ON b.id = cc.bid "
      "WHERE a.x > 10");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  CHECK(sel->from.table == "a");
  REQUIRE(sel->joins.size() == 2);
  CHECK(sel->joins[0].right.table == "b");
  CHECK(sel->joins[0].right.alias.empty());
  REQUIRE(sel->joins[0].on != nullptr);
  CHECK(expr_to_string(sel->joins[0].on.get()) == "(a.id = b.aid)");
  CHECK(sel->joins[1].right.table == "c");
  CHECK(sel->joins[1].right.alias == "cc");
  CHECK(expr_to_string(sel->joins[1].on.get()) == "(b.id = cc.bid)");
}

TEST_CASE("SELECT malformed", "[parser]") {
  parse_err("SELECT FROM t");          // empty select list
  parse_err("SELECT * t");             // missing FROM
  parse_err("SELECT * FROM");          // missing table
  parse_err("SELECT * FROM a JOIN b"); // JOIN without ON
}

// ===========================================================================
// DELETE
// ===========================================================================

TEST_CASE("DELETE with and without WHERE", "[parser]") {
  auto all = parse_ok("DELETE FROM t");
  auto* d1 = static_cast<DeleteStmt*>(all.get());
  CHECK(d1->table == "t");
  CHECK(d1->where == nullptr);

  auto some = parse_ok("DELETE FROM t WHERE id = 7");
  auto* d2 = static_cast<DeleteStmt*>(some.get());
  REQUIRE(d2->where != nullptr);
  CHECK(expr_to_string(d2->where.get()) == "(id = 7)");
}

TEST_CASE("DELETE malformed", "[parser]") {
  parse_err("DELETE t");            // missing FROM
  parse_err("DELETE FROM");         // missing table
  parse_err("DELETE FROM t WHERE"); // missing predicate
}

// ===========================================================================
// Transactions
// ===========================================================================

TEST_CASE("transaction statements", "[parser]") {
  CHECK(parse_ok("BEGIN")->kind == StmtKind::Begin);
  CHECK(parse_ok("BEGIN TRANSACTION")->kind == StmtKind::Begin);
  CHECK(parse_ok("COMMIT")->kind == StmtKind::Commit);
  CHECK(parse_ok("ROLLBACK")->kind == StmtKind::Abort);
  CHECK(parse_ok("ABORT")->kind == StmtKind::Abort);
  // Case-insensitive keywords.
  CHECK(parse_ok("commit;")->kind == StmtKind::Commit);
}

// ===========================================================================
// EXPLAIN
// ===========================================================================

TEST_CASE("EXPLAIN wraps inner select", "[parser]") {
  auto stmt = parse_ok("EXPLAIN SELECT * FROM t WHERE a = 1");
  REQUIRE(stmt->kind == StmtKind::Explain);
  auto* ex = static_cast<ExplainStmt*>(stmt.get());
  REQUIRE(ex->inner != nullptr);
  CHECK(ex->inner->kind == StmtKind::Select);
}

// ===========================================================================
// Expression precedence and operators
// ===========================================================================

TEST_CASE("precedence: AND binds tighter than OR", "[parser]") {
  // a OR b AND c  ==  a OR (b AND c)
  auto stmt = parse_ok("SELECT * FROM t WHERE a OR b AND c");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  CHECK(expr_to_string(sel->where.get()) == "(a OR (b AND c))");
}

TEST_CASE("precedence: arithmetic over comparison", "[parser]") {
  // 1 + 2 * 3 < 10  ==  ((1 + (2 * 3)) < 10)
  auto stmt = parse_ok("SELECT * FROM t WHERE 1 + 2 * 3 < 10");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  CHECK(expr_to_string(sel->where.get()) == "((1 + (2 * 3)) < 10)");
}

TEST_CASE("left associativity of subtraction", "[parser]") {
  auto stmt = parse_ok("SELECT * FROM t WHERE a - b - c = 0");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  CHECK(expr_to_string(sel->where.get()) == "(((a - b) - c) = 0)");
}

TEST_CASE("parentheses override precedence", "[parser]") {
  auto stmt = parse_ok("SELECT * FROM t WHERE (a OR b) AND c");
  auto* sel = static_cast<SelectStmt*>(stmt.get());
  CHECK(expr_to_string(sel->where.get()) == "((a OR b) AND c)");
}

TEST_CASE("unary NOT and negation", "[parser]") {
  auto s1 = parse_ok("SELECT * FROM t WHERE NOT a = b");
  auto* sel1 = static_cast<SelectStmt*>(s1.get());
  // NOT binds looser than comparison: NOT (a = b)
  CHECK(expr_to_string(sel1->where.get()) == "(NOT (a = b))");

  auto s2 = parse_ok("SELECT * FROM t WHERE -a < 0");
  auto* sel2 = static_cast<SelectStmt*>(s2.get());
  CHECK(expr_to_string(sel2->where.get()) == "((-a) < 0)");
}

TEST_CASE("equality and inequality operator spellings", "[parser]") {
  // = and == both Eq; != and <> both Ne.
  auto a = parse_ok("SELECT * FROM t WHERE x == 1");
  CHECK(expr_to_string(static_cast<SelectStmt*>(a.get())->where.get()) ==
        "(x = 1)");
  auto b = parse_ok("SELECT * FROM t WHERE x != 1");
  CHECK(expr_to_string(static_cast<SelectStmt*>(b.get())->where.get()) ==
        "(x != 1)");
  auto c = parse_ok("SELECT * FROM t WHERE x <> 1");
  CHECK(expr_to_string(static_cast<SelectStmt*>(c.get())->where.get()) ==
        "(x != 1)");
  auto d = parse_ok("SELECT * FROM t WHERE x >= 1 AND x <= 9");
  CHECK(expr_to_string(static_cast<SelectStmt*>(d.get())->where.get()) ==
        "((x >= 1) AND (x <= 9))");
}

// ===========================================================================
// Literals
// ===========================================================================

TEST_CASE("literal types", "[parser]") {
  auto stmt = parse_ok(
      "INSERT INTO t VALUES (42, 3.14, 'hello', TRUE, FALSE, NULL)");
  auto& row = static_cast<InsertStmt*>(stmt.get())->rows[0];
  REQUIRE(row.size() == 6);
  auto val = [&](int i) {
    return static_cast<LiteralExpr*>(row[i].get())->value;
  };
  CHECK(val(0).type() == TypeId::Integer);
  CHECK(val(0).as_integer() == 42);
  CHECK(val(1).type() == TypeId::Double);
  CHECK(val(1).as_double() == Approx(3.14));
  CHECK(val(2).type() == TypeId::Varchar);
  CHECK(val(2).as_varchar() == "hello");
  CHECK(val(3).type() == TypeId::Boolean);
  CHECK(val(3).as_boolean());
  CHECK(val(4).type() == TypeId::Boolean);
  CHECK_FALSE(val(4).as_boolean());
  CHECK(val(5).is_null());
  CHECK(val(5).type() == TypeId::Integer);
}

TEST_CASE("scientific-notation double literal", "[parser]") {
  auto stmt = parse_ok("INSERT INTO t VALUES (1e3, 2.5E-2)");
  auto& row = static_cast<InsertStmt*>(stmt.get())->rows[0];
  auto v0 = static_cast<LiteralExpr*>(row[0].get())->value;
  auto v1 = static_cast<LiteralExpr*>(row[1].get())->value;
  CHECK(v0.type() == TypeId::Double);
  CHECK(v0.as_double() == Approx(1000.0));
  CHECK(v1.type() == TypeId::Double);
  CHECK(v1.as_double() == Approx(0.025));
}

TEST_CASE("string escape with doubled quote", "[parser]") {
  // 'it''s' -> it's
  auto stmt = parse_ok("INSERT INTO t VALUES ('it''s a test')");
  auto& row = static_cast<InsertStmt*>(stmt.get())->rows[0];
  auto v = static_cast<LiteralExpr*>(row[0].get())->value;
  CHECK(v.type() == TypeId::Varchar);
  CHECK(v.as_varchar() == "it's a test");
}

// ===========================================================================
// Lexer-level concerns
// ===========================================================================

TEST_CASE("line comments and trailing semicolon", "[parser]") {
  auto stmt = parse_ok(
      "SELECT * FROM t -- this is a comment\n"
      "WHERE a = 1 ; -- trailing");
  CHECK(stmt->kind == StmtKind::Select);
}

TEST_CASE("unterminated string is a graceful error", "[parser]") {
  parse_err("INSERT INTO t VALUES ('oops)");
}

TEST_CASE("unexpected character is a graceful error", "[parser]") {
  parse_err("SELECT * FROM t WHERE a @ b");
}

TEST_CASE("leftover tokens after statement are an error", "[parser]") {
  parse_err("SELECT * FROM t extra garbage here");
  parse_err("COMMIT COMMIT");
}

TEST_CASE("empty and whitespace-only input", "[parser]") {
  parse_err("");
  parse_err("   \n\t  ");
  parse_err("  -- just a comment\n");
}

TEST_CASE("unbalanced parentheses in expression", "[parser]") {
  parse_err("SELECT * FROM t WHERE (a = 1");
  parse_err("SELECT * FROM t WHERE a = 1)");
}
