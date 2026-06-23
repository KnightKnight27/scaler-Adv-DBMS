#include "mvcc/mvcc_manager.h"

namespace minidb {

MVCCManager::MVCCManager(BufferPool *buffer_pool) : buffer_pool_(buffer_pool) {}

bool MVCCManager::InsertVersion(const RecordId &rid, const Tuple &tuple, Transaction *txn) {
    // With persistent MVCC, TupleSerializer already writes created_by = txn_id.
    // The tuple is physically inserted into the page in minidb.cpp before this is called.
    return true;
}

bool MVCCManager::DeleteVersion(const RecordId &rid, Transaction *txn) {
    Page *page = buffer_pool_->FetchPage(rid.page_id);
    if (!page) return false;

    Tuple tuple;
    if (!page->GetTuple(rid.slot_id, &tuple)) {
        buffer_pool_->UnpinPage(rid.page_id, false);
        return false;
    }

    int32_t deleted_by = tuple.GetDeletedBy();
    // Write-write conflict detection
    if (deleted_by != TXN_NULL && deleted_by != txn->GetTransactionId()) {
        buffer_pool_->UnpinPage(rid.page_id, false);
        return false; // Already deleted by another txn!
    }

    tuple.SetDeletedBy(txn->GetTransactionId());
    page->UpdateTuple(rid.slot_id, tuple);
    buffer_pool_->UnpinPage(rid.page_id, true);
    return true;
}

bool MVCCManager::ReadVisibleVersion(const RecordId &rid, Transaction *txn, Tuple *tuple) {
    Page *page = buffer_pool_->FetchPage(rid.page_id);
    if (!page) return false;

    if (!page->GetTuple(rid.slot_id, tuple)) {
        buffer_pool_->UnpinPage(rid.page_id, false);
        return false;
    }
    buffer_pool_->UnpinPage(rid.page_id, false);

    int32_t ts = txn->GetSnapshotTimestamp();
    int32_t current_txn_id = txn->GetTransactionId();
    int32_t created_by = tuple->GetCreatedBy();
    int32_t deleted_by = tuple->GetDeletedBy();

    bool created_visible = false;
    if (created_by == current_txn_id) {
        created_visible = true;
    } else if (commit_map_.find(created_by) != commit_map_.end() && commit_map_[created_by] <= ts) {
        created_visible = true;
    }

    if (!created_visible) return false;

    bool deleted_visible = false;
    if (deleted_by == current_txn_id) {
        deleted_visible = true;
    } else if (deleted_by != TXN_NULL && commit_map_.find(deleted_by) != commit_map_.end() && commit_map_[deleted_by] <= ts) {
        deleted_visible = true;
    }

    return !deleted_visible;
}

int MVCCManager::Vacuum(int32_t min_active_ts, TableInfo &tinfo) {
    int vacuumed_count = 0;
    page_id_t curr = tinfo.first_page_id;
    while (curr != INVALID_PAGE_ID) {
        Page *p = buffer_pool_->FetchPage(curr);
        if (!p) break;
        
        bool dirty = false;
        for (int s = 0; s < p->GetTupleCount(); ++s) {
            Tuple tuple;
            if (p->GetTuple(s, &tuple)) {
                int32_t deleted_by = tuple.GetDeletedBy();
                if (deleted_by != TXN_NULL && deleted_by < min_active_ts) {
                    // This version is dead to everyone!
                    p->DeleteTuple(s);
                    dirty = true;
                    vacuumed_count++;
                    
                    if (tinfo.index && !tinfo.schema.columns.empty() && tinfo.schema.columns[0].name == "id" && tinfo.schema.columns[0].type == ColumnType::INT) {
                        auto values = TupleSerializer::Deserialize(tuple, tinfo.schema);
                        if (!values.empty()) {
                            int32_t key = std::stoi(values[0]);
                            tinfo.index->Delete(key);
                        }
                    }
                }
            }
        }
        
        page_id_t next = p->GetNextPageId();
        buffer_pool_->UnpinPage(curr, dirty);
        curr = next;
    }
    return vacuumed_count;
}

} // namespace minidb
