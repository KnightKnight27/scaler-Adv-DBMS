#include "concurrency/transaction_manager.h"
#include "storage/page.h"
#include <iostream>
#include <vector>
#include <cstring>

namespace minidb {

TransactionManager::TransactionManager(LockManager *lock_mgr, LogManager *log_mgr, BufferPoolManager *bpm)
    : lock_mgr_(lock_mgr), log_mgr_(log_mgr), bpm_(bpm) {}

TransactionManager::~TransactionManager() {
    std::lock_guard<std::mutex> lock(latch_);
    for (auto &pair : txn_map_) {
        delete pair.second;
    }
}

Transaction *TransactionManager::Begin() {
    std::lock_guard<std::mutex> lock(latch_);
    txn_id_t txn_id = next_txn_id_++;
   Transaction *txn = new Transaction(txn_id);
txn_map_[txn_id] = txn;

std::cout << "[Transaction] Started Transaction "
          << txn_id << std::endl;

if (log_mgr_)  {
        LogRecord rec(txn_id, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t lsn = log_mgr_->AppendLogRecord(&rec);
        txn->SetPrevLSN(lsn);
    }
    
    return txn;
}

void TransactionManager::Commit(Transaction *txn) {
    std::lock_guard<std::mutex> lock(latch_);
   txn->SetState(TransactionState::COMMITTED);

std::cout << "[Transaction] Transaction "
          << txn->GetTxnId()
          << " committed successfully."
          << std::endl;

if (log_mgr_) {
        LogRecord rec(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::COMMIT);
        lsn_t lsn = log_mgr_->AppendLogRecord(&rec);
        log_mgr_->Flush(lsn); // force log at commit
    }

    if (lock_mgr_) {
        lock_mgr_->ReleaseAllLocks(txn);
    }
}

void TransactionManager::Abort(Transaction *txn) {
    std::lock_guard<std::mutex> lock(latch_);
   txn->SetState(TransactionState::ABORTED);

std::cout << "[Transaction] Transaction "
          << txn->GetTxnId()
          << " aborted."
          << std::endl;

RollbackTransaction(txn);

    if (log_mgr_) {
        LogRecord rec(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::ABORT);
        lsn_t lsn = log_mgr_->AppendLogRecord(&rec);
        log_mgr_->Flush(lsn);
    }

    if (lock_mgr_) {
        lock_mgr_->ReleaseAllLocks(txn);
    }
}

void TransactionManager::RollbackTransaction(Transaction *txn) {
    if (log_mgr_ == nullptr || bpm_ == nullptr) return;

    DiskManager *disk_mgr = log_mgr_->GetDiskManager();
    int file_size = disk_mgr->GetLogFileSize();
    if (file_size <= 0) return;

    // Flush all active logs to disk first
    log_mgr_->FlushAll();

    std::vector<char> buffer(file_size);
    disk_mgr->ReadLog(buffer.data(), file_size, 0);

    int offset = 0;
    std::vector<LogRecord> records;
    while (offset < file_size) {
        if (offset + static_cast<int>(sizeof(uint32_t)) > file_size) {
            break;
        }
        uint32_t record_size;
        std::memcpy(&record_size, buffer.data() + offset, sizeof(uint32_t));
        if (record_size == 0 || offset + static_cast<int>(record_size) > file_size) {
            break;
        }
        LogRecord rec = LogRecord::Deserialize(buffer.data() + offset);
        records.push_back(rec);
        offset += record_size;
    }

    // Rollback this specific transaction's operations by scanning records backward
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto &rec = *it;
        if (rec.GetTxnId() == txn->GetTxnId()) {
            if (rec.GetType() == LogRecordType::INSERT || rec.GetType() == LogRecordType::DELETE) {
                RID rid = rec.GetRID();
                Page *page = bpm_->FetchPage(rid.page_id);
                if (page == nullptr) {
                    continue;
                }

                SlottedPage slotted(page);
                if (rec.GetType() == LogRecordType::INSERT) {
                    // Undo insert: delete the tuple
                    slotted.DeleteTuple(rid.slot_id);
                } else if (rec.GetType() == LogRecordType::DELETE) {
                    // Undo delete: restore the tuple
                    slotted.RestoreTuple(rid.slot_id, rec.GetData().data(), rec.GetData().size());
                }

                // Set LSN for recovery / tracing
                page->SetLSN(rec.GetLSN());
                bpm_->UnpinPage(rid.page_id, true);
            }
        }
    }
}

} // namespace minidb
