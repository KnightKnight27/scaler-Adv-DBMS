#pragma once
#include <string>
#include <vector>
#include "lsm/lsm_types.h"

namespace minidb {

// The single source of truth for an LSM table's on-disk state: the list of live
// SSTable files (oldest first) plus the next sequence and file numbers. It is a
// small human-readable text file, rewritten atomically (temp + rename) on every
// flush and compaction.
class Manifest {
 public:
  explicit Manifest(std::string path) : path_(std::move(path)) {}

  void load();        // populate from disk if the file exists
  void save() const;  // atomically overwrite the file

  std::vector<std::string> sstables;     // file paths, oldest -> newest
  SeqNo                    next_seq  = 1;
  uint64_t                 next_file = 1;

 private:
  std::string path_;
};

}  // namespace minidb
