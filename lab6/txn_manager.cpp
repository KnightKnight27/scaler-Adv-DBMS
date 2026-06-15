// Lab 6: Transaction manager with MVCC + Strict 2PL + deadlock detection
// Aditya Bhaskara (24BCS10058)
//
// This brings together the three pieces at the core of PostgreSQL's concurrency
// control:
//
//   MVCC          Every write creates a new row version stamped with the
//                 transaction that made it (xmin) and, once superseded, the
//                 transaction that retired it (xmax). A reader walks the version
//                 chain and keeps the first version visible to its snapshot, so
//                 readers never block writers and never see uncommitted data.
//
//   Strict 2PL    A transaction acquires shared or exclusive locks while it runs
//                 (the growing phase) and releases them all at once on commit or
//                 abort (the shrinking phase). Holding every lock until the end
//                 gives serialisable writes and avoids cascading aborts.
//
//   Deadlock      Locks can wait on each other in a cycle. We keep a waits-for
//   detection     graph and run a DFS on each blocked request; if a cycle
//                 appears, the requesting transaction aborts to break it.
//
// Aborts need no physical rollback here: the visibility rule already hides the
// writes of any transaction that is not committed, which is exactly how a real
// system leans on its commit log rather than undoing rows in place.
//
// Build: g++ -std=c++17 -pthread -o txn_manager txn_manager.cpp
// Run:   ./txn_manager

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <list>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using TxId   = uint64_t;
using RowKey = std::string;

enum class TxStatus { Active, Committed, Aborted };
enum class LockMode { Shared, Exclusive };

// One mutex around std::cout so the worker threads in the demo do not interleave.
std::mutex g_print_mu;
void say(const std::string& message) {
    std::lock_guard<std::mutex> lk(g_print_mu);
    std::cout << message << "\n";
}

class DeadlockError : public std::runtime_error {
public:
    explicit DeadlockError(TxId xid)
        : std::runtime_error("deadlock detected, aborting tx " + std::to_string(xid)),
          victim(xid) {}
    TxId victim;
};

// ----------------------------------------------------------------------------
// Transaction table: assigns ids and tracks status and 2PL phase.
// ----------------------------------------------------------------------------
// We use the transaction's own id as its snapshot: a reader with id R sees row
// versions written by committed transactions whose id is below R. That is a
// deliberate simplification of a real snapshot (which also records the exact set
// of in-flight transactions), but it is enough to show snapshot isolation.

class TransactionTable {
public:
    TxId begin() {
        std::lock_guard<std::mutex> lk(mu_);
        TxId id = next_xid_++;
        records_[id] = Record{TxStatus::Active, false};
        return id;
    }

    void set_status(TxId xid, TxStatus status) {
        std::lock_guard<std::mutex> lk(mu_);
        records_.at(xid).status = status;
    }

    void enter_shrinking_phase(TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        if (auto it = records_.find(xid); it != records_.end())
            it->second.shrinking = true;
    }

    bool is_committed(TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = records_.find(xid);
        return it != records_.end() && it->second.status == TxStatus::Committed;
    }

    bool in_shrinking_phase(TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = records_.find(xid);
        return it != records_.end() && it->second.shrinking;
    }

private:
    struct Record {
        TxStatus status;
        bool     shrinking;
    };
    std::mutex                       mu_;
    std::unordered_map<TxId, Record> records_;
    TxId                             next_xid_ = 1;
};

// ----------------------------------------------------------------------------
// Version store: the MVCC heap, one version chain per row (newest first).
// ----------------------------------------------------------------------------

class VersionStore {
public:
    explicit VersionStore(TransactionTable& txns) : txns_(txns) {}

    void insert(const RowKey& key, const std::string& value, TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        heap_[key].push_front(Version{value, xid, 0});
    }

    void update(const RowKey& key, const std::string& value, TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        retire_visible_version(key, xid);
        heap_[key].push_front(Version{value, xid, 0});
    }

    void erase(const RowKey& key, TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        retire_visible_version(key, xid);
    }

    std::optional<std::string> read(const RowKey& key, TxId xid) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = heap_.find(key);
        if (it == heap_.end()) return std::nullopt;
        for (const Version& v : it->second)
            if (visible(v, xid)) return v.value;
        return std::nullopt;
    }

private:
    struct Version {
        std::string value;
        TxId        xmin;   // transaction that created this version
        TxId        xmax;   // transaction that retired it (0 means still live)
    };

    // Stamp the version currently visible to xid as retired by xid.
    void retire_visible_version(const RowKey& key, TxId xid) {
        auto it = heap_.find(key);
        if (it == heap_.end()) return;
        for (Version& v : it->second) {
            if (visible(v, xid) && v.xmax == 0) {
                v.xmax = xid;
                return;
            }
        }
    }

    // The MVCC visibility rule, evaluated against xid's snapshot (which equals
    // xid). A version is visible when its creator is our own transaction or a
    // committed earlier one, and it has not been retired by such a transaction.
    bool visible(const Version& v, TxId reader) {
        bool created_ok = (v.xmin == reader) ||
                          (txns_.is_committed(v.xmin) && v.xmin < reader);
        if (!created_ok) return false;
        if (v.xmax == 0) return true;
        bool retired_for_us = (v.xmax == reader) ||
                              (txns_.is_committed(v.xmax) && v.xmax < reader);
        return !retired_for_us;
    }

    TransactionTable&                                    txns_;
    std::mutex                                           mu_;
    std::unordered_map<RowKey, std::list<Version>>       heap_;
};

