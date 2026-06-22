#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "schema.hpp"
#include "../index/bplus_tree.hpp"
#include "../storage/buffer_pool.hpp"
#include "../storage/disk_manager.hpp"
#include "../storage/heap_file.hpp"

// one table. unique_ptr keeps storage addresses stable.
// destruction order matters: heap, pool (flushes), disk.
struct Table {
    std::string                   name;
    Schema                        schema;
    int                           pk_col = 0;   // pk column (INT)
    std::size_t                   row_count = 0;  // live rows
    std::unique_ptr<DiskManager>  disk;
    std::unique_ptr<BufferPool>   pool;
    std::unique_ptr<HeapFile>     heap;
    std::unique_ptr<BPlusTree>    index;        // pk -> RowID
};

// name -> Table registry, one data file per table
class Catalog {
public:
    explicit Catalog(std::string dir = ".");

    // creates/opens data file, rebuilds index. throws if name taken.
    Table& create_table(const std::string& name, const Schema& schema, int pk_col);

    // nullptr if absent
    Table* get_table(const std::string& name);

private:
    std::string dir_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
};
