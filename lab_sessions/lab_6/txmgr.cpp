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
#include <cassert>

// ─── Transaction state ────────────────────────────────────────────────────────
using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;
    TxStatus status       = TxStatus::ACTIVE;
    bool     in_shrinking = false;
};

static std::atomic<TxID>                     g_next_xid{1};
static std::mutex                            g_tx_mutex;
static std::unordered_map<TxID,Transaction>  g_transactions;

TxID begin_transaction() {
    std::lock_guard lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

// ─── MVCC version chain ───────────────────────────────────────────────────────
struct RowVersion {
    std::string value;
    TxID        xmin;
    TxID        xmax;   // 0 = still live
};

static std::mutex                                        g_heap_mutex;
static std::unordered_map<RowKey,std::list<RowVersion>> g_heap;

bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_ok = (v.xmin == reader_xid) ||
                   (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok) return false;
    if (v.xmax == 0) return true;
    bool xmax_invisible = (v.xmax == reader_xid) ||
                          (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_invisible;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    { std::lock_guard tlk(g_tx_mutex); snap = g_transactions.at(xid).snapshot_xid; }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (auto& v : it->second)
        if (is_visible(v, snap, xid)) return v.value;
    return std::nullopt;
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
    if (it != g_heap.end())
        for (auto& v : it->second)
            if (is_visible(v, snap, xid) && v.xmax == 0) { v.xmax = xid; break; }
    g_heap[key].push_front({new_value, xid, 0});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    { std::lock_guard tlk(g_tx_mutex); snap = g_transactions.at(xid).snapshot_xid; }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    for (auto& v : it->second)
        if (is_visible(v, snap, xid) && v.xmax == 0) { v.xmax = xid; return; }
}

// ─── Lock Manager (Strict 2PL) ────────────────────────────────────────────────
enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest { TxID xid; LockMode mode; bool granted = false; };

struct LockQueue {
    std::list<LockRequest>  requests;
    std::mutex              mu;
    std::condition_variable cv;
};

static std::mutex                                     g_lm_mutex;
static std::unordered_map<RowKey,LockQueue>           g_lock_table;
static std::unordered_map<TxID,std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start,
               const std::unordered_map<TxID,std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stk;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node); stk.insert(node);
        auto it = graph.find(node);
        if (it != graph.end())
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb)) return true;
                if (stk.count(nb)) return true;
            }
        stk.erase(node);
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
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }

    LockQueue& lq = g_lock_table[key];
    std::unique_lock ul(lq.mu);

    for (auto& r : lq.requests)
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED || r.mode == LockMode::EXCLUSIVE) return;
        }

    lq.requests.push_back({xid, mode, false});
    auto& my_req = lq.requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        for (auto& r : lq.requests) {
            if (&r == &my_req) break;
            if (!r.granted) continue;
            if ((mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) && r.xid != xid)
                { conflict = true; blocking.insert(r.xid); }
        }

        if (!conflict) {
            my_req.granted = true;
            { std::lock_guard lk(g_lm_mutex); g_waits_for.erase(xid); }
            return;
        }

        {
            std::lock_guard lk(g_lm_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid && !r.granted; });
                throw DeadlockException(xid);
            }
        }
        lq.cv.wait(ul);
    }
}

void release_locks(TxID xid) {
    { std::lock_guard lk(g_tx_mutex); if (g_transactions.count(xid)) g_transactions.at(xid).in_shrinking = true; }
    for (auto& [key, lq] : g_lock_table) {
        std::unique_lock ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid; });
        lq.cv.notify_all();
    }
    { std::lock_guard lk(g_lm_mutex); g_waits_for.erase(xid); }
}

// ─── Transaction Manager (public API) ─────────────────────────────────────────
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

    void remove(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::EXCLUSIVE);
        mvcc_delete(key, xid);
    }

    void commit(TxID xid) {
        { std::lock_guard lk(g_tx_mutex); g_transactions.at(xid).status = TxStatus::COMMITTED; }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        {
            std::lock_guard lk(g_heap_mutex);
            for (auto& [key, chain] : g_heap)
                for (auto& v : chain) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid && v.xmin != xid) v.xmax = 0;
                }
        }
        { std::lock_guard lk(g_tx_mutex); g_transactions.at(xid).status = TxStatus::ABORTED; }
        release_locks(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

void print_val(const std::optional<std::string>& v, TxID xid, const RowKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main() {
    TransactionManager tm;

    // Scenario 1: MVCC snapshot isolation
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxID t2 = tm.begin();
        TxID t3 = tm.begin();
        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        print_val(tm.read(t2, "balance"), t2, "balance");  // expects 1000
        tm.commit(t2);
    }

    // Scenario 2: Concurrent shared locks
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        print_val(tm.read(t4, "balance"), t4, "balance");
        print_val(tm.read(t5, "balance"), t5, "balance");
        tm.commit(t4);
        tm.commit(t5);
    }

    // Scenario 3: Exclusive lock blocks reader
    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");

        std::thread reader([&]() {
            TxID t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock...\n";
            print_val(tm.read(t7, "balance"), t7, "balance");
            tm.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6);
        reader.join();
    }

    // Scenario 4: Deadlock detection
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxID ta = tm.begin(); TxID tb = tm.begin();
        tm.insert(ta, "A", "val_a"); tm.commit(ta);
        tm.insert(tb, "B", "val_b"); tm.commit(tb);

        TxID t8 = tm.begin();
        TxID t9 = tm.begin();
        acquire_lock("A", t8, LockMode::EXCLUSIVE);
        acquire_lock("B", t9, LockMode::EXCLUSIVE);

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
}