// ----------------------------------------------------------------------------
// Lock manager: Strict 2PL with a waits-for graph for deadlock detection.
// ----------------------------------------------------------------------------

class LockManager {
public:
    explicit LockManager(TransactionTable& txns) : txns_(txns) {}

    // Block until the lock is granted. Throws DeadlockError if granting it would
    // close a cycle in the waits-for graph.
    void acquire(const RowKey& key, TxId xid, LockMode mode) {
        if (txns_.in_shrinking_phase(xid))
            throw std::runtime_error("2PL violation: cannot acquire a lock after releasing one");

        LockQueue& queue = queue_for(key);
        std::unique_lock<std::mutex> lk(queue.mu);

        for (const Request& r : queue.requests) {
            if (r.xid == xid && r.granted &&
                (mode == LockMode::Shared || r.mode == LockMode::Exclusive))
                return;                              // already hold an equal or stronger lock
        }

        queue.requests.push_back(Request{xid, mode, false});

        while (true) {
            std::unordered_set<TxId> blockers = conflicting_holders(queue, xid, mode);
            if (blockers.empty()) {
                grant(queue, xid);
                clear_wait(xid);
                return;
            }

            {
                std::lock_guard<std::mutex> g(graph_mu_);
                waits_for_[xid] = blockers;
                if (creates_cycle(xid)) {
                    waits_for_.erase(xid);
                    drop_pending(queue, xid);
                    throw DeadlockError(xid);
                }
            }
            queue.cv.wait(lk);                       // released by some holder's release_all
        }
    }

    void release_all(TxId xid) {
        txns_.enter_shrinking_phase(xid);
        std::lock_guard<std::mutex> table_lk(table_mu_);
        for (auto& [key, queue] : table_) {
            std::lock_guard<std::mutex> queue_lk(queue.mu);
            queue.requests.remove_if([&](const Request& r) { return r.xid == xid; });
            queue.cv.notify_all();
        }
        clear_wait(xid);
    }

private:
    struct Request {
        TxId     xid;
        LockMode mode;
        bool     granted;
    };
    struct LockQueue {
        std::list<Request>      requests;
        std::mutex              mu;
        std::condition_variable cv;
    };

    LockQueue& queue_for(const RowKey& key) {
        std::lock_guard<std::mutex> lk(table_mu_);
        return table_[key];   // references into unordered_map stay valid across rehash
    }

    // Shared locks are compatible with each other; an exclusive lock on either
    // side is a conflict. Our own held locks never block us.
    std::unordered_set<TxId> conflicting_holders(LockQueue& queue, TxId xid, LockMode mode) {
        std::unordered_set<TxId> blockers;
        for (const Request& r : queue.requests) {
            if (!r.granted || r.xid == xid) continue;
            if (mode == LockMode::Exclusive || r.mode == LockMode::Exclusive)
                blockers.insert(r.xid);
        }
        return blockers;
    }

    void grant(LockQueue& queue, TxId xid) {
        for (Request& r : queue.requests)
            if (r.xid == xid && !r.granted) { r.granted = true; return; }
    }

    void drop_pending(LockQueue& queue, TxId xid) {
        queue.requests.remove_if([&](const Request& r) { return r.xid == xid && !r.granted; });
    }

    void clear_wait(TxId xid) {
        std::lock_guard<std::mutex> g(graph_mu_);
        waits_for_.erase(xid);
    }

    // DFS from the freshly blocked transaction. A back edge to a node already on
    // the recursion stack means the new wait closes a cycle. Caller holds graph_mu_.
    bool creates_cycle(TxId start) {
        std::unordered_set<TxId> on_stack;
        std::unordered_set<TxId> done;
        return dfs(start, on_stack, done);
    }

    bool dfs(TxId node, std::unordered_set<TxId>& on_stack, std::unordered_set<TxId>& done) {
        on_stack.insert(node);
        if (auto it = waits_for_.find(node); it != waits_for_.end()) {
            for (TxId next : it->second) {
                if (on_stack.count(next)) return true;
                if (!done.count(next) && dfs(next, on_stack, done)) return true;
            }
        }
        on_stack.erase(node);
        done.insert(node);
        return false;
    }

    TransactionTable&                                        txns_;
    std::mutex                                               table_mu_;
    std::unordered_map<RowKey, LockQueue>                    table_;
    std::mutex                                               graph_mu_;
    std::unordered_map<TxId, std::unordered_set<TxId>>       waits_for_;
};

