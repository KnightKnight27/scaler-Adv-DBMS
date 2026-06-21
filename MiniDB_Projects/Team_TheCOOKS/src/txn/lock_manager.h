#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "common/config.h"
#include "txn/transaction.h"

namespace walterdb {

// ---------------------------------------------------------------------------
// LockManager -- shared/exclusive locks over opaque resource ids (here, rows),
// implementing STRICT two-phase locking:
//   * locks are acquired on demand during a transaction (growing phase),
//   * and ALL released together at commit/abort (strict: held to the end),
// which yields serializable schedules for the locked items.
//
// Compatibility: S/S compatible; X conflicts with everything.  Requests queue
// FIFO per resource (an incompatible request ahead makes later ones wait), which
// prevents writer starvation.
//
// Deadlock handling: WAIT-FOR GRAPH cycle detection.  When a transaction would
// block, we add edges to the transactions it is waiting on and DFS for a cycle;
// if one exists, the waiting transaction is chosen as the victim and its
// acquire() returns false, signalling the caller to abort and roll it back.
// (Aborting the requester always breaks the cycle it just closed.)
//
// This class is internally synchronised; physical page safety is a separate
// concern handled by per-table latches, not by these logical locks.
// ---------------------------------------------------------------------------
class LockManager {
 public:
  // Acquire a shared / exclusive lock for txn on resource.  Blocks until
  // granted; returns false if txn is chosen as a deadlock victim.
  bool lock_shared(Transaction* txn, uint64_t resource);
  bool lock_exclusive(Transaction* txn, uint64_t resource);

  // Release every lock held by txn (strict-2PL end-of-transaction release).
  void unlock_all(Transaction* txn);

 private:
  struct Request {
    txn_id_t txn;
    LockMode mode;
    bool granted;
  };
  struct ResourceQueue {
    std::list<Request> queue;
  };

  bool acquire(Transaction* txn, uint64_t resource, LockMode mode);
  bool can_grant(const ResourceQueue& rq, txn_id_t txn, LockMode mode) const;
  bool has_cycle(txn_id_t start) const;  // DFS over waits_for_

  std::unordered_map<uint64_t, ResourceQueue> table_;
  std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> waits_for_;
  std::mutex latch_;
  std::condition_variable cv_;
};

}  // namespace walterdb
