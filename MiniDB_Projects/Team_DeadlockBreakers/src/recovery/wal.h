#pragma once

#include "common/types.h"
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace minidb {

/*
 * WAL Log Record Types
 */
enum class LogType { BEGIN, INSERT, DELETE_LOG, COMMIT, ABORT };

/*
 * LogRecord: a single entry in the write-ahead log.
 */
struct LogRecord {
    lsn_t       lsn;
    txn_id_t    txn_id;
    LogType     type;
    std::string table_name;
    std::string record_data;  // serialized row data (for INSERT/DELETE redo)
    std::string key_data;     // primary key value (for index redo)
};

/*
 * WALManager: Write-Ahead Logger.
 *
 * - Appends log records to a sequential log file (wal.log).
 * - Provides REDO-based crash recovery.
 * - Works with FORCE + NO-STEAL policy:
 *     Only committed transactions are redone during recovery.
 */
class WALManager {
public:
    explicit WALManager(const std::string& log_file = "wal.log");
    ~WALManager();

    // Append a log record
    lsn_t AppendLog(txn_id_t txn_id, LogType type,
                    const std::string& table_name = "",
                    const std::string& record_data = "",
                    const std::string& key_data = "");

    // Force flush log to disk
    void Flush();

    // Read all log records from the log file
    std::vector<LogRecord> ReadLog() const;

    // Get current LSN
    lsn_t GetCurrentLSN() const { return current_lsn_; }

    // Get the log file path
    const std::string& GetLogFile() const { return log_file_; }

    // Clear the log (after successful recovery)
    void ClearLog();

private:
    std::string   log_file_;
    std::ofstream writer_;
    lsn_t         current_lsn_ = 0;
    std::mutex    mutex_;

    // Serialize a log record to a string line
    static std::string Serialize(const LogRecord& rec);
    // Deserialize a string line to a log record
    static LogRecord Deserialize(const std::string& line);
    // Convert LogType to string
    static std::string LogTypeToString(LogType type);
    static LogType StringToLogType(const std::string& s);
};

} // namespace minidb
