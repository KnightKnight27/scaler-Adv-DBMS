#include "execution/delete_executor.h"
#include "common/logger.h"

namespace minidb {

void DeleteExecutor::Init() {
    child_executor_->Init();
    has_run_ = false;
}

bool DeleteExecutor::Next(Row* row) {
    if (has_run_) return false;

    TableMetadata* table = context_->GetCatalog()->GetTable(table_oid_);
    if (!table) return false;

    Row child_row;
    uint32_t delete_count = 0;

    // Pull rows to delete from the child operator
    while (child_executor_->Next(&child_row)) {
        // Robustness Check: Prevent out-of-bounds if the child operator yielded an empty row
        if (child_row.columns.empty()) {
            continue;
        }

        // Assume first column is the Primary Key
        std::string primary_key = child_row.columns[0];
        InternalKey lsm_key{table_oid_, primary_key};

        // Write a Tombstone to the WAL, then to the MemTable
        table->wal->Append(LogRecordType::DELETE_TOMBSTONE, lsm_key.Encode(), "");
        table->memtable->Delete(lsm_key);

        // Index maintenance: drop the row from each secondary index. This only
        // works when the child yields full rows (the planner's DELETE path uses
        // a SeqScan, so it does); a PK-only child simply has no indexed columns
        // to remove beyond column 0.
        for (const auto& index : table->indexes) {
            if (index->column_index < child_row.columns.size()) {
                index->tree->Delete(child_row.columns[index->column_index]);
            }
        }

        delete_count++;
    }

    table->wal->Flush(); // Group Commit

    row->columns = { std::to_string(delete_count) };
    has_run_ = true;
    
    LOG_INFO("DeleteExecutor completed. Rows deleted: " + std::to_string(delete_count));
    return true;
}

} // namespace minidb