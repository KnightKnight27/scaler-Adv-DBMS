#pragma once
#include <fstream>
#include <string>
#include <utility>
#include <vector>
#include "lsm/sstable.h"

namespace minidb {

// Writes one immutable SSTable. Keys must be added in ascending order. The file
// is built in a ".tmp" sibling and atomically renamed by finish(), so a crash
// never leaves a half-written SSTable that the manifest references.
class SSTableWriter {
 public:
  explicit SSTableWriter(std::string path);

  void        add(Key key, const ValueEntry& entry);  // ascending key order
  SSTableMeta finish();                                 // index + footer, rename, return meta

 private:
  std::string                            path_;
  std::string                            tmp_path_;
  std::ofstream                          out_;
  std::vector<std::pair<Key, uint64_t>>  sparse_;  // (key, file offset)
  uint64_t count_   = 0;
  Key      min_key_ = 0, max_key_ = 0;
  SeqNo    max_seq_ = 0;
};

}  // namespace minidb
