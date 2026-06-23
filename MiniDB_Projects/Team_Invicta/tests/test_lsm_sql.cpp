// Track C integration test: the SQL engine runs unchanged over an LSM-backed
// table (CREATE TABLE ... USING LSM), including a join against a heap table,
// and the data persists across reopen.
#include <cstdio>
#include <filesystem>
#include "engine/database.h"
#include "test_util.h"

namespace fs = std::filesystem;
using namespace minidb;

int main() {
  std::printf("test_lsm_sql\n");
  std::remove("/tmp/minidb_lsmsql.db");
  std::remove("/tmp/minidb_lsmsql.catalog");
  std::remove("/tmp/minidb_lsmsql.wal");
  fs::remove_all("/tmp/minidb_lsmsql_lsm_events");

  {
    Database db("/tmp/minidb_lsmsql");
    // One LSM table and one heap table — same SQL surface.
    CHECK(db.Execute("CREATE TABLE events (id INTEGER PRIMARY KEY, kind VARCHAR) USING LSM;").ok);
    CHECK(db.Execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name VARCHAR);").ok);

    for (int i = 0; i < 500; ++i) {
      CHECK(db.Execute("INSERT INTO events VALUES (" + std::to_string(i) + ", 'k" +
                       std::to_string(i % 3) + "');").ok);
    }
    for (int i = 0; i < 50; ++i) {
      db.Execute("INSERT INTO users VALUES (" + std::to_string(i) + ", 'u" +
                 std::to_string(i) + "');");
    }

    // COUNT over the LSM table.
    auto c = db.Execute("SELECT COUNT(*) FROM events;");
    CHECK_EQ(c.rows[0][0], std::string("500"));

    // Point lookup by PK (range scan path) over LSM.
    auto p = db.Execute("SELECT kind FROM events WHERE id = 7;");
    CHECK_EQ(static_cast<int>(p.rows.size()), 1);
    CHECK_EQ(p.rows[0][0], std::string("k1"));

    // Duplicate PK rejected on the LSM table too.
    CHECK(!db.Execute("INSERT INTO events VALUES (7, 'dup');").ok);

    // DELETE on LSM (tombstone), then re-count.
    CHECK(db.Execute("DELETE FROM events WHERE id >= 250;").ok);
    auto c2 = db.Execute("SELECT COUNT(*) FROM events;");
    CHECK_EQ(c2.rows[0][0], std::string("250"));

    // Join an LSM table with a heap table — executor is storage-agnostic.
    auto j = db.Execute(
        "SELECT users.name, events.kind FROM users JOIN events ON users.id = events.id;");
    CHECK_EQ(static_cast<int>(j.rows.size()), 50);  // users 0..49 all have events

    db.Flush();
  }

  // Reopen: LSM SSTables + catalog persist the table and its rows.
  {
    Database db("/tmp/minidb_lsmsql");
    auto c = db.Execute("SELECT COUNT(*) FROM events;");
    CHECK_EQ(c.rows[0][0], std::string("250"));
    auto p = db.Execute("SELECT kind FROM events WHERE id = 7;");
    CHECK_EQ(p.rows[0][0], std::string("k1"));
    db.Flush();
  }

  TEST_PASS();
}
