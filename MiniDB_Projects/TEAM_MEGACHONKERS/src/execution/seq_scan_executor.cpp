#include "execution/seq_scan_executor.h"
#include "storage/lsm/sstable_reader.h"
#include "common/logger.h"

namespace minidb {

void SeqScanExecutor::Init() {
    TableMetadata* table = context_->GetCatalog()->GetTable(table_oid_);
    merged_view_.clear();

    // 1. Load data from the oldest to newest SSTable
    // Because std::map overwrites duplicate keys, newer files will correctly 
    // overwrite older versions of the same row.
    std::shared_lock<std::shared_mutex> lock(table->sstable_mutex);
    for (const auto& file_path : table->sstable_paths) {
        auto file_data = SSTableReader::ReadAll(file_path);
        for (const auto& [key, value] : file_data) {
            // Robustness Check: Only load keys that belong to this table's OID
            if (key.size() >= sizeof(table_oid_t)) {
                table_oid_t key_table_id = *reinterpret_cast<const table_oid_t*>(key.data());
                if (key_table_id == table_oid_) {
                    merged_view_[key] = value;
                }
            }
        }
    }
    lock.unlock(); // Drop lock before hitting the MemTable

    // 2. Load the absolute newest data from the MemTable
    auto mem_data = table->memtable->GetAllEntries();
    for (const auto& [key, value] : mem_data) {
        // Robustness Check: Only load keys that belong to this table's OID
        if (key.size() >= sizeof(table_oid_t)) {
            table_oid_t key_table_id = *reinterpret_cast<const table_oid_t*>(key.data());
            if (key_table_id == table_oid_) {
                merged_view_[key] = value;
            }
        }
    }

    // 3. Set the cursor to the beginning of our merged snapshot
    cursor_ = merged_view_.begin();
    LOG_INFO("SeqScanExecutor initialized for OID " + std::to_string(table_oid_));
}

bool SeqScanExecutor::Next(Row* row) {
    while (cursor_ != merged_view_.end()) {
        // If the value is an empty string, it is an LSM Tombstone (deleted row).
        // We must silently skip it.
        if (cursor_->second.empty()) {
            ++cursor_;
            continue;
        }

        // Deserialize the byte array back into a usable Row object
        *row = Row::Deserialize(cursor_->second);
        ++cursor_;
        
        return true; // Yield this row to the parent executor/user
    }

    return false; // EOF reached
}

} // namespace minidb