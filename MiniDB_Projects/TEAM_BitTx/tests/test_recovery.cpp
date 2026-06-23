#include "recovery/recovery.h"
#include "transaction/lock_manager.h"
#include "transaction/transaction.h"

#include <cassert>
#include <cstdio>
#include <iostream>

using namespace minidb;
using namespace std;

int main() {
  remove("/tmp/minidb_test_rec.wal");

  {
    LogManager lm("/tmp/minidb_test_rec.wal");
    LockManager lck;
    {
      Transaction t1(NextTxnId(), &lm, &lck);
      t1.LockExclusive(1);
      t1.LogInsert(0, 0, "old_value_1");
      t1.LogInsert(0, 1, "old_value_2");
      t1.Commit();
    }
    {
      Transaction t2(NextTxnId(), &lm, &lck);
      t2.LockExclusive(2);
      t2.LogInsert(1, 0, "aborted_value");
      t2.Abort();
    }
  }

  {
    LogManager lm("/tmp/minidb_test_rec.wal");
    auto recs = lm.ReadAll();
    size_t begins = 0, commits = 0, aborts = 0, inserts = 0;
    for (const auto& r : recs) {
      if (r.type == LogRecordType::BEGIN)
        begins++;
      else if (r.type == LogRecordType::COMMIT)
        commits++;
      else if (r.type == LogRecordType::ABORT)
        aborts++;
      else if (r.type == LogRecordType::INSERT)
        inserts++;
    }
    assert(begins == 2);
    assert(commits == 1);
    assert(aborts == 1);
    assert(inserts == 3);

    RecoveryManager rm(&lm);
    rm.Redo();
    rm.Undo();
  }

  remove("/tmp/minidb_test_rec.wal");
  cout << "ALL RECOVERY TESTS PASSED" << endl;
  return 0;
}