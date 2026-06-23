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
#include "storage/btree/bplus_tree.h"
#include "recovery/wal.h"

namespace minidb {

// A secondary B+Tree index over a single column of a table. The tree maps the
// indexed column's value -> the full serialized row, so an IndexScan can
// reconstruct the row without touching the LSM storage. (Point-lookup oriented:
// one row per key, so it is most meaningful on unique columns such as a PK.)
struct IndexMetadata {
    std::string name;
    uint32_t column_index;          // position of the indexed column in the schema
    std::unique_ptr<BPlusTree> tree;
};

struct TableMetadata {
    table_oid_t oid;
    std::string name;
    std::unique_ptr<Schema> schema;

    // In an LSM tree, a table is composed of many SSTables on disk
    std::vector<std::string> sstable_paths;

    // Concurrency protection for modifying the sstable list during compactions
    mutable std::shared_mutex sstable_mutex;

    // FIX: Removed inline initialization to prevent uninitialized 'oid' usage.
    std::unique_ptr<MemTable> memtable;
    std::unique_ptr<WAL> wal;

    // Secondary indexes registered on this table (kept in sync by the
    // Insert/Delete executors). Empty for a table with no indexes.
    std::vector<std::unique_ptr<IndexMetadata>> indexes;
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

    // Index management ------------------------------------------------------
    // Builds a B+Tree index on table_name.column_name, back-filling it from any
    // rows already present, and registers it on the table. Returns nullptr if
    // the table or column does not exist, or the index name is taken.
    IndexMetadata* CreateIndex(const std::string& index_name,
                               const std::string& table_name,
                               const std::string& column_name);

    // Returns the first index registered on (table_oid, column_index), or
    // nullptr if none exists. Used by the planner for index-aware planning.
    IndexMetadata* GetIndex(table_oid_t table_oid, uint32_t column_index) const;
};

} // namespace minidb