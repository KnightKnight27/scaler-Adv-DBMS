#ifndef MINIDB_LOG_MANAGER_H
#define MINIDB_LOG_MANAGER_H

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "LogRecord.h"

/**
 * LogManager handles appending log records to the WAL file and reading
 * them back for recovery.
 *
 * ═══════════════════════════════════════════════════════════════════════
 * THE WAL GUARANTEE:
 *
 *   1. BEFORE a dirty page is flushed from the BufferPool to disk,
 *      ALL log records describing changes to that page MUST be
 *      written to the WAL file and flushed (force-on-write).
 *
 *   2. On COMMIT, ALL log records for that transaction MUST be
 *      force-flushed to the WAL (force-on-commit).
 *
 * This ensures that after a crash:
 *   - Committed data can always be reconstructed from the WAL (REDO)
 *   - Uncommitted changes can be rolled back using old values (UNDO)
 *
 * LSN (LOG SEQUENCE NUMBER):
 * Each log record gets a globally unique, monotonically increasing
 * LSN. This provides a total ordering of all operations across all
 * transactions, which is critical for ARIES recovery.
 *
 * FILE FORMAT:
 * The WAL file is a flat sequence of fixed-size LogRecords (36 bytes
 * each). No framing or delimiters — we can seek to any LSN by
 * computing the byte offset: offset = lsn * 36.
 * ═══════════════════════════════════════════════════════════════════════
 */
class LogManager {
public:
    explicit LogManager(const std::string& walFilePath);
    ~LogManager();

    /**
     * Append a log record to the WAL.
     * Assigns an LSN and writes to the file.
     * Returns the assigned LSN.
     */
    int appendLog(LogRecord& record);

    /**
     * Force-flush the WAL file to stable storage.
     * Must be called on COMMIT and before dirty page flushes.
     */
    void flush();

    /**
     * Read all log records from the WAL file (for recovery).
     */
    std::vector<LogRecord> readAllLogs();

    /** Get the current (next) LSN. */
    int getNextLSN() const { return nextLSN_; }

    /** Get the WAL file path. */
    const std::string& getFilePath() const { return walFilePath_; }

private:
    std::string walFilePath_;
    std::fstream walFile_;
    int nextLSN_;
    std::mutex mutex_;
};

#endif // MINIDB_LOG_MANAGER_H
