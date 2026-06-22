#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "schema.hpp"
#include "../common/types.hpp"
#include "../storage/buffer_pool.hpp"
#include "../storage/heap_file.hpp"
#include "../index/bplus_tree.hpp"

namespace minidb {

// One table: its schema, the heap pages that hold its rows, the heap-file
// accessor over those pages, and the in-memory primary-key index.
struct Table {
    std::string name;
    Schema schema;
    std::vector<PageId> pages;
    std::unique_ptr<HeapFile> heap;
    std::unique_ptr<BPlusTree> index;
};

// The table registry. Schemas and per-table page lists are persisted to a
// small text catalog file so they survive restarts; the B+ tree index is
// rebuilt from the heap on load.
class Catalog {
public:
    Catalog(BufferPool& bp, std::string cat_path);

    // Load schemas + page lists from the catalog file and rebuild indexes.
    void load();
    // Persist the current catalog to disk (called on every DDL / page growth).
    void save() const;

    Table* create(const std::string& name, const Schema& schema);
    Table* get(const std::string& name) const;

    const std::unordered_map<std::string, std::unique_ptr<Table>>& tables() const {
        return tables_;
    }

    // Scan the heap and (re)populate a table's primary-key index.
    void rebuildIndex(Table* t) const;

private:
    BufferPool& bp_;
    std::string cat_path_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;

    void wireTable(Table* t);
};

} // namespace minidb
