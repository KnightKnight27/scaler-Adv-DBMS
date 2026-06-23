#include <cassert>
#include <cstdio>
#include <iostream>

#include "catalog/catalog.h"
#include "query/executor.h"
#include "sql/lexer.h"
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

QueryResult Run(Executor &ex, const std::string &sql) {
  Lexer lex(sql);
  Parser parser(lex.Tokenize());
  Statement stmt = parser.ParseStatement();
  return ex.Execute(stmt);
}

int main() {
  std::remove("test_query.db");
  std::remove("test_query.db.wal");
  std::remove("test_query.meta");

  DiskManager dm("test_query.db");
  BufferPool bpm(64, &dm);
  Catalog cat(&bpm, "test_query.meta");
  Executor ex(&cat);

  assert(Run(ex, "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)").ok);
  assert(Run(ex, "CREATE TABLE orders (id INT PRIMARY KEY, user_id INT, amount INT)").ok);

  for (int i = 1; i <= 10; i++) {
    Run(ex, "INSERT INTO users VALUES (" + std::to_string(i) + ", 'user" + std::to_string(i) + "')");
  }
  for (int i = 1; i <= 5; i++) {
    Run(ex, "INSERT INTO orders VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ", " +
                std::to_string(i * 100) + ")");
  }

  // Equality on PK should use IndexScan.
  {
    auto r = Run(ex, "SELECT id, name FROM users WHERE id = 5");
    assert(r.ok);
    assert(r.rows.size() == 1);
    assert(r.rows[0][1] == "user5");
    assert(r.explain.find("IndexScan") != std::string::npos);
  }

  // No predicate -> SeqScan.
  {
    auto r = Run(ex, "SELECT * FROM users");
    assert(r.ok);
    assert(r.rows.size() == 10);
    assert(r.explain.find("SeqScan") != std::string::npos);
  }

  // Join on orders.user_id = users.id (users.id is PK) -> index nested loop join.
  {
    auto r = Run(ex, "SELECT o.id, u.name, o.amount FROM orders o JOIN users u ON o.user_id = u.id WHERE o.amount > 200");
    assert(r.ok);
    assert(r.rows.size() == 3);  // amounts 300,400,500
    assert(r.explain.find("IndexNestedLoopJoin") != std::string::npos);
  }

  // DELETE via index path.
  {
    auto r = Run(ex, "DELETE FROM users WHERE id = 3");
    assert(r.ok);
    auto check = Run(ex, "SELECT * FROM users WHERE id = 3");
    assert(check.rows.empty());
  }

  // --- Regression: duplicate primary key must be rejected, not silently
  // orphan the original row (QA Bug 2). ---
  {
    auto r = Run(ex, "INSERT INTO users VALUES (1, 'duplicate')");
    assert(!r.ok);
    assert(r.message.find("duplicate primary key") != std::string::npos);
    auto check = Run(ex, "SELECT name FROM users WHERE id = 1");
    assert(check.rows.size() == 1);
    assert(check.rows[0][0] == "user1");  // original row untouched
    auto full_scan = Run(ex, "SELECT * FROM users");
    int count_id_1 = 0;
    for (auto &row : full_scan.rows) if (row[0] == "1") count_id_1++;
    assert(count_id_1 == 1);  // SeqScan and IndexScan must agree
  }

  // --- Regression: a value of the wrong type must be rejected before any
  // heap/WAL/index mutation happens (QA Bug 3 - this used to silently
  // corrupt the row or, worse, half-apply it and then report an error). ---
  {
    auto before = Run(ex, "SELECT * FROM users");
    size_t row_count_before = before.rows.size();

    auto r1 = Run(ex, "INSERT INTO users VALUES (99, 12345)");  // INT where VARCHAR expected
    assert(!r1.ok);
    assert(r1.message.find("type mismatch") != std::string::npos);

    auto r2 = Run(ex, "INSERT INTO users VALUES ('not-an-int', 'x')");  // VARCHAR where INT expected
    assert(!r2.ok);
    assert(r2.message.find("type mismatch") != std::string::npos);

    auto after = Run(ex, "SELECT * FROM users");
    assert(after.rows.size() == row_count_before);  // nothing was partially inserted
  }

  // --- Regression: an oversized row must be rejected up front, not
  // silently "succeed" while the row is unreachable by any scan (QA Bug 4). ---
  {
    std::string huge(5000, 'a');
    auto r = Run(ex, "INSERT INTO users VALUES (100, '" + huge + "')");
    assert(!r.ok);
    assert(r.message.find("too large") != std::string::npos);
    auto check = Run(ex, "SELECT * FROM users WHERE id = 100");
    assert(check.rows.empty());
  }

  // --- Regression: doubled single quotes are a standard SQL escape for a
  // literal quote inside a string (QA Bug 7). ---
  {
    auto r = Run(ex, "INSERT INTO users VALUES (101, 'O''Brien')");
    assert(r.ok);
    auto check = Run(ex, "SELECT name FROM users WHERE id = 101");
    assert(check.rows.size() == 1);
    assert(check.rows[0][0] == "O'Brien");
  }

  // --- Regression: a non-INTEGER PRIMARY KEY must be rejected at CREATE
  // TABLE time with a clear message, not accepted and only fail later
  // with a confusing "Value is not INTEGER" the first time INSERT's PK
  // uniqueness check calls AsInt() on it (QA follow-up note). ---
  {
    auto r = Run(ex, "CREATE TABLE bad_pk (id VARCHAR PRIMARY KEY, v INT)");
    assert(!r.ok);
    assert(r.message.find("must be INTEGER") != std::string::npos);
    assert(!cat.TableExists("bad_pk"));  // no half-created table left behind
  }

  std::remove("test_query.db");
  std::remove("test_query.db.wal");
  std::remove("test_query.meta");
  std::cout << "query_test PASSED\n";
  return 0;
}
