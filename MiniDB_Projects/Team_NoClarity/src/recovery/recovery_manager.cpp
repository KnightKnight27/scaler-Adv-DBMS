#include "recovery/recovery_manager.h"
#include <queue>
#include <cstring>
#include <iostream>

namespace minidb {

void RecoveryManager::RunRecovery() {
    std::vector<txn_id_t> active_txns;
    ExecuteAnalysisPhase(active_txns, dpt_);
    ExecuteRedoPhase(dpt_);
    ExecuteUndoPhase(active_txns);
}

void RecoveryManager::ExecuteAnalysisPhase(std::vector<txn_id_t>& active_txns, std::unordered_map<page_id_t, lsn_t>& dpt) {
    active_txns_table_.clear();
    dpt.clear();

    std::ifstream is(log_manager_->GetLogFileName(), std::ios::binary);
    if (!is.is_open()) {
        return;
    }

    LogRecord record;
    while (record.Deserialize(is)) {
        txn_id_t tid = record.txn_id;
        
        if (record.type == LogRecordType::BEGIN) {
            TransactionTableEntry entry;
            entry.txn_id = tid;
            entry.last_lsn = record.lsn;
            entry.status = TransactionTableEntry::TxnStatus::RUNNING;
            active_txns_table_[tid] = entry;
        } else if (record.type == LogRecordType::COMMIT) {
            active_txns_table_[tid].last_lsn = record.lsn;
            active_txns_table_[tid].status = TransactionTableEntry::TxnStatus::COMMITTED;
        } else if (record.type == LogRecordType::ABORT) {
            active_txns_table_[tid].last_lsn = record.lsn;
            active_txns_table_[tid].status = TransactionTableEntry::TxnStatus::ABORTED;
        } else if (record.type == LogRecordType::UPDATE) {
            if (active_txns_table_.find(tid) == active_txns_table_.end()) {
                TransactionTableEntry entry;
                entry.txn_id = tid;
                entry.status = TransactionTableEntry::TxnStatus::RUNNING;
                active_txns_table_[tid] = entry;
            }
            active_txns_table_[tid].last_lsn = record.lsn;
            
            page_id_t pid = record.page_id;
            if (dpt.find(pid) == dpt.end()) {
                dpt[pid] = record.lsn;
            }
        } else if (record.type == LogRecordType::CLR) {
            if (active_txns_table_.find(tid) == active_txns_table_.end()) {
                TransactionTableEntry entry;
                entry.txn_id = tid;
                entry.status = TransactionTableEntry::TxnStatus::RUNNING;
                active_txns_table_[tid] = entry;
            }
            active_txns_table_[tid].last_lsn = record.lsn;
            
            page_id_t pid = record.page_id;
            if (dpt.find(pid) == dpt.end()) {
                dpt[pid] = record.lsn;
            }
        }
    }

    // Identify active (loser) transactions (RUNNING or ABORTED)
    active_txns.clear();
    for (const auto& pair : active_txns_table_) {
        if (pair.second.status == TransactionTableEntry::TxnStatus::RUNNING ||
            pair.second.status == TransactionTableEntry::TxnStatus::ABORTED) {
            active_txns.push_back(pair.first);
        }
    }
}

void RecoveryManager::ExecuteRedoPhase(const std::unordered_map<page_id_t, lsn_t>& dpt) {
    if (dpt.empty()) {
        return;
    }

    // Find min rec_lsn in DPT
    lsn_t min_rec_lsn = -1;
    for (const auto& pair : dpt) {
        if (min_rec_lsn == -1 || pair.second < min_rec_lsn) {
            min_rec_lsn = pair.second;
        }
    }

    std::ifstream is(log_manager_->GetLogFileName(), std::ios::binary);
    if (!is.is_open()) {
        return;
    }

    LogRecord record;
    while (record.Deserialize(is)) {
        if (record.lsn < min_rec_lsn) {
            continue;
        }

        if (record.type == LogRecordType::UPDATE || record.type == LogRecordType::CLR) {
            page_id_t pid = record.page_id;
            auto it = dpt.find(pid);
            if (it != dpt.end() && record.lsn >= it->second) {
                Page* page = bpm_->FetchPage(pid);
                if (page != nullptr) {
                    if (page->GetPageLSN() < record.lsn) {
                        // Redo! Apply after_image to page at offset
                        char* data = page->GetData();
                        std::memcpy(data + record.offset, record.after_image.data(), record.after_image.length());
                        page->SetPageLSN(record.lsn);
                        page->SetDirty(true);
                    }
                    bpm_->UnpinPage(pid, true);
                }
            }
        }
    }
}

void RecoveryManager::ExecuteUndoPhase(std::vector<txn_id_t>& active_txns) {
    if (active_txns.empty()) {
        return;
    }

    // Load all log records into memory for easy LSN lookup
    std::unordered_map<lsn_t, LogRecord> log_records;
    std::ifstream is(log_manager_->GetLogFileName(), std::ios::binary);
    if (is.is_open()) {
        LogRecord rec;
        while (rec.Deserialize(is)) {
            log_records[rec.lsn] = rec;
        }
    }

    // We use a priority queue (max-heap) to select the largest LSN to undo.
    std::priority_queue<lsn_t> undo_heap;
    
    // Map tracking the last LSN for each loser transaction
    std::unordered_map<txn_id_t, lsn_t> txn_last_lsn;

    for (txn_id_t tid : active_txns) {
        auto it = active_txns_table_.find(tid);
        if (it != active_txns_table_.end() && it->second.last_lsn > 0) {
            undo_heap.push(it->second.last_lsn);
            txn_last_lsn[tid] = it->second.last_lsn;
        }
    }

    while (!undo_heap.empty()) {
        lsn_t curr_lsn = undo_heap.top();
        undo_heap.pop();

        auto it = log_records.find(curr_lsn);
        if (it == log_records.end()) {
            continue;
        }

        const LogRecord& record = it->second;
        txn_id_t tid = record.txn_id;

        if (record.type == LogRecordType::UPDATE) {
            // Undo this update!
            Page* page = bpm_->FetchPage(record.page_id);
            if (page != nullptr) {
                char* data = page->GetData();
                // Apply before_image to revert the change
                std::memcpy(data + record.offset, record.before_image.data(), record.before_image.length());
                page->SetDirty(true);

                // Write a Compensation Log Record (CLR)
                lsn_t prev_txn_last_lsn = txn_last_lsn[tid];
                LogRecord clr_rec(tid, prev_txn_last_lsn, LogRecordType::CLR, record.page_id, record.offset,
                                  "", record.before_image, record.prev_lsn);
                
                lsn_t clr_lsn = log_manager_->AppendRecord(clr_rec);
                page->SetPageLSN(clr_lsn);
                txn_last_lsn[tid] = clr_lsn;

                bpm_->UnpinPage(record.page_id, true);
            }

            if (record.prev_lsn > 0) {
                undo_heap.push(record.prev_lsn);
            }
        } else if (record.type == LogRecordType::CLR) {
            // A CLR is never undone. We simply follow its undo_next_lsn
            if (record.undo_next_lsn > 0) {
                undo_heap.push(record.undo_next_lsn);
            }
        } else if (record.type == LogRecordType::BEGIN) {
            // This transaction is completely rolled back. Write ABORT log record.
            lsn_t prev_txn_last_lsn = txn_last_lsn[tid];
            LogRecord abort_rec(tid, prev_txn_last_lsn, LogRecordType::ABORT);
            log_manager_->AppendRecord(abort_rec);
        } else if (record.type == LogRecordType::ABORT) {
            // Follow prev_lsn
            if (record.prev_lsn > 0) {
                undo_heap.push(record.prev_lsn);
            }
        }
    }
}

} // namespace minidb
