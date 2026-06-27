// Crash recovery: committed transactions survive a crash (REDO), and an
// uncommitted transaction's effects are rolled back on restart (UNDO).
#include <cassert>
#include <cstdio>
#include <iostream>
#include "engine/database.h"

using namespace minidb;

static void cleanup() {
  std::remove("t_rec.db");
  std::remove("t_rec.catalog");
  std::remove("t_rec.wal");
}

int main() {
  cleanup();

  // --- Session 1: commit some rows, then crash mid-transaction ---
  {
    Database* db = new Database("t_rec");
    db->Execute("CREATE TABLE acct (id INTEGER PRIMARY KEY, bal INTEGER)");
    db->Execute("INSERT INTO acct VALUES (1, 100)");  // autocommit
    db->Execute("INSERT INTO acct VALUES (2, 200)");  // autocommit

    // An explicit transaction that we never COMMIT.
    db->Execute("BEGIN");
    db->Execute("INSERT INTO acct VALUES (3, 300)");
    db->Execute("INSERT INTO acct VALUES (4, 400)");
    // Force the uncommitted rows out to disk (simulate buffer eviction), so
    // recovery genuinely has to UNDO persisted, uncommitted data.
    db->GetBufferPool()->FlushAll();

    db->SimulateCrash();  // no checkpoint, no clean shutdown
    // intentionally leak `db` -- destructors must NOT run on a crash
  }

  // --- Session 2: reopen -> recovery runs in the constructor ---
  {
    Database db("t_rec");
    auto all = db.Execute("SELECT id FROM acct");
    std::cout << "  rows after recovery: " << all.rows.size() << "\n";
    // Committed rows 1 and 2 survive; uncommitted 3 and 4 are gone.
    assert(all.rows.size() == 2);
    bool has1 = false, has2 = false;
    for (auto& r : all.rows) {
      if (r[0].i == 1) has1 = true;
      if (r[0].i == 2) has2 = true;
      assert(r[0].i != 3 && r[0].i != 4);
    }
    assert(has1 && has2);

    // The recovered index is usable, and the freed PK can be reused.
    auto pt = db.Execute("SELECT bal FROM acct WHERE id = 1");
    assert(pt.rows.size() == 1 && pt.rows[0][0].i == 100);
    db.Execute("INSERT INTO acct VALUES (3, 333)");  // 3 was rolled back -> free
    assert(db.Execute("SELECT id FROM acct").rows.size() == 3);
  }

  cleanup();
  std::cout << "[recovery] committed REDO + uncommitted UNDO across crash OK\n";
  return 0;
}
