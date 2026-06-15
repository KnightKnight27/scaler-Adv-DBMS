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
// Core Types & Enums
// ─────────────────────────────────────────────

using TransactionID = uint64_t;
using RecordKey = std::string;

enum class State { RUNNING, COMMITTED, ROLLED_BACK };
enum class LockType { READ, WRITE };

struct TxContext {
    TransactionID id;
    TransactionID snapshot_id;
    State state = State::RUNNING;
    bool is_shrinking = false; 
};

struct RecordVersion {
    std::string data;
    TransactionID created_by;
    TransactionID deleted_by; // 0 indicates active
};

struct LockReq {
    TransactionID tx_id;
    LockType type;
    bool is_granted = false;
};

struct LockQueue {
    std::list<LockReq> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

class DeadlockError : public std::runtime_error {
public: 
    explicit DeadlockError(TransactionID id)
        : std::runtime_error("Deadlock detected. Forcing rollback on TX: " + std::to_string(id)) {}
};

// ─────────────────────────────────────────────
// Object-Oriented Database Engine
// ─────────────────────────────────────────────

class DatabaseEngine {
private:
    std::atomic<TransactionID> next_tx_id{1};
    
    // State management
    std::mutex tx_mtx;
    std::unordered_map<TransactionID, TxContext> active_txs;

    // MVCC Storage
    std::mutex storage_mtx;
    std::unordered_map<RecordKey, std::list<RecordVersion>> storage;

    // 2PL Lock Manager
    std::mutex lm_mtx;
    std::unordered_map<RecordKey, std::unique_ptr<LockQueue>> locks;
    std::unordered_map<TransactionID, std::unordered_set<TransactionID>> wait_graph;

    // Internal Helpers
    bool is_tx_committed(TransactionID id) {
        std::lock_guard<std::mutex> lock(tx_mtx);
        auto it = active_txs.find(id);
        return it != active_txs.end() && it->second.state == State::COMMITTED;
    }

    bool check_visibility(const RecordVersion& rev, TransactionID snapshot, TransactionID reader) {
        bool created_visible = (rev.created_by == reader) || 
                               (is_tx_committed(rev.created_by) && rev.created_by < snapshot);
        if (!created_visible) return false;

        if (rev.deleted_by == 0) return true;
        
        bool deleted_visible = (rev.deleted_by == reader) || 
                               (is_tx_committed(rev.deleted_by) && rev.deleted_by < snapshot);
        return !deleted_visible;
    }

    bool detect_cycle(TransactionID start) {
        std::unordered_set<TransactionID> visited, path;
        std::function<bool(TransactionID)> dfs = [&](TransactionID current) {
            visited.insert(current);
            path.insert(current);
            auto edges = wait_graph.find(current);
            if (edges != wait_graph.end()) {
                for (TransactionID neighbor : edges->second) {
                    if (!visited.count(neighbor) && dfs(neighbor)) return true;
                    if (path.count(neighbor)) return true;
                }
            }
            path.erase(current);
            return false;
        };
        return dfs(start);
    }

    LockQueue& get_lock_queue(const RecordKey& key) {
        std::lock_guard<std::mutex> lock(lm_mtx);
        if (!locks[key]) {
            locks[key] = std::make_unique<LockQueue>();
        }
        return *locks[key];
    }

    void request_lock(const RecordKey& key, TransactionID tx, LockType type) {
        {
            std::lock_guard<std::mutex> lock(tx_mtx);
            if (active_txs.at(tx).is_shrinking)
                throw std::runtime_error("Strict 2PL Violation: Growing phase ended.");
        }

        LockQueue& lq = get_lock_queue(key);
        std::unique_lock<std::mutex> ul(lq.mtx);

        for (auto& req : lq.queue) {
            if (req.tx_id == tx && req.is_granted) {
                if (type == LockType::READ || req.type == LockType::WRITE) return;
            }
        }

        lq.queue.push_back({tx, type, false});
        auto& current_req = lq.queue.back();

        while (true) {
            bool has_conflict = false;
            std::unordered_set<TransactionID> blockers;
            
            for (auto& req : lq.queue) {
                if (&req == &current_req) break;
                if (!req.is_granted) continue;
                if (type == LockType::WRITE || req.type == LockType::WRITE) {
                    if (req.tx_id != tx) {
                        has_conflict = true;
                        blockers.insert(req.tx_id);
                    }
                }
            }

            if (!has_conflict) {
                current_req.is_granted = true;
                std::lock_guard<std::mutex> lm_lock(lm_mtx);
                wait_graph.erase(tx);
                return;
            }

            {
                std::lock_guard<std::mutex> lm_lock(lm_mtx);
                wait_graph[tx] = blockers;
                if (detect_cycle(tx)) {
                    wait_graph.erase(tx);
                    lq.queue.remove_if([&](const LockReq& r) { return r.tx_id == tx && !r.is_granted; });
                    throw DeadlockError(tx);
                }
            }
            lq.cv.wait(ul);
        }
    }

