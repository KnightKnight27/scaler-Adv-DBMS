// ============================================================================
// lock_manager.h  --  Grants and releases locks, enforcing 2PL and detecting
// deadlocks.
//
// GRANULARITY: we lock whole TABLES (not individual rows).  This is coarse but
// has a big payoff in clarity -- table-level shared/exclusive locks give true
// SERIALIZABLE isolation with no phantom problem, and the rules are tiny:
//
//   * SHARED (S)    lock: needed to READ a table.  Many txns may hold S together.
//   * EXCLUSIVE (X) lock: needed to WRITE a table.  Only one holder, and it
//                          excludes all S holders.
//
// COMPATIBILITY:        held S    held X
//        request S       yes        no
//        request X        no        no
//
// DEADLOCK: two txns can each wait for a lock the other holds, forever.  Before
// a request blocks, we build the WAIT-FOR GRAPH (edge A->B means "A waits for a
// lock B holds") and look for a cycle through the requester.  If there is one we
// refuse the request and the caller aborts that transaction, breaking the cycle.
// ============================================================================
#pragma once

#include "common/common.h"
#include "txn/transaction.h"
#include <condition_variable>

namespace minidb {

class LockManager {
 public:
  // acquire S/X lock on `table` for `txn`.  Blocks until granted.  Throws a
  // TxnError if granting would deadlock (the caller must then abort the txn).
  void lockShared(Transaction *txn, const string &table);
  void lockExclusive(Transaction *txn, const string &table);

  // Release every lock held by txn (called once, at commit or abort).
  void releaseAll(Transaction *txn);

 private:
  struct Request {
    txn_id_t txn;
    LockMode mode;
    bool     granted;
  };
  // The lock state of a single table: an ordered queue of requests.  Granted
  // requests sit at the front; waiters follow.
  struct LockQueue {
    list<Request> queue;
    condition_variable cv;
  };

  // True if `r` can be granted given the requests already granted on this table.
  bool compatible(const LockQueue &q, const Request &r) const;
  // Cycle detection over the wait-for graph, starting from `start`.
  bool hasCycle(txn_id_t start);
  // Build the current wait-for graph from all lock queues.
  unordered_map<txn_id_t, vector<txn_id_t>> buildWaitForGraph();

  void acquire(Transaction *txn, const string &table, LockMode mode);

  mutex latch_;
  unordered_map<string, LockQueue> tables_;  // table name -> its queue
};

}  // namespace minidb
