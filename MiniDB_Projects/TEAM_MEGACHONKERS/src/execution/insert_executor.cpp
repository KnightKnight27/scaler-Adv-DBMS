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
        std::string primary_key = child_row.columns[0];
        InternalKey lsm_key{table_oid_, primary_key};

        // 1. Durability: Write to the WAL first
        table->wal->Append(LogRecordType::PUT, lsm_key.Encode(), child_row.Serialize());

        // 2. Storage: Write to the MemTable
        table->memtable->Put(lsm_key, child_row);

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