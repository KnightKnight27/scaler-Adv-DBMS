#include "execution/insert_executor.h"
#include "common/logger.h"

namespace minidb {

void InsertExecutor::Init() {
    child_executor_->Init();
    has_run_ = false;
}

bool InsertExecutor::Next(Row* row) {
    if (has_run_) {
        return false; 
    }

    TableMetadata* table = context_->GetCatalog()->GetTable(table_oid_);
    if (!table) {
        LOG_ERROR("Table OID " + std::to_string(table_oid_) + " does not exist.");
        return false;
    }

    Row child_row;
    uint32_t insert_count = 0;

    // Pull rows from the child operator
    while (child_executor_->Next(&child_row)) {
        // Robustness Check: Prevent crash if the child operator yielded an empty row
        if (child_row.columns.empty()) {
            continue;
        }

        std::string primary_key = child_row.columns[0];
        InternalKey lsm_key{table_oid_, primary_key};

        std::string serialized = child_row.Serialize();

        // 1. Durability: Write to the WAL first
        table->wal->Append(LogRecordType::PUT, lsm_key.Encode(), serialized);

        // 2. Storage: Write to the MemTable
        table->memtable->Put(lsm_key, child_row);

        // 3. Index maintenance: keep every secondary index in sync with the row.
        for (const auto& index : table->indexes) {
            if (index->column_index < child_row.columns.size()) {
                index->tree->Insert(child_row.columns[index->column_index], serialized);
            }
        }

        insert_count++;
    }

    table->wal->Flush();

    // Output the result of the operation
    row->columns = { std::to_string(insert_count) };
    has_run_ = true;
    
    LOG_INFO("InsertExecutor completed. Rows inserted: " + std::to_string(insert_count));
    return true;
}

} // namespace minidb