#include "recovery/recovery_manager.h"
#include <iostream>
#include <fstream>
#include <map>
#include <queue>
#include <algorithm>

// Slotted page helper functions for recovery mutations
static void RecoveryPageInsert(Page* page, uint16_t slot_id, const std::string& data_str, TxId_t xmin, TxId_t xmax, PageId_t prev_pid, uint16_t prev_sid) {
    PageHeader* hdr = page->GetHeader();
    Slot* slots = page->GetSlots();
    uint16_t payload_size = sizeof(MVCCHeader) + data_str.length();

    hdr->free_space_pointer -= payload_size;
    
    char* target = page->data + hdr->free_space_pointer;
    MVCCHeader* mvcc = reinterpret_cast<MVCCHeader*>(target);
    mvcc->xmin = xmin;
    mvcc->xmax = xmax;
    mvcc->prev_page_id = prev_pid;
    mvcc->prev_slot_id = prev_sid;

    std::memcpy(target + sizeof(MVCCHeader), data_str.data(), data_str.length());

    slots[slot_id].offset = hdr->free_space_pointer;
    slots[slot_id].length = payload_size;

    if (slot_id >= hdr->slot_count) {
        hdr->slot_count = slot_id + 1;
    }
}

static void RecoveryPageDelete(Page* page, uint16_t slot_id) {
    PageHeader* hdr = page->GetHeader();
    Slot* slots = page->GetSlots();
    if (slot_id < hdr->slot_count) {
        slots[slot_id].offset = 0;
        slots[slot_id].length = 0;
    }
}

static void RecoveryPageUpdate(Page* page, uint16_t slot_id, const std::string& data_str, TxId_t xmin, TxId_t xmax, PageId_t prev_pid, uint16_t prev_sid) {
    RecoveryPageInsert(page, slot_id, data_str, xmin, xmax, prev_pid, prev_sid);
}

RecoveryManager::RecoveryManager(LogManager* log_manager, BufferPoolManager* bpm)
    : log_manager_(log_manager), bpm_(bpm) {}

