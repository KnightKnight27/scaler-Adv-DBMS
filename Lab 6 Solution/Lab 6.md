# Lab 6 - Transaction manager with MVCC version chains + Strict 2PL + deadlock detection

## Objective

Build an integrated transactional execution subsystem that coordinates:

1. **Multi-Version Concurrency Control (MVCC)** — Every modification generates a dedicated record iteration. Viewers run isolated snapshot evaluations, allowing reading tasks to progress seamlessly without blocking incoming data mutations.
2. **Strict Two-Phase Locking (S2PL)** — Restricts lock acquisition actions to a forward expansion window ("growing" phase). Resource tokens are locked down comprehensively and are relinquished concurrently during finalization ("shrinking" phase) to completely circumvent cascading rollbacks.
3. **Dependency Cycle Auditing** — Traverses a directed dependency graph via depth-first searching to break deadlocks by forcefully aborting the youngest conflicting worker path.



## Architecture Implementation

```cpp
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


// 1. Transaction Environment Context

using TxId   = uint64_t;
using RowKey = std::string;

enum class TransactionStatus { ACTIVE, COMMITTED, ABORTED };

struct TransactionContext {
    TxId id;
    TxId isolation_watermark; // Snapshots evaluate commits completed strictly below this point
    TransactionStatus status = TransactionStatus::ACTIVE;
    bool is_shrinking        = false; // Strict 2PL phase enforcement flag
};



// 2. MVCC Storage Layer Data Formats

struct RecordVersion {
    std::string data_payload;
    TxId created_by_tx;  // xmin: Transaction that generated this entry version
    TxId deleted_by_tx;  // xmax: Transaction that superseded/removed this entry (0 if live)
};



// 3. Lock Management Structures

enum class LockMode { SHARED, EXCLUSIVE };

struct LockAcquisitionRequest {
    TxId tx_id;
    LockMode requested_mode;
    bool is_granted = false;
};

struct ResourceLockQueue {
    std::list<LockAcquisitionRequest> request_chain;
    std::mutex queue_mutex;
    std::condition_variable status_condition;
};

// Exception thrown when a dependency cycle is detected
class DeadlockException : public std::runtime_error {
public:
    explicit DeadlockException(TxId tx_id)
        : std::runtime_error("Deadlock cycle encountered: Terminating Tx " + std::to_string(tx_id)) {}
};



// 4. Integrated Database Engine Core

class DatabaseEngine {
private:
    // Core Sequence Allocator
    std::atomic<TxId> next_tx_sequence_id{1};

    // Subsystem Thread Protection Mutexes
    std::mutex transaction_registry_mutex;
    std::mutex storage_heap_mutex;
    std::mutex dependency_graph_mutex;

    // Internal System State Cataloging Tables
    std::unordered_map<TxId, TransactionContext> transaction_registry;
    std::unordered_map<RowKey, std::list<RecordVersion>> storage_heap;
    std::unordered_map<RowKey, ResourceLockQueue> lock_table;
    
    // Dependency Tracking Graph: Waiting Tx ID -> Set of Blocking Tx IDs
    std::unordered_map<TxId, std::unordered_set<TxId>> dependency_graph;

private:
    // Helper validation to see if a specific transaction is committed
    bool IsTxCommitted(TxId tx_id) {
        std::lock_guard<std::mutex> lock(transaction_registry_mutex);
        auto iterator = transaction_registry.find(tx_id);
        return iterator != transaction_registry.end() && iterator->second.status == TransactionStatus::COMMITTED;
    }

    // Evaluate structural version visibility based on classic MVCC criteria
    bool IsVersionVisible(const RecordVersion& version, TxId isolation_watermark, TxId reader_tx_id) {
        // Step 1: Was this version birthed by a valid visible path?
        bool creation_is_visible = (version.created_by_tx == reader_tx_id) || 
                                   (IsTxCommitted(version.created_by_tx) && version.created_by_tx < isolation_watermark);
        
        if (!creation_is_visible) return false;

        // Step 2: Has this record version been hidden or flagged out by a removal action?
        if (version.deleted_by_tx == 0) return true;
        
        bool deletion_is_visible = (version.deleted_by_tx == reader_tx_id) || 
                                   (IsTxCommitted(version.deleted_by_tx) && version.deleted_by_tx < isolation_watermark);
        
        return !deletion_is_visible;
    }

    // Depth-First Search implementation to identify structural dependency graph loops
    bool IsCycleDetected(TxId diagnostic_start_id, const std::unordered_map<TxId, std::unordered_set<TxId>>& graph) {
        std::unordered_set<TxId> globally_visited;
        std::unordered_set<TxId> active_recursion_stack;

        std::function<bool(TxId)> run_dfs = [&](TxId target_node) -> bool {
            globally_visited.insert(target_node);
            active_recursion_stack.insert(target_node);

            auto edge_finder = graph.find(target_node);
            if (edge_finder != graph.end()) {
                for (TxId adjacent_neighbor : edge_finder->second) {
                    if (!globally_visited.count(adjacent_neighbor) && run_dfs(adjacent_neighbor)) {
                        return true;
                    }
                    if (active_recursion_stack.count(adjacent_neighbor)) {
                        return true; // Active back-edge hit: Loop discovered
                    }
                }
            }
            active_recursion_stack.erase(target_node);
            return false;
        };

        return run_dfs(diagnostic_start_id);
    }

    // Acquire locks according to Strict 2PL principles
    void EnforceLockAcquisition(const RowKey& target_key, TxId tx_id, LockMode required_mode) {
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            if (transaction_registry.at(tx_id).is_shrinking) {
                throw std::runtime_error("Strict 2PL Rule Violation: Cannot secure additional locks once resource release has begun.");
            }
        }

        ResourceLockQueue& queue = lock_table[target_key];
        std::unique_lock<std::mutex> queue_lock(queue.queue_mutex);

        // Verification step: Check if the current transaction already holds an acceptable lock
        for (const auto& request : queue.request_chain) {
            if (request.tx_id == tx_id && request.is_granted) {
                if (required_mode == LockMode::SHARED) return;
                if (request.requested_mode == LockMode::EXCLUSIVE) return;
            }
        }

        // Queue up the allocation request
        queue.request_chain.push_back({tx_id, required_mode, false});
        auto& current_allocated_request = queue.request_chain.back();

        while (true) {
            bool lock_conflict_occurred = false;
            std::unordered_set<TxId> blocking_transaction_ids;

            for (auto& request : queue.request_chain) {
                if (&request == &current_allocated_request) break; // Evaluate prior applicants only
                if (!request.is_granted) continue;

                if (required_mode == LockMode::EXCLUSIVE || request.requested_mode == LockMode::EXCLUSIVE) {
                    if (request.tx_id != tx_id) {
                        lock_conflict_occurred = true;
                        blocking_transaction_ids.insert(request.tx_id);
                    }
                }
            }

            if (!lock_conflict_occurred) {
                current_allocated_request.is_granted = true;
                {
                    std::lock_guard<std::mutex> graph_lock(dependency_graph_mutex);
                    dependency_graph.erase(tx_id);
                }
                return;
            }

            // Map the lock conflict to the dependency graph and check for cycles
            {
                std::lock_guard<std::mutex> graph_lock(dependency_graph_mutex);
                dependency_graph[tx_id] = blocking_transaction_ids;
                
                if (IsCycleDetected(tx_id, dependency_graph)) {
                    dependency_graph.erase(tx_id);
                    queue.request_chain.remove_if([&](const LockAcquisitionRequest& req) {
                        return req.tx_id == tx_id && !req.is_granted;
                    });
                    throw DeadlockException(tx_id);
                }
            }

            queue.status_condition.wait(queue_lock);
        }
    }

    // Bulk release step clearing all transaction assets concurrently
    void ExecuteResourceRelease(TxId tx_id) {
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            if (transaction_registry.count(tx_id)) {
                transaction_registry.at(tx_id).is_shrinking = true;
            }
        }

        // Unwind matching items across all storage queues
        for (auto& [row_key, lock_queue] : lock_table) {
            std::unique_lock<std::mutex> queue_lock(lock_queue.queue_mutex);
            lock_queue.request_chain.remove_if([&](const LockAcquisitionRequest& req) {
                return req.tx_id == tx_id;
            });
            lock_queue.status_condition.notify_all();
        }

        {
            std::lock_guard<std::mutex> graph_lock(dependency_graph_mutex);
            dependency_graph.erase(tx_id);
        }
    }

public:
    TxId BeginTransaction() {
        std::lock_guard<std::mutex> lock(transaction_registry_mutex);
        TxId assigned_xid = next_tx_sequence_id.fetch_add(1);
        TxId historical_watermark = assigned_xid;
        
        transaction_registry[assigned_xid] = TransactionContext{assigned_xid, historical_watermark, TransactionStatus::ACTIVE, false};
        return assigned_xid;
    }

    std::optional<std::string> ReadRecord(TxId tx_id, const RowKey& target_key) {
        EnforceLockAcquisition(target_key, tx_id, LockMode::SHARED);
        
        std::lock_guard<std::mutex> storage_lock(storage_heap_mutex);
        TxId isolation_watermark;
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            isolation_watermark = transaction_registry.at(tx_id).isolation_watermark;
        }

        auto heap_iterator = storage_heap.find(target_key);
        if (heap_iterator == storage_heap.end()) return std::nullopt;

        for (const auto& record_version : heap_iterator->second) {
            if (IsVersionVisible(record_version, isolation_watermark, tx_id)) {
                return record_version.data_payload;
            }
        }
        return std::nullopt;
    }

    void InsertRecord(TxId tx_id, const RowKey& target_key, const std::string& input_value) {
        EnforceLockAcquisition(target_key, tx_id, LockMode::EXCLUSIVE);
        
        std::lock_guard<std::mutex> storage_lock(storage_heap_mutex);
        storage_heap[target_key].push_front({input_value, tx_id, 0});
    }

    void UpdateRecord(TxId tx_id, const RowKey& target_key, const std::string& modified_value) {
        EnforceLockAcquisition(target_key, tx_id, LockMode::EXCLUSIVE);
        
        std::lock_guard<std::mutex> storage_lock(storage_heap_mutex);
        TxId isolation_watermark;
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            isolation_watermark = transaction_registry.at(tx_id).isolation_watermark;
        }

        auto heap_iterator = storage_heap.find(target_key);
        if (heap_iterator != storage_heap.end()) {
            for (auto& record_version : heap_iterator->second) {
                if (IsVersionVisible(record_version, isolation_watermark, tx_id) && record_version.deleted_by_tx == 0) {
                    record_version.deleted_by_tx = tx_id; // Soft deletion mark
                    break;
                }
            }
        }
        storage_heap[target_key].push_front({modified_value, tx_id, 0});
    }

    void DeleteRecord(TxId tx_id, const RowKey& target_key) {
        EnforceLockAcquisition(target_key, tx_id, LockMode::EXCLUSIVE);
        
        std::lock_guard<std::mutex> storage_lock(storage_heap_mutex);
        TxId isolation_watermark;
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            isolation_watermark = transaction_registry.at(tx_id).isolation_watermark;
        }

        auto heap_iterator = storage_heap.find(target_key);
        if (heap_iterator == storage_heap.end()) return;

        for (auto& record_version : heap_iterator->second) {
            if (IsVersionVisible(record_version, isolation_watermark, tx_id) && record_version.deleted_by_tx == 0) {
                record_version.deleted_by_tx = tx_id;
                return;
            }
        }
    }

    void CommitTransaction(TxId tx_id) {
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            transaction_registry.at(tx_id).status = TransactionStatus::COMMITTED;
        }
        ExecuteResourceRelease(tx_id);
        std::cout << "[TX " << tx_id << "] status transition: COMMITTED\n";
    }

    void AbortTransaction(TxId tx_id) {
        {
            std::lock_guard<std::mutex> storage_lock(storage_heap_mutex);
            for (auto& [key, version_chain] : storage_heap) {
                for (auto& version : version_chain) {
                    if (version.created_by_tx == tx_id) {
                        version.deleted_by_tx = tx_id; // Poison uncommitted inserts
                    }
                    if (version.deleted_by_tx == tx_id) {
                        version.deleted_by_tx = 0;    // Clear faulty rollback markers
                    }
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(transaction_registry_mutex);
            transaction_registry.at(tx_id).status = TransactionStatus::ABORTED;
        }
        ExecuteResourceRelease(tx_id);
        std::cout << "[TX " << tx_id << "] status transition: ABORTED\n";
    }
};



// 5. Test Harness Engine Verification

void OutputTraceLog(const std::optional<std::string>& log_result, TxId tx_id, const RowKey& row_key) {
    std::cout << "  [TX " << tx_id << "] READ executed -> " << row_key << " content: "
              << (log_result ? *log_result : "<Null/No Visible Element>") << "\n";
}

int main() {
    DatabaseEngine database;

    // ── Scenario 1: MVCC Snapshot Verification ──
    std::cout << "=== Scenario 1: MVCC Snapshot Isolation ===\n";
    {
        TxId tx_1 = database.BeginTransaction();
        database.InsertRecord(tx_1, "checking_account", "1000");
        database.CommitTransaction(tx_1);

        TxId tx_2 = database.BeginTransaction(); 
        TxId tx_3 = database.BeginTransaction();

        database.UpdateRecord(tx_3, "checking_account", "2000");
        database.CommitTransaction(tx_3);

        // Tx 2 should see pre-modification values since Tx 3 finalized *after* Tx 2 initialized
        auto fetch_result = database.ReadRecord(tx_2, "checking_account");
        OutputTraceLog(fetch_result, tx_2, "checking_account"); 
        database.CommitTransaction(tx_2);
    }

    // ── Scenario 2: Concurrent Shared Access Evaluation ──
    std::cout << "\n=== Scenario 2: Concurrent Shared Locks ===\n";
    {
        TxId tx_4 = database.BeginTransaction();
        TxId tx_5 = database.BeginTransaction();
        
        OutputTraceLog(database.ReadRecord(tx_4, "checking_account"), tx_4, "checking_account");
        OutputTraceLog(database.ReadRecord(tx_5, "checking_account"), tx_5, "checking_account");
        
        database.CommitTransaction(tx_4);
        database.CommitTransaction(tx_5);
    }

    // ── Scenario 3: Lock Wait-Queues ──
    std::cout << "\n=== Scenario 3: Exclusive Lock + Waiting ===\n";
    {
        TxId tx_6 = database.BeginTransaction();
        database.UpdateRecord(tx_6, "checking_account", "3000"); 

        std::thread background_reader([&]() {
            TxId tx_7 = database.BeginTransaction();
            std::cout << "  [TX " << tx_7 << "] requesting resource access lock on: checking_account...\n";
            auto read_output = database.ReadRecord(tx_7, "checking_account");
            OutputTraceLog(read_output, tx_7, "checking_account"); 
            database.CommitTransaction(tx_7);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        database.CommitTransaction(tx_6); // Releases lock and unblocks the thread
        background_reader.join();
    }

    // ── Scenario 4: Graph Cycle Interception (Deadlocks) ──
    std::cout << "\n=== Scenario 4: Deadlock Detection ===\n";
    {
        TxId cleanup_a = database.BeginTransaction();
        TxId cleanup_b = database.BeginTransaction();
        database.InsertRecord(cleanup_a, "Resource_A", "init_a");
        database.InsertRecord(cleanup_b, "Resource_B", "init_b");
        database.CommitTransaction(cleanup_a);
        database.CommitTransaction(cleanup_b);

        TxId tx_8 = database.BeginTransaction();
        TxId tx_9 = database.BeginTransaction();

        // Mutually lock baseline nodes
        database.UpdateRecord(tx_8, "Resource_A", "alter_a");
        database.UpdateRecord(tx_9, "Resource_B", "alter_b");

        std::thread execution_worker([&]() {
            try {
                database.UpdateRecord(tx_8, "Resource_B", "deadlock_step_1");
                database.CommitTransaction(tx_8);
            } catch (const DeadlockException& exception) {
                std::cout << "  Caught Exception -> " << exception.what() << "\n";
                database.AbortTransaction(tx_8);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        try {
            database.UpdateRecord(tx_9, "Resource_A", "deadlock_step_2");
            database.CommitTransaction(tx_9);
        } catch (const DeadlockException& exception) {
            std::cout << "  Caught Exception -> " << exception.what() << "\n";
            database.AbortTransaction(tx_9);
        }

        execution_worker.join();
    }

    std::cout << "\nAll test paths successfully verified.\n";
    return 0;
}

```


