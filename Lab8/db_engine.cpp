#include "db_engine.hpp"

#include <iostream>
#include <stdexcept>

// ---- MVCCStore Implementation -----------------------------------------------

void MVCCStore::insert_initial(const std::string& key, int val) {
    records_[key].push_back({val, 0, ++current_time_});
}

int MVCCStore::get_clock() const {
    return current_time_;
}

int MVCCStore::fetch_version(const std::string& key, const TransactionHandle& tx) const {
    const auto& history = records_.at(key);

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->ts_commit <= tx.snapshot_ts) {
            return it->data_val;
        }
    }

    throw std::runtime_error("No visible version found for this snapshot");
}

void MVCCStore::commit_writes(TransactionHandle& tx) {
    int new_ts = ++current_time_;

    for (const auto& write_op : tx.pending_writes) {
        records_[write_op.first].push_back({write_op.second, tx.tx_id, new_ts});
    }

    tx.state = TransactionState::Success;
}

void MVCCStore::print_versions() const {
    std::cout << "\n=== Record Versions ===\n";
    for (const auto& pair : records_) {
        std::cout << pair.first << " -> ";
        for (const auto& rev : pair.second) {
            std::cout << "[val:" << rev.data_val << " by:T" << rev.creator_id
                      << " @ts:" << rev.ts_commit << "] ";
        }
        std::cout << '\n';
    }
}

// ---- LockManager Implementation ---------------------------------------------

bool LockManager::acquire_lock(TransactionHandle& tx, const std::string& key, LockMode mode) {
    if (tx.shrink_phase) {
        std::cout << "[Error] T" << tx.tx_id << " requested lock during shrinking phase (2PL violation)\n";
        tx.state = TransactionState::Failed;
        return false;
    }

    LockEntry& entry = lock_table_[key];
    std::set<int> conflicts = get_conflicts(tx.tx_id, entry, mode);

    if (conflicts.empty()) {
        assign_lock(tx, key, entry, mode);
        remove_waiter(tx.tx_id);
        tx.state = TransactionState::Active;
        std::cout << "T" << tx.tx_id << " acquired "
                  << (mode == LockMode::Read ? "Read" : "Write") << " lock on " << key << '\n';
        return true;
    }

    dependencies_[tx.tx_id] = conflicts;
    tx.state = TransactionState::Blocked;

    std::cout << "T" << tx.tx_id << " is waiting for " << key << ", held by: ";
    for (int conf_id : conflicts) std::cout << "T" << conf_id << ' ';
    std::cout << '\n';

    if (check_for_cycles()) {
        std::cout << "Deadlock detected (cycle in wait graph). Rolling back T" << tx.tx_id << '\n';
        rollback(tx);
    }

    return false;
}

void LockManager::release_all_locks(TransactionHandle& tx) {
    tx.shrink_phase = true;

    for (const std::string& k : tx.read_locks) {
        lock_table_[k].readers.erase(tx.tx_id);
    }

    for (const std::string& k : tx.write_locks) {
        if (lock_table_[k].writer == tx.tx_id) {
            lock_table_[k].writer = -1;
        }
    }

    tx.read_locks.clear();
    tx.write_locks.clear();
    remove_waiter(tx.tx_id);
}

void LockManager::rollback(TransactionHandle& tx) {
    tx.pending_writes.clear();
    tx.state = TransactionState::Failed;
    release_all_locks(tx);
}

void LockManager::print_wait_graph() const {
    std::cout << "\n=== Dependencies (Wait-For Graph) ===\n";
    if (dependencies_.empty()) {
        std::cout << "No active waits.\n";
        return;
    }

    for (const auto& dep : dependencies_) {
        std::cout << "T" << dep.first << " waits on -> ";
        for (int b : dep.second) std::cout << "T" << b << ' ';
        std::cout << '\n';
    }
}

