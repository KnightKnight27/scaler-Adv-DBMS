// The catalog: the database's memory of what tables exist.
//
// For each table it holds the Schema, the HeapFile (where rows live), and the
// in-memory B+ tree index on the primary key. The list of tables and their
// schemas is persisted to a small text file; on startup the catalog reloads
// it, reopens each heap file, and rebuilds every index by scanning the rows
// back in. That is why tables survive a restart.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "btree.h"
#include "storage.h"
#include "types.h"

namespace minidb {

struct TableInfo {
    std::string name;
    Schema schema;
    std::unique_ptr<DiskManager> disk;
    std::unique_ptr<BufferPool> pool;
    std::unique_ptr<HeapFile> heap;
    std::unique_ptr<BPlusTree> index;  // null if the first column is not INT
    long rowCount = 0;

    bool isIndexed() const { return index != nullptr; }
};

class Catalog {
public:
    explicit Catalog(const std::string& dataDir = "team_cash_data");

    bool exists(const std::string& name) const;
    TableInfo* get(const std::string& name);
    TableInfo* createTable(const std::string& name, const Schema& schema);

    void onInsert(TableInfo* t, const Value& key, RID rid);
    void onDelete(TableInfo* t, const Value& key);

    void flush();  // write back dirty pages of every table
    std::vector<std::string> tableNames() const;

private:
    std::string dataDir_;
    std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;

    std::string catalogPath() const;
    std::string heapPath(const std::string& name) const;
    std::unique_ptr<TableInfo> openTable(const std::string& name, const Schema& schema);
    void save() const;
    void load();
};

}  // namespace minidb
