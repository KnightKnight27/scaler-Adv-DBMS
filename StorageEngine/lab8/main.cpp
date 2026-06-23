// Lab 8 — Transaction Manager: MVCC + Strict Two-Phase Locking (2PL)
//
// Combines three classic concurrency-control mechanisms:
//   1. MVCC            — every write creates a new row version; readers walk
//                        the version chain and see a consistent snapshot
//                        without blocking writers.
//   2. Strict 2PL      — locks are acquired only in the "growing" phase and
//                        all released together at commit/abort (the shrinking
//                        phase is instantaneous, which avoids cascading aborts).
//   3. Deadlock detect — a waits-for graph is checked for cycles on every
//                        blocked lock request; a cycle aborts the detector.
//
// Build:  g++ -std=c++17 -pthread -o main main.cpp
// Run:    ./main

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <functional>
#include <chrono>

// ─────────────────────────────────────────────
// 1.  Transaction state
// ─────────────────────────────────────────────

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     xid;
    TxID     snapshot_xid;          // sees only commits with xid < snapshot_xid
    TxStatus status      = TxStatus::ACTIVE;
    bool     in_shrinking = false;  // Strict 2PL phase flag
};

// Global transaction table
static std::atomic<TxID>                     g_next_xid{1};
static std::mutex                            g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    // Snapshot = our own xid: any transaction with a larger xid is invisible,
    // which gives snapshot-isolation semantics for this demo.
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

// ─────────────────────────────────────────────
// 2.  MVCC heap — a version chain per row
// ─────────────────────────────────────────────

struct RowVersion {
    std::string value;
    TxID        xmin;   // transaction that created this version
    TxID        xmax;   // transaction that deleted/superseded it (0 = still live)
};

static std::mutex                                        g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;   // newest first

// Visibility rule for a snapshot taken at snapshot_xid by reader_xid.
bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // The creating transaction must be ourselves, or committed before our snapshot.
    bool xmin_ok = (v.xmin == reader_xid)
                || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    // Still live → visible.
    if (v.xmax == 0) return true;

    // If the deletion is visible to us (our own, or committed before our
    // snapshot), then this version is NOT visible; otherwise it still is.
    bool deletion_visible = (v.xmax == reader_xid)
                         || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !deletion_visible;
}

static TxID snapshot_of(TxID xid) {
    std::lock_guard<std::mutex> tlk(g_tx_mutex);
    return g_transactions.at(xid).snapshot_xid;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap = snapshot_of(xid);
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (auto& v : it->second)
        if (is_visible(v, snap, xid)) return v.value;
    return std::nullopt;
}

// INSERT: a fresh version, xmin = us, xmax = 0.
void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

// UPDATE: stamp the currently-visible version with xmax = us, then push a new one.
void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap = snapshot_of(xid);
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;   // logically delete the old version
                break;
            }
        }
    }
    g_heap[key].push_front({new_value, xid, 0});
}

// DELETE: stamp the currently-visible version with xmax = us.
void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap = snapshot_of(xid);
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

// ─────────────────────────────────────────────
// 3.  Lock Manager (Strict 2PL + deadlock detection)
// ─────────────────────────────────────────────
//
// One global mutex + one condition variable guard the whole lock manager.
// This keeps lock ordering trivial (no nested per-queue locks) so the manager
// itself can never deadlock; data deadlocks between transactions are caught by
// the waits-for cycle check below.

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted = false;
};

static std::mutex                                        g_lm_mutex;
static std::condition_variable                           g_lock_cv;
static std::unordered_map<RowKey, std::list<LockRequest>> g_lock_table;

// waits-for graph: waiter -> set of holders it is blocked on.
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

// DFS from `start`; true if it can reach a node already on the recursion stack.
bool has_cycle(TxID start,
               const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID nb : it->second) {
                if (stack.count(nb)) return true;            // back edge → cycle
                if (!visited.count(nb) && dfs(nb)) return true;
            }
        }
        stack.erase(node);
        return false;
    };
    return dfs(start);
}

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

