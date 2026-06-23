#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// wal.h  –  Write-Ahead Log (append-only binary log)
// ─────────────────────────────────────────────────────────────────────────────
#include "common/types.h"
#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>

namespace minidb {

enum class LogType : uint8_t { BEGIN=0, INSERT=1, DELETE=2, COMMIT=3, ABORT=4 };

/**
 * LogRecord – 20-byte fixed-size WAL entry.
 *
 * On-disk format (no padding surprises):
 *   lsn       (4)  monotonically increasing
 *   txn_id    (4)
 *   type      (1)  LogType
 *   _pad      (3)
 *   record_id (4)  affected row (INSERT / DELETE)
 *   record_val(4)  row value    (INSERT only)
 */
struct LogRecord {
    lsn_t    lsn        = -1;
    txn_id_t txn_id     = -1;
    LogType  type       = LogType::BEGIN;
    uint8_t  _pad[3]    = {};
    int32_t  record_id  = 0;
    int32_t  record_val = 0;
};
static_assert(sizeof(LogRecord) == 20, "LogRecord must be 20 bytes");

/**
 * WAL – append-only write-ahead log.
 * Rules enforced by callers:
 *   1. Log BEFORE applying data changes.
 *   2. Call Flush() before acknowledging COMMIT to the user.
 */
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    lsn_t Append(LogRecord& rec);  // sets rec.lsn before writing
    void  Flush();
    const std::string& Path() const { return path_; }

private:
    std::string   path_;
    std::ofstream out_;
    std::mutex    latch_;
    lsn_t         next_lsn_{0};
};

} // namespace minidb