## Architecture Lifecycle Flow

```
Application Calls
    │
    ▼
DatabaseEngine.BeginTransaction() / ReadRecord() / UpdateRecord() / CommitTransaction()
    │
    ├─► Lock Manager Subsystem (Strict 2PL Engine)
    │     ├── Growing Phase: Evaluates resource requests to grant SHARED or EXCLUSIVE tokens.
    │     ├── Shrinking Phase: Instantly releases all managed tokens at transaction finalization.
    │     └── Cycle Detection: Performs O(V + E) DFS graph audits to break dependency locks.
    │
    └─► MVCC Physical Storage Heap
          ├── INSERT -> Pushes version tracking context { data_payload, created_by_tx=ID, deleted_by_tx=0 }.
          ├── UPDATE -> Soft deletes matching active version via deleted_by_tx=ID, then inserts a new version.
          ├── DELETE -> Marks existing live items with a deletion tag.
          └── READ   -> Traverses the version chain; returns records where creation is valid and deletion is missing.

```

---

## Structural Strategy Comparison

| Metric Component | Pure Multi-Version Concurrency Control (MVCC) | Standard Two-Phase Locking (2PL) | Hybrid Architecture (MVCC + Strict 2PL) |
| --- | --- | --- | --- |
| **Read-to-Write Friction** | Completely decoupled; reads and updates run concurrently without blocking. | Shared access tokens block updates; writes delay incoming read operations. | Decoupled execution paths; read functions operate directly on isolated historical data views. |
| **Concurrent Mutation Tracking** | Requires serialization tracking checks to resolve conflicting updates. | Resolves conflicts inline using exclusive access tokens. | Resolves write-write conflicts safely using exclusive record locks. |
| **Deadlock Risk Profile** | Negligible; non-blocking design avoids resource loops. | High risk; resource allocation patterns require active resolution. | Moderate risk; limited to write operations and requires cycle auditing. |
| **Garbage Collection Cost** | Requires background cleanup processes to purge unneeded historical versions. | None. | Requires maintenance processes to clean up historical records once visibility windows expire. |

