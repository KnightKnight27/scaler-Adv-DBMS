#pragma once
#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include "txn/transaction.h"

namespace minidb {

// A lock manager implementing STRICT two-phase locking:
//   - locks are acquired during execution (growing phase)
//   - ALL locks are released together at commit/abort (no early release)
// This guarantees serializability and recoverability.
//
// Locks are taken on string-named resources. We use a coarse table-level
// granularity for normal queries ("table:<name>") and the same machinery
// supports finer row-level locks ("row:<table>:<page>:<slot>") for the
// concurrency demo. Shared (S) and Exclusive (X) modes follow the usual
// compatibility matrix (S/S compatible; anything with X conflicts).
//
// Deadlocks are detected with a wait-for graph: before a transaction blocks we
// add edges to the lock holders and run cycle detection; on a cycle the
// requesting transaction is chosen as the victim and aborted.
class LockManager {
public:
    // Acquire `mode` on `resource` for `txn`. Blocks until granted. Throws
    // TransactionAbortException if granting would deadlock.
    void acquire(Transaction *txn, const std::string &resource, LockMode mode);

    // Release every lock held by `txn` (called exactly once at end-of-txn).
    void release_all(Transaction *txn);

    // Helpers to name lockable resources consistently.
    static std::string table_resource(const std::string &t) { return "table:" + t; }
    static std::string row_resource(const std::string &t, const RID &r) {
        return "row:" + t + ":" + std::to_string(r.page_id) + ":" + std::to_string(r.slot_id);
    }

private:
    struct Request {
        txn_id_t txn;
        LockMode mode;
        bool     granted;
    };
    struct LockQueue {
        std::list<Request>      requests;
        std::condition_variable cv;
    };

    // True if a request of `mode` by `self` conflicts with another granted lock.
    bool has_conflict(LockQueue &q, txn_id_t self, LockMode mode);
    // DFS over waits_for_ looking for a cycle reachable from `start`.
    bool detects_cycle(txn_id_t start);

    std::mutex                                          latch_;
    std::map<std::string, std::unique_ptr<LockQueue>>   table_;     // resource -> queue
    std::map<txn_id_t, std::set<txn_id_t>>              waits_for_; // wait-for graph
};

} // namespace minidb
