#pragma once
#include <condition_variable>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "common/types.h"

namespace minidb {

// Raised when a transaction is chosen as a deadlock victim, or asked to lock
// after entering its shrinking phase (2PL violation).
struct TxnAborted : DBError {
  explicit TxnAborted(const std::string& why) : DBError(why) {}
};

enum class LockMode { SHARED, EXCLUSIVE };

// A lock manager implementing strict Two-Phase Locking with shared/exclusive
// row locks. Blocked acquisitions wait on a condition variable; a waits-for
// graph is checked on every wait so cycles are detected and the youngest
// transaction in the cycle is aborted to break the deadlock.
//
// "Strict" 2PL: locks are held until the transaction commits or aborts, at
// which point Release frees them all at once. This gives serializability and
// avoids cascading aborts.
class LockManager {
 public:
  // Acquire a lock on `key`. Blocks until granted; throws TxnAborted if this
  // transaction is selected as a deadlock victim.
  void Acquire(txn_id_t txn, const std::string& key, LockMode mode);

  // Release every lock held by `txn` (called at commit/abort).
  void Release(txn_id_t txn);

 private:
  struct LockEntry {
    txn_id_t txn;
    LockMode mode;
    bool granted;
  };
  struct ResourceQueue {
    std::list<LockEntry> queue;  // granted entries first, then waiters
  };

  bool IsCompatible(const ResourceQueue& rq, txn_id_t txn, LockMode mode) const;
  bool HasDeadlock(txn_id_t waiter);   // DFS over waits_for_, picks a victim
  bool FindCycle(txn_id_t u, std::unordered_set<txn_id_t>& visited,
                 std::unordered_set<txn_id_t>& on_stack, txn_id_t* victim);

  std::mutex latch_;
  std::condition_variable cv_;
  std::unordered_map<std::string, ResourceQueue> table_;
  // who each txn currently holds locks on (for Release).
  std::unordered_map<txn_id_t, std::unordered_set<std::string>> held_;
  // waits_for_[a] = set of txns a is waiting on. Rebuilt during detection.
  std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> waits_for_;
  std::unordered_set<txn_id_t> aborted_;  // victims notified to abort
};

}  // namespace minidb
