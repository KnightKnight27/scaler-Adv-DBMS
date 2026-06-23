// Demonstrates WAL crash recovery: a committed transaction survives a crash and
// an uncommitted one is rolled back, even though the data pages were never
// flushed to disk (only the WAL was).
#include <cassert>
#include <cstdio>
#include <iostream>
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_table.h"
#include "txn/lock_manager.h"
#include "txn/recovery.h"
#include "txn/txn_manager.h"
#include "txn/wal.h"

using namespace minidb;

static std::string show(const std::optional<Bytes>& v) { return v ? *v : "<gone>"; }

int main() {
  const std::string db = "/tmp/minidb_recovery.db";
  const std::string wal = "/tmp/minidb_recovery.wal";
  std::remove(db.c_str());
  std::remove(wal.c_str());

  PageId first_page;

  // ---- Phase 1: do work, then "crash" without flushing the data pages ----
  {
    DiskManager disk(db);
    BufferPool pool(disk);
    std::unique_ptr<HeapTable> table = make_heap_table(pool);
    first_page = table->first_page();
    LockManager locks;
    LogManager log(wal, /*truncate=*/true);
    TransactionManager tm(locks, log, *table);

    TxnId t1 = tm.begin();
    tm.write(t1, 1, "100");
    tm.write(t1, 2, "200");
    tm.commit(t1);  // COMMIT is forced to the WAL

    TxnId t2 = tm.begin();
    tm.write(t2, 3, "999");  // never committed

    std::cout << "Phase 1: T1 committed (1=100, 2=200); T2 wrote 3=999 but did NOT commit\n";
    std::cout << "Crash: dropping the buffer pool WITHOUT flushing data pages "
                 "(only the WAL is on disk)\n";
    // Leaving this scope destroys pool/table without flush_all(): the data
    // pages are lost; the WAL survives.
  }

  // ---- Phase 2: reopen and recover from the WAL ----
  {
    DiskManager disk(db);
    BufferPool pool(disk);
    HeapTable table(pool, first_page);
    LogManager log(wal, /*truncate=*/false);
    RecoveryManager recovery(log, table);

    int committed = recovery.recover();
    std::cout << "\nPhase 2: recovery replayed " << committed << " committed transaction(s)\n";

    std::optional<Bytes> v1 = table.get(1), v2 = table.get(2), v3 = table.get(3);
    std::cout << "row 1 = " << show(v1) << " | row 2 = " << show(v2)
              << " | row 3 = " << show(v3) << "\n";

    assert(v1 && *v1 == "100");
    assert(v2 && *v2 == "200");
    assert(!v3);
    std::cout << "OK: committed transaction preserved, uncommitted transaction rolled back\n";
  }
  return 0;
}
