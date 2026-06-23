#pragma once
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include "txn/transaction.h"

namespace minidb {

// A row-level lock manager implementing strict two-phase locking with deadlock
// detection. Shared/exclusive locks are requested per RowId; incompatible
// requests block on a per-resource condition variable. When a request would
// block, the manager builds a waits-for graph and, if a cycle exists, aborts
// the youngest transaction in it (highest txn id).
class LockManager {
 public:
  // Acquire a shared/exclusive lock for `txn` on `rid`. Blocks until granted.
  // Throws TransactionAbortException if `txn` is chosen as a deadlock victim.
  bool LockShared(Transaction *txn, const RowId &rid);
  bool LockExclusive(Transaction *txn, const RowId &rid);

  // Release every lock held by `txn` (called at COMMIT/ABORT — strict 2PL).
  void UnlockAll(Transaction *txn);

 private:
  struct LockRequest {
    txn_id_t txn;
    LockMode mode;
    bool     granted;
  };
  struct LockQueue {
    std::list<LockRequest>  reqs;
    std::condition_variable cv;
  };

  bool Acquire(Transaction *txn, const RowId &rid, LockMode mode);
  bool Grantable(LockQueue &q, txn_id_t self, LockMode mode);
  void NotifyAll();  // wake every waiter after the lock table changes
  // Returns true and sets *victim if a deadlock cycle reachable from
  // `requester` exists; the victim is the youngest txn in the cycle.
  bool DetectDeadlock(txn_id_t requester, txn_id_t *victim);

  std::mutex                          latch_;
  std::map<RowId, LockQueue>          table_;
  std::map<txn_id_t, RowId>           waiting_rid_;   // what each waiter blocks on
  std::map<txn_id_t, LockMode>        waiting_mode_;
  std::map<txn_id_t, Transaction *>   txn_map_;       // id -> txn (to abort victims)
};

}  // namespace minidb
