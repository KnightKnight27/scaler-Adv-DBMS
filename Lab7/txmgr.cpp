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
#include <string>
#include <sstream>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>

// Define alias types for Transactions and Records
using TxIdentifier = uint64_t;
using RecordKey    = std::string;

// Transaction status enum representation
enum class TransactionStatus {
    ACTIVE_TX,
    COMMITTED_TX,
    ABORTED_TX
};

// Structures for managing transaction states
struct TxRecord {
    TxIdentifier      id;
    TxIdentifier      snapshot_id;
    TransactionStatus status       = TransactionStatus::ACTIVE_TX;
    bool              in_shrinking = false; // Strict 2PL growing/shrinking phase boundary
};

// Thread-safe registry for transaction states and metadata
class TxRegistry {
private:
    std::mutex                                  registry_mutex;
    std::atomic<TxIdentifier>                   id_generator{1};
    std::unordered_map<TxIdentifier, TxRecord> transactions;

public:
    TxIdentifier begin() {
        std::lock_guard<std::mutex> lock(registry_mutex);
        TxIdentifier xid = id_generator.fetch_add(1);
        transactions[xid] = TxRecord{xid, xid, TransactionStatus::ACTIVE_TX, false};
        return xid;
    }

    bool isCommitted(TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        auto it = transactions.find(xid);
        return it != transactions.end() && it->second.status == TransactionStatus::COMMITTED_TX;
    }

    bool isAborted(TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        auto it = transactions.find(xid);
        return it != transactions.end() && it->second.status == TransactionStatus::ABORTED_TX;
    }

    TxIdentifier getSnapshotId(TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        return transactions.at(xid).snapshot_id;
    }

    void updateStatus(TxIdentifier xid, TransactionStatus status) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        transactions.at(xid).status = status;
    }

    bool hasEnterShrinkingPhase(TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        auto it = transactions.find(xid);
        return it != transactions.end() && it->second.in_shrinking;
    }

    void setShrinkingPhase(TxIdentifier xid, bool in_shrinking) {
        std::lock_guard<std::mutex> lock(registry_mutex);
        if (transactions.count(xid)) {
            transactions.at(xid).in_shrinking = in_shrinking;
        }
    }
};

// Lock modes for 2PL
enum class LockType {
    SHARED_LOCK,
    EXCLUSIVE_LOCK
};

// Represents a request to acquire a lock
struct LockRequestEntry {
    TxIdentifier xid;
    LockType     mode;
    bool         granted = false;
};

// Queue of lock requests on a resource
struct LockQueueControl {
    std::list<LockRequestEntry> requests;
    std::mutex                  queue_mutex;
    std::condition_variable     cv;
};

// Exception thrown when a deadlock is detected
class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxIdentifier xid)
        : std::runtime_error("Deadlock detected, aborting tx " + std::to_string(xid)) {}
};

// Class responsible for concurrency control via Strict 2PL and Deadlock Detection
class ConcurrencyLockManager {
private:
    std::mutex                                                      manager_mutex;
    std::unordered_map<RecordKey, std::unique_ptr<LockQueueControl>> lock_table;
    std::unordered_map<TxIdentifier, std::unordered_set<TxIdentifier>> waits_for;
    TxRegistry&                                                     tx_registry;

    // Depth-First Search to detect cycles in the Waits-For dependency graph
    bool detectCycle(TxIdentifier start, const std::unordered_map<TxIdentifier, std::unordered_set<TxIdentifier>>& graph) {
        std::unordered_set<TxIdentifier> visited;
        std::unordered_set<TxIdentifier> path_stack;
        
        std::function<bool(TxIdentifier)> dfs = [&](TxIdentifier node) -> bool {
            visited.insert(node);
            path_stack.insert(node);
            
            auto it = graph.find(node);
            if (it != graph.end()) {
                for (TxIdentifier neighbor : it->second) {
                    if (!visited.count(neighbor)) {
                        if (dfs(neighbor)) return true;
                    } else if (path_stack.count(neighbor)) {
                        return true;
                    }
                }
            }
            
            path_stack.erase(node);
            return false;
        };
        
        return dfs(start);
    }

public:
    explicit ConcurrencyLockManager(TxRegistry& reg) : tx_registry(reg) {}