void RecoveryManager::Recover(const std::string& log_file_name) {
    std::cout << "[Recovery] Starting ARIES Recovery Process..." << std::endl;

    // 1. ANALYSIS PASS
    std::cout << "[Recovery] Running Analysis Pass..." << std::endl;
    std::unordered_set<TxId_t> active_txns;
    std::unordered_map<PageId_t, Lsn_t> dirty_pages;
    std::unordered_map<TxId_t, Lsn_t> prev_lsn_map;

    std::ifstream log_file(log_file_name, std::ios::binary);
    if (!log_file.is_open()) {
        std::cout << "[Recovery] No WAL log found. Nothing to recover." << std::endl;
        return;
    }

    std::vector<LogRecord> all_records;
    while (log_file) {
        LogRecord rec = LogRecord::Deserialize(log_file);
        if (rec.lsn == 0) break; // EOF
        all_records.push_back(rec);

        prev_lsn_map[rec.txn_id] = rec.lsn;

        if (rec.type == LogRecordType::BEGIN) {
            active_txns.insert(rec.txn_id);
        } else if (rec.type == LogRecordType::COMMIT || rec.type == LogRecordType::ABORT) {
            active_txns.erase(rec.txn_id);
        } else {
            // Data modification
            active_txns.insert(rec.txn_id); // In case BEGIN was missing
            if (dirty_pages.find(rec.page_id) == dirty_pages.end()) {
                dirty_pages[rec.page_id] = rec.lsn;
            }
        }
    }
    log_file.close();

    std::cout << "[Recovery] Analysis complete. Loser Txns: ";
    for (TxId_t tx : active_txns) std::cout << "T" << tx << " ";
    std::cout << "\n[Recovery] Dirty pages: ";
    for (const auto& [p, l] : dirty_pages) std::cout << p << " (recLSN:" << l << ") ";
    std::cout << std::endl;

    // 2. REDO PASS
    std::cout << "[Recovery] Running Redo Pass (Repeating History)..." << std::endl;
    Lsn_t smallest_rec_lsn = 0;
    if (!dirty_pages.empty()) {
        smallest_rec_lsn = all_records.front().lsn; // default
        for (const auto& [page_id, rec_lsn] : dirty_pages) {
            smallest_rec_lsn = std::min(smallest_rec_lsn, rec_lsn);
        }
    }

    for (const auto& rec : all_records) {
        if (rec.lsn < smallest_rec_lsn) continue;

        if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::DELETE || 
            rec.type == LogRecordType::UPDATE || rec.type == LogRecordType::CLR) {
            
            // Check if page is in dirty list
            if (dirty_pages.find(rec.page_id) == dirty_pages.end() || dirty_pages[rec.page_id] > rec.lsn) {
                continue; // Change already flushed or not dirty
            }

            Page* page = bpm_->FetchPage(rec.page_id);
            if (!page) continue;

            if (page->GetHeader()->page_lsn < rec.lsn) {
                // Redo the operation
                std::cout << "[Recovery] Redoing Log LSN " << rec.lsn << ": Tx " << rec.txn_id << " on Page " << rec.page_id << std::endl;
                if (rec.type == LogRecordType::INSERT) {
                    RecoveryPageInsert(page, rec.slot_id, rec.after_image, rec.txn_id, 0, INVALID_PAGE_ID, 0);
                } else if (rec.type == LogRecordType::DELETE) {
                    RecoveryPageDelete(page, rec.slot_id);
                } else if (rec.type == LogRecordType::UPDATE) {
                    RecoveryPageUpdate(page, rec.slot_id, rec.after_image, rec.txn_id, 0, INVALID_PAGE_ID, 0);
                } else if (rec.type == LogRecordType::CLR) {
                    // Redoing CLR applies the before image (which undoes the original write)
                    RecoveryPageUpdate(page, rec.slot_id, rec.after_image, rec.txn_id, 0, INVALID_PAGE_ID, 0);
                }
                page->GetHeader()->page_lsn = rec.lsn;
                bpm_->UnpinPage(rec.page_id, true);
            } else {
                bpm_->UnpinPage(rec.page_id, false);
            }
        }
    }

    // 3. UNDO PASS
    std::cout << "[Recovery] Running Undo Pass (Rolling Back Losers)..." << std::endl;
    // We undo loser transactions in reverse LSN order.
    // Initialize priority queue with last LSN of each active loser transaction
    auto cmp = [](const std::pair<Lsn_t, TxId_t>& a, const std::pair<Lsn_t, TxId_t>& b) {
        return a.first < b.first; // max heap (largest LSN first)
    };
    std::priority_queue<std::pair<Lsn_t, TxId_t>, std::vector<std::pair<Lsn_t, TxId_t>>, decltype(cmp)> undo_pq(cmp);

    for (TxId_t txn_id : active_txns) {
        if (prev_lsn_map[txn_id] > 0) {
            undo_pq.push({prev_lsn_map[txn_id], txn_id});
        }
    }

    // Helper map of all records by LSN for quick lookup during undo traversal
    std::unordered_map<Lsn_t, LogRecord> record_lookup;
    for (const auto& rec : all_records) {
        record_lookup[rec.lsn] = rec;
    }

    while (!undo_pq.empty()) {
        auto [curr_lsn, txn_id] = undo_pq.top();
        undo_pq.pop();

        auto r_it = record_lookup.find(curr_lsn);
        if (r_it == record_lookup.end()) continue;

        LogRecord rec = r_it->second;

        if (rec.type == LogRecordType::INSERT || rec.type == LogRecordType::DELETE || rec.type == LogRecordType::UPDATE) {
            // Undo this record
            std::cout << "[Recovery] Undoing LSN " << rec.lsn << ": Tx T" << rec.txn_id << " on Page " << rec.page_id << std::endl;

            Page* page = bpm_->FetchPage(rec.page_id);
            if (page) {
                // Apply the opposite operation
                if (rec.type == LogRecordType::INSERT) {
                    // Undo insert by deleting
                    RecoveryPageDelete(page, rec.slot_id);
                } else if (rec.type == LogRecordType::DELETE) {
                    if (rec.after_image == "XMAX_SET") {
                        // MVCC rollback delete: reset xmax to 0
                        Slot* slots = page->GetSlots();
                        char* r_ptr = page->data + slots[rec.slot_id].offset;
                        MVCCHeader* mvcc = reinterpret_cast<MVCCHeader*>(r_ptr);
                        mvcc->xmax = 0;
                    } else {
                        // Undo delete by inserting before_image (2PL)
                        RecoveryPageInsert(page, rec.slot_id, rec.before_image, rec.txn_id, 0, INVALID_PAGE_ID, 0);
                    }
                } else if (rec.type == LogRecordType::UPDATE) {
                    // Undo update by inserting before_image
                    RecoveryPageUpdate(page, rec.slot_id, rec.before_image, rec.txn_id, 0, INVALID_PAGE_ID, 0);
                }

                // Write CLR log record
                Lsn_t clr_lsn = log_manager_->AppendRecord(
                    txn_id, 
                    prev_lsn_map[txn_id], 
                    LogRecordType::CLR, 
                    rec.page_id, 
                    rec.slot_id, 
                    rec.after_image, // CLR before_image is the update's after_image
                    rec.before_image, // CLR after_image is the update's before_image (reverted data)
                    rec.prev_lsn     // CLR undo_next_lsn points to prev_lsn of this record
                );

                prev_lsn_map[txn_id] = clr_lsn;
                page->GetHeader()->page_lsn = clr_lsn;
                bpm_->UnpinPage(rec.page_id, true);
            }

            if (rec.prev_lsn > 0) {
                undo_pq.push({rec.prev_lsn, txn_id});
            }
        } 
        else if (rec.type == LogRecordType::CLR) {
            // CLR undo_next_lsn points to the next log record to undo for this transaction
            std::cout << "[Recovery] Skipping CLR LSN " << rec.lsn << ", jumping to next LSN " << rec.undo_next_lsn << std::endl;
            if (rec.undo_next_lsn > 0) {
                undo_pq.push({rec.undo_next_lsn, txn_id});
            }
        } 
        else if (rec.type == LogRecordType::BEGIN) {
            // End of transaction rollback: write ABORT log record to mark it aborted
            std::cout << "[Recovery] Transaction T" << txn_id << " rollback completed." << std::endl;
            log_manager_->AppendRecord(txn_id, prev_lsn_map[txn_id], LogRecordType::ABORT);
        }
    }

    log_manager_->Flush();
    bpm_->FlushAllPages();
    std::cout << "[Recovery] Crash Recovery Complete! Database is fully consistent." << std::endl;
}
