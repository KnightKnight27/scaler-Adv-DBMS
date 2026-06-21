#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lsm/bloom_filter.h"

namespace walterdb {

// A key/value (or tombstone) pair handed to the SSTable writer in sorted order.
struct SSTEntry {
  std::string key;
  std::string value;
  bool tombstone = false;
};

// ---------------------------------------------------------------------------
// SSTable -- a Sorted String Table: an immutable on-disk file of key/value
// entries in key order.  Layout:
//
//   [data]    entries: u32 klen | key | u8 tombstone | u32 vlen | value
//   [index]   sparse index: u32 count, then (u32 klen | key | u64 offset)*  --
//             one entry every SPARSE_INTERVAL keys, so a point lookup binary-
//             searches the small in-memory index then scans only one short run.
//   [bloom]   serialized BloomFilter (skip the file if a key can't be present)
//   [minmax]  min and max key (fast range rejection)
//   [footer]  fixed 40 bytes of section offsets + entry count + magic
//
// Point reads use min/max + bloom to reject, then sparse index + a bounded
// pread.  Full iteration (for compaction / range scans) streams the data region.
// ---------------------------------------------------------------------------
class SSTable {
 public:
  enum class Lookup { Found, Tombstone, Absent };
  static constexpr uint32_t SPARSE_INTERVAL = 16;

  // Build a new SSTable file at `path` from already-sorted entries.
  static void write(const std::string& path, const std::vector<SSTEntry>& sorted);

  explicit SSTable(std::string path);
  ~SSTable();
  SSTable(const SSTable&) = delete;
  SSTable& operator=(const SSTable&) = delete;

  Lookup get(std::string_view key, std::string* out) const;

  // Append entries with keys in [lo, hi) to `out`, reading only the sparse-index
  // blocks that cover the range (so a range scan doesn't read the whole table).
  // Empty lo/hi mean unbounded below/above.
  void collect_range(std::string_view lo, std::string_view hi, std::vector<SSTEntry>& out) const;

  const std::string& min_key() const { return min_key_; }
  const std::string& max_key() const { return max_key_; }
  uint64_t num_entries() const { return num_entries_; }
  uint64_t file_size() const { return file_size_; }
  const std::string& path() const { return path_; }

  // Streaming cursor over all entries in key order (used by compaction and
  // range scans).  Loads the data region into memory on construction.
  class Iterator {
   public:
    explicit Iterator(std::string data);
    bool valid() const { return valid_; }
    std::string_view key() const { return key_; }
    std::string_view value() const { return value_; }
    bool tombstone() const { return tombstone_; }
    void next() { parse(); }

   private:
    void parse();
    std::string data_;
    size_t pos_ = 0;
    std::string_view key_, value_;
    bool tombstone_ = false;
    bool valid_ = false;
  };

  Iterator iterator() const;

 private:
  std::string read_range(uint64_t off, uint64_t len) const;

  std::string path_;
  int fd_ = -1;
  uint64_t file_size_ = 0;
  uint64_t data_end_ = 0;  // == index offset
  uint64_t num_entries_ = 0;
  std::string min_key_, max_key_;
  std::vector<std::pair<std::string, uint64_t>> sparse_;  // sparse index (sorted)
  std::unique_ptr<BloomFilter> bloom_;
};

}  // namespace walterdb
