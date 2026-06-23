#pragma once
#include "storage/page.h"
#include "transaction/transaction.h"
#include <fstream>
#include <mutex>
#include <vector>

namespace minidb {

enum class LogRecordType { BEGIN, COMMIT, ABORT, UPDATE, CHECKPOINT };

struct LogRecord {
    int32_t lsn;
    int32_t prev_lsn;
    txn_id_t txn_id;
    LogRecordType type;
    int32_t commit_ts; // Used for COMMIT records
    
    RecordId rid;
    Tuple before_image;
    Tuple after_image;
    
    std::vector<uint8_t> Serialize() const;
    static LogRecord Deserialize(const uint8_t *data, size_t size, size_t &offset);
};

class LogManager {
public:
    LogManager(const std::string &log_file);
    ~LogManager();

    int32_t AppendLogRecord(LogRecord &record);
    void Flush();
    
    std::vector<LogRecord> ReadLogs();

private:
    std::string log_file_;
    std::fstream log_io_;
    std::mutex latch_;
    int32_t next_lsn_;
};

} // namespace minidb
