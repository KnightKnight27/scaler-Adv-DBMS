// ============================================================================
// lsm_tree.h  --  EXTENSION TRACK C: an LSM-tree key-value storage engine.
//
// An LSM (Log-Structured Merge) tree is the storage design behind RocksDB,
// LevelDB, Cassandra, and many others.  It is optimized for FAST WRITES.
//
// THE BIG IDEA: never do random writes to disk.  Instead:
//   1. All writes go into an in-memory sorted table (the MEMTABLE).  This is
//      O(log n) in RAM and touches no disk -- so writes are very fast.
//   2. When the memtable fills up, it is flushed to disk in one sequential pass
//      as an immutable sorted file: an SSTABLE (Sorted String Table).
//   3. Over time many SSTables pile up.  COMPACTION merges them back together,
//      keeping the newest value for each key and discarding deleted keys.
//
// THE TRADE-OFF (vs a B+ Tree):
//   + Writes are fast and sequential (great for write-heavy workloads).
//   - Reads may have to look in the memtable AND several SSTables ("read
//     amplification"), so point reads can be slower.
//   - The same key can exist in several SSTables until compaction ("space
//     amplification").
//
// DELETES use a TOMBSTONE: a marker entry that says "this key is gone".  It
// shadows older values until compaction physically drops it.
//
// For clarity our keys are 64-bit integers and values are strings.
// ============================================================================
#pragma once

#include "common/common.h"

namespace minidb {

struct LSMEntry {
  int64_t     key;
  string value;
  bool        tombstone;   // true => this key has been deleted
};

struct LSMStats {
  int    num_sstables{0};
  size_t memtable_entries{0};
  size_t sstable_entries{0};   // physical entries across all SSTables
  size_t disk_bytes{0};        // total bytes of SSTable files on disk
  int    flushes{0};
  int    compactions{0};
};

class LSMTree {
 public:
  // memtable_limit  : flush to an SSTable once the memtable holds this many keys
  // compaction_trigger : compact once this many SSTables exist
  LSMTree(string dir, size_t memtable_limit = 1000, int compaction_trigger = 4);

  void put(int64_t key, const string &value);
  bool get(int64_t key, string *out) const;   // false if absent/deleted
  void deleteKey(int64_t key);

  void flush();        // force the current memtable out to an SSTable
  void compact();      // merge all SSTables into one

  LSMStats stats() const;

 private:
  void maybeFlush();
  void writeSSTableFile(int seq, const vector<LSMEntry> &entries);

  // An in-memory image of one on-disk SSTable: entries sorted by key, plus the
  // file path and a sequence number (higher = newer) for merge precedence.
  struct SSTable {
    int                   seq;
    vector<LSMEntry> entries;   // sorted by key
    string           path;
    size_t                bytes;     // file size, for space-amplification stats
  };

  string                       dir_;
  size_t                            memtable_limit_;
  int                               compaction_trigger_;
  map<int64_t, LSMEntry>       memtable_;   // sorted, in RAM
  vector<SSTable>              sstables_;    // newest has the largest seq
  int                               next_seq_{0};
  LSMStats                          stats_;
};

}  // namespace minidb
