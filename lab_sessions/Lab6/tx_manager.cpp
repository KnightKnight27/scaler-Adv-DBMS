#include "tx_manager.h"
#include <iostream>

TxID TransactionManager::begin() {
    return registry_.begin();
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey& key) {
    locks_.acquire(key, xid, LockMode::SHARED);
    return heap_.read(key, xid);
}

void TransactionManager::insert(TxID xid, const RowKey& key, const std::string& value) {
    locks_.acquire(key, xid, LockMode::EXCLUSIVE);
    heap_.insert(key, value, xid);
}

void TransactionManager::update(TxID xid, const RowKey& key, const std::string& value) {
    locks_.acquire(key, xid, LockMode::EXCLUSIVE);
    heap_.update(key, value, xid);
}

void TransactionManager::remove(TxID xid, const RowKey& key) {
    locks_.acquire(key, xid, LockMode::EXCLUSIVE);
    heap_.remove(key, xid);
}

void TransactionManager::commit(TxID xid) {
    registry_.set_status(xid, TxStatus::COMMITTED);
    locks_.release(xid);
    std::cout << "[TX " << xid << "] COMMITTED\n";
}

void TransactionManager::abort(TxID xid) {
    heap_.rollback(xid);
    registry_.set_status(xid, TxStatus::ABORTED);
    locks_.release(xid);
    std::cout << "[TX " << xid << "] ABORTED\n";
}
