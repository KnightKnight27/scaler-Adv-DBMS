#include "tx_manager.h"
#include "mvcc_heap.h"
#include "lock_manager.h"
#include <iostream>

TxID TransactionManager::begin() {
    return MvccHeap::getInstance().beginTransaction();
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey& key) {
    LockManager::getInstance().acquireLock(key, xid, LockMode::SHARED);
    return MvccHeap::getInstance().readKey(key, xid);
}

void TransactionManager::insert(TxID xid, const RowKey& key, const std::string& value) {
    LockManager::getInstance().acquireLock(key, xid, LockMode::EXCLUSIVE);
    MvccHeap::getInstance().insertKey(key, value, xid);
}

void TransactionManager::update(TxID xid, const RowKey& key, const std::string& value) {
    LockManager::getInstance().acquireLock(key, xid, LockMode::EXCLUSIVE);
    MvccHeap::getInstance().updateKey(key, value, xid);
}

void TransactionManager::remove(TxID xid, const RowKey& key) {
    LockManager::getInstance().acquireLock(key, xid, LockMode::EXCLUSIVE);
    MvccHeap::getInstance().deleteKey(key, xid);
}

void TransactionManager::commit(TxID xid) {
    MvccHeap::getInstance().commitTransaction(xid);
    LockManager::getInstance().releaseLocks(xid);
    std::cout << "[TX " << xid << "] COMMITTED\n";
}

void TransactionManager::abort(TxID xid) {
    MvccHeap::getInstance().abortTransaction(xid);
    LockManager::getInstance().releaseLocks(xid);
    std::cout << "[TX " << xid << "] ABORTED\n";
}
