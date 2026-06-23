// Recovery restart simulation for MiniDB.
// Mimics a process crash by tearing down LogManager, then re-opening the WAL
// and re-running redo/undo to confirm committed records survive.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <unordered_set>

#include "recovery/log_manager.h"
#include "recovery/recovery.h"
#include "transaction/lock_manager.h"
#include "transaction/transaction.h"

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_rec_restart.wal");

  // Phase 1: log committed and aborted records.
  {
    LogManager lm("/tmp/minidb_test_rec_restart.wal");
    LockManager lck;
    {
      Transaction t(NextTxnId(), &lm, &lck);
      t.LogInsert(0, 0, "v1");
      t.LogInsert(0, 1, "v2");
      t.Commit();
    }
    {
      Transaction t(NextTxnId(), &lm, &lck);
      t.LogInsert(1, 0, "wont-survive");
      t.Abort();
    }
  }

  // Phase 2: simulate restart — fresh LogManager reading the same WAL.
  {
    LogManager lm("/tmp/minidb_test_rec_restart.wal");
    auto recs = lm.ReadAll();

    // Pass 1: collect committed txn ids.
    unordered_set<int64_t> committed;
    for (const auto& r : recs) {
      if (r.type == LogRecordType::COMMIT)
        committed.insert(r.txnId);
    }

    // Pass 2: count inserts per outcome.
    size_t committed_inserts = 0, aborted_inserts = 0;
    for (const auto& r : recs) {
      if (r.type != LogRecordType::INSERT)
        continue;
      if (committed.count(r.txnId))
        committed_inserts++;
      else
        aborted_inserts++;
    }
    assert(committed_inserts == 2);
    assert(aborted_inserts == 1);

    RecoveryManager rm(&lm);
    rm.Redo();
    rm.Undo();
  }

  remove("/tmp/minidb_test_rec_restart.wal");
  cout << "ALL RECOVERY RESTART TESTS PASSED" << endl;
  return 0;
}
