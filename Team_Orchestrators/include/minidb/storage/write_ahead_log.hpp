#pragma once
// Append-only write-ahead log. Each record carries a transaction id and a type;
// Insert/Delete records carry an image of the affected tuple (after-image for
// inserts, before-image for deletes) so recovery can redo committed work and
// undo uncommitted work. NO-FORCE + STEAL buffer policy: durability comes from
// the log, not from forcing heap pages at commit.
#include "minidb/storage/page.hpp"
#include "minidb/storage/storage_engine.hpp"
#include "minidb/types.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace minidb {

using TxnId = uint64_t;

enum class WalType : uint8_t {
  Begin = 1, Insert = 2, Delete = 3, Commit = 4, Abort = 5, Checkpoint = 6
};

struct WalRecord {
  uint64_t lsn = 0;
  TxnId txn = 0;
  WalType type = WalType::Begin;
  TableId table = 0;
  RID rid;
  std::vector<uint8_t> payload;  // after-image (Insert) or before-image (Delete)
};

class WriteAheadLog {
 public:
  explicit WriteAheadLog(const std::string& path);
  ~WriteAheadLog();

  // Appends a record (assigning it the next LSN, returned). Does not flush.
  uint64_t append(const WalRecord& rec);
  void flush();                       // push buffered records to the OS
  std::vector<WalRecord> read_all();  // all records in append order
  void truncate();                    // empty the log (after checkpoint/recovery)

 private:
  std::string path_;
  std::ofstream out_;
  uint64_t next_lsn_ = 1;
};

}  // namespace minidb
