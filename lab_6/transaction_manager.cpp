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

/**
 * Lab 6: Transaction Manager - MVCC + Strict 2PL + Deadlock Detection
 * 
 * Components:
 * 1. MVCC - Multi-Version Concurrency Control with version chains
 * 2. Strict 2PL - Two-Phase Locking (locks held until commit/abort)
 * 3. Deadlock Detection - Waits-for graph cycle detection
 */

using TxID = uint64_t;
using RowKey = std::string;

// ============================================================================
// Transaction State Management
// ============================================================================

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };

struct Transaction {
    TxID     id;
    TxID     snapshot_xid;   // Snapshot for read consistency
    TxStatus status = TxStatus::ACTIVE;
    bool     in_shrinking = false;   // 2PL phase flag
};

// Global transaction table
static std::atomic<TxID>                          g_next_xid{1};
static std::mutex                                 g_tx_mutex;
static std::unordered_map<TxID, Transaction>      g_transactions;

TxID begin_transaction() {
    std::lock_guard lk(g_tx_mutex);
    TxID xid = g_next_xid.fetch_add(1);
    TxID snap = xid;  // Snapshot: see all commits < this xid
    g_transactions[xid] = Transaction{xid, snap, TxStatus::ACTIVE, false};
    std::cout << "[TX " << xid << "] BEGIN" << std::endl;
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


// ============================================================================
// MVCC Version Chain Management
// ============================================================================

struct RowVersion {
    std::string value;
    TxID        xmin;   // Created by transaction
    TxID        xmax;   // Deleted/updated by (0 = still live)
};

// Each row has a chain of versions (newest first)
static std::mutex                                       g_heap_mutex;
static std::unordered_map<RowKey, std::list<RowVersion>> g_heap;

/**
 * is_visible: Check if a version is visible to transaction T
 * 
 * Visibility rules:
 * - xmin must be committed and xmin < snapshot_xid (or xmin == reader_xid for own writes)
 * - xmax must be 0, or not yet visible to reader
 */
bool is_visible(const RowVersion& v, TxID snapshot_xid, TxID reader_xid) {
    // Check xmin visibility
    bool xmin_ok = (v.xmin == reader_xid) ||  // Own write
                   (is_committed(v.xmin) && v.xmin < snapshot_xid);
    if (!xmin_ok)
        return false;
    
    // Check xmax visibility (version not deleted)
    if (v.xmax == 0)
        return true;  // Not deleted
    
    // Version is deleted, but is deletion visible to us?
    bool xmax_visible = (v.xmax == reader_xid) ||  // We deleted it
                        (is_committed(v.xmax) && v.xmax < snapshot_xid);
    return !xmax_visible;
}

std::optional<std::string> mvcc_read_key(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it == g_heap.end())
        return std::nullopt;
    
    // Walk version chain, return first visible version
    for (const auto& v : it->second) {
        if (is_visible(v, snap, xid))
            return v.value;
    }
    
    return std::nullopt;
}

void mvcc_insert(const RowKey& key, const std::string& value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    g_heap[key].push_front({value, xid, 0});
    std::cout << "[TX " << xid << "] INSERT " << key << " = " << value << std::endl;
}

void mvcc_update(const RowKey& key, const std::string& new_value, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it != g_heap.end()) {
        // Mark old version as deleted
        for (auto& v : it->second) {
            if (is_visible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid;
                break;
            }
        }
    }
    
    // Insert new version
    g_heap[key].push_front({new_value, xid, 0});
    std::cout << "[TX " << xid << "] UPDATE " << key << " = " << new_value << std::endl;
}

void mvcc_delete(const RowKey& key, TxID xid) {
    std::lock_guard lk(g_heap_mutex);
    TxID snap;
    {
        std::lock_guard tlk(g_tx_mutex);
        snap = g_transactions.at(xid).snapshot_xid;
    }
    
    auto it = g_heap.find(key);
    if (it == g_heap.end())
        return;
    
    // Mark visible version as deleted
    for (auto& v : it->second) {
        if (is_visible(v, snap, xid) && v.xmax == 0) {
            v.xmax = xid;
            std::cout << "[TX " << xid << "] DELETE " << key << std::endl;
            return;
        }
    }
}


// ============================================================================
// Lock Manager (Strict 2PL)
// ============================================================================

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

// Waits-for graph: waiter -> set of holders
static std::unordered_map<TxID, std::unordered_set<TxID>> g_waits_for;

/**
 * has_cycle: DFS-based cycle detection in waits-for graph
 */
