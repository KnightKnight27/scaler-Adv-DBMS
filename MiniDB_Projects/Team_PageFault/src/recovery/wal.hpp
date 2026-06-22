#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include "../common/types.hpp"

namespace minidb {

enum class LogType : uint8_t {
    BEGIN = 1,
    INSERT = 2,   // image = after-image (the new row), used for REDO
    DELETE = 3,   // image = before-image (the old row), used for UNDO
    COMMIT = 4,
    ABORT = 5,
};

struct LogRecord {
    LogType type;
    TxnId txid = INVALID_TXN;
    int64_t pk = 0;        // primary key for INSERT/DELETE
    std::string table;     // table name for INSERT/DELETE
    std::string image;     // row bytes for INSERT/DELETE
};

// Append-only write-ahead log. Records are length-prefixed and flushed to disk
// so a committed transaction is recoverable even if its heap pages never left
// the buffer pool. A torn final record (crash mid-write) is detected by a short
// read at recovery time and dropped.
class WAL {
public:
    explicit WAL(std::string path);

    void logBegin(TxnId txid);
    void logInsert(TxnId txid, const std::string& table, int64_t pk, const std::string& image);
    void logDelete(TxnId txid, const std::string& table, int64_t pk, const std::string& image);
    void logCommit(TxnId txid);  // flushed before control returns (durability)
    void logAbort(TxnId txid);

    // Read every intact record (for crash recovery).
    std::vector<LogRecord> readAll();

    // Drop the log after a successful recovery / checkpoint.
    void reset();

private:
    std::string path_;
    std::ofstream out_;

    void append(const LogRecord& rec);
};

} // namespace minidb
