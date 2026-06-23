#include "recovery/recovery_manager.h"
#include <iostream>
#include <unordered_set>
#include <vector>

namespace minidb {

RecoveryManager::RecoveryManager(LogManager* log_manager, BPlusTree* tree)
    : log_manager_(log_manager), tree_(tree) {}

void RecoveryManager::Recover() {
    std::cout << "[Diagnostic] Recovery replay started\n";
    log_manager_->ResetIterator();
    
    std::unordered_set<int> active_txns;
    std::vector<LogRecord> log_history;
    
    LogRecord record;
    while (log_manager_->GetNextLogRecord(&record)) {
        log_history.push_back(record);
        
        if (record.type_ == LogRecordType::BEGIN) {
            active_txns.insert(record.txn_id_);
        } else if (record.type_ == LogRecordType::COMMIT || record.type_ == LogRecordType::ABORT) {
            active_txns.erase(record.txn_id_);
        }
    }
    
    // Now any transaction still in active_txns is a loser txn.
    // We need to undo their operations by scanning backwards.
    if (active_txns.empty()) {
        return; // Nothing to undo
    }
    
    for (auto it = log_history.rbegin(); it != log_history.rend(); ++it) {
        if (active_txns.find(it->txn_id_) != active_txns.end()) {
            if (it->type_ == LogRecordType::INSERT) {
                // To undo an insert, we delete the tuple
                tree_->Remove(it->tuple_.id);
                // Also append a logical abort record to WAL
                log_manager_->AppendLogRecord(LogRecord(it->txn_id_, LogRecordType::ABORT));
            } else if (it->type_ == LogRecordType::DELETE) {
                // To undo a delete, we re-insert the tuple
                // (Note: in our minimal setup, tree->Insert requires RecordId, 
                // but since it's logical logging, we would need to allocate a new slot or ignore for capstone simplicity)
            }
        }
    }
}

} // namespace minidb
