#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "lsm/memtable.h"
#include "lsm/bloom_filter.h"

namespace minidb {

// An immutable, sorted-on-disk run produced by flushing a memtable (or by
// compaction). File format is a sequence of records, ascending by key:
//   int64 key | uint8 tombstone | int32 vlen | value[vlen]
// On open we scan once to build an in-memory offset index and a Bloom filter,
// so a point lookup is: bloom check -> index lookup -> single pread.
class SSTable {
 public:
  // Write a sorted run from key->value entries. Returns bytes written.
  static size_t Write(const std::string& path,
                      const std::vector<std::pair<int64_t, LsmValue>>& sorted);

  explicit SSTable(const std::string& path);  // opens + indexes an existing run

  bool Get(int64_t key, LsmValue* out) const;
  std::vector<std::pair<int64_t, LsmValue>> ReadAll() const;

  const std::string& Path() const { return path_; }
  size_t FileBytes() const { return file_bytes_; }
  size_t Count() const { return index_.size(); }

 private:
  std::string path_;
  std::unordered_map<int64_t, long> index_;  // key -> byte offset
  BloomFilter bloom_;
  size_t file_bytes_ = 0;
};

}  // namespace minidb
