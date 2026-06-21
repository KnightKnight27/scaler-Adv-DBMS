#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <vector>

#include "catalog/schema.h"
#include "common/config.h"

#include "storage/lsm/memtable.h"
#include "recovery/wal.h"

namespace minidb {

struct TableMetadata {
    table_oid_t oid;
    std::string name;
    std::unique_ptr<Schema> schema;
    
    // In an LSM tree, a table is composed of many SSTables on disk
    std::vector<std::string> sstable_paths; 
    
    // Concurrency protection for modifying the sstable list during compactions
    mutable std::shared_mutex sstable_mutex;

    // Add these inside struct TableMetadata
    std::unique_ptr<MemTable> memtable = std::make_unique<MemTable>();
    std::unique_ptr<WAL> wal = std::make_unique<WAL>("wal_" + std::to_string(oid) + ".log");
};

class Catalog {
private:
    std::unordered_map<table_oid_t, std::unique_ptr<TableMetadata>> tables_;
    
    // Secondary index to quickly look up a table by its string name
    std::unordered_map<std::string, table_oid_t> table_names_;
    
    std::atomic<table_oid_t> next_table_oid_{1}; // Start IDs at 1
    mutable std::shared_mutex rw_mutex_;

public:
    Catalog() = default;

    // Registers a new table in the database
    TableMetadata* CreateTable(const std::string& table_name, const Schema& schema);

    // Lookups
    TableMetadata* GetTable(table_oid_t table_oid) const;
    TableMetadata* GetTable(const std::string& table_name) const;

    // LSM Specific: Attach a new SSTable file to a table after a MemTable flush
    void AddSSTable(table_oid_t table_oid, const std::string& file_path);
};

} // namespace minidb