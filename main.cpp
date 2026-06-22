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
#include <algorithm>
#include <chrono>

// ─────────────────────────────────────────────
// 1. Transaction state
// ─────────────────────────────────────────────

using TxID   = uint64_t;
using RowKey = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;
    bool     aborted_by_detector = false;
};

static std::atomic<TxID>                          g_next_xid{1};
static std::mutex                                 g_tx_mutex;
static std::unordered_map<TxID, Transaction>      g_transactions;

TxID begin_transaction() {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    g_transactions[xid] = Transaction{xid, xid, TxStatus::ACTIVE, false, false};
    return xid;
}

bool is_committed(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::COMMITTED;
}

bool is_aborted(TxID xid) {
    std::lock_guard<std::mutex> lk(g_tx_mutex);
    auto it = g_transactions.find(xid);
    return it != g_transactions.end() && it->second.status == TxStatus::ABORTED;
}

// ─────────────────────────────────────────────
// 2. MVCC Version Chain
// ─────────────────────────────────────────────

struct Version {
    TxID        xmin;   // created by
    TxID        xmax;   // deleted/superseded by (0 = still live)
    std::string val;
};

static std::mutex                                       g_heap_mutex;
static std::unordered_map<RowKey, std::list<Version>>   g_heap;

bool is_visible(const Version& v, TxID snapshot_xid, TxID reader_xid) {
    bool xmin_visible = (v.xmin == reader_xid) || (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_visible) return false;

    if (v.xmax == 0) return true;
    bool xmax_visible = (v.xmax == reader_xid) || (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_visible;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it == g_heap.end()) return std::nullopt;
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid)) return v.val;
    }
    return std::nullopt;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    g_heap[key].push_front({xid, 0, value});
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }
    g_heap[key].push_front({xid, 0, new_value});
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard<std::mutex> lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard<std::mutex> tlk(g_tx_mutex);
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

// ─────────────────────────────────────────────
// 3. Strict 2PL Lock Manager & Deadlock Detector
// ─────────────────────────────────────────────

enum class LockMode { SHARED, EXCLUSIVE };

struct LockInfo {
    TxID     xid;
    LockMode mode;
};

struct WaitInfo {
    RowKey   key;
    LockMode mode;
};

static std::mutex                                      g_lm_mutex;
static std::unordered_map<RowKey, std::vector<LockInfo>> g_active_holders;
static std::unordered_map<TxID, WaitInfo>              g_active_waiters;
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

bool find_cycle_dfs(TxID node, 
                    const std::unordered_map<TxID, std::unordered_set<TxID>>& graph,
                    std::unordered_set<TxID>& visited,
                    std::unordered_set<TxID>& stack,
                    std::vector<TxID>& path,
                    std::vector<TxID>& cycle_nodes) {
    visited.insert(node);
    stack.insert(node);
    path.push_back(node);

    auto it = graph.find(node);
    if (it != graph.end()) {
        for (TxID nb : it->second) {
            if (!visited.count(nb)) {
                if (find_cycle_dfs(nb, graph, visited, stack, path, cycle_nodes)) {
                    return true;
                }
            } else if (stack.count(nb)) {
                auto start_it = std::find(path.begin(), path.end(), nb);
                if (start_it != path.end()) {
                    cycle_nodes.assign(start_it, path.end());
                } else {
                    cycle_nodes = path;
                }
                return true;
            }
        }
    }

    stack.erase(node);
    path.pop_back();
    return false;
}

void rebuild_waits_for() {
    g_waits_for.clear();
    for (auto& [waiter_xid, wait_info] : g_active_waiters) {
        auto holders_it = g_active_holders.find(wait_info.key);
        if (holders_it != g_active_holders.end()) {
            for (auto& holder : holders_it->second) {
                if (holder.xid == waiter_xid) continue;
                bool conflict = false;
                if (wait_info.mode == LockMode::EXCLUSIVE || holder.mode == LockMode::EXCLUSIVE) {
                    conflict = true;
                }
                if (conflict) {
                    g_waits_for[waiter_xid].insert(holder.xid);
                }
            }
        }
    }
}

