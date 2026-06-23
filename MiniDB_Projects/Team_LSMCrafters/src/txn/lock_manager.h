#pragma once
#include <condition_variable>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "txn/transaction.h"

namespace minidb {

enum class LockMode { Shared, Exclusive };

// Row-level lock manager enforcing strict two-phase locking. Shared locks are
// mutually compatible; an exclusive lock conflicts with everything. A blocked
// request waits on a condition variable; before waiting it records waits-for
// edges and runs cycle detection, aborting itself (via TxnAbortException) if it
// would close a deadlock cycle. Locks are released only by release_all, which
// the transaction manager calls exactly once at commit or abort -- so a
// transaction can never release a lock early.
//
// A single mutex guards all state, which keeps the manager free of internal
// lock-ordering bugs at the cost of some concurrency (fine for a teaching DB).
class LockManager {
 public:
  void lock_shared(TxnId txn, const RowKey& key)    { acquire(txn, key, LockMode::Shared); }
  void lock_exclusive(TxnId txn, const RowKey& key) { acquire(txn, key, LockMode::Exclusive); }
  void release_all(TxnId txn);

 private:
  struct Request {
    TxnId    txn;
    LockMode mode;
    bool     granted;
  };
  struct Entry {
    std::list<Request> queue;
  };

  void acquire(TxnId txn, const RowKey& key, LockMode mode);
  bool compatible(LockMode a, LockMode b) const {
    return a == LockMode::Shared && b == LockMode::Shared;
  }
  bool can_grant(const Entry& entry, TxnId txn, LockMode mode) const;
  bool reaches(TxnId from, TxnId target, std::unordered_set<TxnId>& seen) const;

  std::mutex              mu_;
  std::condition_variable cv_;
  std::unordered_map<RowKey, Entry, RowKeyHash>                       table_;
  std::unordered_map<TxnId, std::unordered_set<RowKey, RowKeyHash>>   held_;
  std::unordered_map<TxnId, std::unordered_set<TxnId>>               waits_for_;
};

}  // namespace minidb
