// Catalog: the database's metadata directory.
//
// Holds, for every table: its schema, the heap file's first page id, the
// in-memory primary-key B+ tree, and lightweight statistics for the optimizer.
// Metadata (names, schemas, root page ids) is persisted to a small sidecar
// file; the primary-key index is rebuilt from the heap on open.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog/schema.hpp"
#include "index/bplus_tree.hpp"

namespace minidb {

struct TableInfo {
    int         table_id;
    std::string name;
    Schema      schema;
    PageId      first_page_id;
    std::unique_ptr<BPlusTree> pk_index;   // null if table has no primary key

    // Statistics maintained for the cost-based optimizer.
    size_t  row_count = 0;
    int64_t min_key = 0;       // only meaningful for INTEGER primary keys
    int64_t max_key = 0;
    bool    key_range_valid = false;
};

class Catalog {
public:
    // Register a new table; takes ownership of its TableInfo.
    TableInfo* add_table(std::unique_ptr<TableInfo> info) {
        TableInfo* ptr = info.get();
        name_to_table_[info->name] = ptr;
        tables_.push_back(std::move(info));
        return ptr;
    }

    TableInfo* get_table(const std::string& name) const {
        auto it = name_to_table_.find(name);
        return it == name_to_table_.end() ? nullptr : it->second;
    }

    const std::vector<std::unique_ptr<TableInfo>>& tables() const { return tables_; }
    int next_table_id() const { return static_cast<int>(tables_.size()); }

    // Persist / load table metadata (not the index) to a sidecar file.
    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    std::vector<std::unique_ptr<TableInfo>> tables_;
    std::unordered_map<std::string, TableInfo*> name_to_table_;
};

}  // namespace minidb
