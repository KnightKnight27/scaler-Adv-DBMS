// The SAME SQL surface runs over an LSM-backed table: CREATE ... USING LSM,
// then INSERT / SELECT / WHERE / index(range) scan / JOIN with a heap table /
// DELETE / transactions / persistence. Proves the LSM engine is integrated
// under the executor, not just benchmarked standalone.
#include <cassert>
#include <cstdio>
#include <iostream>
#include "engine/database.h"

using namespace minidb;

static void cleanup() {
  std::remove("t_lsmsql.db");
  std::remove("t_lsmsql.catalog");
  std::remove("t_lsmsql.wal");
  for (int i = 0; i < 50; i++) {
    std::remove(("t_lsmsql.kv_" + std::to_string(i) + ".sst").c_str());
    std::remove(("t_lsmsql.evt_" + std::to_string(i) + ".sst").c_str());
  }
  std::remove("t_lsmsql.kv.manifest");
  std::remove("t_lsmsql.evt.manifest");
}

int main() {
  cleanup();
  {
    Database db("t_lsmsql");
    db.Execute("CREATE TABLE kv (id INTEGER PRIMARY KEY, label VARCHAR) USING LSM");
    db.Execute("CREATE TABLE evt (eid INTEGER PRIMARY KEY, kref INTEGER)");  // heap

    for (int i = 1; i <= 50; i++)
      db.Execute("INSERT INTO kv VALUES (" + std::to_string(i) + ", 'L" + std::to_string(i) + "')");

    // Full scan + COUNT over LSM.
    assert(db.Execute("SELECT * FROM kv").rows.size() == 50);
    assert(db.Execute("SELECT COUNT(*) FROM kv").rows[0][0].i == 50);

    // Point lookup -> optimizer should choose a (range) index scan on the LSM.
    auto pt = db.Execute("SELECT label FROM kv WHERE id = 42");
    std::cout << "  plan: " << pt.plan << "\n";
    assert(pt.rows.size() == 1 && pt.rows[0][0].s == "L42");

    // Range scan over LSM.
    auto rg = db.Execute("SELECT id FROM kv WHERE id >= 10 AND id <= 14");
    assert(rg.rows.size() == 5);

    // DELETE over LSM (tombstone under the hood).
    assert(db.Execute("DELETE FROM kv WHERE id = 42").affected == 1);
    assert(!db.Execute("SELECT * FROM kv WHERE id = 42").rows.size());

    // JOIN: LSM table x heap table.
    db.Execute("INSERT INTO evt VALUES (1, 10)");
    db.Execute("INSERT INTO evt VALUES (2, 11)");
    auto j = db.Execute("SELECT kv.label, evt.eid FROM kv JOIN evt ON kv.id = evt.kref");
    assert(j.rows.size() == 2);

    // Transaction + rollback over the LSM table.
    db.Execute("BEGIN");
    db.Execute("INSERT INTO kv VALUES (100, 'temp')");
    assert(db.Execute("SELECT * FROM kv WHERE id = 100").rows.size() == 1);
    db.Execute("ROLLBACK");
    assert(db.Execute("SELECT * FROM kv WHERE id = 100").rows.size() == 0);

    std::cout << "[lsm-sql] CRUD+WHERE+range+JOIN+txn over LSM table OK\n";
  }

  // Reopen: LSM table persists via its manifest/SSTables + catalog.
  {
    Database db("t_lsmsql");
    assert(db.Execute("SELECT * FROM kv").rows.size() == 49);  // 50 - deleted 42
    assert(db.Execute("SELECT label FROM kv WHERE id = 1").rows[0][0].s == "L1");
    std::cout << "[lsm-sql] persistence across reopen OK\n";
  }

  cleanup();
  std::cout << "[lsm-sql] OK\n";
  return 0;
}
