// MiniDB - Catalog: the in-memory registry of tables (name -> schema + heap file + stats).
// The indexing milestone (M2) extends TableInfo with B+Tree indexes; kept out here so the
// storage layer compiles and demos on its own.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../common/schema.h"
#include "../storage/buffer_pool.h"
#include "../storage/heap_file.h"

namespace minidb {

class BPlusTree;  // attached per-column in M2 (index/)

struct IndexInfo {
    std::string column;
    int col_idx = -1;
    bool unique = false;
    std::shared_ptr<BPlusTree> tree;  // shared_ptr: no complete-type needed at this header
};

struct TableInfo {
    std::string name;
    Schema schema;
    std::unique_ptr<HeapFile> heap;
    std::vector<IndexInfo> indexes;
    uint64_t num_rows = 0;  // running cardinality estimate for the optimizer

    IndexInfo* FindIndexOn(int col_idx) {
        for (auto& ix : indexes)
            if (ix.col_idx == col_idx) return &ix;
        return nullptr;
    }
};

class Catalog {
public:
    explicit Catalog(BufferPool* bp) : bp_(bp) {}

    TableInfo* CreateTable(const std::string& name, const Schema& schema);
    TableInfo* GetTable(const std::string& name);
    std::vector<std::string> ListTables() const;

    // Defined in index/ (needs the full BPlusTree type). Builds and populates an index.
    IndexInfo* CreateIndex(const std::string& table, const std::string& column, bool unique);

    BufferPool* buffer_pool() { return bp_; }

private:
    BufferPool* bp_;
    std::map<std::string, std::unique_ptr<TableInfo>> tables_;
};

}  // namespace minidb