bool has_cycle(TxID start, const std::unordered_map<TxID, std::unordered_set<TxID>>& graph) {
    std::unordered_set<TxID> visited, stack;
    
    std::function<bool(TxID)> dfs = [&](TxID node) -> bool {
        visited.insert(node);
        stack.insert(node);
        
        auto it = graph.find(node);
        if (it != graph.end()) {
            for (TxID nb : it->second) {
                if (!visited.count(nb) && dfs(nb))
                    return true;
                if (stack.count(nb))
                    return true;  // Cycle detected
            }
        }
        
        stack.erase(node);
        return false;
    };
    
    return dfs(start);
}

/**
 * DeadlockException: Thrown when deadlock is detected
 */
class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxID xid)
        : std::runtime_error("Deadlock detected, aborting TX " + std::to_string(xid)) {}
};

/**
 * acquire_lock: Acquire lock with deadlock detection
 * Throws DeadlockException if cycle detected
 */
void acquire_lock(const RowKey& key, TxID xid, LockMode mode) {
    // 2PL: Cannot acquire lock in shrinking phase
    {
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions.at(xid).in_shrinking)
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
    }
    
    LockQueue& lq = g_lock_table[key];
    std::unique_lock ul(lq.mu);
    
    // Check if we already hold this lock
    for (auto& r : lq.requests) {
        if (r.xid == xid && r.granted) {
            if (mode == LockMode::SHARED)
                return;  // Already have lock
            if (r.mode == LockMode::EXCLUSIVE)
                return;  // Already have stronger lock
            // Could implement lock upgrade here
        }
    }
    
    // Add our request
    lq.requests.push_back({xid, mode, false});
    auto& my_req = lq.requests.back();
    
    while (true) {
        // Can we grant the lock?
        bool conflict = false;
        std::unordered_set<TxID> blocking;
        
        for (auto& r : lq.requests) {
            if (&r == &my_req)
                break;  // Only look at earlier requests
            if (!r.granted)
                continue;
            
            // Conflict if: requesting X, or holder has X
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
            std::cout << "[TX " << xid << "] LOCK " << key 
                      << " (" << (mode == LockMode::SHARED ? "S" : "X") << ")" << std::endl;
            return;
        }
        
        // Record waits-for and check for cycle
        {
            std::lock_guard lk(g_lm_mutex);
            g_waits_for[xid] = blocking;
            if (has_cycle(xid, g_waits_for)) {
                g_waits_for.erase(xid);
                lq.requests.remove_if([&](const LockRequest& r) {
                    return r.xid == xid && !r.granted;
                });
                throw DeadlockException(xid);
            }
        }
        
        lq.cv.wait(ul);  // Wait for lock release
    }
}

void release_locks(TxID xid) {
    // Mark shrinking phase
    {
        std::lock_guard lk(g_tx_mutex);
        if (g_transactions.count(xid))
            g_transactions.at(xid).in_shrinking = true;
    }
    
    // Release all locks
    for (auto& [key, lq] : g_lock_table) {
        std::unique_lock ul(lq.mu);
        lq.requests.remove_if([&](const LockRequest& r) {
            return r.xid == xid;
        });
        lq.cv.notify_all();
    }
    
    {
        std::lock_guard lk(g_lm_mutex);
        g_waits_for.erase(xid);
    }
}


// ============================================================================
// Transaction Manager API
// ============================================================================

class TransactionManager {
public:
    TxID begin() {
        return begin_transaction();
    }
    
    std::optional<std::string> read(TxID xid, const RowKey& key) {
        acquire_lock(key, xid, LockMode::SHARED);
        auto result = mvcc_read_key(key, xid);
        if (result) {
            std::cout << "[TX " << xid << "] READ " << key << " = " << *result << std::endl;
        } else {
            std::cout << "[TX " << xid << "] READ " << key << " = <not found>" << std::endl;
        }
        return result;
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
        std::cout << "[TX " << xid << "] COMMITTED" << std::endl;
    }
    
    void abort(TxID xid) {
        // Roll back: mark all versions written by xid as invisible
        {
            std::lock_guard lk(g_heap_mutex);
            for (auto& [key, chain] : g_heap) {
                for (auto& v : chain) {
                    if (v.xmin == xid)
                        v.xmax = xid;  // Make own inserts invisible
                    if (v.xmax == xid)
                        v.xmax = 0;    // Undo own deletes
                }
            }
        }
        
        {
            std::lock_guard lk(g_tx_mutex);
            g_transactions.at(xid).status = TxStatus::ABORTED;
        }
        release_locks(xid);
        std::cout << "[TX " << xid << "] ABORTED" << std::endl;
    }
};


