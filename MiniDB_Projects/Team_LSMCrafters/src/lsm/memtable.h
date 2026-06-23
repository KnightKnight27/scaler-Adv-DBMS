#pragma once
#include <map>
#include <optional>
#include "lsm/lsm_types.h"

namespace minidb {

// In-memory write buffer for the LSM engine. A sorted map keeps entries ordered
// so the table can be flushed to a sorted SSTable and scanned in key order. An
// update or delete is just a newer entry for the key; older versions may still
// live in SSTables until compaction removes them.
class MemTable {
 public:
  void put(Key key, Bytes value, SeqNo seq);
  void del(Key key, SeqNo seq);
  std::optional<ValueEntry> get(Key key) const;

  std::size_t approx_bytes() const { return bytes_; }
  bool        empty() const { return table_.empty(); }
  void        clear() { table_.clear(); bytes_ = 0; }

  const std::map<Key, ValueEntry>& entries() const { return table_; }

 private:
  std::map<Key, ValueEntry> table_;
  std::size_t               bytes_ = 0;
};

}  // namespace minidb
