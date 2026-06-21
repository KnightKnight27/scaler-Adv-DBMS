#include "catch.hpp"

#include <unistd.h>

#include <cstdio>
#include <set>
#include <string>

#include "engine/database.h"

using namespace walterdb;

namespace {
std::string base_for(const char* tag) {
  return std::string("/tmp/walterdb_rec_") + tag + "_" + std::to_string(::getpid());
}
void cleanup(const std::string& base) {
  ::remove((base + ".wdb").c_str());
  ::remove((base + ".catalog").c_str());
  ::remove((base + ".wal").c_str());
}
}  // namespace

TEST_CASE("crash recovery: committed survives, uncommitted is rolled back", "[recovery][crash]") {
  std::string base = base_for("crash");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE t (id INT PRIMARY KEY, v INT)");
    // Committed work (auto-commit, durable in the WAL via fsync at commit).
    db.run("INSERT INTO t VALUES (1,10),(2,20),(3,30)");

    // Uncommitted work: a transaction that never commits.
    db.run("BEGIN");
    db.run("INSERT INTO t VALUES (4,40),(5,50)");
    // ... crash here, before COMMIT, without a clean checkpoint ...
    db.simulate_crash();
  }
  {
    Database db(base);  // constructor replays the WAL (recovery)
    auto all = db.run("SELECT * FROM t");
    REQUIRE(all.ok);
    REQUIRE(all.rows.size() == 3);  // only the committed rows

    std::set<int64_t> ids;
    for (const auto& r : all.rows) ids.insert(r[0].as_integer());
    REQUIRE(ids == std::set<int64_t>{1, 2, 3});

    // The uncommitted rows are gone; a committed one is intact.
    REQUIRE(db.run("SELECT v FROM t WHERE id = 4").rows.empty());
    auto two = db.run("SELECT v FROM t WHERE id = 2");
    REQUIRE(two.rows.size() == 1);
    REQUIRE(two.rows[0][0].as_integer() == 20);
  }
  cleanup(base);
}

TEST_CASE("ROLLBACK undoes a transaction's inserts and deletes", "[recovery][txn]") {
  std::string base = base_for("rollback");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE t (id INT PRIMARY KEY, v INT)");
    db.run("INSERT INTO t VALUES (1,10)");

    db.run("BEGIN");
    db.run("INSERT INTO t VALUES (2,20)");
    db.run("DELETE FROM t WHERE id = 1");
    // Inside the transaction the change is visible.
    REQUIRE(db.run("SELECT * FROM t").rows.size() == 1);
    REQUIRE(db.run("SELECT v FROM t WHERE id = 2").rows.size() == 1);

    db.run("ROLLBACK");

    // Back to the pre-transaction state: id=1 present, id=2 absent.
    auto after = db.run("SELECT * FROM t");
    REQUIRE(after.rows.size() == 1);
    REQUIRE(after.rows[0][0].as_integer() == 1);
    REQUIRE(db.run("SELECT * FROM t WHERE id = 2").rows.empty());
  }
  cleanup(base);
}

TEST_CASE("clean shutdown truncates the WAL and data persists", "[recovery][clean]") {
  std::string base = base_for("clean");
  cleanup(base);
  {
    Database db(base);
    db.run("CREATE TABLE t (id INT PRIMARY KEY, v INT)");
    db.run("INSERT INTO t VALUES (1,1),(2,2),(3,3),(4,4)");
    db.run("COMMIT");  // no-op (auto-commit), exercises the path
    // clean destructor -> checkpoint (flush + WAL truncate)
  }
  {
    Database db(base);
    REQUIRE(db.run("SELECT * FROM t").rows.size() == 4);  // persisted via data file
  }
  cleanup(base);
}
