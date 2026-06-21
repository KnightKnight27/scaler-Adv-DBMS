#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine/storage_engine.h"
#include "lsm/memtable.h"
#include "lsm/sstable.h"

namespace walterdb {

// ===========================================================================
// LSMEngine -- Track C: a log-structured merge-tree storage engine implementing
// the shared StorageEngine interface, so it can be A/B benchmarked against the
// heap-file + B+tree engine through the exact same API.
//
// Write path:  append to the WAL -> insert into the in-memory MemTable.  When
//   the MemTable exceeds a size threshold it is flushed, in sorted order, to a
//   new immutable SSTable and the WAL is truncated.  Writes are therefore
//   sequential appends -- no in-place random page writes -- which is where LSM
//   wins write throughput.
// Read path:   MemTable first, then SSTables newest -> oldest (newer data
//   shadows older); the first hit wins, a tombstone means "deleted".  Each
//   SSTable's bloom filter + min/max keys let most lookups skip it entirely.
// Delete:      a tombstone write (same as a put); the obsolete value is
//   physically dropped later, during compaction.
// Compaction:  size-tiered -- when the SSTable count reaches a threshold they
//   are merged into one (newest value per key wins, tombstones dropped), which
//   bounds read amplification and reclaims space.
//
// On open, the WAL is replayed into the MemTable (crash recovery for writes that
// hadn't been flushed yet) -- the same write-ahead-logging principle as the
// relational recovery module.
// ===========================================================================
class LSMEngine : public StorageEngine {
 public:
  explicit LSMEngine(std::string base_path, size_t memtable_threshold_bytes = (1u << 20));
  ~LSMEngine() override;

  // StorageEngine interface.
  Status put(std::string_view key, std::string_view value) override;
  std::optional<std::string> get(std::string_view key) override;
  Status remove(std::string_view key) override;
  std::unique_ptr<KVIterator> scan(std::string_view lo, std::string_view hi) override;
  void flush() override;
  std::string name() const override { return "LSM"; }

  // Track-C specifics used by the benchmark / tests.
  void compact();                                  // merge all SSTables into one
  size_t num_sstables() const { return sstables_.size(); }
  uint64_t disk_size() const;                      // total on-disk bytes (SSTables + WAL)
  uint64_t bytes_written() const { return bytes_written_; }  // cumulative physical writes

  // Auto-compaction fires once the SSTable count reaches this many.  Raise it
  // (e.g. to measure pre-compaction storage amplification) or lower it to keep
  // read amplification tight.
  void set_compaction_threshold(size_t n) { compaction_threshold_ = n; }

  static constexpr size_t COMPACTION_THRESHOLD = 4;

 private:
  void flush_memtable();        // MemTable -> new SSTable (+ truncate WAL)
  void load_sstables();         // discover and open existing SSTables on start
  void replay_wal();            // rebuild MemTable from the WAL on start
  void wal_append(uint8_t op, std::string_view key, std::string_view value);
  std::string sstable_path(uint64_t seq) const;

  std::string base_path_;
  std::string dir_;
  std::string basename_;
  size_t threshold_;

  MemTable memtable_;
  std::vector<std::unique_ptr<SSTable>> sstables_;  // index 0 = newest
  uint64_t next_seq_ = 0;
  int wal_fd_ = -1;
  size_t compaction_threshold_ = COMPACTION_THRESHOLD;
  uint64_t bytes_written_ = 0;
};

}  // namespace walterdb
