#pragma once
#include <memory>
#include <string>
#include <vector>
#include "lsm/memtable.h"
#include "lsm/sstable.h"

namespace minidb {

// Log-structured merge tree (Track C extension). Turns random writes into
// sequential ones:
//   write -> MemTable (in RAM, sorted)
//   full  -> flush MemTable to an immutable SSTable (sorted run on disk)
//   many  -> size-tiered compaction merges runs, dropping shadowed keys/tombstones
//   read  -> MemTable, then SSTables newest->oldest (Bloom-filtered)
class LSMTree {
 public:
  // `prefix` is a path stem; SSTables are "<prefix>_<seq>.sst" plus a manifest.
  explicit LSMTree(const std::string& prefix,
                   size_t memtable_limit = 2000,
                   size_t compaction_trigger = 4);

  void Put(int64_t key, const std::string& value);
  void Delete(int64_t key);
  bool Get(int64_t key, std::string* out);  // false if absent or tombstoned
  std::vector<std::pair<int64_t, std::string>> Scan(int64_t low, int64_t high);

  void Flush();        // force the memtable out to an SSTable
  void Compact();      // force a full size-tiered compaction

  // Stats for benchmarking.
  size_t NumSSTables() const { return ssts_.size(); }
  size_t Flushes() const { return flushes_; }
  size_t Compactions() const { return compactions_; }
  size_t DiskBytes() const;     // total SSTable bytes (for write amplification)
  size_t LiveBytes() const { return live_bytes_; }  // logical data size

 private:
  void MaybeFlush();
  void WriteManifest();
  void LoadManifest();

  std::string prefix_;
  size_t memtable_limit_;
  size_t compaction_trigger_;
  MemTable mem_;
  std::vector<std::unique_ptr<SSTable>> ssts_;  // [0]=oldest ... back()=newest
  int64_t next_seq_ = 0;
  size_t flushes_ = 0;
  size_t compactions_ = 0;
  size_t live_bytes_ = 0;
};

}  // namespace minidb
