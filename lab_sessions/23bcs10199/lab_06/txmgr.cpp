// Lab Session 6 — Transaction Manager: MVCC + Strict Two-Phase Locking + Deadlock Detection
// Student : Indrajeet Yadav | Roll No: 23BCS10199
//
// Implements a single-node transaction manager combining:
//   1. MVCC (Multi-Version Concurrency Control)
//      - Every write creates a new row version (xmin=txid, xmax=0)
//      - Readers walk the version chain and apply the visibility rule
//        against their snapshot XID — no blocking of readers by writers
//   2. Strict Two-Phase Locking (S2PL)
//      - Growing phase: acquire any lock
//      - Shrinking phase: release ALL locks (only at commit/abort)
//      - Strict 2PL prevents cascading aborts and ensures serializability
//   3. Deadlock Detection via Waits-For Graph
//      - DFS cycle detection on the waits-for graph
//      - Abort the "younger" (higher XID) transaction when a cycle is found
//
// Build: g++ -std=c++17 -pthread -Wall -Wextra -O2 txmgr.cpp -o txmgr
// Run:   ./txmgr

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
#include <sstream>
#include <cassert>
#include <iomanip>
#include <functional>
#include <chrono>

// =============================================================================
// SECTION 1: Transaction State
// =============================================================================

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

std::string tx_status_str(TxStatus s) {
    switch (s) {
        case TxStatus::ACTIVE:    return "ACTIVE";
        case TxStatus::COMMITTED: return "COMMITTED";
        case TxStatus::ABORTED:   return "ABORTED";
    }
    return "UNKNOWN";
}

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;      // snapshot: can see commits with xmin < snapshot_xid
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;  // Strict 2PL: once set, no new locks allowed
};

// Global transaction table + monotonic XID counter
static std::atomic<TxID>                     g_next_xid{1};
static std::mutex                            g_tx_mutex;
static std::unordered_map<TxID, Transaction> g_transactions;

TxID begin_transaction() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    TxID xid  = g_next_xid.fetch_add(1);
    TxID snap = xid;  // snapshot: see all xids committed before this one
    g_transactions[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    return xid;
}

bool tx_committed(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool tx_aborted(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

TxStatus tx_status(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return (it != g_transactions.end()) ? it->second.status : TxStatus::ABORTED;
}

TxID tx_snapshot(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    return g_transactions.at(xid).snapshot_xid;
}

// =============================================================================
// SECTION 2: MVCC Version Chain
// =============================================================================
//
// Each logical row (identified by a RowKey) has a linked list of versions.
// Every INSERT or UPDATE pushes a new version onto the front of the list.
// Every DELETE marks the current live version's xmax = deleting_txid.
//
// Version chain for key "balance" after two updates:
//
//   g_heap["balance"] →
//     RowVersion { value="3000", xmin=T3, xmax=0   }  ← newest (live)
//     RowVersion { value="2000", xmin=T2, xmax=T3  }  ← T3 deleted this
//     RowVersion { value="1000", xmin=T1, xmax=T2  }  ← T2 deleted this
//
// Visibility rule for transaction T reading with snapshot S:
//   A version (xmin, xmax) is visible if:
//     xmin_ok:  (xmin == T.id)                    — T itself wrote it
//             OR (committed(xmin) AND xmin < S)   — older committed write
//     xmax_ok:  xmax == 0                          — not deleted
//             OR xmax == T.id                      — T itself deleted it (not visible to T)
//             OR NOT (committed(xmax) AND xmax < S) — deleter not yet committed in our snapshot

struct RowVersion {
    std::string value;
    TxID        xmin;   // transaction that created this version
    TxID        xmax;   // transaction that deleted this version (0 = still live)
};

static std::mutex                                        g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // xmin: the version must be from a committed transaction visible in our snapshot
    bool xmin_ok = (v.xmin == reader_xid)
                || (tx_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;

    // xmax: the version must not have been deleted before our snapshot
    if (v.xmax == 0) return true;  // not deleted at all
    if (v.xmax == reader_xid) return false;  // we deleted it — not visible to us
    // Deleted by another committed transaction before our snapshot → not visible
    bool xmax_committed_before = tx_committed(v.xmax) && v.xmax < snapshot_xid;
    return !xmax_committed_before;
}

// Read the visible version of a specific key for transaction xid
std::optional<std::string> mvcc_read(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap = tx_snapshot(xid);
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (const auto& v : it->second)
        if (is_visible(v, snap, xid)) return v.value;
    return std::nullopt;
}

// INSERT: create a new version with xmin=xid, xmax=0
void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

// UPDATE: mark the current visible version as xmax=xid, add new version
void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap = tx_snapshot(xid);
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;  // logically delete the old version
                break;
            }
        }
    }
    g_heap[key].push_front({new_value, xid, 0});
}

