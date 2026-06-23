#pragma once
#include <memory>
#include <string>
#include <vector>
#include "lsm/manifest.h"
#include "lsm/memtable.h"
#include "lsm/merge_iterator.h"
#include "lsm/sstable_reader.h"
#include "lsm/wal.h"
#include "storage/storage_engine.h"

namespace minidb {

// Tuning knobs for the LSM engine.
struct LsmOptions {
  std::size_t memtable_threshold = 1u << 20;  // bytes before a flush
  int         l0_trigger         = 4;          // SSTables before a compaction
  bool        sync_wal           = true;
};

// LSM-tree storage engine (Track C). Writes go to an in-memory MemTable backed
// by a write-ahead log; when the MemTable fills it is flushed to an immutable,
// sorted SSTable. Reads check the MemTable then SSTables newest-first. When
// enough SSTables accumulate, compaction merges them into one, discarding
// overwritten values and tombstones. It implements the same StorageEngine
// interface as HeapTable, so the executor and benchmark use it unchanged.
class LsmTable : public StorageEngine {
 public:
  explicit LsmTable(std::string dir, LsmOptions options = {});

  void insert(Key key, const Bytes& value) override;
  std::optional<Bytes> get(Key key) override;
  void erase(Key key) override;
  std::unique_ptr<RowCursor> scan() override;
  std::unique_ptr<RowCursor> index_range(Key lo, Key hi) override;
  bool supports_index_scan() const override { return false; }  // LSM reads go through scan/get
  const TableStats& stats() const override { return stats_; }
  void flush() override;  // flush the active MemTable to an SSTable

  // Benchmark helpers (beyond the StorageEngine interface).
  void     force_compact();
  uint64_t disk_bytes() const;  // total bytes across all live SSTables

 private:
  SeqNo       next_seq() { return next_seq_++; }
  void        maybe_flush();
  void        maybe_compact();
  void        flush_active();
  std::string new_sstable_path();
  std::vector<std::unique_ptr<MergeSource>> make_sources();  // memtable + all SSTables
  void        note_key(Key key);

  std::string dir_;
  LsmOptions  opt_;
  MemTable    active_;
  std::vector<std::unique_ptr<SSTableReader>> sstables_;  // oldest -> newest
  Manifest    manifest_;
  LsmWal      wal_;
  TableStats  stats_;
  SeqNo       next_seq_  = 1;
  uint64_t    next_file_ = 1;
};

}  // namespace minidb
