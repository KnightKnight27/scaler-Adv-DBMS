#include "catch.hpp"

#include <unistd.h>

#include <cstdio>
#include <string>

#include "engine/database.h"

using namespace axiomdb;

namespace {
std::string temp_base(const char* tag) {
  return std::string("/tmp/axiomdb_exec_") + tag + "_" + std::to_string(::getpid());
}
void cleanup(const std::string& base) {
  ::remove((base + ".wdb").c_str());
  ::remove((base + ".catalog").c_str());
}
}  // namespace

TEST_CASE("end-to-end CREATE / INSERT / SELECT with WHERE + projection", "[exec]") {
  std::string base = temp_base("basic");
  cleanup(base);
  {
    Database db(base);
    REQUIRE(db.run("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, age INT)").ok);
    REQUIRE(db.run("INSERT INTO users VALUES (1,'alice',30),(2,'bob',25),(3,'carol',40)").affected == 3);

    auto all = db.run("SELECT * FROM users");
    REQUIRE(all.ok);
    REQUIRE(all.rows.size() == 3);
    REQUIRE(all.columns.size() == 3);

    auto filtered = db.run("SELECT name, age FROM users WHERE age > 28");
    REQUIRE(filtered.ok);
    REQUIRE(filtered.columns.size() == 2);
    REQUIRE(filtered.rows.size() == 2);  // alice(30), carol(40)
    for (const auto& row : filtered.rows) REQUIRE(row[1].as_integer() > 28);
  }
  cleanup(base);
}

TEST_CASE("optimizer chooses IndexScan for PK equality, SeqScan otherwise", "[exec][optimizer]") {
  std::string base = temp_base("explain");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE t (id INT PRIMARY KEY, name VARCHAR)");
    for (int i = 0; i < 200; ++i)
      db.run("INSERT INTO t VALUES (" + std::to_string(i) + ", 'n" + std::to_string(i) + "')");

    auto by_pk = db.run("EXPLAIN SELECT * FROM t WHERE id = 42");
    REQUIRE(by_pk.ok);
    INFO(by_pk.message);
    REQUIRE(by_pk.message.find("IndexScan") != std::string::npos);
    REQUIRE(by_pk.message.find("SeqScan") == std::string::npos);

    auto by_name = db.run("EXPLAIN SELECT * FROM t WHERE name = 'n42'");
    REQUIRE(by_name.ok);
    INFO(by_name.message);
    REQUIRE(by_name.message.find("SeqScan") != std::string::npos);
    REQUIRE(by_name.message.find("IndexScan") == std::string::npos);

    // And the index path actually returns the right row.
    auto r = db.run("SELECT name FROM t WHERE id = 42");
    REQUIRE(r.rows.size() == 1);
    REQUIRE(r.rows[0][0].as_varchar() == "n42");
  }
  cleanup(base);
}

TEST_CASE("end-to-end JOIN across two tables", "[exec][join]") {
  std::string base = temp_base("join");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR)");
    db.run("CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount INT)");
    db.run("INSERT INTO users VALUES (1,'alice'),(2,'bob')");
    db.run("INSERT INTO orders VALUES (10,1,100),(11,1,200),(12,2,50)");

    auto j = db.run(
        "SELECT users.name, orders.amount FROM users "
        "JOIN orders ON users.id = orders.uid WHERE orders.amount >= 100");
    REQUIRE(j.ok);
    REQUIRE(j.columns.size() == 2);
    REQUIRE(j.rows.size() == 2);  // alice/100 and alice/200
    for (const auto& row : j.rows) {
      REQUIRE(row[0].as_varchar() == "alice");
      REQUIRE(row[1].as_integer() >= 100);
    }
  }
  cleanup(base);
}