void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    {
        std::lock_guard<std::mutex> lk(g_lm_mutex);
        auto it = g_active_holders.find(key);
        if (it != g_active_holders.end()) {
            for (auto& h : it->second) {
                if (h.xid == xid) {
                    if (mode == LockMode::SHARED || h.mode == LockMode::EXCLUSIVE) {
                        return; 
                    }
                }
            }
        }
    }

    while (true) {
        if (is_aborted(xid)) {
            throw DeadlockException(xid);
        }

        std::unique_lock<std::mutex> lk(g_lm_mutex);
        
        bool conflict = false;
        auto holders_it = g_active_holders.find(key);
        if (holders_it != g_active_holders.end()) {
            for (auto& h : holders_it->second) {
                if (h.xid != xid) {
                    if (mode == LockMode::EXCLUSIVE || h.mode == LockMode::EXCLUSIVE) {
                        conflict = true;
                    }
                }
            }
        }

        if (!conflict) {
            g_active_waiters.erase(xid);
            g_active_holders[key].push_back({xid, mode});
            return;
        }

        g_active_waiters[xid] = {key, mode};

        rebuild_waits_for();
        
        std::vector<TxID> cycle_nodes;
        std::unordered_set<TxID> visited, stack;
        std::vector<TxID> path;
        
        bool has_cycle = false;
        for (auto& [waiter, _] : g_waits_for) {
            if (!visited.count(waiter)) {
                if (find_cycle_dfs(waiter, g_waits_for, visited, stack, path, cycle_nodes)) {
                    has_cycle = true;
                    break;
                }
            }
        }

        if (has_cycle) {
            TxID victim = 0;
            for (TxID node : cycle_nodes) {
                if (node > victim) {
                    victim = node;
                }
            }

            std::cout << "[Deadlock Detector] Found cycle: ";
            for (size_t idx = 0; idx < cycle_nodes.size(); idx++) {
                std::cout << "Tx" << cycle_nodes[idx] << (idx + 1 < cycle_nodes.size() ? " -> " : "");
            }
            std::cout << " -> Tx" << cycle_nodes[0] << "\n";
            std::cout << "[Deadlock Detector] Victim selected: Tx" << victim << " (youngest)\n";

            if (victim == xid) {
                g_active_waiters.erase(xid);
                lk.unlock();
                throw DeadlockException(xid);
            } else {
                {
                    std::lock_guard<std::mutex> tlk(g_tx_mutex);
                    g_transactions[victim].status = TxStatus::ABORTED;
                    g_transactions[victim].aborted_by_detector = true;
                }
                std::cout << "[Deadlock Detector] Aborted Tx" << victim << " to resolve deadlock\n";
                
                g_active_waiters.erase(victim);
                for (auto& [k, holders] : g_active_holders) {
                    holders.erase(std::remove_if(holders.begin(), holders.end(),
                                                 [=](const LockInfo& h) { return h.xid == victim; }),
                                  holders.end());
                }

                rebuild_waits_for();
            }
        }

        lk.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void release_locks(TxID xid) {
    std::lock_guard<std::mutex> lk(g_lm_mutex);
    g_active_waiters.erase(xid);
    for (auto& [key, holders] : g_active_holders) {
        holders.erase(std::remove_if(holders.begin(), holders.end(),
                                     [=](const LockInfo& h) { return h.xid == xid; }),
                      holders.end());
    }
}

// ─────────────────────────────────────────────
// 4. Transaction Manager
// ─────────────────────────────────────────────

class TransactionManager {
public:
    TxID begin() { 
        return begin_transaction(); 
    }

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
            std::lock_guard<std::mutex> lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::COMMITTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxID xid) {
        {
            std::lock_guard<std::mutex> lk(g_heap_mutex);
            for (auto& [key, chain] : g_heap) {
                for (auto& v : chain) {
                    if (v.xmin == xid) v.xmax = xid;
                    if (v.xmax == xid) v.xmax = 0;
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
// 5. Deadlock Simulation Trace
// ─────────────────────────────────────────────

int main() {
    TransactionManager tm;

    // Align start transactions so that the next are Tx3 and Tx4
    TxID tx1 = tm.begin(); // ID 1
    TxID tx2 = tm.begin(); // ID 2
    (void)tx1;
    (void)tx2;

    std::cout << "=== Database Transaction Manager Initialized ===\n";
    std::cout << "Starting Deadlock Simulation with Tx3 and Tx4...\n\n";

    std::thread th3([&]() {
        TxID tx3 = tm.begin();
        std::cout << "[TX 3] Started\n";
        try {
            tm.insert(tx3, "A", "val_a_3");
            std::cout << "[TX 3] Wrote to Record A (holds X-Lock on A)\n";
            
            // Wait for Tx4 to acquire lock on B
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::cout << "[TX 3] Requesting write on Record B...\n";
            tm.update(tx3, "B", "val_b_3");
            std::cout << "[TX 3] Successfully wrote to Record B\n";
            
            tm.commit(tx3);
        } catch (const DeadlockException& e) {
            std::cout << "[TX 3] " << e.what() << "\n";
            tm.abort(tx3);
        }
    });

    std::thread th4([&]() {
        // Wait for Tx3 to begin and write to A
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        TxID tx4 = tm.begin();
        std::cout << "[TX 4] Started\n";
        try {
            tm.insert(tx4, "B", "val_b_4");
            std::cout << "[TX 4] Wrote to Record B (holds X-Lock on B)\n";
            
            // Wait for Tx3 to request B and get blocked
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            
            std::cout << "[TX 4] Requesting write on Record A...\n";
            tm.update(tx4, "A", "val_a_4");
            std::cout << "[TX 4] Successfully wrote to Record A\n";
            
            tm.commit(tx4);
        } catch (const DeadlockException& e) {
            std::cout << "[TX 4] " << e.what() << "\n";
            tm.abort(tx4);
        }
    });

    th3.join();
    th4.join();

    std::cout << "\nSimulation Complete.\n";
    return 0;
}
