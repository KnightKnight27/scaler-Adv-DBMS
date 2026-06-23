#pragma once
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "lsm/sstable.h"

namespace minidb {

// Reads an immutable SSTable. On construction it loads the footer and the
// sparse index into memory. A point get() seeks via the sparse index then scans
// a short run of records. iterate() returns a sequential cursor used by scans
// and compaction.
class SSTableReader {
 public:
  explicit SSTableReader(std::string path);

  std::optional<ValueEntry> get(Key key) const;
  const SSTableMeta& meta() const { return meta_; }

  // Sequential cursor over the data block (its own file handle).
  class Iter {
   public:
    Iter(const std::string& path, uint64_t data_end);
    bool next(Key& key, ValueEntry& entry);
   private:
    std::ifstream in_;
    uint64_t      data_end_;
  };
  Iter iterate() const { return Iter(meta_.path, index_offset_); }

 private:
  SSTableMeta                            meta_;
  uint64_t                               index_offset_ = 0;
  std::vector<std::pair<Key, uint64_t>>  sparse_;
  mutable std::ifstream                  in_;  // reused by get() (single-threaded)
};

}  // namespace minidb
