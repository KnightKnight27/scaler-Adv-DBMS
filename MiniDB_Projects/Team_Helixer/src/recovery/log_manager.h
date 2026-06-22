#pragma once
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "common/types.h"
#include "catalog/schema.h"

namespace minidb {

// Write-Ahead Log record kinds. Every state change is logged BEFORE the change
// is considered durable (WAL rule). Recovery replays committed transactions.
enum class LogType : uint8_t {
    BEGIN, COMMIT, ABORT, CREATE_TABLE, INSERT, DELETE, CHECKPOINT
};

// A single log record. Not all fields are used by every type:
//   BEGIN/COMMIT/ABORT/CHECKPOINT : type, lsn, txn
//   CREATE_TABLE                  : + table, schema
//   INSERT / DELETE               : + table, tuple_bytes (schema-serialized row)
struct LogRecord {
    LogType           type;
    lsn_t             lsn{INVALID_LSN};
    txn_id_t          txn{INVALID_TXN_ID};
    std::string       table;
    Schema            schema;       // CREATE_TABLE only
    std::vector<char> tuple_bytes;  // INSERT/DELETE only
};

// The LogManager owns the append-only log file. append() assigns the next LSN
// and buffers the record; flush() forces the buffer to disk (called at COMMIT
// to satisfy durability). read_all() is used by recovery to scan the log.
class LogManager {
public:
    explicit LogManager(const std::string &log_file);
    ~LogManager();

    // Append a record (lsn assigned here) and return its LSN.
    lsn_t append(LogRecord rec);

    // Force all buffered log records to disk. This is the durability point.
    void flush();

    // Read every record from the log (recovery analysis/redo).
    std::vector<LogRecord> read_all();

    lsn_t next_lsn() const { return next_lsn_; }

private:
    std::string  file_name_;
    std::fstream io_;
    lsn_t        next_lsn_{1};
    std::mutex   latch_;
};

} // namespace minidb