    LockQueueControl& getOrCreateQueue(const RecordKey& key) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        auto it = lock_table.find(key);
        if (it == lock_table.end()) {
            lock_table[key] = std::make_unique<LockQueueControl>();
        }
        return *lock_table[key];
    }

    void acquireLock(const RecordKey& key, TxIdentifier xid, LockType mode) {
        // Enforce 2PL growing/shrinking phase boundary check
        if (tx_registry.hasEnterShrinkingPhase(xid)) {
            throw std::runtime_error("2PL violation: cannot acquire lock in shrinking phase");
        }

        LockQueueControl& lq = getOrCreateQueue(key);
        std::unique_lock<std::mutex> queue_lock(lq.queue_mutex);

        // Check if lock is already held in a compatible mode
        for (auto& r : lq.requests) {
            if (r.xid == xid && r.granted) {
                if (mode == LockType::SHARED_LOCK) return;
                if (r.mode == LockType::EXCLUSIVE_LOCK) return;
            }
        }

        lq.requests.push_back({xid, mode, false});
        auto& my_req = lq.requests.back();

        while (true) {
            bool conflict = false;
            std::unordered_set<TxIdentifier> blocking_txs;

            // Determine if there are conflicting lock requests before ours in the queue
            for (auto& r : lq.requests) {
                if (&r == &my_req) break;
                if (!r.granted) continue;
                if (mode == LockType::EXCLUSIVE_LOCK || r.mode == LockType::EXCLUSIVE_LOCK) {
                    if (r.xid != xid) {
                        conflict = true;
                        blocking_txs.insert(r.xid);
                    }
                }
            }

            if (!conflict) {
                my_req.granted = true;
                {
                    std::lock_guard<std::mutex> lock(manager_mutex);
                    waits_for.erase(xid);
                }
                return;
            }

            // Register blocking relations and check for deadlocks
            {
                std::lock_guard<std::mutex> lock(manager_mutex);
                waits_for[xid] = blocking_txs;
                if (detectCycle(xid, waits_for)) {
                    waits_for.erase(xid);
                    lq.requests.remove_if([&](const LockRequestEntry& r) {
                        return r.xid == xid && !r.granted;
                    });
                    throw DeadlockException(xid);
                }
            }

            lq.cv.wait(queue_lock);
        }
    }

    void releaseLocks(TxIdentifier xid) {
        tx_registry.setShrinkingPhase(xid, true);

        std::vector<LockQueueControl*> queues_to_process;
        {
            std::lock_guard<std::mutex> lock(manager_mutex);
            for (auto& [key, lq] : lock_table) {
                queues_to_process.push_back(lq.get());
            }
            waits_for.erase(xid);
        }

        // Release mutex safely to prevent circular lock ordering deadlock
        for (auto* lq : queues_to_process) {
            std::lock_guard<std::mutex> queue_lock(lq->queue_mutex);
            lq->requests.remove_if([&](const LockRequestEntry& r) { return r.xid == xid; });
            lq->cv.notify_all();
        }
    }
};

// Represents a version of a logical row
struct DataVersion {
    std::string  value;
    TxIdentifier xmin; // Transaction that created this version
    TxIdentifier xmax; // Transaction that deleted/updated this version (0 if active)
};

// Class handling Multi-Version Concurrency Control (MVCC) version chains
class MVCCStorage {
private:
    std::mutex                                          heap_mutex;
    std::unordered_map<RecordKey, std::list<DataVersion>> versioned_heap;
    TxRegistry&                                         tx_registry;

    // Visibility logic implementation based on PostgreSQL's snapshot rules
    bool isVersionVisible(const DataVersion& v, TxIdentifier snapshot_id, TxIdentifier reader_xid) {
        bool xmin_ok = (v.xmin == reader_xid)
                    || (tx_registry.isCommitted(v.xmin) && v.xmin < snapshot_id);
        if (!xmin_ok) return false;

        if (v.xmax == 0) return true;
        bool xmax_invisible = (v.xmax == reader_xid)
                            || (tx_registry.isCommitted(v.xmax) && v.xmax < snapshot_id);
        return !xmax_invisible;
    }

public:
    explicit MVCCStorage(TxRegistry& reg) : tx_registry(reg) {}

    std::optional<std::string> readRecord(const RecordKey& key, TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(heap_mutex);
        TxIdentifier snap = tx_registry.getSnapshotId(xid);

        auto it = versioned_heap.find(key);
        if (it == versioned_heap.end()) return std::nullopt;

        for (auto& v : it->second) {
            if (isVersionVisible(v, snap, xid)) {
                return v.value;
            }
        }
        return std::nullopt;
    }

    void insert(const RecordKey& key, const std::string& value, TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(heap_mutex);
        versioned_heap[key].push_front({value, xid, 0});
    }

    void update(const RecordKey& key, const std::string& new_value, TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(heap_mutex);
        TxIdentifier snap = tx_registry.getSnapshotId(xid);

        auto it = versioned_heap.find(key);
        if (it != versioned_heap.end()) {
            for (auto& v : it->second) {
                if (isVersionVisible(v, snap, xid) && v.xmax == 0) {
                    v.xmax = xid; // logically delete older active version
                    break;
                }
            }
        }
        versioned_heap[key].push_front({new_value, xid, 0});
    }

    void remove(const RecordKey& key, TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(heap_mutex);
        TxIdentifier snap = tx_registry.getSnapshotId(xid);

        auto it = versioned_heap.find(key);
        if (it == versioned_heap.end()) return;

        for (auto& v : it->second) {
            if (isVersionVisible(v, snap, xid) && v.xmax == 0) {
                v.xmax = xid; // logically delete version
                return;
            }
        }
    }

