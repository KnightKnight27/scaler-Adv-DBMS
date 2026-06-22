#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/config.h"

namespace minidb {

enum class LockMode { S, X };  // shared (read) / exclusive (write)

// A lock manager implementing Strict Two-Phase Locking. Resources are addressed
// by an int64 id (the engine encodes table+key into one). Because MiniDB drives
// concurrency with a deterministic single-threaded scheduler (the toolchain has
// no std::thread), "blocking" is represented logically: Acquire returns false
// and the request is parked as waiting, rather than blocking an OS thread. This
// makes deadlock scenarios fully reproducible.
class LockManager {
 public:
  // Try to acquire `mode` on `res` for `txn`. Returns true if granted, false if
  // it must wait (the request is recorded as waiting).
  bool Acquire(TxnId txn, int64_t res, LockMode mode);

  // Release every lock held/waited by `txn` (called at commit or abort, per
  // strict 2PL). Returns the transactions whose waiting requests just became
  // granted as a result, so the scheduler can wake them.
  std::vector<TxnId> ReleaseAll(TxnId txn);

  // Look for a cycle in the wait-for graph. If found, returns true and sets
  // *victim to the youngest (largest id) transaction in the cycle.
  bool DetectDeadlock(TxnId* victim);

  // Human-readable lock-table dump for demos.
  std::string Dump() const;

 private:
  struct Req {
    TxnId    txn;
    LockMode mode;
    bool     granted;
  };
  // S/S is compatible; anything involving X conflicts.
  static bool Conflicts(LockMode a, LockMode b) {
    return a == LockMode::X || b == LockMode::X;
  }
  void GrantWaiters(int64_t res, std::vector<TxnId>* woken);

  std::unordered_map<int64_t, std::vector<Req>> table_;  // resource -> requests
};

}  // namespace minidb
