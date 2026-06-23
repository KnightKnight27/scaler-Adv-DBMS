#include "recovery/recovery_manager.h"
#include "catalog/catalog.h"
#include "recovery/wal.h"

#include <set>
#include <stdexcept>

namespace minidb {

RecoveryResult RecoveryManager::Recover() {
    std::vector<LogRecord> records = wal_->ReadLog();
    std::set<txn_id_t> committed;
    for (const auto& record : records) {
        if (record.type == LogType::COMMIT) committed.insert(record.txn_id);
    }

    RecoveryResult result;
    std::set<std::string> touched;
    for (const auto& record : records) {
        if (record.type != LogType::INSERT &&
            record.type != LogType::DELETE_LOG) continue;
        if (!committed.count(record.txn_id)) {
            result.ignored++;
            continue;
        }

        TableInfo* table = catalog_->GetTable(record.table_name);
        if (!table) throw std::runtime_error(
            "Recovery table not registered: " + record.table_name);
        int64_t key = std::stoll(record.key_data);

        if (record.type == LogType::INSERT) {
            if (table->primary_index->Search(key)) continue;
            Row row = HeapFile::DeserializeRow(record.record_data.data(),
                static_cast<uint16_t>(record.record_data.size()), table->schema);
            RecordId rid = table->heap_file->InsertTuple(row);
            if (rid.page_id == INVALID_PAGE_ID) {
                throw std::runtime_error("Recovery insert failed");
            }
            table->primary_index->Insert(key, rid);
        } else {
            auto rid = table->primary_index->Search(key);
            if (!rid) continue;
            table->heap_file->DeleteTuple(*rid);
            table->primary_index->Delete(key);
        }
        touched.insert(table->name);
        result.redone++;
    }

    for (const auto& name : touched) {
        catalog_->GetTable(name)->heap_file->Flush();
    }
    return result;
}

} // namespace minidb
