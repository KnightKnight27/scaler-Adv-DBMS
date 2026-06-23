#pragma once
#include <string>
#include <vector>
#include "lsm/sstable.h"
#include "lsm/sstable_reader.h"

namespace minidb {

// Compaction strategy: size-tiered "merge everything". All current SSTables are
// merged into a single new SSTable. Because nothing older survives the merge,
// it is safe to drop both overwritten versions and tombstones, which is what
// reclaims space. (A leveled scheme would only be able to drop tombstones at
// the bottom level.)
class Compactor {
 public:
  static SSTableMeta compact(const std::vector<SSTableReader*>& inputs,
                             const std::string& out_path);
};

}  // namespace minidb
