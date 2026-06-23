#include "transaction/tx_manager.h"
#include <iostream>

TxManager::TxManager(LockManager& lm) : lm_(lm) {}

TxID TxManager::begin() {
    const TxID xid = next_xid_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        tx_table_[xid] = TxStatus::ACTIVE;
    }
    std::cout << "[TxManager] TX " << xid << " BEGUN\n";
    return xid;
}

void TxManager::lockRead(TxID xid, const std::string& resource_key) {
    lm_.acquireLock(xid, resource_key, LockMode::SHARED);
}

void TxManager::lockWrite(TxID xid, const std::string& resource_key) {
    lm_.acquireLock(xid, resource_key, LockMode::EXCLUSIVE);
}

void TxManager::commit(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        tx_table_[xid] = TxStatus::COMMITTED;
    }
    lm_.releaseAll(xid);
    std::cout << "[TxManager] TX " << xid << " COMMITTED\n";
}

void TxManager::abort(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(tx_mutex_);
        tx_table_[xid] = TxStatus::ABORTED;
    }
    lm_.releaseAll(xid);
    std::cout << "[TxManager] TX " << xid << " ABORTED\n";
}

bool TxManager::isActive(TxID xid) const {
    std::lock_guard<std::mutex> lk(tx_mutex_);
    const auto it = tx_table_.find(xid);
    return it != tx_table_.end() && it->second == TxStatus::ACTIVE;
}

TxStatus TxManager::status(TxID xid) const {
    std::lock_guard<std::mutex> lk(tx_mutex_);
    const auto it = tx_table_.find(xid);
    if (it == tx_table_.end()) {
        return TxStatus::ABORTED;
    }
    return it->second;
}
