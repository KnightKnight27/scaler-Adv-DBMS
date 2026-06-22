#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "recovery/log_record.h"

namespace minidb {

// Append-only write-ahead log. Records are fixed-size and written in order.
// Flush() forces the log to disk; the WAL protocol requires Flush() before a
// commit is acknowledged and before any data page modified by a txn is written
// back ("write-ahead").
class LogManager {
 public:
  // truncate=true starts a fresh log (used at the start of a run); false opens
  // for append.
  LogManager(const std::string& path, bool truncate);
  ~LogManager();

  void Begin(int32_t txn);
  void Update(int32_t txn, int32_t idx, int32_t before, int32_t after);
  void Commit(int32_t txn);
  void Abort(int32_t txn);

  void Flush();  // fflush + force to disk

  // Read the entire log back (used by recovery).
  static std::vector<LogRecord> ReadAll(const std::string& path);

 private:
  void Append(const LogRecord& r);
  FILE* f_;
};

}  // namespace minidb