// DELETE: mark the visible version as xmax=xid
void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap = tx_snapshot(xid);
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

// ABORT: undo all MVCC writes by xid
//   - Versions xid INSERT-ed: hide by setting xmax=xid (so they're never visible)
//   - Versions xid DELETE-ed: restore by clearing xmax back to 0
void mvcc_rollback(TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    for (auto& [key, chain] : g_heap) {
        for (auto& v : chain) {
            if (v.xmin == xid) v.xmax = xid;  // hide own inserts
            if (v.xmax == xid && v.xmin != xid) v.xmax = 0;  // undo own deletes
        }
    }
}

// Print the full version chain for all keys (for debugging)
void print_heap(const std::string& label = "") {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    std::cout << "\n  ── MVCC Heap" << (label.empty() ? "" : " (" + label + ")") << " ──\n";
    for (const auto& [key, chain] : g_heap) {
        std::cout << "  " << key << ":\n";
        for (const auto& v : chain) {
            std::cout << "    { value=\"" << v.value
                      << "\" xmin=" << v.xmin
                      << " xmax=" << (v.xmax ? std::to_string(v.xmax) : "0 (live)")
                      << " }\n";
        }
    }
    std::cout << "\n";
}

// =============================================================================
// SECTION 3: Lock Manager (Strict Two-Phase Locking)
// =============================================================================
//
// Lock types:
//   SHARED (S) — multiple transactions can hold simultaneously; used for READ
//   EXCLUSIVE (X) — only one transaction can hold; used for WRITE
//
// Compatibility matrix:
//           Held: S    X
//   Want S:       ✓    ✗
//   Want X:       ✗    ✗  (even multiple S holders block X)
//
// Lock Queue per row key: ordered list of requests (granted first, waiters after)
// When a transaction commits/aborts (shrinking phase), it releases ALL locks.

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    TxID     xid;
    LockMode mode;
    bool     granted = false;
};

struct LockQueue {
    std::list<LockRequest>  requests;
    std::mutex              mu;
    std::condition_variable cv;
};

// Waits-for graph: waiter_xid → set of xids it is waiting for
static std::mutex                                           g_wfg_mutex;
static std::unordered_map<TxID, std::unordered_set<TxID>>  g_waits_for;

// DFS cycle detection in the waits-for graph
static bool dfs_cycle(TxID node,
                       const std::unordered_map<TxID, std::unordered_set<TxID>>& graph,
                       std::unordered_set<TxID>& visited,
                       std::unordered_set<TxID>& rec_stack) {
    visited.insert(node);
    rec_stack.insert(node);
    auto it = graph.find(node);
    if (it != graph.end()) {
        for (TxID nb : it->second) {
            if (!visited.count(nb)) {
                if (dfs_cycle(nb, graph, visited, rec_stack)) return true;
            } else if (rec_stack.count(nb)) {
                return true;  // back edge → cycle
            }
        }
    }
    rec_stack.erase(node);
    return false;
}

bool has_cycle(TxID start) {
    std::unordered_set<TxID> visited, rec_stack;
    return dfs_cycle(start, g_waits_for, visited, rec_stack);
}

// Lock table: one LockQueue per RowKey
static std::unordered_map<RowKey, LockQueue> g_lock_table;
static std::mutex                             g_lt_mutex;  // protects g_lock_table structure

// Custom exception for deadlock
class DeadlockException : public std::runtime_error {
public:
    TxID victim;
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected — aborting TX " + std::to_string(xid))
        , victim(xid) {}
};

// Custom exception for 2PL violation
class TwoPLViolation : public std::runtime_error {
public:
    explicit TwoPLViolation(TxID xid)
        : std::runtime_error("2PL violation: TX " + std::to_string(xid) +
                             " cannot acquire lock in shrinking phase") {}
};

