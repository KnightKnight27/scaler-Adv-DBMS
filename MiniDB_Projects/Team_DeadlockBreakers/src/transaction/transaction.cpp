#include "transaction/transaction.h"
#include "catalog/catalog.h"

#include <set>

namespace minidb {

void Transaction::StageInsert(TableInfo* table, const Row& row, int64_t key) {
    pending_writes_.push_back(
        {PendingWriteType::INSERT, table, row, {}, key});
}

void Transaction::StageDelete(TableInfo* table, const RecordId& record_id,
                              int64_t key) {
    pending_writes_.push_back(
        {PendingWriteType::DELETE, table, {}, record_id, key});
}

bool Transaction::Apply() {
    std::set<TableInfo*> changed_tables;
    for (const PendingWrite& write : pending_writes_) {
        if (write.type == PendingWriteType::INSERT) {
            RecordId record = write.table->heap_file->InsertTuple(write.row);
            if (record.page_id == INVALID_PAGE_ID) return false;
            write.table->primary_index->Insert(write.key, record);
        } else {
            if (!write.table->heap_file->DeleteTuple(write.record_id)) return false;
            write.table->primary_index->Delete(write.key);
        }
        changed_tables.insert(write.table);
    }
    for (TableInfo* table : changed_tables) table->heap_file->Flush();
    pending_writes_.clear();
    return true;
}

} // namespace minidb