// ----------------------------------------------------------------------------
// Transaction manager: the public API tying locks and versions together.
// ----------------------------------------------------------------------------

class TransactionManager {
public:
    TxId begin() {
        TxId xid = txns_.begin();
        say("[tx " + std::to_string(xid) + "] begin");
        return xid;
    }

    std::optional<std::string> read(TxId xid, const RowKey& key) {
        locks_.acquire(key, xid, LockMode::Shared);
        return store_.read(key, xid);
    }

    void insert(TxId xid, const RowKey& key, const std::string& value) {
        locks_.acquire(key, xid, LockMode::Exclusive);
        store_.insert(key, value, xid);
    }

    void update(TxId xid, const RowKey& key, const std::string& value) {
        locks_.acquire(key, xid, LockMode::Exclusive);
        store_.update(key, value, xid);
    }

    void remove(TxId xid, const RowKey& key) {
        locks_.acquire(key, xid, LockMode::Exclusive);
        store_.erase(key, xid);
    }

    void commit(TxId xid) {
        txns_.set_status(xid, TxStatus::Committed);
        locks_.release_all(xid);
        say("[tx " + std::to_string(xid) + "] commit");
    }

    void abort(TxId xid) {
        txns_.set_status(xid, TxStatus::Aborted);
        locks_.release_all(xid);
        say("[tx " + std::to_string(xid) + "] abort");
    }

    // Exposed so the deadlock demo can grab raw locks without a read or write.
    void lock(TxId xid, const RowKey& key, LockMode mode) { locks_.acquire(key, xid, mode); }

private:
    TransactionTable txns_;
    VersionStore     store_{txns_};
    LockManager      locks_{txns_};
};

// ----------------------------------------------------------------------------
// Demo scenarios
// ----------------------------------------------------------------------------

void report_read(TxId xid, const RowKey& key, const std::optional<std::string>& value) {
    say("  [tx " + std::to_string(xid) + "] read " + key + " = " +
        (value ? *value : "<not visible>"));
}

void scenario_snapshot_isolation(TransactionManager& tm) {
    say("\n=== Scenario 1: MVCC snapshot isolation ===");
    TxId t1 = tm.begin();
    tm.insert(t1, "balance", "1000");
    tm.commit(t1);

    TxId t2 = tm.begin();   // snapshot taken here, before t3 writes
    TxId t3 = tm.begin();

    tm.update(t3, "balance", "2000");
    tm.commit(t3);

    report_read(t2, "balance", tm.read(t2, "balance"));   // still 1000 for t2
    tm.commit(t2);
}

void scenario_shared_locks(TransactionManager& tm) {
    say("\n=== Scenario 2: concurrent shared locks ===");
    TxId t4 = tm.begin();
    TxId t5 = tm.begin();
    report_read(t4, "balance", tm.read(t4, "balance"));   // latest committed: 2000
    report_read(t5, "balance", tm.read(t5, "balance"));   // shared lock, also granted
    tm.commit(t4);
    tm.commit(t5);
}

void scenario_exclusive_blocks_shared(TransactionManager& tm) {
    say("\n=== Scenario 3: exclusive lock makes a reader wait ===");
    TxId t6 = tm.begin();
    tm.update(t6, "balance", "3000");   // holds the exclusive lock on balance

    std::thread reader([&tm]() {
        TxId t7 = tm.begin();
        say("  [tx " + std::to_string(t7) + "] waiting for shared lock on balance...");
        report_read(t7, "balance", tm.read(t7, "balance"));   // unblocks after t6 commits
        tm.commit(t7);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tm.commit(t6);   // releasing the lock lets the reader proceed
    reader.join();
}

void scenario_deadlock(TransactionManager& tm) {
    say("\n=== Scenario 4: deadlock detection ===");
    TxId t8 = tm.begin();
    TxId t9 = tm.begin();

    tm.lock(t8, "A", LockMode::Exclusive);   // t8 holds A
    tm.lock(t9, "B", LockMode::Exclusive);   // t9 holds B

    std::thread first([&tm, t8]() {
        try {
            tm.lock(t8, "B", LockMode::Exclusive);   // waits on t9
            tm.commit(t8);
        } catch (const DeadlockError& e) {
            say(std::string("  ") + e.what());
            tm.abort(t8);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));   // let t8 register its wait

    try {
        tm.lock(t9, "A", LockMode::Exclusive);   // closes the cycle t9 -> t8 -> t9
        tm.commit(t9);
    } catch (const DeadlockError& e) {
        say(std::string("  ") + e.what());
        tm.abort(t9);
    }

    first.join();
}

int main() {
    TransactionManager tm;
    scenario_snapshot_isolation(tm);
    scenario_shared_locks(tm);
    scenario_exclusive_blocks_shared(tm);
    scenario_deadlock(tm);
    say("\nAll scenarios complete.");
    return 0;
}
