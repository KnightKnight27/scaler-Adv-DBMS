#include "txn/txn_manager.h"
#include <iostream>

TxnManager::TxnManager(LockManager* lock_mgr) : lock_mgr_(lock_mgr) {}

int TxnManager::Begin() {
    std::lock_guard<std::mutex> lock(mu_);
    int id = next_txn_id_++;
    Transaction txn;
    txn.txn_id = id;
    txn.state = TxnState::ACTIVE;
    transactions_[id] = txn;
    std::cout << "[Txn] BEGIN Txn" << id << "\n";
    return id;
}

void TxnManager::Commit(int txn_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return;
    it->second.state = TxnState::COMMITTED;
    std::cout << "[Txn] COMMIT Txn" << txn_id << "\n";
    // Strict 2PL: release all locks at commit
    lock_mgr_->UnlockAll(txn_id);
}

void TxnManager::Abort(int txn_id, std::function<void(const WriteRecord&)> undo_fn) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return;

    std::cout << "[Txn] ABORT Txn" << txn_id << "\n";

    // Undo writes in reverse order
    if (undo_fn) {
        auto& writes = it->second.write_set;
        for (int i = writes.size() - 1; i >= 0; i--) {
            std::cout << "[Txn] Undoing write by Txn" << txn_id << " on ("
                      << writes[i].rid.page_id << "," << writes[i].rid.slot_id << ")\n";
            undo_fn(writes[i]);
        }
    }

    it->second.state = TxnState::ABORTED;
    lock_mgr_->UnlockAll(txn_id);
}

void TxnManager::AddWriteRecord(int txn_id, const WriteRecord& record) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = transactions_.find(txn_id);
    if (it != transactions_.end()) {
        it->second.write_set.push_back(record);
    }
}

TxnState TxnManager::GetState(int txn_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return TxnState::ABORTED;
    return it->second.state;
}

Transaction* TxnManager::GetTxn(int txn_id) {
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return nullptr;
    return &it->second;
}
