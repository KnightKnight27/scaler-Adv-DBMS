#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

enum class TransactionState {
    Active,
    Committed,
    Aborted
};

struct Version {
    int value;
    long long created_timestamp;
    long long expired_timestamp;
    int writer_transaction_id;
    bool deleted;
};

struct Transaction {
    int id = 0;
    long long snapshot_timestamp = 0;
    TransactionState state = TransactionState::Active;
    std::map<std::string, int> local_writes;
    std::set<std::string> held_locks;
};

struct LockInfo {
    int owner_transaction_id = 0;
};

class TransactionManager {
public:
    void seedValue(const std::string& key, int value) {
        if (store_.count(key) != 0) {
            throw std::runtime_error("key already initialized: " + key);
        }

        store_[key].push_back(Version{value, 0, 0, 0, false});
    }

    int beginTransaction() {
        Transaction tx;
        tx.id = next_transaction_id_++;
        tx.snapshot_timestamp = commit_counter_;
        tx.state = TransactionState::Active;
        transactions_[tx.id] = tx;
        return tx.id;
    }

    int readValue(int transaction_id, const std::string& key) const {
        const Transaction& tx = getTransaction(transaction_id);
        ensureActive(tx);

        const auto local_it = tx.local_writes.find(key);
        if (local_it != tx.local_writes.end()) {
            return local_it->second;
        }

        const auto store_it = store_.find(key);
        if (store_it == store_.end()) {
            throw std::runtime_error("reading missing key '" + key + "'");
        }

        const std::vector<Version>& chain = store_it->second;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const bool visible_created = it->created_timestamp <= tx.snapshot_timestamp;
            const bool visible_not_expired =
                (it->expired_timestamp == 0 || it->expired_timestamp > tx.snapshot_timestamp);

            if (visible_created && visible_not_expired) {
                return it->value;
            }
        }

        throw std::runtime_error("reading missing key '" + key + "' for snapshot");
    }

    void writeValue(int transaction_id, const std::string& key, int value) {
        Transaction& tx = getActiveTransaction(transaction_id);
        acquireWriteLock(tx, key);
        tx.local_writes[key] = value;
    }

    void commitTransaction(int transaction_id) {
        Transaction& tx = getTransaction(transaction_id);
        if (tx.state == TransactionState::Aborted) {
            throw std::runtime_error("committing aborted transaction T" + std::to_string(transaction_id));
        }

        ensureActive(tx);

        const long long commit_timestamp = ++commit_counter_;

        for (const auto& entry : tx.local_writes) {
            const std::string& key = entry.first;
            const int value = entry.second;

            ensureWriteLock(tx, key);

            std::vector<Version>& chain = store_[key];
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                if (it->expired_timestamp == 0) {
                    it->expired_timestamp = commit_timestamp;
                    break;
                }
            }

            chain.push_back(Version{value, commit_timestamp, 0, tx.id, false});
        }

        tx.state = TransactionState::Committed;
        tx.local_writes.clear();
        releaseLocks(tx);
        clearWaitEdges(tx.id);
    }

    void abortTransaction(int transaction_id) {
        Transaction& tx = getTransaction(transaction_id);
        ensureActive(tx);
        abortInternal(tx, "abort requested");
    }

    void vacuum() {
        const long long minimum_snapshot = minimumActiveSnapshot();

        for (auto& entry : store_) {
            std::vector<Version>& chain = entry.second;
            if (chain.size() <= 1) {
                continue;
            }

            std::vector<Version> kept_versions;
            kept_versions.reserve(chain.size());

            for (std::size_t index = 0; index < chain.size(); ++index) {
                const Version& version = chain[index];
                const bool latest_live_version = (index == chain.size() - 1);
                const bool safe_to_remove =
                    !latest_live_version &&
                    version.expired_timestamp != 0 &&
                    version.expired_timestamp < minimum_snapshot;

                if (!safe_to_remove) {
                    kept_versions.push_back(version);
                }
            }

            chain.swap(kept_versions);
        }
    }

    std::size_t versionCount(const std::string& key) const {
        const auto it = store_.find(key);
        if (it == store_.end()) {
            throw std::runtime_error("reading missing key '" + key + "'");
        }

        return it->second.size();
    }

    void printVersionChain(const std::string& key) const {
        const auto it = store_.find(key);
        if (it == store_.end()) {
            throw std::runtime_error("reading missing key '" + key + "'");
        }

        std::cout << "Version chain for " << key << ": ";
        for (std::size_t index = 0; index < it->second.size(); ++index) {
            const Version& version = it->second[index];
            if (index > 0) {
                std::cout << " | ";
            }

            std::cout << "[value=" << version.value
                      << ", create=" << version.created_timestamp
                      << ", expire=" << version.expired_timestamp
                      << ", writer=T" << version.writer_transaction_id
                      << "]";
        }
        std::cout << '\n';
    }

    std::string transactionStateName(int transaction_id) const {
        const Transaction& tx = getTransaction(transaction_id);
        if (tx.state == TransactionState::Active) {
            return "active";
        }
        if (tx.state == TransactionState::Committed) {
            return "committed";
        }
        return "aborted";
    }

