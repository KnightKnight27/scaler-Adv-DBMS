#include "transaction/TransactionManager.hpp"

using namespace std;

namespace minidb {

int TransactionManager::beginTransaction() {
    int id = next_txn_id_++;
    states_[id] = TxnState::ACTIVE;
    return id;
}

bool TransactionManager::commit(int txn_id) {
    if (states_[txn_id] != TxnState::ACTIVE) return false;
    lock_manager_.releaseAllLocks(txn_id);
    states_[txn_id] = TxnState::COMMITTED;
    undo_logs_.erase(txn_id);
    return true;
}

bool TransactionManager::rollback(int txn_id) {
    if (states_[txn_id] != TxnState::ACTIVE) return false;
    lock_manager_.releaseAllLocks(txn_id);
    states_[txn_id] = TxnState::ABORTED;
    return true;
}

bool TransactionManager::isActive(int txn_id) const {
    auto it = states_.find(txn_id);
    return it != states_.end() && it->second == TxnState::ACTIVE;
}

TxnState TransactionManager::getState(int txn_id) const {
    auto it = states_.find(txn_id);
    return it == states_.end() ? TxnState::INACTIVE : it->second;
}

void TransactionManager::recordInsert(int txn_id, const string& table, const Row& row, const RowLocation& loc) {
    undo_logs_[txn_id].push_back(UndoRecord{table, row, loc, true});
}

const vector<TransactionManager::UndoRecord>& TransactionManager::getUndoLog(int txn_id) const {
    static const vector<UndoRecord> empty;
    auto it = undo_logs_.find(txn_id);
    return it == undo_logs_.end() ? empty : it->second;
}

}  // namespace minidb
