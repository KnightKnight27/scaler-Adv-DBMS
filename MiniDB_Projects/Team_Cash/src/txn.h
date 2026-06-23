// Transactions: Strict Two-Phase Locking (2PL) with deadlock detection.
//
// The lock manager grants shared (read) and exclusive (write) locks per key.
// Under Strict 2PL a transaction holds every lock it takes until it commits or
// aborts (the "shrinking phase" happens all at once at the end), which gives
// serializable isolation.
//
// Concurrency is modelled as interleaved lock requests from a single driver
// (a textbook 2PL schedule) rather than OS threads, so the demos are
// deterministic and easy to explain. When a request would wait, we add an edge
// to a waits-for graph and look for a cycle; a cycle is a deadlock, and we
// abort the youngest transaction to break it.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace minidb {

enum class LockMode { Shared, Exclusive };

class LockManager {
public:
    enum Outcome { Granted, Waiting, Deadlock };

    // Try to take `mode` lock on `key` for `txn`.
    Outcome acquire(int txn, int64_t key, LockMode mode);
    // Strict 2PL: release everything the transaction holds, at commit/abort.
    void release(int txn);

    std::string waitsForGraph() const;

private:
    struct Holder { int txn; LockMode mode; };
    std::map<int64_t, std::vector<Holder>> table_;  // key -> current holders
    std::map<int, std::set<int>> waitsFor_;          // txn -> txns it waits on

    static bool incompatible(LockMode a, LockMode b) {
        return a == LockMode::Exclusive || b == LockMode::Exclusive;
    }
    bool reaches(int from, int target, std::set<int>& seen) const;
};

class TransactionManager {
public:
    int begin() { return ++counter_; }
    void commit(int txn) { lm_.release(txn); }
    void abort(int txn) { lm_.release(txn); }
    LockManager& locks() { return lm_; }

private:
    int counter_ = 0;
    LockManager lm_;
};

}  // namespace minidb
