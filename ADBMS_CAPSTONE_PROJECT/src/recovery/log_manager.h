#pragma once
#include "recovery/log_record.h"
#include <fstream>
#include <mutex>

namespace minidb {

class LogManager {
public:
    explicit LogManager(const std::string& log_file_name);
    ~LogManager();

    // Appends a log record and flushes it immediately (force)
    void AppendLogRecord(const LogRecord& record);
    
    // Support reading log records for recovery
    void ResetIterator();
    bool GetNextLogRecord(LogRecord* record);

private:
    std::string file_name_;
    std::fstream log_io_;
    std::mutex latch_;
};

} // namespace minidb
