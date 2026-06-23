// Parser tests: each supported statement shape parses into the expected AST.
#include <cstdio>
#include "sql/parser.h"
#include "test_util.h"

using namespace minidb;

int main() {
  std::printf("test_sql\n");
  Parser p;

  // CREATE TABLE with PK + storage engine selection.
  {
    auto s = p.Parse("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR(32), age INT);");
    CHECK(s.type == StmtType::CREATE_TABLE);
    CHECK_EQ(s.create.table, std::string("users"));
    CHECK_EQ(static_cast<int>(s.create.columns.size()), 3);
    CHECK(s.create.columns[0].is_primary_key);
    CHECK(s.create.columns[0].type == TypeId::INTEGER);
    CHECK(s.create.columns[1].type == TypeId::VARCHAR);
    CHECK(s.create.storage == StorageType::HEAP);

    auto s2 = p.Parse("CREATE TABLE events (id INT PRIMARY KEY, payload VARCHAR) USING LSM;");
    CHECK(s2.create.storage == StorageType::LSM);
  }

  // INSERT with mixed literals incl. negative number.
  {
    auto s = p.Parse("INSERT INTO users VALUES (1, 'alice', -5);");
    CHECK(s.type == StmtType::INSERT);
    CHECK_EQ(static_cast<int>(s.insert.values.size()), 3);
    CHECK_EQ(s.insert.values[0].i, 1);
    CHECK_EQ(s.insert.values[1].s, std::string("alice"));
    CHECK_EQ(s.insert.values[2].i, -5);
  }

  // SELECT * with WHERE (multiple AND conjuncts, various operators).
  {
    auto s = p.Parse("SELECT * FROM users WHERE age >= 18 AND name != 'bob';");
    CHECK(s.type == StmtType::SELECT);
    CHECK(s.select.star);
    CHECK_EQ(static_cast<int>(s.select.where.size()), 2);
    CHECK(s.select.where[0].op == CompareOp::GE);
    CHECK_EQ(s.select.where[0].value.i, 18);
    CHECK(s.select.where[1].op == CompareOp::NE);
  }

  // Projection list.
  {
    auto s = p.Parse("SELECT id, name FROM users WHERE id = 7;");
    CHECK(!s.select.star);
    CHECK_EQ(static_cast<int>(s.select.columns.size()), 2);
    CHECK_EQ(s.select.columns[1], std::string("name"));
    CHECK(s.select.where[0].op == CompareOp::EQ);
  }

  // COUNT(*).
  {
    auto s = p.Parse("SELECT COUNT(*) FROM users;");
    CHECK(s.select.count_star);
  }

  // JOIN ... ON with qualified columns.
  {
    auto s = p.Parse("SELECT * FROM orders JOIN users ON orders.uid = users.id WHERE orders.amt > 100;");
    CHECK(s.select.join.present);
    CHECK_EQ(s.select.join.table, std::string("users"));
    CHECK_EQ(s.select.join.left_col, std::string("orders.uid"));
    CHECK_EQ(s.select.join.right_col, std::string("users.id"));
    CHECK_EQ(static_cast<int>(s.select.where.size()), 1);
  }

  // DELETE with and without WHERE.
  {
    auto s = p.Parse("DELETE FROM users WHERE id = 3;");
    CHECK(s.type == StmtType::DELETE);
    CHECK_EQ(s.del.where[0].value.i, 3);
    auto s2 = p.Parse("DELETE FROM users;");
    CHECK(s2.del.where.empty());
  }

  // Transaction control.
  {
    CHECK(p.Parse("BEGIN;").type == StmtType::BEGIN);
    CHECK(p.Parse("COMMIT;").type == StmtType::COMMIT);
    CHECK(p.Parse("ROLLBACK;").type == StmtType::ROLLBACK);
  }

  // SQL line comments are ignored by the lexer.
  {
    auto s = p.Parse("SELECT id FROM users -- trailing comment\n WHERE id = 5;");
    CHECK(s.type == StmtType::SELECT);
    CHECK_EQ(s.select.where[0].value.i, 5);
  }

  // Error cases.
  {
    bool threw = false;
    try { p.Parse("SELECT FROM;"); } catch (const ParseError &) { threw = true; }
    CHECK(threw);
    threw = false;
    try { p.Parse("FOOBAR x;"); } catch (const ParseError &) { threw = true; }
    CHECK(threw);
  }

  TEST_PASS();
}
