#pragma once
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#include "lsm/bloom_filter.h"
#include "lsm/memtable.h"

namespace minidb {

// An immutable, sorted, on-disk run of key -> entry produced by flushing a
// MemTable (or by compaction). File layout:
//
//   [count:int64]
//   count × [ key:8 | deleted:1 | vlen:4 | value bytes ]   (sorted by key)
//   [bloom filter blob]
//   [bloom_size:int64]                                     (trailer)
//
// On open, the per-key index and the Bloom filter are loaded into memory;
// values are read from disk on demand. A point lookup checks the Bloom filter
// first to skip files that cannot contain the key.
class SSTable {
 public:
  struct IndexEntry {
    int64_t       key;
    std::streamoff value_off;
    uint32_t      vlen;
    bool          deleted;
  };

  // Write a new SSTable file from sorted entries.
  static void Build(const std::string &path, const std::map<int64_t, LSMEntry> &entries);

  // Open an existing SSTable for reading.
  explicit SSTable(const std::string &path);

  // Point lookup: Bloom filter -> binary search -> disk read. Returns true if
  // this run has a version of the key (possibly a tombstone).
  bool Get(int64_t key, LSMEntry *out);

  // Ordered access for merge/compaction/scan.
  const std::vector<IndexEntry> &index() const { return index_; }
  LSMEntry ReadAt(size_t i);

  size_t  count() const { return index_.size(); }
  int64_t min_key() const { return index_.empty() ? 0 : index_.front().key; }
  int64_t max_key() const { return index_.empty() ? 0 : index_.back().key; }
  const std::string &path() const { return path_; }

 private:
  std::string             path_;
  std::ifstream           in_;
  std::vector<IndexEntry> index_;
  BloomFilter             bloom_;
};

}  // namespace minidb