// ============================================================================
// Test Scenarios
// ============================================================================

int main() {
    std::cout << "=== Lab 6: Transaction Manager (MVCC + Strict 2PL + Deadlock Detection) ===" << std::endl;
    std::cout << "PostgreSQL-style concurrency control\n" << std::endl;
    
    TransactionManager tm;
    
    // ========== Scenario 1: MVCC Snapshot Isolation ==========
    std::cout << "┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Scenario 1: MVCC Snapshot Isolation                │" << std::endl;
    std::cout << "│ T2 sees old value even after T3 commits            │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘" << std::endl;
    {
        TxID t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);
        
        TxID t2 = tm.begin();  // Snapshot after t1
        TxID t3 = tm.begin();
        
        // T3 updates balance
        tm.update(t3, "balance", "2000");
        tm.commit(t3);
        
        // T2 should still see old value (1000)
        auto val = tm.read(t2, "balance");
        std::cout << "Expected: 1000, Got: " << (val ? *val : "NULL") << std::endl;
        tm.commit(t2);
    }
    std::cout << std::endl;
    
    // ========== Scenario 2: Concurrent Shared Locks ==========
    std::cout << "┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Scenario 2: Concurrent Shared Locks                │" << std::endl;
    std::cout << "│ Multiple readers can hold shared lock              │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘" << std::endl;
    {
        TxID t4 = tm.begin();
        TxID t5 = tm.begin();
        
        tm.read(t4, "balance");  // Shared lock
        tm.read(t5, "balance");  // Shared lock - both granted
        
        tm.commit(t4);
        tm.commit(t5);
    }
    std::cout << std::endl;
    
    // ========== Scenario 3: Exclusive Lock Blocks Others ==========
    std::cout << "┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Scenario 3: Exclusive Lock + Waiting               │" << std::endl;
    std::cout << "│ Writer blocks reader until commit                  │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘" << std::endl;
    {
        TxID t6 = tm.begin();
        tm.update(t6, "balance", "3000");  // X lock
        
        // T7 runs in separate thread, will block
        std::thread reader([&]() {
            TxID t7 = tm.begin();
            std::cout << "[TX " << t7 << "] Waiting for lock..." << std::endl;
            auto val = tm.read(t7, "balance");  // Blocks until t6 commits
            std::cout << "[TX " << t7 << "] Got value: " << (val ? *val : "NULL") << std::endl;
            tm.commit(t7);
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        tm.commit(t6);  // Release lock
        reader.join();
    }
    std::cout << std::endl;
    
    // ========== Scenario 4: Deadlock Detection ==========
    std::cout << "┌────────────────────────────────────────────────────┐" << std::endl;
    std::cout << "│ Scenario 4: Deadlock Detection                     │" << std::endl;
    std::cout << "│ T8 holds A, T9 holds B → cycle → abort one         │" << std::endl;
    std::cout << "└────────────────────────────────────────────────────┘" << std::endl;
    {
        // Setup: Create two keys
        TxID setup = tm.begin();
        tm.insert(setup, "A", "val_a");
        tm.insert(setup, "B", "val_b");
        tm.commit(setup);
        
        TxID t8 = tm.begin();
        TxID t9 = tm.begin();
        
        // T8 locks A
        acquire_lock("A", t8, LockMode::EXCLUSIVE);
        std::cout << "[TX " << t8 << "] Has lock on A" << std::endl;
        
        // T9 locks B
        acquire_lock("B", t9, LockMode::EXCLUSIVE);
        std::cout << "[TX " << t9 << "] Has lock on B" << std::endl;
        
        // Now create deadlock: T8 wants B, T9 wants A
        std::thread th1([&]() {
            try {
                std::cout << "[TX " << t8 << "] Trying to lock B..." << std::endl;
                acquire_lock("B", t8, LockMode::EXCLUSIVE);
                tm.commit(t8);
            } catch (const DeadlockException& e) {
                std::cout << e.what() << std::endl;
                tm.abort(t8);
            }
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        try {
            std::cout << "[TX " << t9 << "] Trying to lock A..." << std::endl;
            acquire_lock("A", t9, LockMode::EXCLUSIVE);
            tm.commit(t9);
        } catch (const DeadlockException& e) {
            std::cout << e.what() << std::endl;
            tm.abort(t9);
        }
        
        th1.join();
    }
    std::cout << std::endl;
    
    std::cout << "✓ All scenarios complete!" << std::endl;
    
    return 0;
}