std::set<int> LockManager::get_conflicts(int t_id, const LockEntry& entry, LockMode mode) const {
    std::set<int> conflicting_txs;

    if (entry.writer != -1 && entry.writer != t_id) {
        conflicting_txs.insert(entry.writer);
    }

    if (mode == LockMode::Write) {
        for (int reader_id : entry.readers) {
            if (reader_id != t_id) conflicting_txs.insert(reader_id);
        }
    }

    return conflicting_txs;
}

void LockManager::assign_lock(TransactionHandle& tx, const std::string& key, LockEntry& entry, LockMode mode) {
    if (mode == LockMode::Read) {
        entry.readers.insert(tx.tx_id);
        tx.read_locks.insert(key);
    } else {
        entry.readers.erase(tx.tx_id);
        tx.read_locks.erase(key);
        entry.writer = tx.tx_id;
        tx.write_locks.insert(key);
    }
}

bool LockManager::check_for_cycles() const {
    std::set<int> visited, recursion_stack;

    for (const auto& node : dependencies_) {
        if (detect_cycle_dfs(node.first, visited, recursion_stack)) {
            return true;
        }
    }
    return false;
}

bool LockManager::detect_cycle_dfs(int current, std::set<int>& visited, std::set<int>& recursion_stack) const {
    if (recursion_stack.count(current)) return true;
    if (visited.count(current)) return false;

    visited.insert(current);
    recursion_stack.insert(current);

    auto iter = dependencies_.find(current);
    if (iter != dependencies_.end()) {
        for (int neighbor : iter->second) {
            if (detect_cycle_dfs(neighbor, visited, recursion_stack)) {
                return true;
            }
        }
    }

    recursion_stack.erase(current);
    return false;
}

void LockManager::remove_waiter(int t_id) {
    dependencies_.erase(t_id);

    for (auto& pair : dependencies_) {
        pair.second.erase(t_id);
    }

    for (auto iter = dependencies_.begin(); iter != dependencies_.end();) {
        if (iter->second.empty()) {
            iter = dependencies_.erase(iter);
        } else {
            ++iter;
        }
    }
}

// ---- DatabaseSystem Implementation ------------------------------------------

DatabaseSystem::DatabaseSystem() {
    mvcc_.insert_initial("A", 100);
    mvcc_.insert_initial("B", 200);
}

TransactionHandle DatabaseSystem::begin_transaction() {
    TransactionHandle tx;
    tx.tx_id = id_generator_++;
    tx.snapshot_ts = mvcc_.get_clock();
    std::cout << "\nStarted Transaction T" << tx.tx_id << " (Snapshot TS: " << tx.snapshot_ts << ")\n";
    return tx;
}

void DatabaseSystem::perform_read(TransactionHandle& tx, const std::string& key) {
    if (tx.state == TransactionState::Failed) return;

    if (lock_mgr_.acquire_lock(tx, key, LockMode::Read)) {
        int val = mvcc_.fetch_version(key, tx);
        std::cout << "T" << tx.tx_id << " read value " << key << " = " << val << '\n';
    }
}

void DatabaseSystem::perform_write(TransactionHandle& tx, const std::string& key, int val) {
    if (tx.state == TransactionState::Failed) return;

    if (lock_mgr_.acquire_lock(tx, key, LockMode::Write)) {
        tx.pending_writes.push_back({key, val});
        std::cout << "T" << tx.tx_id << " buffered write " << key << " = " << val << '\n';
    }
}

void DatabaseSystem::perform_commit(TransactionHandle& tx) {
    if (tx.state == TransactionState::Failed) {
        std::cout << "T" << tx.tx_id << " is in aborted state, skipping commit.\n";
        return;
    }

    mvcc_.commit_writes(tx);
    lock_mgr_.release_all_locks(tx);
    std::cout << "T" << tx.tx_id << " has been committed successfully.\n";
}

void DatabaseSystem::display_status() const {
    lock_mgr_.print_wait_graph();
    mvcc_.print_versions();
}
