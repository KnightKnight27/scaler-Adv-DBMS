#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "catalog/catalog.h"
#include "engine/storage_engine.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace minidb {

// The classic row store: each table is a heap file (rows by RID) plus a primary
// B+Tree mapping the int64 primary key -> RID.
//   put   = heap.insert -> B+Tree.insert(key, rid)
//   get   = B+Tree.search(key) -> heap.get(rid)
//   scan  = heap sequential scan        (the optimizer's "table scan")
//   range = B+Tree leaf-chain walk      (the optimizer's "index scan")
// This is the baseline the LSM engine (M5) is benchmarked against.
class RowStoreEngine : public StorageEngine {
public:
    RowStoreEngine(Catalog* catalog, BufferPool* bp, DiskManager* disk)
        : catalog_(catalog), bp_(bp), disk_(disk) {}

    void create_table(const std::string& table, const Schema& schema, int pk_col) override;
    bool put(const std::string& table, std::int64_t key, const std::string& row) override;
    bool get(const std::string& table, std::int64_t key, std::string& out) override;
    bool erase(const std::string& table, std::int64_t key) override;
    std::unique_ptr<Cursor> scan(const std::string& table) override;
    std::unique_ptr<Cursor> range(const std::string& table, std::int64_t lo, std::int64_t hi) override;
    void flush() override;
    EngineStats stats(const std::string& table) override;

private:
    TableInfo& require(const std::string& table);

    Catalog*     catalog_;
    BufferPool*  bp_;
    DiskManager* disk_;
    // In-memory hint of each heap's tail page, so inserts append in O(1) instead
    // of re-walking the page chain.
    std::unordered_map<std::string, PageId> heap_tail_;
};

} // namespace minidb
