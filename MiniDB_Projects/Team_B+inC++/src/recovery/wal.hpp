#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "../txn/transaction.hpp"  // TxID

// INSERT = after-image (REDO), DELETE = before-image (UNDO)
enum class LogType : std::uint8_t { BEGIN, INSERT, DELETE, COMMIT, ABORT };

struct LogRecord {
    LogType     type;
    TxID        txid = 0;
    std::string table;   // INSERT/DELETE only
    int         pk = 0;  // INSERT/DELETE only (primary key)
    std::string image;   // INSERT: after-image; DELETE: before-image
};

// append-only WAL. log record durable (flushed) before its page reaches disk
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    void append(const LogRecord& rec);  // buffered
    void flush();
    std::vector<LogRecord> read_all();  // for recovery

private:
    std::string   path_;
    std::ofstream out_;
};
