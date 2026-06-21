// ============================================================================
//  catalog.hpp — The database's metadata: what tables exist and how they're
//  shaped. One TableInfo per table bundles the three things every access path
//  needs: the Schema (column types), the TableHeap (where rows live), and the
//  primary-key B+ tree index (the fast lookup path).
//
//  In a production DB the catalog is itself stored in system tables on disk.
//  Here it lives in memory and is repopulated when tables are (re)created — a
//  documented simplification consistent with the in-memory index decision.
// ============================================================================
#pragma once

#include "../index/bplus_tree.hpp"
#include "../record/table_heap.hpp"
#include "../record/tuple.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace minidb {

struct TableInfo {
    std::string name;
    Schema      schema;
    int         pk_index;                     // which column is the primary key (col 0)
    size_t      num_rows = 0;                  // live row count — fed to the optimizer's cost model
    std::unique_ptr<TableHeap> heap;
    std::unique_ptr<BPlusTree> index;         // primary key -> RID

    // Extract the primary-key value of a tuple as the int64 the index keys on.
    // (Primary key is required to be the first column and of type INT here.)
    int64_t pk_of(const Tuple& t) const { return t.values[pk_index].i; }
};

class Catalog {
public:
    explicit Catalog(BufferPoolManager* bpm) : bpm_(bpm) {}

    // Define a new table. First column is treated as the INT primary key.
    TableInfo* create_table(const std::string& name, const Schema& schema) {
        if (tables_.count(name)) throw std::runtime_error("table already exists: " + name);
        if (schema.columns.empty() || schema.columns[0].type != ColType::INT)
            throw std::runtime_error("first column must be an INT primary key");

        auto info = std::make_unique<TableInfo>();
        info->name   = name;
        info->schema = schema;
        info->pk_index = 0;
        info->heap  = std::make_unique<TableHeap>(bpm_, TableHeap::create(bpm_));
        info->index = std::make_unique<BPlusTree>();

        TableInfo* raw = info.get();
        tables_[name] = std::move(info);
        return raw;
    }

    TableInfo* get_table(const std::string& name) {
        auto it = tables_.find(name);
        if (it == tables_.end()) throw std::runtime_error("no such table: " + name);
        return it->second.get();
    }

    bool has_table(const std::string& name) const { return tables_.count(name) > 0; }

private:
    BufferPoolManager* bpm_;
    std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
};

}  // namespace minidb