    void free_locks(TransactionID tx) {
        {
            std::lock_guard<std::mutex> lock(tx_mtx);
            if (active_txs.count(tx)) active_txs.at(tx).is_shrinking = true;
        }

        for (auto& [key, lq_ptr] : locks) {
            std::unique_lock<std::mutex> ul(lq_ptr->mtx);
            lq_ptr->queue.remove_if([&](const LockReq& r) { return r.tx_id == tx; });
            lq_ptr->cv.notify_all();
        }

        std::lock_guard<std::mutex> lm_lock(lm_mtx);
        wait_graph.erase(tx);
    }

public:
    TransactionID start_tx() {
        std::lock_guard<std::mutex> lock(tx_mtx);
        TransactionID id = next_tx_id.fetch_add(1);
        active_txs[id] = TxContext{id, id, State::RUNNING, false};
        return id;
    }

    std::optional<std::string> fetch(TransactionID tx, const RecordKey& key) {
        request_lock(key, tx, LockType::READ);
        
        std::lock_guard<std::mutex> lock(storage_mtx);
        TransactionID snapshot = active_txs.at(tx).snapshot_id;
        
        auto it = storage.find(key);
        if (it != storage.end()) {
            for (auto& rev : it->second) {
                if (check_visibility(rev, snapshot, tx)) return rev.data;
            }
        }
        return std::nullopt;
    }

    void write_new(TransactionID tx, const RecordKey& key, const std::string& data) {
        request_lock(key, tx, LockType::WRITE);
        std::lock_guard<std::mutex> lock(storage_mtx);
        storage[key].push_front({data, tx, 0});
    }

    void modify(TransactionID tx, const RecordKey& key, const std::string& data) {
        request_lock(key, tx, LockType::WRITE);
        std::lock_guard<std::mutex> lock(storage_mtx);
        TransactionID snapshot = active_txs.at(tx).snapshot_id;
        
        auto it = storage.find(key);
        if (it != storage.end()) {
            for (auto& rev : it->second) {
                if (check_visibility(rev, snapshot, tx) && rev.deleted_by == 0) {
                    rev.deleted_by = tx;
                    break;
                }
            }
        }
        storage[key].push_front({data, tx, 0});
    }

    void commit(TransactionID tx) {
        {
            std::lock_guard<std::mutex> lock(tx_mtx);
            active_txs.at(tx).state = State::COMMITTED;
        }
        free_locks(tx);
        std::cout << "[SUCCESS] Transaction " << tx << " committed.\n";
    }

    void rollback(TransactionID tx) {
        {
            std::lock_guard<std::mutex> lock(storage_mtx);
            for (auto& [key, history] : storage) {
                for (auto& rev : history) {
                    if (rev.created_by == tx) rev.deleted_by = tx; 
                    if (rev.deleted_by == tx) rev.deleted_by = 0;   
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(tx_mtx);
            active_txs.at(tx).state = State::ROLLED_BACK;
        }
        free_locks(tx);
        std::cout << "[WARN] Transaction " << tx << " rolled back.\n";
    }
};

// ─────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────

void log_read(const std::optional<std::string>& data, TransactionID tx, const RecordKey& key) {
    std::cout << "  -> TX_" << tx << " reads '" << key << "': " 
              << (data ? *data : "NULL") << "\n";
}

int main() {
    DatabaseEngine db;

    std::cout << "--- Test 1: Snapshot Isolation (Inventory View) ---\n";
    TransactionID t1 = db.start_tx();
    db.write_new(t1, "item_apples", "50");
    db.commit(t1);

    TransactionID t2 = db.start_tx(); 
    TransactionID t3 = db.start_tx();

    db.modify(t3, "item_apples", "20");
    db.commit(t3);

    log_read(db.fetch(t2, "item_apples"), t2, "item_apples"); // Should see 50
    db.commit(t2);

    std::cout << "\n--- Test 2: Deadlock Resolution ---\n";
    TransactionID t4 = db.start_tx();
    TransactionID t5 = db.start_tx();

    db.write_new(t4, "warehouse_A", "active");
    db.write_new(t5, "warehouse_B", "active");
    db.commit(t4);
    db.commit(t5);

    TransactionID t6 = db.start_tx();
    TransactionID t7 = db.start_tx();

    // Trigger locks manually through reads/writes to simulate cycle
    try {
        db.modify(t6, "warehouse_A", "locked");
        db.modify(t7, "warehouse_B", "locked");

        std::thread edge_thread([&]() {
            try {
                db.modify(t6, "warehouse_B", "locked");
                db.commit(t6);
            } catch (DeadlockError& e) {
                std::cout << "  " << e.what() << "\n";
                db.rollback(t6);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        try {
            db.modify(t7, "warehouse_A", "locked");
            db.commit(t7);
        } catch (DeadlockError& e) {
            std::cout << "  " << e.what() << "\n";
            db.rollback(t7);
        }

        edge_thread.join();
    } catch(...) {}

    std::cout << "\nTests finished executing.\n";
    return 0;
}
