// ============================================================================
// log_manager.h  --  Appends records to the WAL file and flushes them to disk.
//
// Records are first buffered in memory (fast) and written to disk in a batch by
// flush().  flush() is called at two moments to keep the WAL rule:
//   1. By the buffer pool, just before it writes any dirty data page to disk.
//   2. At COMMIT, so a committed transaction is durable even if the data pages
//      it touched are still only in memory.
// ============================================================================
#pragma once

#include "common/common.h"
#include "recovery/log_record.h"

namespace minidb {

class LogManager {
 public:
  explicit LogManager(const string &wal_file);
  ~LogManager();

  // Assign the next LSN to `rec`, buffer it, and return that LSN.
  lsn_t append(LogRecord rec);

  // Write all buffered records to the WAL file and flush to the OS.
  void flush();

  // Read the entire WAL from disk (used by recovery at startup).
  vector<LogRecord> readAll();

 private:
  string  wal_file_;
  ofstream out_;            // append handle
  string  buffer_;          // serialized records not yet flushed
  lsn_t        next_lsn_{0};
  mutex   latch_;
};

}  // namespace minidb
