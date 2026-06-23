#include "minidb/database.hpp"
#include "test_util.hpp"
#include <cstdio>
using namespace minidb;

static size_t row_count(Database& db, const char* sql) {
  return db.execute(sql).rows.size();
}

static void run_tests() {
  std::remove("t_txn.data");
  std::remove("t_txn.catalog");
  std::remove("t_txn.wal");
  Database db("t_txn");
  db.execute("CREATE TABLE t (id INT);");

  // Autocommit insert persists.
  db.execute("INSERT INTO t VALUES (1);");
  CHECK_EQ(row_count(db, "SELECT id FROM t;"), (size_t)1);

  // Committed transaction persists.
  db.execute("BEGIN;");
  db.execute("INSERT INTO t VALUES (2);");
  db.execute("COMMIT;");
  CHECK_EQ(row_count(db, "SELECT id FROM t;"), (size_t)2);

  // Rolled-back insert reverts (and is visible within the txn first).
  db.execute("BEGIN;");
  db.execute("INSERT INTO t VALUES (3);");
  CHECK_EQ(row_count(db, "SELECT id FROM t;"), (size_t)3);  // read-your-writes
  db.execute("ROLLBACK;");
  CHECK_EQ(row_count(db, "SELECT id FROM t;"), (size_t)2);  // reverted

  // Rolled-back delete restores the row.
  db.execute("BEGIN;");
  db.execute("DELETE FROM t WHERE id = 1;");
  CHECK_EQ(row_count(db, "SELECT id FROM t;"), (size_t)1);
  db.execute("ROLLBACK;");
  CHECK_EQ(row_count(db, "SELECT id FROM t;"), (size_t)2);  // restored
}

TEST_MAIN()
