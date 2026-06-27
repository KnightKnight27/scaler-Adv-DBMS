#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "common/types.h"

namespace minidb {

enum class LogType : int32_t { BEGIN, INSERT, DELETE, COMMIT, ABORT, CHECKPOINT };

// One write-ahead log record. For INSERT, `row` is the after-image; for DELETE
// it is the before-image (needed to undo a delete during recovery).
struct LogRecord {
  lsn_t lsn = INVALID_LSN;
  LogType type;
  txn_id_t txn = INVALID_TXN_ID;
  std::string table;
  int64_t key = 0;
  std::string row;
};

// Append-only binary write-ahead log. The engine flushes a transaction's
// records before COMMIT returns, so committed transactions survive a crash.
class WAL {
 public:
  explicit WAL(const std::string& path);
  ~WAL();

  lsn_t Append(const LogRecord& rec);  // returns assigned LSN
  void Flush();                        // fsync to disk
  std::vector<LogRecord> ReadAll();    // for recovery
  void Truncate();                     // clear the log (after a checkpoint)

 private:
  std::string path_;
  std::FILE* fp_ = nullptr;
  lsn_t next_lsn_ = 0;
};

}  // namespace minidb
