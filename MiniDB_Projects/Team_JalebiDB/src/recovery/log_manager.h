#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "recovery/log_record.h"
#include <mutex>
#include <thread>
#include <condition_variable>

namespace minidb {

class LogManager {
public:
    explicit LogManager(DiskManager *disk_manager);
    ~LogManager();

    // Append a log record to the log buffer
    lsn_t AppendLogRecord(LogRecord *log_record);

    // Flush log records up to the given LSN to disk
    void Flush(lsn_t lsn);

    // Flush all log records in the buffer to disk
    void FlushAll();

    DiskManager *GetDiskManager() const { return disk_manager_; }

    // Get the next LSN to be assigned
    lsn_t GetNextLSN() const { return next_lsn_; }

    // Get the highest LSN successfully written to disk
    lsn_t GetPersistentLSN() const { return persistent_lsn_; }

private:
    void RunFlushThread();

    DiskManager *disk_manager_;
    lsn_t next_lsn_{0};
    lsn_t persistent_lsn_{INVALID_LSN};

    // Log buffer
    static constexpr int LOG_BUFFER_SIZE = 16 * 1024; // 16KB
    char log_buffer_[LOG_BUFFER_SIZE];
    int log_buffer_offset_{0};

    // Double buffering for asynchronous flushing
    char flush_buffer_[LOG_BUFFER_SIZE];
    int flush_buffer_size_{0};

    std::mutex latch_;
    std::thread flush_thread_;
    std::condition_variable cv_;
    bool run_flush_thread_{true};
};

} // namespace minidb
