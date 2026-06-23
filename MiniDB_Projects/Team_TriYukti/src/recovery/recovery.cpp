#include "recovery/recovery.h"
#include "mvcc/mvcc_manager.h"
#include <iostream>

namespace minidb {

RecoveryManager::RecoveryManager(LogManager *log_manager, BufferPool *buffer_pool, MVCCManager *mvcc_manager)
    : log_manager_(log_manager), buffer_pool_(buffer_pool), mvcc_manager_(mvcc_manager) {}

void RecoveryManager::Recover() {
    std::vector<LogRecord> logs = log_manager_->ReadLogs();
    if (logs.empty()) return;
    
    Analysis(logs);
    Redo(logs);
    Undo(logs);
}

void RecoveryManager::Analysis(const std::vector<LogRecord> &logs) {
    for (const auto &log : logs) {
        if (log.type == LogRecordType::BEGIN) {
            active_txns_[log.txn_id] = log.lsn;
        } else if (log.type == LogRecordType::COMMIT) {
            active_txns_.erase(log.txn_id);
            if (mvcc_manager_) {
                mvcc_manager_->RecordCommit(log.txn_id, log.commit_ts);
            }
        } else if (log.type == LogRecordType::ABORT) {
            active_txns_.erase(log.txn_id);
        } else if (log.type == LogRecordType::UPDATE) {
            active_txns_[log.txn_id] = log.lsn;
            if (dirty_pages_.find(log.rid.page_id) == dirty_pages_.end()) {
                dirty_pages_[log.rid.page_id] = log.lsn;
            }
        } else if (log.type == LogRecordType::CHECKPOINT) {
            // Checkpoint processing
        }
    }
}

void RecoveryManager::Redo(const std::vector<LogRecord> &logs) {
    int32_t min_rec_lsn = -1;
    for (const auto& pair : dirty_pages_) {
        if (min_rec_lsn == -1 || pair.second < min_rec_lsn) {
            min_rec_lsn = pair.second;
        }
    }
    if (min_rec_lsn == -1) min_rec_lsn = 0;
    
    for (const auto &log : logs) {
        if (log.lsn < min_rec_lsn) continue;
        
        if (log.type == LogRecordType::UPDATE) {
            if (dirty_pages_.find(log.rid.page_id) == dirty_pages_.end() || dirty_pages_[log.rid.page_id] > log.lsn) {
                continue;
            }
            Page *page = buffer_pool_->FetchPage(log.rid.page_id);
            if (page) {
                if (page->GetLSN() < log.lsn) {
                    if (!log.after_image.data_.empty()) {
                        if (log.before_image.data_.empty()) {
                            // It was an INSERT
                            RecordId rid;
                            page->InsertTuple(log.after_image, &rid); 
                        } else {
                            // It was an UPDATE (logical delete)
                            page->UpdateTuple(log.rid.slot_id, log.after_image);
                        }
                    }
                    page->SetLSN(log.lsn);
                    buffer_pool_->UnpinPage(log.rid.page_id, true);
                } else {
                    buffer_pool_->UnpinPage(log.rid.page_id, false);
                }
            }
        }
    }
}

void RecoveryManager::Undo(const std::vector<LogRecord> &logs) {
    for (auto it = logs.rbegin(); it != logs.rend(); ++it) {
        const auto &log = *it;
        if (active_txns_.find(log.txn_id) != active_txns_.end()) {
            if (log.type == LogRecordType::UPDATE) {
                Page *page = buffer_pool_->FetchPage(log.rid.page_id);
                if (page) {
                    if (page->GetLSN() >= log.lsn) {
                        if (log.before_image.data_.empty()) {
                            // It was an INSERT, so delete it
                            page->DeleteTuple(log.rid.slot_id);
                        } else {
                            // It was an UPDATE, restore before image
                            page->UpdateTuple(log.rid.slot_id, log.before_image);
                        }
                        page->SetLSN(log.lsn); // In real ARIES we append CLR
                        buffer_pool_->UnpinPage(log.rid.page_id, true);
                    } else {
                        buffer_pool_->UnpinPage(log.rid.page_id, false);
                    }
                }
            } else if (log.type == LogRecordType::BEGIN) {
                active_txns_.erase(log.txn_id);
            }
        }
    }
}

} // namespace minidb
