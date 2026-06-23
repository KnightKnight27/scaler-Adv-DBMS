#include "minidb/database.hpp"
#include "test_util.hpp"
#include <cstdio>
using namespace minidb;

static void run_tests() {
  std::remove("t_rec.data");
  std::remove("t_rec.catalog");
  std::remove("t_rec.wal");

  // Session 1: commit one transaction, leave a second in-flight, then crash.
  {
    Database db("t_rec");
    db.execute("CREATE TABLE t (id INT, v INT);");
    db.execute("BEGIN;");
    db.execute("INSERT INTO t VALUES (1, 10);");
    db.execute("INSERT INTO t VALUES (2, 20);");
    db.execute("INSERT INTO t VALUES (3, 30);");
    db.execute("COMMIT;");

    db.execute("BEGIN;");
    db.execute("INSERT INTO t VALUES (4, 40);");
    db.execute("INSERT INTO t VALUES (5, 50);");
    db.debug_flush_pages();  // STEAL: uncommitted rows reach disk
    db.debug_crash();        // abandon without a clean shutdown
  }

  // Session 2: recovery runs in the constructor.
  {
    Database db("t_rec");
    QueryResult r = db.execute("SELECT id FROM t;");
    CHECK_EQ(r.rows.size(), (size_t)3);  // only the committed rows survive
    bool h1 = false, h2 = false, h3 = false;
    for (const auto& row : r.rows) {
      int64_t id = row[0].as_int();
      if (id == 1) h1 = true;
      if (id == 2) h2 = true;
      if (id == 3) h3 = true;
    }
    CHECK(h1 && h2 && h3);
  }
}

TEST_MAIN()
