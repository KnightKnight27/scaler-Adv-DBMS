// lock_manager.h
// -----------------------------------------------------------------------------
// Strict Two-Phase Locking (Strict 2PL) lock manager + wait-for graph +
// deadlock detection.
//
// In our chosen MV2PL model only WRITES take locks (exclusive, X). Reads are
// lock-free MVCC snapshot reads. We still implement the full S/X compatibility
// matrix and lock UPGRADE so the manager is general and matches the assignment.
//
// Compatibility matrix (requested vs held-by-OTHERS):
//          held S    held X
//   req S    OK       WAIT
//   req X   WAIT      WAIT
//   (a txn already holding S on an item may UPGRADE to X iff no OTHER txn holds
//    a lock on that item.)
//
// "Strict" => locks are NOT released early. TxnManager releases all of a txn's
// locks together only at commit or abort.
//
// When a request cannot be granted, the requester WAITS. We record edges in the
// wait-for graph (requester -> every txn currently holding an incompatible lock)
// and run DFS cycle detection. If a cycle exists we pick a victim and report it
// so the caller can abort that txn, breaking the deadlock.
// -----------------------------------------------------------------------------
#ifndef LAB8_LOCK_MANAGER_H
#define LAB8_LOCK_MANAGER_H

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "version_store.h"

namespace lab8 {

enum class LockMode { S, X };

// Result of a lock acquisition attempt.
enum class LockResult {
    Granted,    // lock is now held by the requester
    Blocked,    // would have to wait, but no deadlock cycle (benign wait)
    Deadlock    // would block AND a deadlock cycle was detected
};

struct LockReply {
    LockResult result;
    TxnId      victim = 0;  // valid only when result == Deadlock: txn to abort
};

class LockManager {
public:
    // State of locks on a single data item.
    struct LockEntry {
        std::unordered_set<TxnId> shared;   // holders of S
        TxnId                     exclusive = 0;  // holder of X (0 = none)
    };

    // Try to acquire `mode` on `item` for `txn`.
    // Returns Granted if the lock is (now) held, or Deadlock with a victim if
    // the request would block in a cycle. If it would block WITHOUT a cycle,
    // the caller decides how to handle the wait (in our deterministic single-
    // threaded sim, a benign block is reported via would_block()).
    LockReply acquire(TxnId txn, const std::string& item, LockMode mode);

    // Does `txn` already hold a lock granting `mode` on `item`?
    bool holds(TxnId txn, const std::string& item, LockMode mode) const;

    // Returns true if a fresh request for (txn, item, mode) would have to wait
    // (i.e. some OTHER txn holds an incompatible lock). Used by the demo to
    // narrate "T2's write blocks".
    bool would_block(TxnId txn, const std::string& item, LockMode mode) const;

    // Release every lock held by txn (called on commit/abort) and clear its
    // wait-for edges.
    void release_all(TxnId txn);

    // Expose the wait-for graph for narration/inspection.
    const std::unordered_map<TxnId, std::set<TxnId>>& wait_for() const {
        return wait_for_;
    }

private:
    // Add edges requester -> each blocker, then DFS for a cycle.
    // Returns the chosen victim if a cycle is found, else 0.
    TxnId detect_deadlock(TxnId requester,
                          const std::vector<TxnId>& blockers);

    bool dfs_cycle(TxnId start, TxnId node,
                   std::unordered_set<TxnId>& visiting,
                   std::unordered_set<TxnId>& done) const;

    // List the txns (other than `txn`) that hold a lock incompatible with a
    // request of `mode` on `item`.
    std::vector<TxnId> conflict_holders(TxnId txn, const std::string& item,
                                        LockMode mode) const;

    std::unordered_map<std::string, LockEntry>    locks_;
    std::unordered_map<TxnId, std::set<TxnId>>    wait_for_;
};

}  // namespace lab8

#endif  // LAB8_LOCK_MANAGER_H
