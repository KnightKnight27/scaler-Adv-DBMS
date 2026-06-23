#include "txn/recovery.h"
#include <unordered_set>

namespace minidb {

int RecoveryManager::recover() {
  std::vector<LogRecord> records = log_.read_all();

  // Analysis: which transactions committed?
  std::unordered_set<TxnId> committed;
  for (const LogRecord& r : records)
    if (r.type == LogType::Commit) committed.insert(r.txn);

  // Redo: replay every committed data operation, in log order.
  for (const LogRecord& r : records) {
    if (r.table != table_id_ || !committed.count(r.txn)) continue;
    if (r.type == LogType::Insert)      table_.insert(r.key, r.after);
    else if (r.type == LogType::Delete) table_.erase(r.key);
  }

  // Undo: reverse every uncommitted data operation, newest first.
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    const LogRecord& r = *it;
    if (r.table != table_id_ || committed.count(r.txn)) continue;
    if (r.type == LogType::Insert) {
      if (r.has_before) table_.insert(r.key, r.before);
      else              table_.erase(r.key);
    } else if (r.type == LogType::Delete) {
      if (r.has_before) table_.insert(r.key, r.before);
    }
  }

  table_.flush();
  return static_cast<int>(committed.size());
}

}  // namespace minidb