    void rollback(TxIdentifier xid) {
        std::lock_guard<std::mutex> lock(heap_mutex);
        for (auto& [key, chain] : versioned_heap) {
            for (auto& v : chain) {
                if (v.xmin == xid) {
                    v.xmax = xid; // Hide inserts created by aborted transaction
                } else if (v.xmax == xid) {
                    v.xmax = 0;   // Restore updates/deletes committed by aborted transaction
                }
            }
        }
    }
};

// Main database coordinator class integrating registry, locking, and storage components
class DBTransactionManager {
private:
    TxRegistry             tx_registry;
    ConcurrencyLockManager lock_manager;
    MVCCStorage            mvcc_storage;

public:
    DBTransactionManager()
        : lock_manager(tx_registry), mvcc_storage(tx_registry) {}

    TxIdentifier begin() {
        return tx_registry.begin();
    }

    std::optional<std::string> read(TxIdentifier xid, const RecordKey& key) {
        lock_manager.acquireLock(key, xid, LockType::SHARED_LOCK);
        return mvcc_storage.readRecord(key, xid);
    }

    void insert(TxIdentifier xid, const RecordKey& key, const std::string& value) {
        lock_manager.acquireLock(key, xid, LockType::EXCLUSIVE_LOCK);
        mvcc_storage.insert(key, value, xid);
    }

    void update(TxIdentifier xid, const RecordKey& key, const std::string& value) {
        lock_manager.acquireLock(key, xid, LockType::EXCLUSIVE_LOCK);
        mvcc_storage.update(key, value, xid);
    }

    void remove(TxIdentifier xid, const RecordKey& key) {
        lock_manager.acquireLock(key, xid, LockType::EXCLUSIVE_LOCK);
        mvcc_storage.remove(key, xid);
    }

    void acquireLockDirectly(const RecordKey& key, TxIdentifier xid, LockType mode) {
        lock_manager.acquireLock(key, xid, mode);
    }

    void commit(TxIdentifier xid) {
        tx_registry.updateStatus(xid, TransactionStatus::COMMITTED_TX);
        lock_manager.releaseLocks(xid);
        std::cout << "[TX " << xid << "] COMMITTED\n";
    }

    void abort(TxIdentifier xid) {
        mvcc_storage.rollback(xid);
        tx_registry.updateStatus(xid, TransactionStatus::ABORTED_TX);
        lock_manager.releaseLocks(xid);
        std::cout << "[TX " << xid << "] ABORTED\n";
    }
};

void print_val(const std::optional<std::string>& v, TxIdentifier xid, const RecordKey& key) {
    std::cout << "  [TX " << xid << "] READ " << key << " = "
              << (v ? *v : "<not visible>") << "\n";
}

int main() {
    DBTransactionManager tm;

    // ── Scenario 1: Basic MVCC snapshot isolation ──
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxIdentifier t1 = tm.begin();
        tm.insert(t1, "balance", "1000");
        tm.commit(t1);

        TxIdentifier t2 = tm.begin();   
        TxIdentifier t3 = tm.begin();

        tm.update(t3, "balance", "2000");
        tm.commit(t3);

        auto v = tm.read(t2, "balance");
        print_val(v, t2, "balance");   
        tm.commit(t2);
    }

    // ── Scenario 2: Two-Phase Locking — concurrent reads ──
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxIdentifier t4 = tm.begin();
        TxIdentifier t5 = tm.begin();
        print_val(tm.read(t4, "balance"), t4, "balance");  
        print_val(tm.read(t5, "balance"), t5, "balance");  
        tm.commit(t4);
        tm.commit(t5);
    }

    // ── Scenario 3: Exclusive lock blocks shared ──
    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxIdentifier t6 = tm.begin();
        tm.update(t6, "balance", "3000");  

        std::thread reader([&]() {
            TxIdentifier t7 = tm.begin();
            std::cout << "  [TX " << t7 << "] waiting for shared lock on balance...\n";
            auto v = tm.read(t7, "balance");
            print_val(v, t7, "balance");  
            tm.commit(t7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tm.commit(t6);    
        reader.join();
    }

    // ── Scenario 4: Deadlock detection ──
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxIdentifier ta = tm.begin();
        TxIdentifier tb = tm.begin();

        tm.insert(ta, "A", "val_a");
        tm.insert(tb, "B", "val_b");
        tm.commit(ta);
        tm.commit(tb);

        TxIdentifier t8 = tm.begin();
        TxIdentifier t9 = tm.begin();

        tm.acquireLockDirectly("A", t8, LockType::EXCLUSIVE_LOCK);
        tm.acquireLockDirectly("B", t9, LockType::EXCLUSIVE_LOCK);

        std::thread th1([&]() {
            try {
                tm.acquireLockDirectly("B", t8, LockType::EXCLUSIVE_LOCK);
                tm.commit(t8);
            } catch (DeadlockException& e) {
                std::cout << "  " << e.what() << "\n";
                tm.abort(t8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            tm.acquireLockDirectly("A", t9, LockType::EXCLUSIVE_LOCK);
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
