#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <optional>
#include <atomic>
#include <sstream>
#include <cassert>
#include <functional>

// Transaction state variables
using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;   // read snapshot (see commits < snapshot_xid)
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;   // tracks 2PL phase
};

// global tx table and lock
static std::atomic<TxID>                          g_next_xid{1};
static std::mutex                                 g_tx_mutex;
static std::unordered_map<TxID, Transaction>      g_transactions;

TxID begin_transaction() {
    std::lock_guard lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    
    // the snapshot sees all commits that finished before this xid
    TxID snap = xid;
    g_transactions[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

// MVCC version tracking
struct RowVersion {
    std::string value;
    TxID        xmin;   // created by this tx
    TxID        xmax;   // deleted/updated by this tx (0 means it's still live)
};

// mapping logical rows to chains of versions (newest goes first)
static std::mutex                                       g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

// helper for visibility check based on snapshot_xid
bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // xmin needs to be committed and <= our snapshot
    bool xmin_ok = (v.xmin == reader_xid)          // my own write
                 || (is_committed(v.xmin) && v.xmin < snapshot_xid);
                 
    if (!xmin_ok) return false;

    // xmax check: version shouldn't be deleted before our snapshot
    if (v.xmax == 0) return true;
    
    bool xmax_invisible = (v.xmax == reader_xid)   // deleted by myself
                        || (is_committed(v.xmax) && v.xmax < snapshot_xid);
                        
    return !xmax_invisible;
}

std::optional<std::string> mvcc_read(TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    for (auto& [key, chain] : g_heap) {
        // returning first visible version
        for (auto& v : chain) {
            if (is_visible(v, snap, xid)) return v.value;
        }
    }
    return std::nullopt;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid)) return v.value;
    }
    return std::nullopt;
}

// insert creates a new version with xmax=0
void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
}

// update marks the old version's xmax=xid and inserts a new one
void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;   // logically deleting old version
                break;
            }
        }
    }
    g_heap[key].push_front({new_value, xid, 0});
}

// delete marks the visible version's xmax=xid
void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return;
    
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            return;
        }
    }
}

// Lock Manager for Strict 2PL
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

// waits-for graph for deadlocks
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stack;
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb)) return true;
                if (stack.count(nb)) return true;
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
    : std::runtime_error("Deadlock detected! aborting tx " + std::to_string(xid)) {}
};

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    // block acquiring if in shrinking phase (2PL rule)
    {
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL rule broken: acquiring lock during shrinking phase");
    }

    LockQueue& lq = g_lock_table[key];
    std::unique_lock ul(lq.mu);

    // check existing locks for this tx
    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED) return;
            if (r.mode == LockMode::EXCLUSIVE) return;
            // upgrade case: shared to exclusive - needs no other shared holders
        }
    }

    lq.requests.push_back({xid, mode, false});
    auto& my_req = lq.requests.back();

    while (true) {
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        
        for (auto& r : lq.requests) {
            if (&r == &my_req) break;
            if (!r.granted) continue;
            
            if (mode == LockMode::EXCLUSIVE || r.mode == LockMode::EXCLUSIVE) {
                if (r.xid != xid) { 
                    conflict = true; 
                    blocking.insert(r.xid); 
                }
            }
        }

        if (!conflict) {
            my_req.granted = true;
            {
                std::lock_guard lk(g_lm_mutex);
                g_waits_for.erase(xid);
            }
            return;
        }

        // record the waits-for edges for deadlock checking
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
    // start shrinking phase
    {
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }

    // drop all locks for this xid
    for (auto& [key, lq] : g_lock_table) {
        std::unique_lock ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r){ return r.xid == xid; });
        lq.cv.notify_all();
    }

    {
        std::lock_guard lk(g_lm_mutex);
        g_waits_for.erase(xid);
    }
}

// Transaction Manager class
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
        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        // rollback mvcc writes
        {
            std::lock_guard lk(g_heap_mutex);
            for (auto& [key, chain] : g_heap) {
                for (auto& v : chain) {
                    if (v.xmin == xid) v.xmax = xid;  // hide my own inserts
                    if (v.xmax == xid) v.xmax = 0;    // undo my own deletes
                }
            }
        }
        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
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
        TxID ta = tm.begin();
        TxID tb = tm.begin();

        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

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
    return 0;
}
