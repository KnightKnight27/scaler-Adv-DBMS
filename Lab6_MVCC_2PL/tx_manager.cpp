#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <atomic>
#include <list>
#include <stdexcept>

using txn_id_t = uint64_t;

enum class TxnState {
    ACTIVE,
    COMMITTED,
    ABORTED
};

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

struct Version {
    txn_id_t tx_id;     
    int value;          
    Version* prev;      
    bool is_committed;  
};

struct Tuple {
    int key;
    Version* latest;
};

struct LockRequest {
    txn_id_t txn_id;
    LockMode lock_mode;
    bool is_granted;
};

struct LockHead {
    std::mutex mutex;
    std::condition_variable cv;
    std::list<LockRequest> request_queue;
    txn_id_t exclusive_holder = 0;
    std::unordered_set<txn_id_t> shared_holders;
};

class LockManager {
private:
    std::mutex map_mutex;
    std::unordered_map<int, LockHead*> lock_table;
    
    std::mutex graph_mutex;
    std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> waits_for;

    std::mutex txn_locks_mutex;
    std::unordered_map<txn_id_t, std::unordered_set<int>> txn_held_locks;

public:
    LockManager() = default;

    LockHead* GetLockHead(int key) {
        std::lock_guard<std::mutex> lock(map_mutex);
        if (lock_table.find(key) == lock_table.end()) {
            lock_table[key] = new LockHead();
        }
        return lock_table[key];
    }

    void AddWaitsFor(txn_id_t waiting_txn, txn_id_t holder_txn) {
        std::lock_guard<std::mutex> lock(graph_mutex);
        if (waiting_txn != holder_txn) {
            waits_for[waiting_txn].insert(holder_txn);
        }
    }

    void RemoveWaitsFor(txn_id_t waiting_txn) {
        std::lock_guard<std::mutex> lock(graph_mutex);
        waits_for.erase(waiting_txn);
    }

    void RemoveTxnFromGraph(txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(graph_mutex);
        waits_for.erase(txn_id);
        for (auto& pair : waits_for) {
            pair.second.erase(txn_id);
        }
    }

    bool HasCycleDFS(txn_id_t node, std::unordered_set<txn_id_t>& visited, std::unordered_set<txn_id_t>& rec_stack, std::vector<txn_id_t>& path) {
        visited.insert(node);
        rec_stack.insert(node);
        path.push_back(node);

        auto it = waits_for.find(node);
        if (it != waits_for.end()) {
            for (txn_id_t neighbor : it->second) {
                if (rec_stack.count(neighbor)) {
                    path.push_back(neighbor);
                    return true;
                }
                if (!visited.count(neighbor)) {
                    if (HasCycleDFS(neighbor, visited, rec_stack, path)) {
                        return true;
                    }
                }
            }
        }

        path.pop_back();
        rec_stack.erase(node);
        return false;
    }

    txn_id_t FindDeadlockVictim() {
        std::lock_guard<std::mutex> lock(graph_mutex);
        std::unordered_set<txn_id_t> visited;
        std::unordered_set<txn_id_t> rec_stack;
        std::vector<txn_id_t> path;

        for (const auto& pair : waits_for) {
            txn_id_t start_node = pair.first;
            if (!visited.count(start_node)) {
                path.clear();
                if (HasCycleDFS(start_node, visited, rec_stack, path)) {
                    txn_id_t cycle_start = path.back();
                    txn_id_t victim = 0;
                    bool in_cycle = false;
                    for (txn_id_t n : path) {
                        if (n == cycle_start) {
                            in_cycle = true;
                        }
                        if (in_cycle) {
                            if (n > victim) {
                                victim = n;
                            }
                        }
                    }
                    return victim;
                }
            }
        }
        return 0;
    }

