#include "recovery/recovery_manager.h"
#include "storage/page.h"
#include <iostream>
#include <vector>
#include <unordered_set>

namespace minidb {

RecoveryManager::RecoveryManager(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager)
    : disk_manager_(disk_manager), buffer_pool_manager_(buffer_pool_manager) {}

void RecoveryManager::RunRecovery() {
    int file_size = disk_manager_->GetLogFileSize();
    if (file_size <= 0) {
        std::cout << "[Recovery] Log file empty. No recovery needed." << std::endl;
        return;
    }

    std::vector<char> buffer(file_size);
    disk_manager_->ReadLog(buffer.data(), file_size, 0);

    int offset = 0;
    std::vector<LogRecord> records;
    while (offset < file_size) {
        if (offset + static_cast<int>(sizeof(uint32_t)) > file_size) {
            break;
        }
        uint32_t record_size;
        std::memcpy(&record_size, buffer.data() + offset, sizeof(uint32_t));
        if (record_size == 0 || offset + static_cast<int>(record_size) > file_size) {
            break; // EOF or corruption
        }
        LogRecord rec = LogRecord::Deserialize(buffer.data() + offset);
        records.push_back(rec);
        offset += record_size;
    }

    std::cout << "[Recovery] Parsed " << records.size() << " log records from WAL." << std::endl;

    // --- PHASE 1: ANALYSIS ---
    std::cout << "[Recovery] Phase 1: Analysis..." << std::endl;
    std::unordered_set<txn_id_t> active_txns;
    for (const auto &rec : records) {
        if (rec.GetType() == LogRecordType::BEGIN) {
            active_txns.insert(rec.GetTxnId());
        } else if (rec.GetType() == LogRecordType::COMMIT || rec.GetType() == LogRecordType::ABORT) {
            active_txns.erase(rec.GetTxnId());
        }
    }

    std::cout << "[Recovery] Active (loser) transactions to roll back: ";
    if (active_txns.empty()) {
        std::cout << "None";
    } else {
        for (txn_id_t id : active_txns) {
            std::cout << id << " ";
        }
    }
    std::cout << std::endl;

    // --- PHASE 2: REDO ---
    std::cout << "[Recovery] Phase 2: Redo..." << std::endl;
    int redo_count = 0;
    for (const auto &rec : records) {
        if (rec.GetType() == LogRecordType::INSERT || rec.GetType() == LogRecordType::DELETE) {
            RID rid = rec.GetRID();
            Page *page = buffer_pool_manager_->FetchPage(rid.page_id);
            if (page == nullptr) {
                std::cerr << "[Recovery] Error: Failed to fetch page " << rid.page_id << " during redo." << std::endl;
                continue;
            }

            // Compare page LSN with record LSN
            if (page->GetLSN() < rec.GetLSN() || page->GetLSN() == INVALID_LSN) {
                SlottedPage slotted(page);
                if (rec.GetType() == LogRecordType::INSERT) {
                    slotted.RestoreTuple(rid.slot_id, rec.GetData().data(), rec.GetData().size());
                } else if (rec.GetType() == LogRecordType::DELETE) {
                    slotted.DeleteTuple(rid.slot_id);
                }
                page->SetLSN(rec.GetLSN());
                buffer_pool_manager_->UnpinPage(rid.page_id, true);
                redo_count++;
            } else {
                buffer_pool_manager_->UnpinPage(rid.page_id, false);
            }
        }
    }
    std::cout << "[Recovery] Redone " << redo_count << " operations." << std::endl;

    // --- PHASE 3: UNDO ---
    std::cout << "[Recovery] Phase 3: Undo..." << std::endl;
    int undo_count = 0;
    // Scan backward
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto &rec = *it;
        if (active_txns.find(rec.GetTxnId()) != active_txns.end()) {
            if (rec.GetType() == LogRecordType::INSERT || rec.GetType() == LogRecordType::DELETE) {
                RID rid = rec.GetRID();
                Page *page = buffer_pool_manager_->FetchPage(rid.page_id);
                if (page == nullptr) {
                    std::cerr << "[Recovery] Error: Failed to fetch page " << rid.page_id << " during undo." << std::endl;
                    continue;
                }

                SlottedPage slotted(page);
                if (rec.GetType() == LogRecordType::INSERT) {
                    // Undo insert by deleting
                    slotted.DeleteTuple(rid.slot_id);
                    std::cout << "[Recovery] Undo INSERT of RID " << rid.ToString() << " for Txn " << rec.GetTxnId() << std::endl;
                } else if (rec.GetType() == LogRecordType::DELETE) {
                    // Undo delete by restoring
                    slotted.RestoreTuple(rid.slot_id, rec.GetData().data(), rec.GetData().size());
                    std::cout << "[Recovery] Undo DELETE of RID " << rid.ToString() << " for Txn " << rec.GetTxnId() << std::endl;
                }

                page->SetLSN(rec.GetLSN()); // Update page LSN
                buffer_pool_manager_->UnpinPage(rid.page_id, true);
                undo_count++;
            }
        }
    }
    std::cout << "[Recovery] Undone " << undo_count << " operations." << std::endl;

    // Write ABORT records for loser transactions to the log
    // This completes the crash recovery process.
    std::cout << "[Recovery] Recovery complete." << std::endl;
}

} // namespace minidb
