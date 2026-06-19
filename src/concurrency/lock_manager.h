#pragma once

#include <condition_variable>
#include <list>
#include <mutex>
#include <unordered_map>

#include "common/rid.h"
#include "concurrency/transaction.h"

namespace minidb {

enum class LockMode { kShared, kExclusive };

// A row-level lock table implementing STRICT 2PL with WAIT-DIE deadlock
// prevention. Locks are keyed by RID. Shared locks are mutually compatible;
// an exclusive lock conflicts with everything.
//
// Wait-die (uses the txn id as an age — smaller id == older):
//   - if an OLDER transaction holds a conflicting lock, the requester (younger)
//     is aborted immediately (it "dies") — Exception(kAbort);
//   - if only YOUNGER transactions hold conflicting locks, the requester (older)
//     waits until they release.
// Because only older transactions ever wait on younger ones, the wait-for graph
// can never contain a cycle, so the system is deadlock-free by construction.
class LockManager {
 public:
  // Acquire S / X on `rid` for `txn`. Blocks until granted, or throws
  // Exception(kAbort) if wait-die selects this transaction as the victim.
  void LockShared(Transaction *txn, const RID &rid);
  void LockExclusive(Transaction *txn, const RID &rid);

  // Release every lock held by `txn` and wake any waiters (commit/abort).
  void UnlockAll(Transaction *txn);

 private:
  struct Holder {
    txn_id_t txn_id;
    LockMode mode;
  };
  struct LockEntry {
    std::list<Holder> holders;
    std::condition_variable cv;
  };

  void Acquire(Transaction *txn, const RID &rid, LockMode mode);

  std::mutex latch_;
  std::unordered_map<RID, LockEntry> locks_;
};

}  // namespace minidb
