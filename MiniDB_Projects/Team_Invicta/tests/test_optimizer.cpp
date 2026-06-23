// End-to-end execution + optimizer tests: SELECT/WHERE/JOIN/COUNT/DELETE over
// the engine, and verification that the optimizer picks the index vs full scan
// based on predicate selectivity.
#include <cstdio>
#include "engine/database.h"
#include "test_util.h"

using namespace minidb;

static bool Contains(const std::string &hay, const std::string &needle) {
  return hay.find(needle) != std::string::npos;
}

int main() {
  std::printf("test_optimizer\n");
  std::remove(TmpFile("opt.db").c_str());
  std::remove(TmpFile("opt.catalog").c_str());

  Database db(TmpFile("opt"));

  CHECK(db.Execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR, age INTEGER);").ok);
  CHECK(db.Execute("CREATE TABLE orders (oid INTEGER PRIMARY KEY, uid INTEGER, amt INTEGER);").ok);

  const int N = 1000;
  for (int i = 0; i < N; ++i) {
    db.Execute("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u" +
               std::to_string(i) + "', " + std::to_string(i % 100) + ");");
  }
  // Each user gets one order.
  for (int i = 0; i < N; ++i) {
    db.Execute("INSERT INTO orders VALUES (" + std::to_string(i) + ", " +
               std::to_string(i) + ", " + std::to_string(i * 2) + ");");
  }

  // Highly selective PK predicate -> IndexScan.
  {
    auto r = db.Execute("SELECT id, name FROM users WHERE id = 42;");
    CHECK(r.ok && r.is_query);
    CHECK_EQ(static_cast<int>(r.rows.size()), 1);
    CHECK_EQ(r.rows[0][0], std::string("42"));
    CHECK_EQ(r.rows[0][1], std::string("u42"));
    CHECK(Contains(r.explain, "IndexScan"));
  }

  // Non-selective predicate (matches nearly all rows) -> SeqScan.
  {
    auto r = db.Execute("SELECT COUNT(*) FROM users WHERE id >= 1;");
    CHECK(r.ok);
    CHECK_EQ(r.rows[0][0], std::string("999"));
    CHECK(Contains(r.explain, "SeqScan"));
  }

  // Moderately selective range -> IndexScan, correct rows.
  {
    auto r = db.Execute("SELECT COUNT(*) FROM users WHERE id >= 990;");
    CHECK_EQ(r.rows[0][0], std::string("10"));
    CHECK(Contains(r.explain, "IndexScan"));
  }

  // Predicate on a non-PK column -> SeqScan + filter.
  {
    auto r = db.Execute("SELECT COUNT(*) FROM users WHERE age = 7;");
    CHECK_EQ(r.rows[0][0], std::string("10"));  // ids 7,107,207,...,907
    CHECK(Contains(r.explain, "SeqScan"));
  }

  // JOIN users x orders on id = uid, then filter.
  {
    auto r = db.Execute(
        "SELECT users.name, orders.amt FROM users JOIN orders ON users.id = orders.uid "
        "WHERE orders.amt > 1990;");
    CHECK(r.ok);
    // amt = id*2 > 1990 -> id >= 996 -> ids 996..999 => 4 rows.
    CHECK_EQ(static_cast<int>(r.rows.size()), 4);
    CHECK(Contains(r.explain, "Join order"));
  }

  // DELETE with predicate, then re-count.
  {
    auto d = db.Execute("DELETE FROM users WHERE id >= 500;");
    CHECK(d.ok);
    CHECK_EQ(d.message, std::string("500 row(s) deleted"));
    auto r = db.Execute("SELECT COUNT(*) FROM users;");
    CHECK_EQ(r.rows[0][0], std::string("500"));
  }

  // Duplicate PK rejected.
  {
    auto r = db.Execute("INSERT INTO users VALUES (1, 'dup', 1);");
    CHECK(!r.ok);
    CHECK(Contains(r.message, "duplicate"));
  }

  db.Flush();
  TEST_PASS();
}
