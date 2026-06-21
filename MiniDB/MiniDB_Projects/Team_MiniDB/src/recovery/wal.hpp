#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "../txn/transaction.hpp"  // TxID

// Write-ahead log record types. INSERT carries the new row's bytes (the
// after-image, for REDO); DELETE carries the old row's bytes (the before-image,
// for UNDO). BEGIN/COMMIT/ABORT carry only the txid.
enum class LogType : std::uint8_t { BEGIN, INSERT, DELETE, COMMIT, ABORT };

struct LogRecord {
    LogType     type;
    TxID        txid = 0;
    std::string table;   // INSERT/DELETE only
    int         pk = 0;  // INSERT/DELETE only (primary key)
    std::string image;   // INSERT: after-image; DELETE: before-image
};

// Append-only write-ahead log. The golden rule (write-ahead): a change's log
// record must be durable (flush()) before the data page it describes reaches
// disk. We flush at COMMIT, so a committed transaction's records always survive
// a crash even if its heap pages never made it out of the buffer pool.
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    void append(const LogRecord& rec);  // buffered
    void flush();                       // make everything appended so far durable
    std::vector<LogRecord> read_all();  // read the whole log back (for recovery)

private:
    std::string   path_;
    std::ofstream out_;  // opened in append+binary
};
