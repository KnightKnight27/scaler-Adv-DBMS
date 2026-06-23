#pragma once
#include <atomic>
#include <mutex>

#include "../storage/disk_manager.h"
#include "log_record.h"

namespace minidb {

// Appends log records to DiskManager's WAL file and assigns each one a
// monotonically increasing LSN. Every Append* call flushes synchronously
// (DiskManager::AppendLogRecord flushes immediately) - that's the WAL
// invariant this project relies on: a log record is durable before the
// operation it describes is considered done.
class WalManager {
 public:
  explicit WalManager(DiskManager *disk_manager) : disk_manager_(disk_manager) {}

  void AppendBegin(txn_id_t txn_id) { Append({NextLsn(), txn_id, LogType::BEGIN, "", {}, ""}); }
  void AppendInsert(txn_id_t txn_id, const std::string &table, RID rid, const std::string &data) {
    Append({NextLsn(), txn_id, LogType::INSERT, table, rid, data});
  }
  void AppendDelete(txn_id_t txn_id, const std::string &table, RID rid, const std::string &before_image) {
    Append({NextLsn(), txn_id, LogType::DELETE, table, rid, before_image});
  }
  void AppendCommit(txn_id_t txn_id) { Append({NextLsn(), txn_id, LogType::COMMIT, "", {}, ""}); }
  void AppendAbort(txn_id_t txn_id) { Append({NextLsn(), txn_id, LogType::ABORT, "", {}, ""}); }

 private:
  int64_t NextLsn() { return next_lsn_++; }
  void Append(const LogRecord &rec) {
    std::string bytes = rec.Serialize();
    int32_t len = static_cast<int32_t>(bytes.size());
    std::string framed;
    framed.append(reinterpret_cast<const char *>(&len), sizeof(len));
    framed.append(bytes);
    disk_manager_->AppendLogRecord(framed.data(), framed.size());
  }

  DiskManager *disk_manager_;
  std::atomic<int64_t> next_lsn_{1};
};

}  // namespace minidb
