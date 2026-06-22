#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <queue>

using tx_id_t = uint64_t;
using db_key_t = std::string;
using value_t = std::string;

enum class TxStatus { ACTIVE, COMMITTED, ABORTED };
enum class LockMode { SHARED, EXCLUSIVE };

struct Version {
    tx_id_t tx_id;
    value_t value;
    std::shared_ptr<Version> next;
    Version(tx_id_t id, value_t val) : tx_id(id), value(val), next(nullptr) {}
};

struct RecordHead {
    std::shared_mutex rw_mutex;
    std::shared_ptr<Version> oldest_version;
    tx_id_t exclusive_lock_owner = 0;
    std::unordered_set<tx_id_t> shared_lock_owners;
};

class TransactionManager {
private:
    std::atomic<tx_id_t> next_tx_id{1};
    std::unordered_map<tx_id_t, TxStatus> tx_table;
    std::unordered_map<db_key_t, std::unique_ptr<RecordHead>> storage;
    
    std::unordered_map<tx_id_t, std::unordered_set<db_key_t>> tx_held_locks;
    std::unordered_map<db_key_t, std::queue<std::pair<tx_id_t, LockMode>>> lock_table_waiters;
    
    std::mutex manager_mutex;
    std::atomic<bool> running{true};
    std::thread deadlock_detector_thread;

    bool has_cycle_dfs(tx_id_t u, std::unordered_map<tx_id_t, std::unordered_set<tx_id_t>>& graph,
                       std::unordered_set<tx_id_t>& visited, std::unordered_set<tx_id_t>& rec_stack, tx_id_t& victim) {
        visited.insert(u);
        rec_stack.insert(u);

        for (tx_id_t v : graph[u]) {
            if (rec_stack.count(v)) {
                victim = u; 
                return true;
            }
            if (!visited.count(v)) {
                if (has_cycle_dfs(v, graph, visited, rec_stack, victim)) return true;
            }
        }
        rec_stack.erase(u);
        return false;
    }

    void detect_deadlocks() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::lock_guard<std::mutex> lock(manager_mutex);

            std::unordered_map<tx_id_t, std::unordered_set<tx_id_t>> wait_for_graph;
            for (auto& [key, queue] : lock_table_waiters) {
                if (queue.empty()) continue;
                
                auto& head = storage[key];
                std::unordered_set<tx_id_t> current_owners;
                if (head->exclusive_lock_owner != 0) current_owners.insert(head->exclusive_lock_owner);
                for (auto id : head->shared_lock_owners) current_owners.insert(id);

                std::queue<std::pair<tx_id_t, LockMode>> temp_q = queue;
                while (!temp_q.empty()) {
                    tx_id_t waiter = temp_q.front().first;
                    for (tx_id_t owner : current_owners) {
                        if (waiter != owner) {
                            wait_for_graph[waiter].insert(owner);
                        }
                    }
                    current_owners.insert(waiter);
                    temp_q.pop();
                }
            }

            std::unordered_set<tx_id_t> visited;
            std::unordered_set<tx_id_t> rec_stack;
            tx_id_t victim = 0;

            for (auto& [tx, neighbors] : wait_for_graph) {
                if (!visited.count(tx)) {
                    if (has_cycle_dfs(tx, wait_for_graph, visited, rec_stack, victim)) {
                        std::cout << "[Deadlock Detector] Found cycle! Aborting Transaction " << victim << std::endl;
                        abort_tx_internal(victim);
                        break;
                    }
                }
            }
        }
    }

    void abort_tx_internal(tx_id_t tx_id) {
        tx_table[tx_id] = TxStatus::ABORTED;
        release_locks(tx_id);
    }

    void release_locks(tx_id_t tx_id) {
        for (const auto& key : tx_held_locks[tx_id]) {
            auto& head = storage[key];
            if (head->exclusive_lock_owner == tx_id) {
                head->exclusive_lock_owner = 0;
            }
            head->shared_lock_owners.erase(tx_id);

            auto& queue = lock_table_waiters[key];
            if (!queue.empty() && queue.front().first == tx_id) {
                queue.pop();
            }
        }
        tx_held_locks.erase(tx_id);
    }

