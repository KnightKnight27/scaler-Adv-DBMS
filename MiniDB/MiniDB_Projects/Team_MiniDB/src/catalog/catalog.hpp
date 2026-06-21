#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "schema.hpp"
#include "../index/bplus_tree.hpp"
#include "../storage/buffer_pool.hpp"
#include "../storage/disk_manager.hpp"
#include "../storage/heap_file.hpp"

// Everything the engine needs to operate on one table. The storage objects are
// held by unique_ptr so their addresses are stable: BufferPool holds a
// DiskManager& and HeapFile holds both — those references stay valid because
// the pointed-to objects never move, even as the Table is stored in the map.
// Declaration order matters for destruction: heap, then pool (flushes), then
// disk — so the pool's flush-on-destroy still has a live DiskManager.
struct Table {
    std::string                   name;
    Schema                        schema;
    int                           pk_col = 0;   // primary-key column (must be INT)
    std::unique_ptr<DiskManager>  disk;
    std::unique_ptr<BufferPool>   pool;
    std::unique_ptr<HeapFile>     heap;
    std::unique_ptr<BPlusTree>    index;        // primary key -> RowID
};

// The catalog is the name → Table registry. Each table is its own data file
// (`<dir>/<name>.db`). Opening a table whose file already has data rebuilds the
// in-memory PK index by scanning the heap.
class Catalog {
public:
    explicit Catalog(std::string dir = ".");

    // Register a table. Creates/opens its data file and (re)builds its index.
    // Throws if the table name is already registered this session.
    Table& create_table(const std::string& name, const Schema& schema, int pk_col);

    // Look up a registered table, or nullptr.
    Table* get_table(const std::string& name);

private:
    std::string dir_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
};
