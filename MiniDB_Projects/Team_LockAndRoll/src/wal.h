#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "common.h"
#include "storage.h"

namespace minidb {

enum class LogType : uint8_t { BEGIN, COMMIT, ABORT, INSERT, DELETE, CHECKPOINT };

struct LogRecord {
  LogType type;
  lsn_t lsn = INVALID_LSN;
  txn_id_t txn = INVALID_TXN;

  file_id_t file_id = -1;
  page_id_t page_id = INVALID_PAGE_ID;
  slot_id_t slot = -1;
  uint16_t old_off = 0, old_len = 0;
  uint16_t new_off = 0, new_len = 0;
  std::vector<uint8_t> old_bytes;
  std::vector<uint8_t> new_bytes;
};

// implements LogFlusher so the buffer pool can flush log before page
class LogManager : public LogFlusher {
 public:
  explicit LogManager(std::string path);
  ~LogManager() override;

  // assigns and returns the LSN
  lsn_t append(LogRecord rec);

  lsn_t log_begin(txn_id_t txn);
  lsn_t log_commit(txn_id_t txn);
  lsn_t log_abort(txn_id_t txn);
  lsn_t log_insert(txn_id_t txn, file_id_t fid, page_id_t pid, slot_id_t slot,
                   uint16_t off, const std::vector<uint8_t>& bytes);
  lsn_t log_delete(txn_id_t txn, file_id_t fid, page_id_t pid, slot_id_t slot,
                   uint16_t off, const std::vector<uint8_t>& bytes);

  void flush_to(lsn_t lsn) override;
  void flush();

  // when false, COMMIT isn't fsync'd right away: faster, but a crash may lose
  // the most recently committed txns
  void set_sync_on_commit(bool on) { sync_on_commit_ = on; }
  bool sync_on_commit() const { return sync_on_commit_; }

  std::vector<LogRecord> read_all();

  lsn_t last_lsn() const { return next_lsn_ - 1; }

 private:
  static std::vector<uint8_t> encode(const LogRecord& r);

  std::string path_;
  int fd_ = -1;
  lsn_t next_lsn_ = 1;       // LSN 0 is reserved; fresh pages have page_lsn 0
  lsn_t persisted_lsn_ = 0;
  bool sync_on_commit_ = true;
  std::vector<uint8_t> pending_;
  std::mutex latch_;
};

class RecoveryManager {
 public:
  RecoveryManager(BufferPool* bpool, LogManager* log) : bpool_(bpool), log_(log) {}

  // files referenced by the log must already be open in the DiskManager
  size_t recover();

 private:
  BufferPool* bpool_;
  LogManager* log_;
};

}  // namespace minidb