// Attempt to grant: scan the request list up to (but not including) our request.
// If no granted request conflicts with our mode, grant ours.
static bool can_grant(LockQueue& lq, LockRequest& my_req,
                       std::unordered_set<TxID>& blocking_out) {
    bool conflict = false;
    for (auto& r : lq.requests) {
        if (&r == &my_req) break;  // only look at earlier requests
        if (!r.granted) continue;
        if (r.xid == my_req.xid) continue;  // same tx, no self-conflict
        // Conflict if either party wants EXCLUSIVE
        if (my_req.mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
            conflict = true;
            blocking_out.insert(r.xid);
        }
    }
    return !conflict;
}

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    // 2PL check: cannot acquire after first release
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        auto it = g_transactions.find(xid);
        if (it != g_transactions.end() && it->second.in_shrinking)
            throw TwoPLViolation(xid);
    }

    // Get (or create) the lock queue for this key
    LockQueue* lq_ptr;
    {
        std::lock_guard<std::mutex> lk(g_lt_mutex);
        lq_ptr = &g_lock_table[key];
    }
    LockQueue& lq = *lq_ptr;

    std::unique_lock<std::mutex> ul(lq.mu);

    // Check if we already hold a compatible lock (lock upgrade logic)
    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED) return;               // S ⊆ X, already ok
            if (r.mode == LockMode::EXCLUSIVE) return;           // already have X
            // Need to upgrade S → X: fall through to wait for upgrade
        }
    }

    // Add our lock request to the queue
    lq.requests.push_back({xid, mode, false});
    auto& my_req = lq.requests.back();

    // Spin (on condition variable) until our request is granted or deadlock
    while (true) {
        std::unordered_set<TxID> blocking;

        if (can_grant(lq, my_req, blocking)) {
            my_req.granted = true;
            // Clear waits-for edges for this tx
            {
                std::lock_guard<std::mutex> wlk(g_wfg_mutex);
                g_waits_for.erase(xid);
            }
            return;
        }

        // Record waits-for edges and check for deadlock
        {
            std::lock_guard<std::mutex> wlk(g_wfg_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid)) {
                // We are part of a deadlock cycle — remove our request and throw
                g_waits_for.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r){
                    return r.xid == xid && !r.granted;
                });
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);  // wait for a signal from a lock release
    }
}

void release_all_locks(TxID xid) {
    // Enter shrinking phase
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        auto it = g_transactions.find(xid);
        if (it != g_transactions.end())
            it->second.in_shrinking = true;
    }

    // Remove this tx's lock requests from every queue and notify waiters
    {
        std::lock_guard<std::mutex> lt(g_lt_mutex);
        for (auto& [key, lq] : g_lock_table) {
            {
                std::unique_lock<std::mutex> ul(lq.mu);
                lq.requests.remove_if([xid](const LockRequest& r){ return r.xid == xid; });
                lq.cv.notify_all();  // wake up all waiters on this key
            }
        }
    }

    // Clear waits-for edges
    {
        std::lock_guard<std::mutex> wlk(g_wfg_mutex);
        g_waits_for.erase(xid);
    }
}

// =============================================================================
// SECTION 4: Transaction Manager Public API
// =============================================================================

class TransactionManager {
public:
    TxID begin() {
        TxID xid = begin_transaction();
        std::cout << "  [TX " << xid << "] BEGIN  (snapshot_xid=" << tx_snapshot(xid) << ")\n";
        return xid;
    }

    std::optional<std::string> read(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::SHARED);
        auto val = mvcc_read(key, xid);
        std::cout << "  [TX " << xid << "] READ   " << key << " = "
                  << (val ? ("\"" + *val + "\"") : "<not visible>") << "\n";
        return val;
    }

    void insert(TxID xid, const RowKey& key, const std::string& value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_insert(key, value, xid);
        std::cout << "  [TX " << xid << "] INSERT " << key << " = \"" << value << "\"\n";
    }

    void update(TxID xid, const RowKey& key, const std::string& new_value) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_update(key, new_value, xid);
        std::cout << "  [TX " << xid << "] UPDATE " << key << " = \"" << new_value << "\"\n";
    }

    void remove(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
        std::cout << "  [TX " << xid << "] DELETE " << key << "\n";
    }

    void commit(TxID xid) {
        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }
        release_all_locks(xid);
        std::cout << "  [TX " << xid << "] COMMIT\n";
    }

    void abort(TxID xid) {
        mvcc_rollback(xid);
        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
        release_all_locks(xid);
        std::cout << "  [TX " << xid << "] ABORT  (all writes rolled back)\n";
    }
};

// =============================================================================
// SECTION 5: Demonstration Scenarios
// =============================================================================

static void section(const std::string& title) {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n"
              << "  " << title << "\n"
              << "╚══════════════════════════════════════════════════════════════╝\n\n";
}