private:
    const Transaction& getTransaction(int transaction_id) const {
        const auto it = transactions_.find(transaction_id);
        if (it == transactions_.end()) {
            throw std::runtime_error("unknown transaction id T" + std::to_string(transaction_id));
        }
        return it->second;
    }

    Transaction& getTransaction(int transaction_id) {
        auto it = transactions_.find(transaction_id);
        if (it == transactions_.end()) {
            throw std::runtime_error("unknown transaction id T" + std::to_string(transaction_id));
        }
        return it->second;
    }

    Transaction& getActiveTransaction(int transaction_id) {
        Transaction& tx = getTransaction(transaction_id);
        ensureActive(tx);
        return tx;
    }

    void ensureActive(const Transaction& tx) const {
        if (tx.state != TransactionState::Active) {
            throw std::runtime_error("transaction not active: T" + std::to_string(tx.id));
        }
    }

    bool isTransactionActive(int transaction_id) const {
        const auto it = transactions_.find(transaction_id);
        return it != transactions_.end() && it->second.state == TransactionState::Active;
    }

    void acquireWriteLock(Transaction& tx, const std::string& key) {
        LockInfo& info = lock_table_[key];

        if (info.owner_transaction_id == tx.id) {
            clearOutgoingWaits(tx.id);
            tx.held_locks.insert(key);
            return;
        }

        if (info.owner_transaction_id == 0 || !isTransactionActive(info.owner_transaction_id)) {
            info.owner_transaction_id = tx.id;
            tx.held_locks.insert(key);
            clearOutgoingWaits(tx.id);
            return;
        }

        const int owner = info.owner_transaction_id;
        waits_for_[tx.id].insert(owner);

        const std::vector<int> cycle = detectCycle();
        if (!cycle.empty()) {
            const int victim = *std::max_element(cycle.begin(), cycle.end());
            std::cout << "Deadlock detected: " << formatCycle(cycle)
                      << " | Victim: T" << victim << '\n';

            Transaction& victim_tx = getTransaction(victim);
            abortInternal(victim_tx, "deadlock victim");

            if (victim == tx.id) {
                throw std::runtime_error("deadlock victim abort for T" + std::to_string(tx.id));
            }

            if (info.owner_transaction_id == 0) {
                info.owner_transaction_id = tx.id;
                tx.held_locks.insert(key);
                clearOutgoingWaits(tx.id);
                return;
            }
        }

        throw std::runtime_error(
            "lock conflict on key '" + key + "': T" + std::to_string(tx.id) +
            " waits for T" + std::to_string(owner));
    }

    void ensureWriteLock(const Transaction& tx, const std::string& key) const {
        if (tx.held_locks.count(key) == 0) {
            throw std::runtime_error("writing without lock on key '" + key + "'");
        }
    }

    void releaseLocks(Transaction& tx) {
        for (const std::string& key : tx.held_locks) {
            auto lock_it = lock_table_.find(key);
            if (lock_it != lock_table_.end() && lock_it->second.owner_transaction_id == tx.id) {
                lock_it->second.owner_transaction_id = 0;
            }
        }
        tx.held_locks.clear();
    }

    void clearOutgoingWaits(int transaction_id) {
        waits_for_.erase(transaction_id);
    }

    void clearWaitEdges(int transaction_id) {
        waits_for_.erase(transaction_id);

        for (auto it = waits_for_.begin(); it != waits_for_.end();) {
            it->second.erase(transaction_id);
            if (it->second.empty()) {
                it = waits_for_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<int> detectCycle() const {
        std::set<int> visited;
        std::set<int> active_stack;
        std::vector<int> path;
        std::vector<int> cycle;

        for (const auto& entry : waits_for_) {
            const int transaction_id = entry.first;
            if (visited.count(transaction_id) == 0 &&
                detectCycleDfs(transaction_id, visited, active_stack, path, cycle)) {
                return cycle;
            }
        }

        return {};
    }

    bool detectCycleDfs(
        int current,
        std::set<int>& visited,
        std::set<int>& active_stack,
        std::vector<int>& path,
        std::vector<int>& cycle) const {
        visited.insert(current);
        active_stack.insert(current);
        path.push_back(current);

        const auto it = waits_for_.find(current);
        if (it != waits_for_.end()) {
            for (int next : it->second) {
                if (active_stack.count(next) != 0) {
                    const auto start = std::find(path.begin(), path.end(), next);
                    cycle.assign(start, path.end());
                    return true;
                }

                if (visited.count(next) == 0 &&
                    detectCycleDfs(next, visited, active_stack, path, cycle)) {
                    return true;
                }
            }
        }

        path.pop_back();
        active_stack.erase(current);
        return false;
    }

    std::string formatCycle(const std::vector<int>& cycle) const {
        std::ostringstream out;
        for (std::size_t index = 0; index < cycle.size(); ++index) {
            if (index > 0) {
                out << " -> ";
            }
            out << "T" << cycle[index];
        }

        if (!cycle.empty()) {
            out << " -> T" << cycle.front();
        }

        return out.str();
    }

    void abortInternal(Transaction& tx, const std::string& reason) {
        tx.local_writes.clear();
        releaseLocks(tx);
        tx.state = TransactionState::Aborted;
        clearWaitEdges(tx.id);
        std::cout << "Aborted T" << tx.id << " (" << reason << ")\n";
    }

    long long minimumActiveSnapshot() const {
        long long minimum_snapshot = commit_counter_ + 1;
        bool found_active = false;

        for (const auto& entry : transactions_) {
            const Transaction& tx = entry.second;
            if (tx.state == TransactionState::Active) {
                found_active = true;
                minimum_snapshot = std::min(minimum_snapshot, tx.snapshot_timestamp);
            }
        }

        if (!found_active) {
            return commit_counter_ + 1;
        }

        return minimum_snapshot;
    }

    int next_transaction_id_ = 1;
    long long commit_counter_ = 0;
    std::map<std::string, std::vector<Version>> store_;
    std::map<int, Transaction> transactions_;
    std::map<std::string, LockInfo> lock_table_;
    std::map<int, std::set<int>> waits_for_;
};

void requireCheck(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error("check failed: " + message);
    }
}

TransactionManager createWarehouseManager() {
    TransactionManager manager;
    manager.seedValue("laptop_stock", 15);
    manager.seedValue("monitor_stock", 20);
    manager.seedValue("keyboard_stock", 30);
    manager.seedValue("mouse_stock", 40);
    manager.seedValue("cable_stock", 50);
    return manager;
}

void scenarioBasicCommit() {
    TransactionManager manager = createWarehouseManager();

    const int t1 = manager.beginTransaction();
    std::cout << "T" << t1 << " begins and writes laptop_stock = 12\n";
    manager.writeValue(t1, "laptop_stock", 12);
    manager.commitTransaction(t1);
    std::cout << "T" << t1 << " commits\n";

    const int t2 = manager.beginTransaction();
    const int value = manager.readValue(t2, "laptop_stock");
    std::cout << "T" << t2 << " reads laptop_stock = " << value << '\n';
    requireCheck(value == 12, "new transaction should see committed value");
    manager.commitTransaction(t2);

    std::cout << "PASS: committed warehouse update is visible to the next transaction.\n";
}

void scenarioSnapshotIsolation() {
    TransactionManager manager = createWarehouseManager();

    const int t1 = manager.beginTransaction();
    const int first_read = manager.readValue(t1, "monitor_stock");
    std::cout << "T" << t1 << " begins with snapshot and reads monitor_stock = " << first_read << '\n';

    const int t2 = manager.beginTransaction();
    std::cout << "T" << t2 << " writes monitor_stock = 14 and commits\n";
    manager.writeValue(t2, "monitor_stock", 14);
    manager.commitTransaction(t2);

    const int second_read = manager.readValue(t1, "monitor_stock");
    std::cout << "T" << t1 << " reads monitor_stock again and still sees " << second_read << '\n';

    const int t3 = manager.beginTransaction();
    const int third_read = manager.readValue(t3, "monitor_stock");
    std::cout << "T" << t3 << " starts later and reads monitor_stock = " << third_read << '\n';

    requireCheck(first_read == 20, "initial snapshot should see old value");
    requireCheck(second_read == 20, "older snapshot should keep seeing old value");
    requireCheck(third_read == 14, "new snapshot should see committed value");

    manager.commitTransaction(t1);
    manager.commitTransaction(t3);

    std::cout << "PASS: MVCC snapshot isolation behaves as expected.\n";
}

void scenarioWriteLockConflict() {
    TransactionManager manager = createWarehouseManager();

    const int t1 = manager.beginTransaction();
    const int t2 = manager.beginTransaction();

    std::cout << "T" << t1 << " writes keyboard_stock = 26 and keeps the lock\n";
    manager.writeValue(t1, "keyboard_stock", 26);

    bool saw_conflict = false;
    try {
        std::cout << "T" << t2 << " tries to write keyboard_stock = 24 before T" << t1 << " commits\n";
        manager.writeValue(t2, "keyboard_stock", 24);
    } catch (const std::runtime_error& error) {
        saw_conflict = true;
        std::cout << "Expected conflict: " << error.what() << '\n';
    }

    requireCheck(saw_conflict, "second writer should be blocked while the first lock is held");

    manager.commitTransaction(t1);
    std::cout << "T" << t1 << " commits and releases the lock\n";

    std::cout << "T" << t2 << " retries keyboard_stock = 24\n";
    manager.writeValue(t2, "keyboard_stock", 24);
    manager.commitTransaction(t2);

    const int t3 = manager.beginTransaction();
    const int final_value = manager.readValue(t3, "keyboard_stock");
    std::cout << "T" << t3 << " reads keyboard_stock = " << final_value << '\n';

    requireCheck(final_value == 24, "retry after commit should succeed");
    manager.commitTransaction(t3);

    std::cout << "PASS: strict 2PL write lock conflict and retry are correct.\n";
}

void scenarioAbortRollback() {
    TransactionManager manager = createWarehouseManager();

    const int t1 = manager.beginTransaction();
    std::cout << "T" << t1 << " writes mouse_stock = 18\n";
    manager.writeValue(t1, "mouse_stock", 18);

    const int own_view = manager.readValue(t1, "mouse_stock");
    std::cout << "T" << t1 << " reads its own uncommitted value = " << own_view << '\n';
    requireCheck(own_view == 18, "transaction should see its own pending write");

    manager.abortTransaction(t1);

    const int t2 = manager.beginTransaction();
    const int visible_value = manager.readValue(t2, "mouse_stock");
    std::cout << "T" << t2 << " reads mouse_stock after abort = " << visible_value << '\n';
    requireCheck(visible_value == 40, "aborted write must not become visible");
    manager.commitTransaction(t2);

    std::cout << "PASS: abort correctly rolls back local writes.\n";
}

void scenarioDeadlockDetection() {
    TransactionManager manager = createWarehouseManager();

    const int t1 = manager.beginTransaction();
    const int t2 = manager.beginTransaction();

    std::cout << "T" << t1 << " writes laptop_stock = 13\n";
    manager.writeValue(t1, "laptop_stock", 13);

    std::cout << "T" << t2 << " writes monitor_stock = 17\n";
    manager.writeValue(t2, "monitor_stock", 17);

    bool saw_wait = false;
    try {
        std::cout << "T" << t1 << " tries to write monitor_stock and must wait for T" << t2 << '\n';
        manager.writeValue(t1, "monitor_stock", 16);
    } catch (const std::runtime_error& error) {
        saw_wait = true;
        std::cout << "Expected wait/conflict: " << error.what() << '\n';
    }
    requireCheck(saw_wait, "first conflicting write should create a wait edge");

    bool saw_deadlock = false;
    try {
        std::cout << "T" << t2 << " now tries to write laptop_stock and closes the cycle\n";
        manager.writeValue(t2, "laptop_stock", 11);
    } catch (const std::runtime_error& error) {
        saw_deadlock = true;
        std::cout << "Expected deadlock handling: " << error.what() << '\n';
    }

    requireCheck(saw_deadlock, "deadlock should be detected");
    requireCheck(manager.transactionStateName(t2) == "aborted", "youngest transaction should be the victim");

    std::cout << "T" << t1 << " retries monitor_stock after victim abort\n";
    manager.writeValue(t1, "monitor_stock", 16);
    manager.commitTransaction(t1);

    const int t3 = manager.beginTransaction();
    const int laptop = manager.readValue(t3, "laptop_stock");
    const int monitor = manager.readValue(t3, "monitor_stock");
    std::cout << "T" << t3 << " reads laptop_stock = " << laptop
              << " and monitor_stock = " << monitor << '\n';

    requireCheck(laptop == 13, "surviving transaction should keep its first write");
    requireCheck(monitor == 16, "surviving transaction should complete after retry");
    manager.commitTransaction(t3);

    std::cout << "PASS: deadlock detection aborts the youngest victim and lets the other transaction finish.\n";
}

void scenarioVacuumCleanup() {
    TransactionManager manager = createWarehouseManager();

    const int t1 = manager.beginTransaction();
    const int baseline = manager.readValue(t1, "cable_stock");
    std::cout << "T" << t1 << " begins early and reads cable_stock = " << baseline << '\n';

    const int t2 = manager.beginTransaction();
    manager.writeValue(t2, "cable_stock", 45);
    manager.commitTransaction(t2);
    std::cout << "T" << t2 << " commits cable_stock = 45\n";

    const int t3 = manager.beginTransaction();
    manager.writeValue(t3, "cable_stock", 41);
    manager.commitTransaction(t3);
    std::cout << "T" << t3 << " commits cable_stock = 41\n";

    const std::size_t before_vacuum = manager.versionCount("cable_stock");
    std::cout << "Version count before vacuum = " << before_vacuum << '\n';
    manager.printVersionChain("cable_stock");

    manager.vacuum();
    const std::size_t while_old_snapshot_active = manager.versionCount("cable_stock");
    std::cout << "Version count while old snapshot is active = " << while_old_snapshot_active << '\n';
    requireCheck(while_old_snapshot_active == before_vacuum, "old snapshot should keep expired versions alive");

    manager.commitTransaction(t1);
    std::cout << "T" << t1 << " finishes\n";

    manager.vacuum();
    const std::size_t after_vacuum = manager.versionCount("cable_stock");
    std::cout << "Version count after vacuum = " << after_vacuum << '\n';
    manager.printVersionChain("cable_stock");

    requireCheck(before_vacuum == 3, "three versions should exist before cleanup");
    requireCheck(after_vacuum == 1, "vacuum should remove old expired versions");

    std::cout << "PASS: vacuum removes versions only after old snapshots finish.\n";
}

template <typename ScenarioFunction>
void runScenario(const std::string& title, ScenarioFunction scenario) {
    std::cout << "\n=== " << title << " ===\n";
    scenario();
}

int main() {
    try {
        runScenario("Scenario 1: Basic transaction commit", scenarioBasicCommit);
        runScenario("Scenario 2: MVCC snapshot isolation", scenarioSnapshotIsolation);
        runScenario("Scenario 3: Strict 2PL write lock conflict", scenarioWriteLockConflict);
        runScenario("Scenario 4: Abort rollback", scenarioAbortRollback);
        runScenario("Scenario 5: Deadlock detection", scenarioDeadlockDetection);
        runScenario("Scenario 6: Vacuum cleanup", scenarioVacuumCleanup);

        std::cout << "\nAll Lab 8 transaction manager checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nLab 8 demo failed: " << error.what() << '\n';
        return 1;
    }
}
