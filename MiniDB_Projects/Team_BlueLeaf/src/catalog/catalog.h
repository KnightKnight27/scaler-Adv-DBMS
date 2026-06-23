#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "catalog/schema.h"
#include "common/types.h"
#include "storage/buffer_pool.h"

namespace minidb {

// Metadata for one index: which column it keys on and the page id of its B+Tree
// root (which can change as the tree grows, so it is kept up to date here).
struct IndexInfo {
    std::string name;
    int         key_col;
    PageId      root;
    bool        primary;
};

// Metadata for one table: its schema, the heap's first page, and its indexes.
struct TableInfo {
    std::string            name;
    Schema                 schema;
    int                    pk_col     = -1;
    PageId                 heap_first = INVALID_PAGE_ID;
    std::vector<IndexInfo> indexes;

    const IndexInfo* primary_index() const {
        for (const auto& ix : indexes) if (ix.primary) return &ix;
        return nullptr;
    }
};

// The Catalog owns table/index metadata and persists it to a small sidecar file
// so a database survives across runs. Creating a table also allocates its heap
// and (if it has a primary key) its primary B+Tree.
class Catalog {
public:
    Catalog(BufferPool* bp, std::string meta_path);

    TableInfo* create_table(const std::string& name, const Schema& schema, int pk_col);
    TableInfo* get_table(const std::string& name);
    bool       has_table(const std::string& name) const { return tables_.count(name) > 0; }
    std::vector<std::string> table_names() const;

    // Record a new B+Tree root for an index (after a root split) and persist.
    void update_index_root(const std::string& table, const std::string& index, PageId new_root);

    void save() const;

private:
    void load();

    BufferPool*                                 bp_;
    std::string                                 meta_path_;
    std::unordered_map<std::string, TableInfo>  tables_;
};

} // namespace minidb
