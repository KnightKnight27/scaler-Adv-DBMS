#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "common/types.h"
#include "storage/log_flusher.h"  // LogFlusher and LSN

namespace minidb {

enum class LogType : uint8_t { Begin, Insert, Delete, Commit, Abort };

// One write-ahead log record. For data records we keep both the before-image
// (for undo) and after-image (for redo) of a key, which makes both directions
// simple, idempotent operations on the storage engine.
struct LogRecord {
  LSN     lsn   = kInvalidLSN;
  TxnId   txn   = 0;
  LogType type  = LogType::Begin;
  TableId table = 0;
  Key     key   = 0;
  bool    has_before = false;
  bool    has_after  = false;
  Bytes   before;
  Bytes   after;
};

// Append-only write-ahead log. Records are buffered in memory by append() and
// written to the file by flush_upto(); committing a transaction forces its
// records to disk. Implements LogFlusher so the buffer pool can enforce the
// write-ahead rule on page eviction.
class LogManager : public LogFlusher {
 public:
  explicit LogManager(std::string path, bool truncate = false);

  LSN  append(const LogRecord& record);  // assign an LSN, buffer the record
  void flush_upto(LSN lsn) override;      // persist records with lsn <= target
  void flush_all() { flush_upto(next_lsn_ - 1); }
  LSN  flushed_lsn() const { return flushed_; }

  std::vector<LogRecord> read_all();      // read the whole log back (for recovery)

 private:
  std::string            path_;
  std::ofstream          out_;
  std::mutex             mu_;
  std::vector<LogRecord> buffer_;     // appended but not yet flushed
  LSN                    next_lsn_ = 1;
  LSN                    flushed_  = kInvalidLSN;
};

}  // namespace minidb