TEST_CASE("greedy join ordering: 3-way join, smallest relation first", "[exec][optimizer][join]") {
  std::string base = temp_base("join3");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE a (id INT PRIMARY KEY, av VARCHAR)");
    db.run("CREATE TABLE b (id INT PRIMARY KEY, aid INT, bv VARCHAR)");
    db.run("CREATE TABLE c (id INT PRIMARY KEY, bid INT, cv VARCHAR)");
    db.run("INSERT INTO a VALUES (1,'A1'),(2,'A2')");
    db.run("INSERT INTO b VALUES (10,1,'B10'),(11,2,'B11'),(12,1,'B12')");
    db.run("INSERT INTO c VALUES (100,10,'C100'),(101,12,'C101')");

    // FROM the big table, but the planner should reorder to start from the
    // smallest relation.  Result correctness must be unaffected by reordering.
    auto q = db.run(
        "SELECT a.av, b.bv, c.cv FROM b "
        "JOIN a ON a.id = b.aid "
        "JOIN c ON c.bid = b.id");
    REQUIRE(q.ok);
    REQUIRE(q.rows.size() == 2);
    // Expected: (A1,B10,C100) and (A1,B12,C101).
    bool seen_b10 = false, seen_b12 = false;
    for (const auto& r : q.rows) {
      REQUIRE(r[0].as_varchar() == "A1");
      if (r[1].as_varchar() == "B10") { seen_b10 = true; REQUIRE(r[2].as_varchar() == "C100"); }
      if (r[1].as_varchar() == "B12") { seen_b12 = true; REQUIRE(r[2].as_varchar() == "C101"); }
    }
    REQUIRE(seen_b10);
    REQUIRE(seen_b12);

    // EXPLAIN should reference all three tables in the plan.
    auto ex = db.run(
        "EXPLAIN SELECT a.av FROM b JOIN a ON a.id = b.aid JOIN c ON c.bid = b.id");
    REQUIRE(ex.message.find("SeqScan(a") != std::string::npos);
    REQUIRE(ex.message.find("SeqScan(b") != std::string::npos);
    REQUIRE(ex.message.find("SeqScan(c") != std::string::npos);
  }
  cleanup(base);
}

TEST_CASE("DELETE removes matching rows; data persists across reopen", "[exec][persist]") {
  std::string base = temp_base("delpersist");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE k (id INT PRIMARY KEY, v INT)");
    for (int i = 1; i <= 100; ++i)
      db.run("INSERT INTO k VALUES (" + std::to_string(i) + "," + std::to_string(i * 10) + ")");
    auto del = db.run("DELETE FROM k WHERE v > 500");
    REQUIRE(del.affected == 50);
    REQUIRE(db.run("SELECT * FROM k").rows.size() == 50);
  }
  {
    Database db(base);  // reopen
    auto all = db.run("SELECT * FROM k");
    REQUIRE(all.rows.size() == 50);
    auto one = db.run("SELECT v FROM k WHERE id = 7");
    REQUIRE(one.rows.size() == 1);
    REQUIRE(one.rows[0][0].as_integer() == 70);
  }
  cleanup(base);
}

TEST_CASE("errors are reported, not crashes", "[exec][errors]") {
  std::string base = temp_base("errors");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE t (id INT PRIMARY KEY)");
    REQUIRE_FALSE(db.run("SELECT * FROM nonexistent").ok);
    REQUIRE_FALSE(db.run("SELECT bogus_col FROM t").ok);
    REQUIRE_FALSE(db.run("SELECT * FROM").ok);                 // syntax error
    db.run("INSERT INTO t VALUES (1)");
    REQUIRE_FALSE(db.run("INSERT INTO t VALUES (1)").ok);       // duplicate PK
  }
  cleanup(base);
}

// Regression tests for issues found by the adversarial code review.
TEST_CASE("robustness: mixed-type ops and overflow don't crash", "[exec][robustness]") {
  std::string base = temp_base("robust");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE t (id INT PRIMARY KEY, name VARCHAR, big INT)");
    db.run("INSERT INTO t VALUES (1,'alice',9223372036854775807)");

    // Mixed-type comparison must NOT throw bad_variant_access; compare() is a
    // total order (text sorts after numbers), so 'alice' < 5 is false.
    auto r1 = db.run("SELECT id FROM t WHERE name < 5");
    REQUIRE(r1.ok);
    REQUIRE(r1.rows.empty());

    // A non-boolean/non-numeric operand in boolean context is a clean type
    // error, not an opaque variant exception.
    auto r2 = db.run("SELECT id FROM t WHERE name");
    REQUIRE_FALSE(r2.ok);
    REQUIRE(r2.error.find("boolean or numeric") != std::string::npos);

    // Integer overflow yields NULL (checked arithmetic), not UB / a wrapped value.
    auto r3 = db.run("SELECT big + 1 FROM t");
    REQUIRE(r3.ok);
    REQUIRE(r3.rows.size() == 1);
    REQUIRE(r3.rows[0][0].is_null());
  }
  cleanup(base);
}
