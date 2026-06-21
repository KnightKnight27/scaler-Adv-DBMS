#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "common/config.h"

namespace walterdb {

// Types of WAL record.  WALterDB logs LOGICAL operations (row images keyed by
// primary key) rather than physical page diffs -- combined with idempotent
// replay (upsert / delete-by-key) this keeps redo/undo simple and avoids
// page-LSN bookkeeping across the heterogeneous page types.
enum class LogType : uint8_t {
  Begin = 1,
  Insert = 2,   // row_image = the inserted tuple
  Delete = 3,   // row_image = the deleted tuple (needed to undo)
  Commit = 4,
  Abort = 5,
};

struct LogRecord {
  lsn_t lsn = INVALID_LSN;
  LogType type = LogType::Begin;
  txn_id_t txn = INVALID_TXN_ID;
  uint32_t table_id = 0;
  std::string row_image;  // empty for Begin/Commit/Abort
};

// ---------------------------------------------------------------------------
// WalManager -- the write-ahead log: an append-only, self-framing record
// stream backing crash recovery.
//
//   * append on every modification (logged BEFORE the change can reach disk;
//     see BufferPool's pre-flush sync hook),
//   * fsync at COMMIT so a committed transaction is durable even if its data
//     pages are still in the buffer pool (NO-FORCE),
//   * read_all() replays the stream during recovery; a torn trailing record
//     (a write interrupted by a crash) is detected by its length frame and
//     dropped,
//   * truncate() clears the log at a clean checkpoint (the data file is then
//     authoritative, so the log is no longer needed).
// ---------------------------------------------------------------------------
class WalManager {
 public:
  explicit WalManager(std::string path);
  ~WalManager();

  WalManager(const WalManager&) = delete;
  WalManager& operator=(const WalManager&) = delete;

  lsn_t log_begin(txn_id_t txn);
  lsn_t log_insert(txn_id_t txn, uint32_t table_id, std::string_view row);
  lsn_t log_delete(txn_id_t txn, uint32_t table_id, std::string_view row);
  lsn_t log_commit(txn_id_t txn);
  lsn_t log_abort(txn_id_t txn);

  void sync();                          // fsync the log to stable storage
  std::vector<LogRecord> read_all();    // parse the whole stream (for recovery)
  void truncate();                      // drop all records (checkpoint)

  bool empty() const { return next_lsn_ == 0; }

 private:
  lsn_t append(LogType type, txn_id_t txn, uint32_t table_id, std::string_view row);

  std::string path_;
  int fd_ = -1;
  lsn_t next_lsn_ = 0;
  std::mutex latch_;  // serialises appends/sync from concurrent transactions
};

}  // namespace walterdb
