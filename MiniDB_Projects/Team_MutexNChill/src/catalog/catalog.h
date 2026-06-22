#pragma once
#include "../storage/heap_file.h"
#include "../index/bplus_tree.h"
#include <string>
#include <map>

// Stats about one column — used by the optimizer to estimate
// how many rows a WHERE predicate will filter out.
struct ColStats {
    int min_val = 0;
    int max_val = 0;
    int distinct = 0; // number of distinct values seen
};

// Everything the system knows about one table.
struct TableInfo {
    std::string name;
    int         row_count  = 0;
    int         page_count = 1;
    ColStats    id_stats;
    ColStats    age_stats;

    // Owning pointers — created when the table is registered.
    HeapFile*  heap = nullptr;
    BPlusTree* index = nullptr; // primary-key index on 'id'
};

// Catalog is a global registry of all tables.
// It owns the HeapFile and BPlusTree for each table.
class Catalog {
public:
    Catalog() = default;
    ~Catalog();

    // Register a new table. Creates its heap file (filename = name + ".db")
    // and an empty B+ Tree index.
    void createTable(const std::string& name);

    // Look up a table. Returns nullptr if not found.
    TableInfo* getTable(const std::string& name);

    // Update statistics after an insert.
    void recordInsert(const std::string& table, const Row& row);

    // Update statistics after a delete.
    void recordDelete(const std::string& table);

private:
    std::map<std::string, TableInfo*> tables;
};
