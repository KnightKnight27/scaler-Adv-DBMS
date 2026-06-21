#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace walterdb {

// One logical entry: a value, or a tombstone marking a deletion (which must
// still shadow older values in SSTables until compaction physically drops it).
struct MemEntry {
  std::string value;
  bool tombstone = false;
};

// ---------------------------------------------------------------------------
// MemTable -- the in-memory, sorted write buffer at the top of the LSM.  Recent
// puts/deletes land here first (after the WAL); when it exceeds a size
// threshold it is flushed, sorted, to a new immutable SSTable.
//
// A std::map gives sorted iteration for free (cheap to flush in key order and to
// merge during compaction).  A skip list would be more "authentic" but a
// balanced tree is correct and simple -- a stated trade-off.
// ---------------------------------------------------------------------------
class MemTable {
 public:
  enum class Lookup { Found, Tombstone, Absent };

  void put(std::string_view key, std::string_view value);
  void remove(std::string_view key);  // writes a tombstone

  Lookup get(std::string_view key, std::string* out) const;

  size_t size_bytes() const { return bytes_; }
  size_t count() const { return table_.size(); }
  bool empty() const { return table_.empty(); }
  void clear();

  // Sorted view for flushing / merging.
  const std::map<std::string, MemEntry, std::less<>>& entries() const { return table_; }

 private:
  std::map<std::string, MemEntry, std::less<>> table_;
  size_t bytes_ = 0;  // rough in-memory footprint, drives the flush threshold
};

}  // namespace walterdb
