#pragma once

#include "common/types.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace minidb {

/*
 * TableInfo: metadata for a single table, including schema, heap file, and index.
 */
struct TableInfo {
    std::string                  name;
    Schema                       schema;
    std::unique_ptr<HeapFile>    heap_file;
    std::unique_ptr<BPlusTree>   primary_index;  // index on first column (primary key)
    int                          pk_col = 0;     // which column is the primary key

    uint32_t GetTupleCount() const {
        return heap_file ? heap_file->GetTupleCount() : 0;
    }
};

/*
 * Catalog: tracks all tables in the database.
 */
class Catalog {
public:
    Catalog() = default;

    // Create a new table. Returns pointer to TableInfo, or nullptr on failure.
    TableInfo* CreateTable(const std::string& name, const Schema& schema,
                           BufferPoolManager* bpm);

    // Look up a table by name
    TableInfo* GetTable(const std::string& name);

    // Get all table names
    std::vector<std::string> GetTableNames() const;

private:
    std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
};

} // namespace minidb
