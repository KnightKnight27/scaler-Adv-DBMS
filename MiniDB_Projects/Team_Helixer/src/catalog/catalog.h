#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "catalog/schema.h"
#include "storage/table_heap.h"
#include "index/btree.h"

namespace minidb {

// Per-table runtime metadata: its schema, physical storage (heap), and an
// optional primary-key B+Tree index. The index is only built when the primary
// key column is an INTEGER (our B+Tree key type).
struct TableInfo {
    std::string                name;
    Schema                     schema;
    std::unique_ptr<TableHeap> heap;
    std::unique_ptr<BPlusTree> index;     // primary-key index, may be null
    bool                       has_index{false};
    size_t                     row_count{0}; // live-row statistic for the optimizer
};

// The Catalog is the system's metadata registry. It owns every TableInfo and
// persists a compact description of all tables (name, columns, pk, first page)
// to a sidecar file so the database can be reopened after a crash/shutdown.
// Indexes are NOT persisted; they are rebuilt from the base tables on open.
class Catalog {
public:
    explicit Catalog(BufferPoolManager *bpm) : bpm_(bpm) {}

    // Create a new table. Throws if the name already exists.
    TableInfo *create_table(const std::string &name, const Schema &schema);

    TableInfo *get_table(const std::string &name);
    std::vector<std::string> table_names() const;

    // Persist / load the metadata sidecar (text format, one section per table).
    void save(const std::string &path) const;
    void load(const std::string &path);

    // After data is loaded/recovered, repopulate every primary-key index by
    // scanning its table. Called once at startup.
    void rebuild_indexes();

private:
    // Construct a TableInfo (heap + optional index) from a schema and the
    // table's first page id (INVALID for a brand-new table).
    TableInfo *install(const std::string &name, const Schema &schema, page_id_t first_page);

    BufferPoolManager                                  *bpm_;
    std::map<std::string, std::unique_ptr<TableInfo>>   tables_;
};

} // namespace minidb