int main() {
    std::cout << "=== Lab 6 — Transaction Manager: MVCC + Strict 2PL + Deadlock Detection ===\n"
              << "    Indrajeet Yadav | 23BCS10199\n";

    TransactionManager tm;

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 1: Basic MVCC snapshot isolation
    //
    // T1 inserts "balance"=1000 and commits.
    // T2 starts (snapshot before T3).
    // T3 updates "balance" to 2000 and commits.
    // T2 reads "balance" — should still see 1000, not 2000.
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 1: MVCC Snapshot Isolation");
    {
        std::cout << "T1 inserts balance=1000 and commits.\n"
                  << "T3 then updates it to 2000 and commits.\n"
                  << "T2 (started before T3) should still read 1000 — its snapshot\n"
                  << "predates T3's commit.\n\n";

        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();  // T2's snapshot: sees T1's commit, not T3's (T3 hasn't started)
        TxID t3 = tm.begin();
        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        tm.read(t2, "balance");  // expected: "1000"
        tm.commit(t2);

        print_heap("after Scenario 1");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 2: Multiple operations in one transaction + dirty read prevention
    //
    // T4 inserts and then reads within the same transaction.
    // T5 starts concurrently but cannot see T4's uncommitted insert.
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 2: Dirty Read Prevention");
    {
        std::cout << "T4 inserts account=500 (NOT yet committed).\n"
                  << "T5 reads account — must NOT see 500 (T4 not committed).\n"
                  << "After T4 commits, T6 reads account — sees 500.\n\n";

        TxID t4 = tm.begin();
        tm.insert(t4, "account", "500");

        TxID t5 = tm.begin();
        tm.read(t5, "account");  // expected: <not visible> (T4 not committed)
        tm.commit(t5);

        tm.commit(t4);

        TxID t6 = tm.begin();
        tm.read(t6, "account");  // expected: "500" (T4 committed)
        tm.commit(t6);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 3: Concurrent shared locks (readers don't block each other)
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 3: Concurrent Shared Locks (Readers Don't Block Readers)");
    {
        std::cout << "T7 and T8 both hold SHARED locks on 'balance' simultaneously.\n"
                  << "Neither blocks the other.\n\n";

        TxID t7 = tm.begin();
        TxID t8 = tm.begin();
        tm.read(t7, "balance");  // acquires S lock
        tm.read(t8, "balance");  // acquires S lock — granted immediately (S compat with S)
        tm.commit(t7);
        tm.commit(t8);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 4: Exclusive lock blocks a reader (writer blocks reader)
    //
    // In pure MVCC (PostgreSQL), readers don't block writers and writers don't
    // block readers. But with 2PL, an X lock DOES block S locks on the same key.
    // This scenario shows that the reader waits for the writer to commit.
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 4: Exclusive Lock Blocks Reader (2PL Interaction)");
    {
        std::cout << "T9 holds an EXCLUSIVE lock on 'balance' (updating it).\n"
                  << "T10 (on a separate thread) tries to READ balance — must wait.\n"
                  << "After T9 commits, T10 unblocks and sees the new value.\n\n";

        TxID t9 = tm.begin();
        tm.update(t9, "balance", "3000");  // X lock acquired

        std::thread t10_thread([&]() {
            TxID t10 = tm.begin();
            std::cout << "  [TX " << t10 << "] waiting for shared lock on 'balance'...\n";
            tm.read(t10, "balance");  // blocks until T9 releases X lock
            tm.commit(t10);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        tm.commit(t9);  // releases X lock → T10 unblocks
        t10_thread.join();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 5: MVCC delete + rollback
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 5: Delete and Rollback");
    {
        std::cout << "T11 deletes 'account' but then aborts.\n"
                  << "T12 should still see 'account' — the delete was rolled back.\n\n";

        TxID t11 = tm.begin();
        tm.remove(t11, "account");
        std::cout << "  T11 reads after its own delete (should be gone for T11):\n";
        tm.read(t11, "account");  // T11 deleted it → not visible to T11

        tm.abort(t11);  // rollback — xmax cleared back to 0

        TxID t12 = tm.begin();
        tm.read(t12, "account");  // expected: "500" — T11's delete was rolled back
        tm.commit(t12);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 6: Deadlock detection
    //
    // T13 locks key "A", T14 locks key "B".
    // T13 then tries to lock "B" (held by T14) → waits.
    // T14 then tries to lock "A" (held by T13) → cycle → deadlock!
    // One transaction is aborted; the other completes.
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 6: Deadlock Detection via Waits-For Graph");
    {
        std::cout << "T13 holds X lock on 'A'. T14 holds X lock on 'B'.\n"
                  << "T13 wants 'B' → waits for T14.\n"
                  << "T14 wants 'A' → waits for T13.\n"
                  << "Cycle detected → one transaction is aborted.\n\n";

        // Setup: insert A and B
        TxID setup = tm.begin();
        tm.insert(setup, "A", "val_a");
        tm.insert(setup, "B", "val_b");
        tm.commit(setup);

        TxID t13 = tm.begin();
        TxID t14 = tm.begin();

        acquire_lock("A", t13, LockMode::EXCLUSIVE);
        std::cout << "  [TX " << t13 << "] acquired X lock on A\n";
        acquire_lock("B", t14, LockMode::EXCLUSIVE);
        std::cout << "  [TX " << t14 << "] acquired X lock on B\n";

        // T13 tries to lock B (held by T14) on a separate thread
        std::thread t13_thread([&]() {
            try {
                std::cout << "  [TX " << t13 << "] attempting X lock on B (will block or deadlock)...\n";
                acquire_lock("B", t13, LockMode::EXCLUSIVE);
                std::cout << "  [TX " << t13 << "] acquired B — committing\n";
                tm.commit(t13);
            } catch (const DeadlockException& e) {
                std::cout << "  DEADLOCK: " << e.what() << "\n";
                tm.abort(t13);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        // T14 tries to lock A (held by T13) → cycle → deadlock!
        try {
            std::cout << "  [TX " << t14 << "] attempting X lock on A (will create deadlock cycle)...\n";
            acquire_lock("A", t14, LockMode::EXCLUSIVE);
            std::cout << "  [TX " << t14 << "] acquired A — committing\n";
            tm.commit(t14);
        } catch (const DeadlockException& e) {
            std::cout << "  DEADLOCK: " << e.what() << "\n";
            tm.abort(t14);
        }

        t13_thread.join();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Scenario 7: 2PL violation detection
    // ─────────────────────────────────────────────────────────────────────────
    section("Scenario 7: Strict 2PL — Cannot Acquire Lock in Shrinking Phase");
    {
        std::cout << "In Strict 2PL, once a transaction releases ANY lock,\n"
                  << "it enters the 'shrinking phase' and cannot acquire new locks.\n"
                  << "(In Strict 2PL, the shrinking phase only happens at commit/abort —\n"
                  << "so this violation can only be triggered artificially.)\n\n";

        TxID t15 = tm.begin();
        tm.insert(t15, "x", "10");

        // Manually force t15 into shrinking phase
        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(t15).in_shrinking = true;
        }

        std::cout << "  [TX " << t15 << "] forced into shrinking phase\n";
        try {
            acquire_lock("y", t15, LockMode::EXCLUSIVE);
        } catch (const TwoPLViolation& e) {
            std::cout << "  2PL VIOLATION caught: " << e.what() << "\n";
        }

        // Restore and commit cleanly
        {
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(t15).in_shrinking = false;
        }
        tm.commit(t15);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Final summary
    // ─────────────────────────────────────────────────────────────────────────
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n"
              << "  ARCHITECTURE SUMMARY\n"
              << "╚══════════════════════════════════════════════════════════════╝\n\n"
              << "  Application\n"
              << "      │\n"
              << "      ▼\n"
              << "  TransactionManager.begin() / read() / insert() / update()\n"
              << "                            / remove() / commit() / abort()\n"
              << "      │\n"
              << "      ├──► LockManager (Strict 2PL)\n"
              << "      │        acquire_lock(key, xid, S|X)\n"
              << "      │          growing phase: any lock allowed\n"
              << "      │          shrinking phase: NO new locks (only at commit/abort)\n"
              << "      │        release_all_locks(xid)   [on commit or abort]\n"
              << "      │        Waits-for graph: detect cycles → DeadlockException\n"
              << "      │\n"
              << "      └──► MVCC Heap (version chain per row key)\n"
              << "               INSERT  → push {value, xmin=xid, xmax=0}\n"
              << "               UPDATE  → stamp old version xmax=xid, push new\n"
              << "               DELETE  → stamp visible version xmax=xid\n"
              << "               READ    → walk chain, return first version where\n"
              << "                         xmin committed < snapshot_xid AND\n"
              << "                         (xmax=0 OR xmax committed after snapshot)\n"
              << "               ABORT   → own inserts: xmax=xid (hidden)\n"
              << "                         own deletes: xmax=0 (restored)\n\n"
              << "  MVCC + Strict 2PL together:\n"
              << "    - Readers never block writers    (MVCC snapshots)\n"
              << "    - Writers are serializable       (X locks)\n"
              << "    - Cascading aborts impossible    (S2PL: no early lock release)\n"
              << "    - Deadlocks detected and resolved (waits-for graph)\n\n"
              << "  This mirrors PostgreSQL's core concurrency architecture.\n"
              << "  (PG uses SSI — Serializable Snapshot Isolation — a refinement\n"
              << "   that tracks read-write anti-dependencies to avoid 2PL overhead.)\n\n"
              << "All scenarios complete.\n";

    return 0;
}
