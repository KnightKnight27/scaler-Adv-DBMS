#include "catalog/catalog.h"
#include "common/logger.h"
#include "storage/lsm/sstable_reader.h"
#include <mutex>
#include <map>

namespace minidb {

TableMetadata* Catalog::CreateTable(const std::string& table_name, const Schema& schema) {
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);

    // Prevent duplicate table names
    if (table_names_.find(table_name) != table_names_.end()) {
        LOG_ERROR("Table '" + table_name + "' already exists.");
        return nullptr;
    }

    table_oid_t table_oid = next_table_oid_++;
    
    auto table_meta = std::make_unique<TableMetadata>();
    table_meta->oid = table_oid;
    table_meta->name = table_name;
    table_meta->schema = std::make_unique<Schema>(schema);

    // FIX: Initialize MemTable and WAL *after* the valid OID has been assigned
    table_meta->memtable = std::make_unique<MemTable>();
    table_meta->wal = std::make_unique<WAL>("wal_" + std::to_string(table_oid) + ".log");

    // Store raw pointer for returning before we move the unique_ptr into the map
    TableMetadata* result = table_meta.get();

    tables_[table_oid] = std::move(table_meta);
    table_names_[table_name] = table_oid;

    LOG_INFO("Created table '" + table_name + "' with OID " + std::to_string(table_oid));
    return result;
}

TableMetadata* Catalog::GetTable(table_oid_t table_oid) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    auto it = tables_.find(table_oid);
    if (it != tables_.end()) {
        return it->second.get();
    }
    return nullptr;
}

TableMetadata* Catalog::GetTable(const std::string& table_name) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    auto it = table_names_.find(table_name);
    if (it != table_names_.end()) {
        // Now that we have the OID, look up the actual metadata
        return tables_.at(it->second).get();
    }
    return nullptr;
}

void Catalog::AddSSTable(table_oid_t table_oid, const std::string& file_path) {
    TableMetadata* table = GetTable(table_oid);
    if (table != nullptr) {
        std::unique_lock<std::shared_mutex> lock(table->sstable_mutex);
        table->sstable_paths.push_back(file_path);
        LOG_INFO("Attached SSTable " + file_path + " to table OID " + std::to_string(table_oid));
    }
}

IndexMetadata* Catalog::CreateIndex(const std::string& index_name,
                                    const std::string& table_name,
                                    const std::string& column_name) {
    TableMetadata* table = GetTable(table_name);
    if (table == nullptr) {
        LOG_ERROR("Cannot create index '" + index_name + "': table '" + table_name +
                  "' does not exist.");
        return nullptr;
    }

    // Resolve the column to an index in the schema.
    uint32_t col_idx;
    try {
        col_idx = table->schema->GetColIndex(column_name);
    } catch (const std::exception&) {
        LOG_ERROR("Cannot create index '" + index_name + "': column '" + column_name +
                  "' not found in '" + table_name + "'.");
        return nullptr;
    }

    // Reject a duplicate index name on this table.
    for (const auto& existing : table->indexes) {
        if (existing->name == index_name) {
            LOG_ERROR("Index '" + index_name + "' already exists on '" + table_name + "'.");
            return nullptr;
        }
    }

    auto index_meta = std::make_unique<IndexMetadata>();
    index_meta->name = index_name;
    index_meta->column_index = col_idx;
    index_meta->tree = std::make_unique<BPlusTree>();

    // Back-fill the index from rows already stored for this table. We merge the
    // SSTables (oldest -> newest) and then the MemTable, mirroring SeqScan, so
    // the freshest version of each primary key wins before we index it.
    std::map<std::string, std::string> merged;
    {
        std::shared_lock<std::shared_mutex> lock(table->sstable_mutex);
        for (const auto& path : table->sstable_paths) {
            for (const auto& [key, value] : SSTableReader::ReadAll(path)) {
                if (key.size() >= sizeof(table_oid_t) &&
                    *reinterpret_cast<const table_oid_t*>(key.data()) == table->oid) {
                    merged[key] = value;
                }
            }
        }
    }
    for (const auto& [key, value] : table->memtable->GetAllEntries()) {
        if (key.size() >= sizeof(table_oid_t) &&
            *reinterpret_cast<const table_oid_t*>(key.data()) == table->oid) {
            merged[key] = value;
        }
    }

    size_t back_filled = 0;
    for (const auto& [key, value] : merged) {
        if (value.empty()) continue; // tombstone: skip deleted rows
        Row row = Row::Deserialize(value);
        if (col_idx < row.columns.size()) {
            index_meta->tree->Insert(row.columns[col_idx], value);
            ++back_filled;
        }
    }

    IndexMetadata* result = index_meta.get();
    table->indexes.push_back(std::move(index_meta));

    LOG_INFO("Created index '" + index_name + "' on " + table_name + "(" + column_name +
             "), back-filled " + std::to_string(back_filled) + " row(s).");
    return result;
}

IndexMetadata* Catalog::GetIndex(table_oid_t table_oid, uint32_t column_index) const {
    TableMetadata* table = GetTable(table_oid);
    if (table == nullptr) return nullptr;
    for (const auto& index : table->indexes) {
        if (index->column_index == column_index) return index.get();
    }
    return nullptr;
}

} // namespace minidb