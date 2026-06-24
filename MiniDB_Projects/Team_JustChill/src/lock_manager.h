// lock_manager.h — Track 3 (Query & Concurrency)
//
// Strict Two-Phase Locking (2PL) with TABLE-LEVEL granularity.
// Per the track brief we keep locks coarse (whole-table, not per-row) to save
// time, track them in an unordered_map, and resolve deadlocks with a timeout:
// if a lock cannot be acquired within kLockTimeout, acquire() throws
// std::runtime_error so the caller can abort the transaction.
//
// "Strict" 2PL: a transaction holds every lock it acquires until it commits or
// aborts, at which point release_all() drops them in one shot.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace minidb {

using TxnId = uint64_t;  // 0 is reserved to mean "no transaction".

enum class LockMode { Shared, Exclusive };

class LockManager {
 public:
  // How long to wait for a conflicting lock before giving up (deadlock guard).
  static constexpr std::chrono::seconds kLockTimeout{3};

  LockManager() = default;

  // Acquire `mode` on `table` for transaction `txn`. Blocks until the lock is
  // compatible or kLockTimeout elapses. On timeout throws std::runtime_error.
  // Re-acquiring a lock already held (or upgrading Shared -> Exclusive) is
  // handled transparently.
  void acquire(TxnId txn, const std::string& table, LockMode mode);

  // Release every lock held by `txn` (call on commit or abort). Strict 2PL:
  // this is the only place locks are released.
  void release_all(TxnId txn);

 private:
  struct TableLock {
    std::unordered_set<TxnId> shared_holders;
    TxnId exclusive_holder = 0;  // 0 == not exclusively held
  };

  // Decide whether `txn` may be granted `mode` on `tl` right now. Must be
  // called with mutex_ held.
  bool canGrant(const TableLock& tl, TxnId txn, LockMode mode) const;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::unordered_map<std::string, TableLock> table_locks_;
  // Reverse index so release_all() is O(locks held) instead of scanning all.
  std::unordered_map<TxnId, std::unordered_set<std::string>> txn_tables_;
};

}  // namespace minidb
