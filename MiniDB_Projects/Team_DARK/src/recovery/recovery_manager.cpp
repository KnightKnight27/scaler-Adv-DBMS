#include "recovery/recovery_manager.h"

#include "storage/page.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace minidb {

namespace {

struct TxUndoRecord {
    uint64_t lsn = 0;
    LogRecordType type = LogRecordType::BEGIN;
    std::vector<char> payload;
};

uint64_t GetPageLsn(BufferPoolManager* buffer_pool, page_id_t page_id) {
    char* data = buffer_pool->FetchPage(page_id);
    Page page(data);
    const uint64_t lsn = page.GetHeader().lsn;
    buffer_pool->UnpinPage(page_id);
    return lsn;
}

}  // namespace

RecoveryState RecoveryManager::Recover(DiskManager* disk_manager, BufferPoolManager* buffer_pool,
                                       TableHeap* heap, LogManager* log_manager) {
    RecoveryState state{};
    const std::vector<LogRecord> records = log_manager->LoadAll();
    if (records.empty()) {
        return state;
    }

    std::unordered_map<TxID, TxStatus> tx_status;
    std::unordered_map<TxID, std::vector<TxUndoRecord>> tx_undo_records;
    page_id_t max_page_id = INVALID_PAGE_ID;

    for (const LogRecord& record : records) {
        switch (record.type) {
            case LogRecordType::BEGIN: {
                const TxID tx_id = LogManager::DecodeTxId(record.payload);
                tx_status[tx_id] = TxStatus::ACTIVE;
                state.next_xid = std::max(state.next_xid, tx_id + 1);
                break;
            }
            case LogRecordType::COMMIT: {
                const TxID tx_id = LogManager::DecodeTxId(record.payload);
                tx_status[tx_id] = TxStatus::COMMITTED;
                state.next_xid = std::max(state.next_xid, tx_id + 1);
                break;
            }
            case LogRecordType::ABORT: {
                const TxID tx_id = LogManager::DecodeTxId(record.payload);
                tx_status[tx_id] = TxStatus::ABORTED;
                state.next_xid = std::max(state.next_xid, tx_id + 1);
                break;
            }
            case LogRecordType::INSERT_ROW:
            case LogRecordType::UPDATE_XMAX: {
                TxID tx_id = 0;
                int32_t page_id = 0;
                uint16_t slot_index = 0;
                if (record.type == LogRecordType::INSERT_ROW) {
                    uint16_t row_offset = 0;
                    uint16_t row_length = 0;
                    std::string key;
                    uint64_t xmin = 0;
                    uint64_t xmax = 0;
                    uint64_t prev_version_tid = 0;
                    std::string value;
                    LogManager::DecodeInsertRow(record.payload, tx_id, page_id, slot_index,
                                                row_offset, row_length, key, xmin, xmax,
                                                prev_version_tid, value);
                    max_page_id = std::max(max_page_id, page_id);

                    const uint64_t page_lsn = GetPageLsn(buffer_pool, page_id);
                    if (record.lsn > page_lsn) {
                        RowVersionHeader header{};
                        header.xmin = xmin;
                        header.xmax = xmax;
                        header.prev_version_tid = prev_version_tid;
                        heap->RecoveryRedoInsert({page_id, slot_index}, row_offset, row_length,
                                                 key, header, value, record.lsn);
                    } else {
                        heap->RecoveryRegisterKey(key, {page_id, slot_index});
                    }
                } else {
                    uint64_t old_xmax = 0;
                    uint64_t new_xmax = 0;
                    LogManager::DecodeUpdateXmax(record.payload, tx_id, page_id, slot_index,
                                                old_xmax, new_xmax);
                    max_page_id = std::max(max_page_id, page_id);

                    const uint64_t page_lsn = GetPageLsn(buffer_pool, page_id);
                    if (record.lsn > page_lsn) {
                        heap->RecoveryRedoSetXmax({page_id, slot_index}, new_xmax, record.lsn);
                    }
                }

                TxUndoRecord undo{};
                undo.lsn = record.lsn;
                undo.type = record.type;
                undo.payload = record.payload;
                tx_undo_records[tx_id].push_back(std::move(undo));
                state.next_xid = std::max(state.next_xid, tx_id + 1);
                break;
            }
        }
    }

    std::vector<TxID> active_txs;
    for (const auto& entry : tx_status) {
        if (entry.second == TxStatus::ACTIVE) {
            active_txs.push_back(entry.first);
        }
        state.transactions[entry.first] = entry.second;
    }

    for (const TxID tx_id : active_txs) {
        auto undo_it = tx_undo_records.find(tx_id);
        if (undo_it == tx_undo_records.end()) {
            state.transactions[tx_id] = TxStatus::ABORTED;
            continue;
        }

        const std::vector<TxUndoRecord>& undo_list = undo_it->second;
        for (auto rit = undo_list.rbegin(); rit != undo_list.rend(); ++rit) {
            if (rit->type == LogRecordType::INSERT_ROW) {
                TxID record_tx_id = 0;
                int32_t page_id = 0;
                uint16_t slot_index = 0;
                uint16_t row_offset = 0;
                uint16_t row_length = 0;
                std::string key;
                uint64_t xmin = 0;
                uint64_t xmax = 0;
                uint64_t prev_version_tid = 0;
                std::string value;
                LogManager::DecodeInsertRow(rit->payload, record_tx_id, page_id, slot_index,
                                            row_offset, row_length, key, xmin, xmax,
                                            prev_version_tid, value);
                heap->RecoveryUndoInsert({page_id, slot_index}, record_tx_id, key);
            } else if (rit->type == LogRecordType::UPDATE_XMAX) {
                TxID record_tx_id = 0;
                int32_t page_id = 0;
                uint16_t slot_index = 0;
                uint64_t old_xmax = 0;
                uint64_t new_xmax = 0;
                LogManager::DecodeUpdateXmax(rit->payload, record_tx_id, page_id, slot_index,
                                            old_xmax, new_xmax);
                heap->RecoveryUndoSetXmax({page_id, slot_index}, old_xmax);
            }
        }
        state.transactions[tx_id] = TxStatus::ABORTED;
    }

    if (max_page_id != INVALID_PAGE_ID) {
        state.next_page_id = max_page_id + 1;
        state.insert_page_id = max_page_id;
    }
    heap->SetMetadata(state.next_page_id, state.insert_page_id);

    buffer_pool->FlushAllPages();
    (void)disk_manager;

    return state;
}

}  // namespace minidb
