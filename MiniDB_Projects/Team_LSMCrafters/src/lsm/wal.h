#pragma once
#include <fstream>
#include <string>
#include "lsm/lsm_types.h"
#include "lsm/memtable.h"

namespace minidb {

// Write-ahead log for the LSM engine's active MemTable. Each put/delete is
// appended here before the MemTable is updated, so unflushed writes survive a
// reopen. Once the MemTable is flushed to an SSTable, the log is reset. (This is
// the LSM's own durability; the B+Tree engine has a separate WAL in src/txn.)
class LsmWal {
 public:
  LsmWal(std::string path, bool truncate);

  void append_put(Key key, const Bytes& value, SeqNo seq, bool sync);
  void append_del(Key key, SeqNo seq, bool sync);
  void reset();  // truncate after the MemTable has been flushed

  // Replays a log file into a MemTable; returns the highest seq seen.
  static SeqNo replay(const std::string& path, MemTable& into);

 private:
  std::string   path_;
  std::ofstream out_;
};

}  // namespace minidb
