#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"

namespace minidb {

// One write-ahead-log record. MiniDB logs at the logical row level (command
// logging): each committed transaction's PUT/ERASE operations are replayed at
// recovery. This is the redo half of ARIES; because DDL is checkpointed
// immediately (so on-disk structures stay consistent) and we assume NO-STEAL
// for the demo, no undo/CLR pass is needed. See docs/DESIGN_DECISIONS.md for the
// full ARIES contrast.
enum class LogType { PUT, ERASE, COMMIT };

struct LogRecord {
    LogType      type;
    TxId         tx;
    std::string  table;   // PUT, ERASE
    std::int64_t key = 0; // PUT, ERASE
    std::string  row;     // PUT (raw encoded row bytes)
};

// Append-only WAL backed by a text file. The cardinal rule (log-before-data) is
// enforced by callers logging an op before applying it, and flushing the log at
// commit before acknowledging.
class LogManager {
public:
    explicit LogManager(std::string path) : path_(std::move(path)) {}

    void append(const LogRecord& rec);  // buffer a record
    void flush();                        // force buffered records to the file (commit point)
    void truncate();                     // clear the WAL (used at checkpoint)
    bool empty() const;                  // true if the WAL file has no records

    std::vector<LogRecord> read_all() const;  // parse all records (for recovery)

    const std::string& path() const { return path_; }

private:
    std::string              path_;
    std::vector<std::string> buffer_;  // unflushed serialized lines
};

} // namespace minidb
