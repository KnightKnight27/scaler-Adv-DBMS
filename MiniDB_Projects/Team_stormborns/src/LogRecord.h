#ifndef MINIDB_LOG_RECORD_H
#define MINIDB_LOG_RECORD_H

#include <cstdint>
#include <iostream>
#include <string>

/**
 * LogRecord represents a single entry in the Write-Ahead Log (WAL).
 *
 * ═══════════════════════════════════════════════════════════════════════
 * WAL PROTOCOL (Write-Ahead Logging):
 * "No dirty page can be flushed to disk until all log records describing
 *  changes to that page have been written to the WAL on stable storage."
 *
 * This is the FUNDAMENTAL rule that enables crash recovery. If we crash:
 *   - The WAL on disk tells us exactly what changes were made
 *   - We can REDO committed changes that didn't reach the data files
 *   - We can UNDO uncommitted changes that DID reach the data files
 *
 * LOG RECORD TYPES:
 *   BEGIN    - Transaction started
 *   INSERT   - New record inserted (old value is meaningless)
 *   UPDATE   - Record modified (both old and new values recorded)
 *   DELETE   - Record deleted (old value preserved for undo)
 *   COMMIT   - Transaction committed (all changes are durable)
 *   ABORT    - Transaction aborted (changes must be undone)
 *
 * SERIALIZATION FORMAT (fixed-size for simplicity):
 *   4 bytes: LSN (Log Sequence Number — unique, monotonically increasing)
 *   4 bytes: txnId
 *   4 bytes: logType (enum as int)
 *   4 bytes: tableId
 *   4 bytes: recordId
 *   4 bytes: oldId    (old record.id — for UNDO)
 *   4 bytes: oldVal   (old record.val — for UNDO)
 *   4 bytes: newId    (new record.id — for REDO)
 *   4 bytes: newVal   (new record.val — for REDO)
 *   ─────────
 *   36 bytes total per log record
 *
 * DESIGN TRADE-OFF:
 * Fixed-size records waste space (e.g., BEGIN doesn't need old/new values)
 * but make reading/seeking in the log file trivially fast. A production
 * WAL (like PostgreSQL's) uses variable-length records with a type-
 * specific header, which is space-efficient but harder to parse.
 * ═══════════════════════════════════════════════════════════════════════
 */

enum class LogType {
    BEGIN    = 0,
    INSERT   = 1,
    UPDATE   = 2,
    DELETE_OP = 3,  // avoid conflict with C's delete keyword
    COMMIT   = 4,
    ABORT    = 5
};

struct LogRecord {
    static constexpr int SERIALIZED_SIZE = 36;  // 9 × 4 bytes

    int32_t lsn      = 0;
    int32_t txnId    = 0;
    LogType type     = LogType::BEGIN;
    int32_t tableId  = 0;
    int32_t recordId = 0;

    // Old values (for UNDO)
    int32_t oldId    = 0;
    int32_t oldVal   = 0;

    // New values (for REDO)
    int32_t newId    = 0;
    int32_t newVal   = 0;

    /**
     * Serialize this log record to a binary output stream.
     * Writes exactly SERIALIZED_SIZE bytes.
     */
    void serialize(std::ostream& out) const {
        int32_t typeInt = static_cast<int32_t>(type);
        out.write(reinterpret_cast<const char*>(&lsn),      sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&txnId),    sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&typeInt),   sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&tableId),  sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&recordId), sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&oldId),    sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&oldVal),   sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&newId),    sizeof(int32_t));
        out.write(reinterpret_cast<const char*>(&newVal),   sizeof(int32_t));
    }

    /**
     * Deserialize a log record from a binary input stream.
     * Reads exactly SERIALIZED_SIZE bytes.
     * Returns false if the stream doesn't have enough data.
     */
    static bool deserialize(std::istream& in, LogRecord& rec) {
        int32_t typeInt;
        in.read(reinterpret_cast<char*>(&rec.lsn),      sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.txnId),    sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&typeInt),       sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.tableId),  sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.recordId), sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.oldId),    sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.oldVal),   sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.newId),    sizeof(int32_t));
        in.read(reinterpret_cast<char*>(&rec.newVal),   sizeof(int32_t));

        if (!in.good() && !in.eof()) return false;
        if (in.gcount() < static_cast<std::streamsize>(sizeof(int32_t))) return false;

        rec.type = static_cast<LogType>(typeInt);
        return true;
    }

    std::string toString() const {
        std::string typeStr;
        switch (type) {
            case LogType::BEGIN:     typeStr = "BEGIN";  break;
            case LogType::INSERT:    typeStr = "INSERT"; break;
            case LogType::UPDATE:    typeStr = "UPDATE"; break;
            case LogType::DELETE_OP: typeStr = "DELETE"; break;
            case LogType::COMMIT:    typeStr = "COMMIT"; break;
            case LogType::ABORT:     typeStr = "ABORT";  break;
        }
        return "LSN=" + std::to_string(lsn) +
               " Txn=" + std::to_string(txnId) +
               " " + typeStr +
               " Table=" + std::to_string(tableId) +
               " Rec=" + std::to_string(recordId) +
               " Old=(" + std::to_string(oldId) + "," + std::to_string(oldVal) + ")" +
               " New=(" + std::to_string(newId) + "," + std::to_string(newVal) + ")";
    }
};

#endif // MINIDB_LOG_RECORD_H
