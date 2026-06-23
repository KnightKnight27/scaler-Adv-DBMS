#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/lsm/memtable.h"
#include "engine/lsm/sstable.h"
#include "engine/storage_engine.h"

namespace minidb {

// LSM-tree storage engine (Extension Track C). Per table:
//   write: append to the in-memory MemTable; when it fills, flush it as a new
//          immutable SSTable (sorted run). Deletes write tombstones.
//   read:  MemTable first, then SSTables newest -> oldest, each gated by a Bloom
//          filter; the first hit (value or tombstone) wins.
//   compaction: when too many SSTables accumulate, merge them into one,
//          keeping the newest value per key and dropping tombstones — this
//          bounds read amplification and reclaims space (space amplification).
//
// Implements the same StorageEngine interface as the row store, so the benchmark
// drives both identically.
class LsmEngine : public StorageEngine {
public:
    explicit LsmEngine(std::string dir,
                       std::size_t memtable_limit_bytes = 1u << 20,  // 1 MiB
                       std::size_t compaction_trigger   = 4);

    void create_table(const std::string& table, const Schema& schema, int pk_col) override;
    bool put(const std::string& table, std::int64_t key, const std::string& row) override;
    bool get(const std::string& table, std::int64_t key, std::string& out) override;
    bool erase(const std::string& table, std::int64_t key) override;
    std::unique_ptr<Cursor> scan(const std::string& table) override;
    std::unique_ptr<Cursor> range(const std::string& table, std::int64_t lo, std::int64_t hi) override;
    void flush() override;
    EngineStats stats(const std::string& table) override;

    // Force a compaction of one table (used by benchmarks to show its effect).
    void compact(const std::string& table);

private:
    struct LsmTable {
        MemTable                              mem;
        std::vector<std::shared_ptr<SSTable>> ssts;     // newest first
        std::uint64_t                         seq      = 1;
        int                                   next_id  = 0;
    };

    LsmTable& require(const std::string& table);
    void      flush_table(const std::string& table, LsmTable& t);
    std::string sst_path(const std::string& table, int id) const;

    std::string  dir_;
    std::size_t  mem_limit_;
    std::size_t  compaction_trigger_;
    std::unordered_map<std::string, LsmTable> tables_;
};

} // namespace minidb
