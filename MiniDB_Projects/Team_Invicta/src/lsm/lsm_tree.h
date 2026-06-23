#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "lsm/memtable.h"
#include "lsm/sstable.h"

namespace minidb {

// A log-structured merge tree. Writes go to an in-memory MemTable; when it
// fills, it is flushed to a new immutable SSTable. Reads check the MemTable,
// then SSTables newest-to-oldest (first hit wins). Size-tiered compaction
// merges accumulated SSTables into one, discarding overwritten keys and
// tombstones. This is MiniDB's Track C storage engine.
class LSMTree {
 public:
  // `dir` holds this tree's SSTable files. `mem_limit` is the MemTable flush
  // threshold (bytes); `compaction_trigger` is the SSTable count that fires a
  // compaction.
  explicit LSMTree(std::string dir, size_t mem_limit = (1u << 20),
                   size_t compaction_trigger = 4);

  void Put(int64_t key, const std::string &value);
  void Delete(int64_t key);

  // Returns true and sets *value if the key is live (present and not deleted).
  bool Get(int64_t key, std::string *value);

  // Live key/value pairs in key order (merged across MemTable + SSTables).
  std::vector<std::pair<int64_t, std::string>> ScanAll();
  std::vector<std::pair<int64_t, std::string>> Range(int64_t low, int64_t high);

  size_t LiveCount();
  bool   KeyRange(int64_t *min_key, int64_t *max_key);

  void   Flush();                                 // force MemTable -> SSTable
  size_t num_sstables() const { return ssts_.size(); }

 private:
  void        MaybeFlush();
  void        FlushMemTable();
  void        Compact();
  void        LoadExisting();
  std::string NextSSTablePath();
  void        RefreshStats();

  std::string                          dir_;
  size_t                               mem_limit_;
  size_t                               compaction_trigger_;
  MemTable                             mem_;
  std::vector<std::unique_ptr<SSTable>> ssts_;  // oldest -> newest
  uint64_t                             seq_{0};

  // Cached statistics for the optimizer; invalidated on every write.
  bool    stats_valid_{false};
  size_t  live_count_{0};
  int64_t min_key_{0}, max_key_{0};
  bool    has_keys_{false};
};

}  // namespace minidb
