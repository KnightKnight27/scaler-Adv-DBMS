// Lab 6: Transaction Manager — MVCC + Two-Phase Locking
// Canonical POSIX/C++11+ version (std::thread + std::mutex + std::condition_variable).
//
// Compile (any modern GCC/Clang/MSVC):
//   Linux/macOS:  g++ -std=c++17 -pthread -o txmgr txmgr_posix.cpp
//   MSVC:         cl /EHsc /std:c++17 txmgr_posix.cpp
// Run:           ./txmgr
//
// This is the "textbook" version that mirrors PostgreSQL's concurrency design.
// A Win32-thread variant (txmgr.cpp) is also provided that runs on legacy MinGW.

#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <chrono>
#include <functional>
#include <string>

// ════════════════════════════════════════════════════════════════════
// 1. Transaction state
// ════════════════════════════════════════════════════════════════════

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;
};

static std::mutex                                 g_tx_mutex;
static std::unordered_map<TxID, Transaction>      g_transactions;
static TxID                                       g_next_xid = 1;

TxID begin_transaction() {
    std::lock_guard lk(g_tx_mutex);
    TxID xid = ++g_next_xid;
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

// ════════════════════════════════════════════════════════════════════
// 2. MVCC version chain
// ════════════════════════════════════════════════════════════════════

struct RowVersion {
    std::string value;
    TxID        xmin = 0;
    TxID        xmax = 0;
};

static std::mutex                                       g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

struct ReadResult {
    std::string value;
    bool        found = false;
    ReadResult() = default;
    ReadResult(std::string v, bool f) : value(std::move(v)), found(f) {}
};

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid)
                || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;
    if (v.xmax == 0) return true;

    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(v.xmax);
    bool committed = it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
    bool own = (v.xmax == reader_xid);

    if (own) return true;
    if (committed && v.xmax < snapshot_xid) return false;
    return true;
}

ReadResult mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    { std::lock_guard tlk(g_tx_mutex); snap = g_transactions.at(xid).snapshot_xid; }

    auto it = g_heap.find(key);
    if (it == g_heap.end()) return ReadResult{};
    for (auto& v : it->second)
        if (is_visible(v, snap, xid)) return ReadResult{v.value, true};
    return ReadResult{};
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    { std::lock_guard tlk(g_tx_mutex); snap = g_transactions.at(xid).snapshot_xid; }

    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }
    g_heap[key].push_front({new_value, xid, 0});
}

// ════════════════════════════════════════════════════════════════════
// 3. Lock Manager (Strict 2PL)
// ════════════════════════════════════════════════════════════════════

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

static std::mutex                                      g_lm_mutex;
static std::unordered_map<RowKey, LockQueue>           g_lock_table;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        if (!visited.insert(node).second) return false;
        auto it = graph.find(node);
        if (it == graph.end()) return false;
        for (TxID nb : it->second) {
            if (nb == start) return true;
            if (dfs(nb)) return true;
        }
        return false;
    };
    return dfs(start);
}

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    {
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: acquire in shrinking phase");
    }

    LockQueue& lq = g_lock_table[key];
    std::unique_lock ul(lq.mu);

    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted && r.mode == LockMode::EXCLUSIVE) return;
    }

    lq.requests.push_back({xid, mode, false});
    auto it_self = std::prev(lq.requests.end());

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto it = lq.requests.begin(); it != it_self; ++it) {
            if (!it->granted) continue;
            if (mode == LockMode::EXCLUSIVE || it->mode == LockMode::EXCLUSIVE) {
                if (it->xid != xid) { conflict = true; blocking.insert(it->xid); }
            }
        }

        if (!conflict) {
            it_self->granted = true;
            { std::lock_guard lk(g_lm_mutex); g_waits_for.erase(xid); }
            return;
        }

        {
            std::lock_guard lk(g_lm_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.erase(it_self);
                throw DeadlockException(xid);
            }
        }

        lq.cv.wait(ul);
    }
}

void release_locks(TxID xid) {
    {
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }
    for (auto& kv : g_lock_table) {
        auto& lq = kv.second;
        std::unique_lock ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid; });
        lq.cv.notify_all();
    }
    { std::lock_guard lk(g_lm_mutex); g_waits_for.erase(xid); }
}

// ════════════════════════════════════════════════════════════════════
// 4. Public Transaction Manager
// ════════════════════════════════════════════════════════════════════

class TransactionManager {
public:
    TxID begin() { return begin_transaction(); }

    ReadResult read(TxID xid, const RowKey& key) {
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

    void commit(TxID xid) {
        { std::lock_guard lk(g_tx_mutex); g_transactions.at(xid).status = TxStatus::COMMITTED; }
        release_locks(xid);
        std::cout << "  [TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        {
            std::lock_guard lk(g_heap_mutex);
            for (auto& kv : g_heap)
                for (auto& v : kv.second) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid) v.xmax = 0;
                }
        }
        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
        release_locks(xid);
        std::cout << "  [TX " << xid << "] ABORTED\n";
    }
};

// ════════════════════════════════════════════════════════════════════
// 5. Demo scenarios
// ════════════════════════════════════════════════════════════════════

static void print_val(const ReadResult& v, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v.found ? v.value : "<not visible>") << "\n";
}

int main() {
    TransactionManager tm;

    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();
        TxID t3 = tm.begin();
        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        auto v = tm.read(t2, "balance");
        print_val(v, t2, "balance");
        tm.commit(t2);
    }

    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        print_val(tm.read(t4, "balance"), t4, "balance");
        print_val(tm.read(t5, "balance"), t5, "balance");
        tm.commit(t4);
        tm.commit(t5);
    }

    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");

        std::thread reader([&]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            auto v = tm.read(t7, "balance");
            print_val(v, t7, "balance");
            tm.commit(t7);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6);
        reader.join();
    }

    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID t8 = tm.begin();
        TxID t9 = tm.begin();
        tm.insert(t8, "A", "val_a");
        tm.insert(t9, "B", "val_b");

        std::thread th([&]() {
            try {
                std::cout << "  [TX " << t8 << "] wants EXCLUSIVE on B\n";
                tm.update(t8, "B", "updated_b");
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        try {
            std::cout << "  [TX " << t9 << "] wants EXCLUSIVE on A\n";
            tm.update(t9, "A", "updated_a");
            tm.commit(t9);
        } catch (DeadlockException& e) {
            std::cout << "  " << e.what() << "\n";
            tm.abort(t9);
        }
        th.join();
    }

    std::cout << "\nAll scenarios complete.\n";
    return 0;
}