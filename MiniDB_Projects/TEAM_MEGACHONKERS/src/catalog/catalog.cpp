#include "catalog/catalog.h"
#include "common/logger.h"
#include <mutex>

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

} // namespace minidb