// LSM-Tree: a write-optimized key-value store (Extension Track C).
//
// Writes are appended to an in-memory MemTable (O(1) amortized, no random disk
// writes). When the MemTable fills, it is flushed as a new immutable SSTable.
// Reads consult the MemTable first, then SSTables newest -> oldest, stopping at
// the first hit (value or tombstone). When SSTables accumulate, compaction
// k-way-merges them into one run, keeping the newest value per key and dropping
// tombstones -- bounding read amplification and reclaiming space.
//
// This contrasts with the B+ Tree heap-store (the core engine): the LSM turns
// random writes into sequential ones, trading some read/space amplification for
// much higher write throughput. The benchmark quantifies that trade-off.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lsm/memtable.hpp"
#include "lsm/sstable.hpp"

namespace minidb {

struct LSMOptions {
    size_t memtable_bytes = 1 << 20;   // 1 MiB before a flush
    size_t compaction_trigger = 4;     // # SSTables before compaction
};

class LSMTree {
public:
    using Options = LSMOptions;

    LSMTree(std::string dir, LSMOptions opts = LSMOptions());

    void put(const std::string& key, const std::string& value);
    void del(const std::string& key);
    std::optional<std::string> get(const std::string& key);   // nullopt if absent/deleted

    void flush();        // force the active MemTable to an SSTable
    void compact();      // merge all SSTables into one
    void maybe_flush();
    void maybe_compact();

    struct Stats {
        size_t memtable_entries = 0;
        size_t num_sstables = 0;
        size_t flushes = 0;
        size_t compactions = 0;
    };
    Stats stats() const;
    size_t num_sstables() const { return sstables_.size(); }

private:
    std::string next_sstable_path();

    std::string dir_;
    Options opts_;
    MemTable mem_;
    std::vector<std::unique_ptr<SSTable>> sstables_;   // oldest -> newest
    int next_id_ = 0;
    size_t flushes_ = 0;
    size_t compactions_ = 0;
};

}  // namespace minidb