    bool AcquireLock(txn_id_t txn_id, int key, LockMode mode) {
        LockHead* lh = GetLockHead(key);
        std::unique_lock<std::mutex> lock(lh->mutex);

        lh->request_queue.push_back({txn_id, mode, false});
        auto req_it = std::prev(lh->request_queue.end());

        while (true) {
            bool conflict = false;
            if (mode == LockMode::EXCLUSIVE) {
                if (lh->exclusive_holder != 0 && lh->exclusive_holder != txn_id) {
                    conflict = true;
                    AddWaitsFor(txn_id, lh->exclusive_holder);
                }
                for (txn_id_t shared_holder : lh->shared_holders) {
                    if (shared_holder != txn_id) {
                        conflict = true;
                        AddWaitsFor(txn_id, shared_holder);
                    }
                }
            } else {
                if (lh->exclusive_holder != 0 && lh->exclusive_holder != txn_id) {
                    conflict = true;
                    AddWaitsFor(txn_id, lh->exclusive_holder);
                }
            }

            if (!conflict) {
                for (auto it = lh->request_queue.begin(); it != req_it; ++it) {
                    if (mode == LockMode::EXCLUSIVE || it->lock_mode == LockMode::EXCLUSIVE) {
                        conflict = true;
                        AddWaitsFor(txn_id, it->txn_id);
                        break;
                    }
                }
            }

            if (!conflict) {
                req_it->is_granted = true;
                RemoveWaitsFor(txn_id);
                if (mode == LockMode::EXCLUSIVE) {
                    lh->exclusive_holder = txn_id;
                } else {
                    lh->shared_holders.insert(txn_id);
                }
                
                {
                    std::lock_guard<std::mutex> txn_lock(txn_locks_mutex);
                    txn_held_locks[txn_id].insert(key);
                }
                return true;
            }

            lh->cv.wait(lock);

            bool still_in_queue = false;
            for (const auto& r : lh->request_queue) {
                if (r.txn_id == txn_id) {
                    still_in_queue = true;
                    break;
                }
            }
            if (!still_in_queue) {
                return false; 
            }
        }
    }

    void ReleaseLocks(txn_id_t txn_id) {
        std::unordered_set<int> locks_to_release;
        {
            std::lock_guard<std::mutex> txn_lock(txn_locks_mutex);
            auto it = txn_held_locks.find(txn_id);
            if (it != txn_held_locks.end()) {
                locks_to_release = it->second;
                txn_held_locks.erase(it);
            }
        }

        for (int key : locks_to_release) {
            LockHead* lh = GetLockHead(key);
            std::lock_guard<std::mutex> lock(lh->mutex);

            if (lh->exclusive_holder == txn_id) {
                lh->exclusive_holder = 0;
            }
            lh->shared_holders.erase(txn_id);

            lh->request_queue.remove_if([txn_id](const LockRequest& req) {
                return req.txn_id == txn_id;
            });

            lh->cv.notify_all();
        }

        RemoveTxnFromGraph(txn_id);
    }

    void ForceAbortRequest(txn_id_t txn_id) {
        std::lock_guard<std::mutex> map_lock(map_mutex);
        for (auto& pair : lock_table) {
            LockHead* lh = pair.second;
            std::lock_guard<std::mutex> lock(lh->mutex);
            bool removed = false;
            lh->request_queue.remove_if([txn_id, &removed](const LockRequest& req) {
                if (req.txn_id == txn_id && !req.is_granted) {
                    removed = true;
                    return true;
                }
                return false;
            });
            if (removed) {
                lh->cv.notify_all();
            }
        }
        RemoveTxnFromGraph(txn_id);
    }
};

class Database {
private:
    std::unordered_map<int, Tuple> tuples;
    std::mutex db_mutex;
    LockManager lock_manager;
    std::unordered_map<txn_id_t, TxnState> txn_states;
    std::mutex txn_state_mutex;
    std::atomic<txn_id_t> next_txn_id{1};
    std::atomic<bool> is_running{true};
    std::thread deadlock_thread;

    void DeadlockDetectionLoop() {
        while (is_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            txn_id_t victim = lock_manager.FindDeadlockVictim();
            if (victim != 0) {
                std::cout << "[DeadlockDetector] Deadlock cycle detected! Aborting youngest transaction T" << victim << std::endl;
                Abort(victim);
            }
        }
    }

public:
    Database() {
        tuples[101] = {101, new Version{0, 1000, nullptr, true}};
        tuples[102] = {102, new Version{0, 2000, nullptr, true}};
        deadlock_thread = std::thread(&Database::DeadlockDetectionLoop, this);
    }

    ~Database() {
        is_running = false;
        if (deadlock_thread.joinable()) {
            deadlock_thread.join();
        }
        for (auto& pair : tuples) {
            Version* cur = pair.second.latest;
            while (cur) {
                Version* tmp = cur->prev;
                delete cur;
                cur = tmp;
            }
        }
    }

    txn_id_t BeginTransaction() {
        txn_id_t tid = next_txn_id++;
        std::lock_guard<std::mutex> lock(txn_state_mutex);
        txn_states[tid] = TxnState::ACTIVE;
        std::cout << "[TxManager] Transaction T" << tid << " started." << std::endl;
        return tid;
    }

    TxnState GetState(txn_id_t txn_id) {
        std::lock_guard<std::mutex> lock(txn_state_mutex);
        return txn_states[txn_id];
    }

