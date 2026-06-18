#include "tx_manager.h"
#include "mvcc_heap.h"
#include "lock_manager.h"
#include <iostream>

TxID TransactionManager::begin() { 
    return begin_transaction(); 
}

std::optional<std::string> TransactionManager::read(TxID xid, const RowKey& key) {
    acquire_lock(key, xid, LockMode::SHARED);
    return mvcc_read_key(key, xid);
}

void TransactionManager::insert(TxID xid, const RowKey& key, const std::string& value) {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    mvcc_insert(key, value, xid);
}

void TransactionManager::update(TxID xid, const RowKey& key, const std::string& value) {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    mvcc_update(key, value, xid);
}

void TransactionManager::remove(TxID xid, const RowKey& key) {
    acquire_lock(key, xid, LockMode::EXCLUSIVE);
    mvcc_delete(key, xid);
}

void TransactionManager::commit(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        g_transactions.at(xid).status = TxStatus::COMMITTED;
    }
    release_locks(xid);
    std::cout << "[TX " << xid << "] COMMITTED\n";
}

void TransactionManager::abort(TxID xid) {
    {
        std::lock_guard<std::mutex> lk(g_heap_mutex);
        for (auto& [key, chain] : g_heap) {
            for (auto& v : chain) {
                if (v.xmin == xid) v.xmax = xid;  
                if (v.xmax == xid) v.xmax = 0;    
            }
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_tx_mutex);
        g_transactions.at(xid).status = TxStatus::ABORTED;
    }
    release_locks(xid);
    std::cout << "[TX " << xid << "] ABORTED\n";
}