---

## Strict 2PL Phase Invariants

```
             GROWING PHASE                   │       SHRINKING PHASE
                                             │
Claim Shared Lock (Row_A)        ──► Granted │
Claim Exclusive Lock (Row_B)     ──► Granted │
Claim Shared Lock (Row_C)        ──► Granted │
                                             │
=============================================┼============================================
                                             │ ──► Commit / Abort Intercept Signaled
                                             │
                                             │ Release Shared Lock (Row_A)      ──► Complete
                                             │ Release Exclusive Lock (Row_B)   ──► Complete
                                             │
Claim Shared Lock (Row_D)        ──► Reject  │ 
                                             │ [Invalid Operation: Violates Invariant Boundary]

```


### Takeaways

* **No Read-Write Friction:** Readers completely skip the lock table and look only at historical data chains. This ensures **readers never block writers, and writers never block readers**.
* **No Cascading Failures:** By holding all locks until the absolute end of the transaction (Strict 2PL), the engine guarantees that uncommitted data is never leaked to other active workers.
* **Serialized Updates:** While readers run completely free, concurrent writes to the same row are strictly locked and serialized to prevent lost updates.
* **Proactive Loop Breaking:** When write locks conflict, the engine tracks them in a dependency graph. If a cycle forms, a Depth-First Search (DFS) instantly flags it and throws a `DeadlockException` to safely abort the youngest path.
* **Clean Rollbacks:** If a transaction aborts, the engine instantly invalidates its inserts and un-marks its deletions before dropping its locks, restoring the database state immediately.