#pragma once
#include <string>
#include <vector>
#include "common/config.h"

namespace minidb {

enum class LogType { BEGIN, COMMIT, ABORT, INSERT, DELETE };

// One write-ahead log record. For INSERT, `image` is the new row's bytes; for
// DELETE, `image` is the deleted row's bytes (so the change can be undone).
struct LogRecord {
  lsn_t       lsn{INVALID_LSN};
  txn_id_t    txn{INVALID_TXN_ID};
  LogType     type{LogType::BEGIN};
  std::string table;
  int64_t     key{0};
  std::string image;
};

// Append-only write-ahead log. Records are appended and flushed before the
// corresponding change is made to the data pages (the write-ahead rule), so a
// crash can be recovered by replaying this log.
class WAL {
 public:
  explicit WAL(std::string file);
  ~WAL();

  // Append a record (lsn assigned here) and flush it durably to disk.
  lsn_t Append(LogRecord rec);

  // Read every record currently in the log (used during recovery).
  std::vector<LogRecord> ReadAll();

  // Truncate the log after a successful checkpoint/recovery.
  void Reset();

 private:
  void OpenAppend();

  std::string file_;
  lsn_t       next_lsn_{0};
  // (stream is opened per Append to keep the file flushed and crash-safe)
};

}  // namespace minidb
