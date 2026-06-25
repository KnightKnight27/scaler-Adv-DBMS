#include "transaction_manager.h"
#include <iostream>

TransactionID TransactionManager::beginTransaction() {
    TransactionID tx_id = next_tx_id++;
    global_timestamp++;

    auto tx = std::make_shared<Transaction>(tx_id, global_timestamp);
    transactions[tx_id] = tx;

    std::cout << "TX" << tx_id << " started (timestamp=" << global_timestamp << ")\n";
    return tx_id;
}

bool TransactionManager::read(TransactionID tx_id, const std::string &key, std::string &value) {
    auto it = transactions.find(tx_id);
    if (it == transactions.end() || it->second->state != TransactionState::ACTIVE) {
        std::cerr << "TX" << tx_id << " is not active\n";
        return false;
    }

    auto &tx = it->second;
    tx->read_set.insert(key);

    // Acquire shared lock
    if (!lock_manager.acquireLock(key, tx_id, LockType::SHARED)) {
        std::cout << "TX" << tx_id << " waiting for shared lock on " << key << "\n";

        // Deadlock detection: simplified approach
        deadlock_detector.addWaitForEdge(tx_id, 1); // Placeholder edge

        if (deadlock_detector.detectCycle()) {
            std::cout << "Deadlock detected for TX" << tx_id << "! Aborting...\n";
            abort(tx_id);
            return false;
        }
    }

    // Get version visible to this transaction
    auto &version_chain = data[key];
    auto version = version_chain.getVersionAt(tx->timestamp);

    if (version) {
        auto data_it = version->data.find(key);
        if (data_it != version->data.end()) {
            value = data_it->second;
        }
    }

    std::cout << "TX" << tx_id << " read " << key << "=" << value << "\n";
    return true;
}

bool TransactionManager::write(TransactionID tx_id, const std::string &key, const std::string &value) {
    auto it = transactions.find(tx_id);
    if (it == transactions.end() || it->second->state != TransactionState::ACTIVE) {
        std::cerr << "TX" << tx_id << " is not active\n";
        return false;
    }

    auto &tx = it->second;
    tx->write_set.insert(key);

    // Acquire exclusive lock
    if (!lock_manager.acquireLock(key, tx_id, LockType::EXCLUSIVE)) {
        std::cout << "TX" << tx_id << " waiting for exclusive lock on " << key << "\n";

        deadlock_detector.addWaitForEdge(tx_id, 1); // Placeholder edge

        if (deadlock_detector.detectCycle()) {
            std::cout << "Deadlock detected for TX" << tx_id << "! Aborting...\n";
            abort(tx_id);
            return false;
        }
    }

    // Create new version
    auto &version_chain = data[key];
    auto version = version_chain.createVersion(tx_id, tx->timestamp);
    version->data[key] = value;

    std::cout << "TX" << tx_id << " write " << key << "=" << value << "\n";
    return true;
}

bool TransactionManager::commit(TransactionID tx_id) {
    auto it = transactions.find(tx_id);
    if (it == transactions.end()) return false;

    auto &tx = it->second;
    if (tx->state != TransactionState::ACTIVE) return false;

    tx->state = TransactionState::COMMITTED;

    // Release all locks held by this transaction
    lock_manager.releaseAllLocks(tx_id);
    deadlock_detector.removeTransaction(tx_id);

    std::cout << "TX" << tx_id << " committed\n";
    return true;
}

bool TransactionManager::abort(TransactionID tx_id) {
    auto it = transactions.find(tx_id);
    if (it == transactions.end()) return false;

    auto &tx = it->second;
    tx->state = TransactionState::ABORTED;

    // Roll back versions created by this transaction
    for (auto &pair : data) {
        auto &version_chain = pair.second;
        const auto &versions = version_chain.getAllVersions();

        for (int i = versions.size() - 1; i >= 0; --i) {
            if (versions[i]->created_by == tx_id) {
                version_chain.removeLastVersion();
            }
        }
    }

    // Release all locks
    lock_manager.releaseAllLocks(tx_id);
    deadlock_detector.removeTransaction(tx_id);

    std::cout << "TX" << tx_id << " aborted\n";
    return true;
}

void TransactionManager::printVersionChains() const {
    std::cout << "\n=== Version Chains ===\n";
    for (const auto &pair : data) {
        std::cout << "Key: " << pair.first << " | Versions: ";
        for (const auto &version : pair.second.getAllVersions()) {
            std::cout << "V" << version->version_id << "(TX" << version->created_by << ") ";
        }
        std::cout << "\n";
    }
}

void TransactionManager::printTransactionState() const {
    std::cout << "\n=== Transaction State ===\n";
    for (const auto &pair : transactions) {
        std::cout << "TX" << pair.first << " state=";
        if (pair.second->state == TransactionState::ACTIVE) {
            std::cout << "ACTIVE";
        } else if (pair.second->state == TransactionState::COMMITTED) {
            std::cout << "COMMITTED";
        } else {
            std::cout << "ABORTED";
        }
        std::cout << "\n";
    }
}