// Grants the lock or blocks until it can be granted. Throws DeadlockException
// if waiting would close a cycle in the waits-for graph.
void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    std::unique_lock<std::mutex> ul(g_lm_mutex);

    // Strict 2PL: no new locks once the shrinking phase has begun.
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    auto& q = g_lock_table[key];

    // Already hold a compatible (or stronger) lock? Then we're done.
    for (auto& r : q) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED)            return;
            if (r.mode == LockMode::EXCLUSIVE)       return;
            // (shared → exclusive upgrade falls through and re-queues)
        }
    }

    q.push_back({xid, mode, false});
    LockRequest* my_req = &q.back();   // pointers into std::list stay valid

    while (true) {
        // A conflict exists if any earlier granted holder (not us) clashes.
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto& r : q) {
            if (&r == my_req) break;            // only earlier requests matter
            if (!r.granted || r.xid == xid) continue;
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                conflict = true;
                blocking.insert(r.xid);
            }
        }

        if (!conflict) {
            my_req->granted = true;
            g_waits_for.erase(xid);
            return;
        }

        // Record who we're waiting on, then look for a deadlock cycle.
        g_waits_for[xid] = blocking;
        if (has_cycle(xid, g_waits_for)) {
            g_waits_for.erase(xid);
            q.remove_if([&](const LockRequest& r){ return r.xid == xid && !r.granted; });
            throw DeadlockException(xid);
        }

        g_lock_cv.wait(ul);   // released on commit/abort, then we re-check
    }
}

// Strict 2PL shrinking phase: release every lock held by xid at once.
void release_locks(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    std::lock_guard<std::mutex> ul(g_lm_mutex);
    for (auto& [key, q] : g_lock_table)
        q.remove_if([&](const LockRequest& r){ return r.xid == xid; });
    g_waits_for.erase(xid);
    g_lock_cv.notify_all();   // wake every waiter to re-check
}

// ─────────────────────────────────────────────
// 4.  Transaction Manager (public API)
// ─────────────────────────────────────────────

class TransactionManager {
public:
    TxID begin() { return begin_transaction(); }

    std::optional<std::string> read(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::SHARED);
        return mvcc_read_key(key, xid);
    }

    void insert(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_insert(key, value, xid);
    }

    void update(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_update(key, value, xid);
    }

    void del(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
    }

    void commit(TxID xid) {
        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        // Roll back our MVCC effects: own inserts become invisible, own
        // deletes are undone.
        {
            std::lock_guard<std::mutex> lk(g_heap_mutex);
            for (auto& [key, chain] : g_heap) {
                for (auto& v : chain) {
                    if (v.xmin == xid) v.xmax = xid;   // hide rows we created
                    if (v.xmax == xid) v.xmax = 0;     // undo deletions we made
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

// ─────────────────────────────────────────────
// 5.  Demo scenarios
// ─────────────────────────────────────────────

void print_val(const std::optional<std::string>& v, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main() {
    TransactionManager tm;

    // ── Scenario 1: MVCC snapshot isolation ──
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();   // snapshot taken here
        TxID t3 = tm.begin();

        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        // t2's snapshot predates t3, so it still sees 1000.
        print_val(tm.read(t2, "balance"), t2, "balance");
        tm.commit(t2);
    }

    // ── Scenario 2: concurrent shared locks ──
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        print_val(tm.read(t4, "balance"), t4, "balance");  // shared lock granted
        print_val(tm.read(t5, "balance"), t5, "balance");  // shared lock granted too
        tm.commit(t4);
        tm.commit(t5);
    }

    // ── Scenario 3: exclusive lock blocks a reader until commit ──
    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");   // holds EXCLUSIVE on "balance"

        std::thread reader([&]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            auto v = tm.read(t7, "balance");   // blocks until t6 commits
            print_val(v, t7, "balance");
            tm.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6);     // releases the lock, unblocking the reader
        reader.join();
    }

    // ── Scenario 4: deadlock detection ──
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID ta = tm.begin();
        TxID tb = tm.begin();
        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();

        acquire_lock("A", t8, LockMode::EXCLUSIVE);   // t8 holds A
        acquire_lock("B", t9, LockMode::EXCLUSIVE);   // t9 holds B

        // t8 wants B (held by t9); t9 wants A (held by t8) → cycle.
        std::thread th1([&]() {
            try {
                acquire_lock("B", t8, LockMode::EXCLUSIVE);
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            acquire_lock("A", t9, LockMode::EXCLUSIVE);
            tm.commit(t9);
        } catch (DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }

        th1.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}
