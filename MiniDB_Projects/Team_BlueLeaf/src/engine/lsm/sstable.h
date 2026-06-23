#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "engine/lsm/bloom_filter.h"
#include "engine/lsm/memtable.h"

namespace minidb {

// An immutable, sorted on-disk run produced by flushing a MemTable (or by
// compaction). On-disk record format, in ascending key order:
//   [int64 key][uint8 tombstone][uint32 row_len][row bytes]
//
// On construction the writer also builds an in-memory key->offset index and a
// Bloom filter (a real LSM uses a sparse block index; a full index keeps this
// simple). A point lookup is: Bloom check -> index lookup -> one disk read.
class SSTable {
public:
    // Flush sorted entries to `path`, returning an open reader. Tombstones are
    // written too (they shadow older values until compaction drops them).
    static std::shared_ptr<SSTable> create(const std::string& path,
                                           const std::map<std::int64_t, LsmEntry>& entries);

    // Reopen an existing SSTable file (rebuilds index + Bloom by scanning it).
    static std::shared_ptr<SSTable> open(const std::string& path);

    // Point lookup. Returns true if the key is present in this table (the entry
    // may be a tombstone, which the caller treats as "deleted").
    bool get(std::int64_t key, LsmEntry& out) const;

    // Visit every entry in ascending key order (for compaction / scans).
    void for_each(const std::function<void(std::int64_t, const LsmEntry&)>& fn) const;

    const std::string& path() const { return path_; }
    std::uint64_t      file_size() const { return file_size_; }
    std::size_t        count() const { return index_.size(); }

private:
    SSTable(std::string path, std::map<std::int64_t, std::uint64_t> index,
            BloomFilter bloom, std::uint64_t file_size)
        : path_(std::move(path)), index_(std::move(index)),
          bloom_(std::move(bloom)), file_size_(file_size) {}

    std::string                            path_;
    std::map<std::int64_t, std::uint64_t>  index_;  // key -> byte offset in file
    BloomFilter                            bloom_;
    std::uint64_t                          file_size_;
};

} // namespace minidb
