// Crash recovery test. A child process writes committed and uncommitted data
// and then "crashes" (_exit, skipping all flush/destructors). The parent
// reopens the database and verifies that committed transactions survived and
// uncommitted ones were rolled back via WAL replay.
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include "engine/database.h"
#include "test_util.h"

using namespace minidb;

static const char *PREFIX = "/tmp/minidb_rec";

int main() {
  std::printf("test_recovery\n");
  std::fflush(stdout);  // flush before fork so the child doesn't re-emit it
  std::remove("/tmp/minidb_rec.db");
  std::remove("/tmp/minidb_rec.catalog");
  std::remove("/tmp/minidb_rec.wal");

  pid_t pid = fork();
  CHECK(pid >= 0);

  if (pid == 0) {
    // --- Child: do work, then crash without flushing. ---
    Database *db = new Database(PREFIX);  // intentionally leaked; no destructor
    db->Execute("CREATE TABLE acct (id INTEGER PRIMARY KEY, bal INTEGER);");
    for (int i = 1; i <= 5; ++i) {  // committed (autocommit)
      db->Execute("INSERT INTO acct VALUES (" + std::to_string(i) + ", " +
                  std::to_string(i * 100) + ");");
    }
    // An explicit transaction that never commits.
    db->Execute("BEGIN;");
    db->Execute("INSERT INTO acct VALUES (99, 9999);");
    db->Execute("DELETE FROM acct WHERE id = 3;");  // uncommitted delete
    // Crash: skip Flush()/destructors entirely.
    std::fflush(nullptr);
    _exit(0);
  }

  // --- Parent: wait for the "crash", then reopen and recover. ---
  int status = 0;
  waitpid(pid, &status, 0);

  Database db(PREFIX);  // constructor runs WAL recovery

  // Committed rows 1,2,4,5 survive; row 3 survives too (its delete was
  // uncommitted and must be rolled back); row 99 (uncommitted insert) is gone.
  auto count = db.Execute("SELECT COUNT(*) FROM acct;");
  CHECK(count.ok);
  CHECK_EQ(count.rows[0][0], std::string("5"));

  auto r3 = db.Execute("SELECT bal FROM acct WHERE id = 3;");
  CHECK_EQ(static_cast<int>(r3.rows.size()), 1);  // uncommitted delete rolled back
  CHECK_EQ(r3.rows[0][0], std::string("300"));

  auto r99 = db.Execute("SELECT bal FROM acct WHERE id = 99;");
  CHECK_EQ(static_cast<int>(r99.rows.size()), 0);  // uncommitted insert gone

  auto r1 = db.Execute("SELECT bal FROM acct WHERE id = 1;");
  CHECK_EQ(r1.rows[0][0], std::string("100"));  // committed insert preserved

  db.Flush();
  TEST_PASS();
}