    void Commit(txn_id_t txn_id) {
        std::unique_lock<std::mutex> lock(txn_state_mutex);
        if (txn_states[txn_id] != TxnState::ACTIVE) {
            return;
        }
        txn_states[txn_id] = TxnState::COMMITTED;
        lock.unlock();

        std::lock_guard<std::mutex> db_lock(db_mutex);
        for (auto& pair : tuples) {
            Version* cur = pair.second.latest;
            while (cur) {
                if (cur->tx_id == txn_id) {
                    cur->is_committed = true;
                }
                cur = cur->prev;
            }
        }

        lock_manager.ReleaseLocks(txn_id);
        std::cout << "[TxManager] Transaction T" << txn_id << " committed successfully." << std::endl;
    }

    void Abort(txn_id_t txn_id) {
        std::unique_lock<std::mutex> lock(txn_state_mutex);
        if (txn_states[txn_id] != TxnState::ACTIVE) {
            return;
        }
        txn_states[txn_id] = TxnState::ABORTED;
        lock.unlock();

        lock_manager.ForceAbortRequest(txn_id);
        lock_manager.ReleaseLocks(txn_id);
        std::cout << "[TxManager] Transaction T" << txn_id << " aborted & rolled back." << std::endl;
    }

    int Read(txn_id_t txn_id, int key) {
        if (GetState(txn_id) == TxnState::ABORTED) {
            throw std::runtime_error("Transaction aborted");
        }

        if (!lock_manager.AcquireLock(txn_id, key, LockMode::SHARED)) {
            throw std::runtime_error("Transaction aborted while waiting for lock");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        Tuple& tuple = tuples.at(key);
        Version* cur = tuple.latest;
        while (cur) {
            if (cur->is_committed || cur->tx_id == txn_id) {
                return cur->value;
            }
            cur = cur->prev;
        }
        throw std::runtime_error("No visible version found");
    }

    void Write(txn_id_t txn_id, int key, int value) {
        if (GetState(txn_id) == TxnState::ABORTED) {
            throw std::runtime_error("Transaction aborted");
        }

        if (!lock_manager.AcquireLock(txn_id, key, LockMode::EXCLUSIVE)) {
            throw std::runtime_error("Transaction aborted while waiting for lock");
        }

        std::lock_guard<std::mutex> lock(db_mutex);
        Tuple& tuple = tuples.at(key);
        Version* new_ver = new Version{txn_id, value, tuple.latest, false};
        tuple.latest = new_ver;
        std::cout << "[MVCC] Transaction T" << txn_id << " wrote value " << value << " to key " << key << std::endl;
    }
};

void RunTxn1(Database& db) {
    txn_id_t tid = db.BeginTransaction();
    try {
        db.Write(tid, 101, 1500);
        std::cout << "[Worker T1] Wrote to key 101. Sleeping..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "[Worker T1] Trying to write to key 102..." << std::endl;
        db.Write(tid, 102, 2500);

        db.Commit(tid);
        std::cout << "[Worker T1] Completed successfully." << std::endl;
    } catch (const std::exception& e) {
        db.Abort(tid);
        std::cout << "[Worker T1] Exception caught: " << e.what() << std::endl;
    }
}

void RunTxn2(Database& db) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
    txn_id_t tid = db.BeginTransaction();
    try {
        db.Write(tid, 102, 3000);
        std::cout << "[Worker T2] Wrote to key 102. Sleeping..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "[Worker T2] Trying to write to key 101..." << std::endl;
        db.Write(tid, 101, 4000);

        db.Commit(tid);
        std::cout << "[Worker T2] Completed successfully." << std::endl;
    } catch (const std::exception& e) {
        db.Abort(tid);
        std::cout << "[Worker T2] Exception caught: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== Starting Lab 6: Transaction Manager (MVCC + Strict 2PL + Deadlock Detection) ===" << std::endl;
    Database db;

    std::thread t1(RunTxn1, std::ref(db));
    std::thread t2(RunTxn2, std::ref(db));

    t1.join();
    t2.join();

    std::cout << "\nChecking final database state:" << std::endl;
    txn_id_t tid_check = db.BeginTransaction();
    try {
        std::cout << "Key 101: " << db.Read(tid_check, 101) << std::endl;
        std::cout << "Key 102: " << db.Read(tid_check, 102) << std::endl;
        db.Commit(tid_check);
    } catch (const std::exception& e) {
        db.Abort(tid_check);
        std::cout << "Error checking database state: " << e.what() << std::endl;
    }

    return 0;
}