public:
    TransactionManager() {
        deadlock_detector_thread = std::thread(&TransactionManager::detect_deadlocks, this);
    }

    ~TransactionManager() {
        running = false;
        if (deadlock_detector_thread.joinable()) {
            deadlock_detector_thread.join();
        }
    }

    void init_key(db_key_t key, value_t init_val) {
        auto head = std::make_unique<RecordHead>();
        head->oldest_version = std::make_shared<Version>(0, init_val);
        storage[key] = std::move(head);
    }

    tx_id_t begin_tx() {
        std::lock_guard<std::mutex> lock(manager_mutex);
        tx_id_t id = next_tx_id++;
        tx_table[id] = TxStatus::ACTIVE;
        return id;
    }

    bool write(tx_id_t tx_id, db_key_t key, value_t val) {
        std::unique_lock<std::mutex> lock(manager_mutex);
        if (tx_table[tx_id] != TxStatus::ACTIVE) return false;

        auto& head = storage[key];
        if (head->exclusive_lock_owner == tx_id) {
            auto new_ver = std::make_shared<Version>(tx_id, val);
            new_ver->next = head->oldest_version;
            head->oldest_version = new_ver;
            return true;
        }

        if (head->exclusive_lock_owner == 0 && head->shared_lock_owners.empty()) {
            head->exclusive_lock_owner = tx_id;
            tx_held_locks[tx_id].insert(key);
            auto new_ver = std::make_shared<Version>(tx_id, val);
            new_ver->next = head->oldest_version;
            head->oldest_version = new_ver;
            return true;
        }

        lock_table_waiters[key].push({tx_id, LockMode::EXCLUSIVE});
        
        while (running && tx_table[tx_id] == TxStatus::ACTIVE) {
            if (head->exclusive_lock_owner == 0 && head->shared_lock_owners.empty() && lock_table_waiters[key].front().first == tx_id) {
                lock_table_waiters[key].pop();
                head->exclusive_lock_owner = tx_id;
                tx_held_locks[tx_id].insert(key);
                auto new_ver = std::make_shared<Version>(tx_id, val);
                new_ver->next = head->oldest_version;
                head->oldest_version = new_ver;
                return true;
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lock.lock();
        }
        return false;
    }

    std::pair<bool, value_t> read(tx_id_t tx_id, db_key_t key) {
        std::unique_lock<std::mutex> lock(manager_mutex);
        if (tx_table[tx_id] != TxStatus::ACTIVE) return {false, ""};

        auto& head = storage[key];
        if (head->exclusive_lock_owner == tx_id || head->shared_lock_owners.count(tx_id)) {
            auto curr = head->oldest_version;
            while (curr) {
                if (curr->tx_id == tx_id || tx_table[curr->tx_id] == TxStatus::COMMITTED || curr->tx_id == 0) {
                    return {true, curr->value};
                }
                curr = curr->next;
            }
        }

        if (head->exclusive_lock_owner == 0) {
            head->shared_lock_owners.insert(tx_id);
            tx_held_locks[tx_id].insert(key);
            auto curr = head->oldest_version;
            while (curr) {
                if (curr->tx_id == tx_id || tx_table[curr->tx_id] == TxStatus::COMMITTED || curr->tx_id == 0) {
                    return {true, curr->value};
                }
                curr = curr->next;
            }
        }

        lock_table_waiters[key].push({tx_id, LockMode::SHARED});

        while (running && tx_table[tx_id] == TxStatus::ACTIVE) {
            if (head->exclusive_lock_owner == 0 && lock_table_waiters[key].front().first == tx_id) {
                lock_table_waiters[key].pop();
                head->shared_lock_owners.insert(tx_id);
                tx_held_locks[tx_id].insert(key);
                auto curr = head->oldest_version;
                while (curr) {
                    if (curr->tx_id == tx_id || tx_table[curr->tx_id] == TxStatus::COMMITTED || curr->tx_id == 0) {
                        return {true, curr->value};
                    }
                    curr = curr->next;
                }
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lock.lock();
        }
        return {false, ""};
    }

    void commit(tx_id_t tx_id) {
        std::lock_guard<std::mutex> lock(manager_mutex);
        if (tx_table[tx_id] != TxStatus::ACTIVE) return;
        tx_table[tx_id] = TxStatus::COMMITTED;
        release_locks(tx_id);
        std::cout << "Transaction " << tx_id << " Committed Successfully." << std::endl;
    }
};

int main() {
    TransactionManager tm;
    tm.init_key("A", "10");
    tm.init_key("B", "20");

    std::cout << "--- Starting Deadlock Scenario ---" << std::endl;
    tx_id_t tx1 = tm.begin_tx();
    tx_id_t tx2 = tm.begin_tx();

    std::thread t1([&]() {
        if (tm.write(tx1, "A", "11")) {
            std::cout << "Tx1 wrote A=11" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << "Tx1 trying to write B..." << std::endl;
            if (tm.write(tx1, "B", "12")) {
                std::cout << "Tx1 wrote B=12" << std::endl;
                tm.commit(tx1);
            } else {
                std::cout << "Tx1 aborted/failed." << std::endl;
            }
        }
    });

    std::thread t2([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (tm.write(tx2, "B", "21")) {
            std::cout << "Tx2 wrote B=21" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << "Tx2 trying to write A..." << std::endl;
            if (tm.write(tx2, "A", "22")) {
                std::cout << "Tx2 wrote A=22" << std::endl;
                tm.commit(tx2);
            } else {
                std::cout << "Tx2 aborted/failed." << std::endl;
            }
        }
    });

    t1.join();
    t2.join();

    return 0;
}
