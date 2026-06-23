// MiniDB - LogManager: append-only Write-Ahead Log on its own file (db ".wal").
// Records are buffered and written on Append; Flush() forces them out (called at COMMIT so a
// committed transaction is durable). ReadAll() reads the whole log back for crash recovery.
#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "log_record.h"

namespace minidb {

class LogManager {
public:
    explicit LogManager(const std::string& wal_file);
    ~LogManager();

    int64_t Append(const LogRecord& r);  // returns the record's LSN (byte offset)
    void Flush();                        // push buffered records to the file (durability point)
    std::vector<LogRecord> ReadAll();    // full scan, used by recovery
    bool Empty() const { return size_ == 0; }

private:
    std::string file_;
    std::ofstream out_;
    int64_t size_ = 0;
};

}  // namespace minidb
