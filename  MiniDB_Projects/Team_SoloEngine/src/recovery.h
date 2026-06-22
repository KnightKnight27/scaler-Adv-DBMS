#pragma once

#include "table.h"

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

using txn_id_t = int32_t;   // keep consistent with transaction.h

// ─── Log record types ─────────────────────────────────────────────────────────

enum class LogType : int32_t { BEGIN = 0, INSERT = 1, COMMIT = 2, ABORT = 3 };

// ─── Fixed-size (40-byte) log record ─────────────────────────────────────────
// Layout:
//   [int32  txn_id][int32  type][int32  page_id][int32  slot_num]
//   [int64  id    ][int64  val1][int64  val2]
// For non-INSERT records the page_id/slot_num/id/val1/val2 fields are zero.

struct LogRecord {
    int32_t txn_id  {0};
    LogType type    {LogType::BEGIN};
    int32_t page_id {0};
    int32_t slot_num{0};
    int64_t id      {0};
    int64_t val1    {0};
    int64_t val2    {0};
};
static_assert(sizeof(LogRecord) == 40, "LogRecord must be exactly 40 bytes");

// ─── LogManager ──────────────────────────────────────────────────────────────
// Opens/creates `path` in append mode.  Thread-safe.  Call Flush() to force
// records to disk (required before reporting COMMIT to the outside world).

class LogManager {
public:
    explicit LogManager(const std::string &path);
    ~LogManager();

    void AppendRecord(const LogRecord &rec);
    void Flush();

private:
    std::ofstream out_;
    std::mutex    mutex_;
};

// ─── RecoveryManager ─────────────────────────────────────────────────────────
// Reads the WAL and redoes committed INSERT records into the given heap.

class RecoveryManager {
public:
    RecoveryManager(const std::string &log_path, TableHeap *heap);
    void Redo();

private:
    std::string log_path_;
    TableHeap  *heap_;
};
