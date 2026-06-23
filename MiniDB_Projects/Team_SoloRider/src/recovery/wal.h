#pragma once
#include "common/types.h"
#include <vector>
#include <string>
#include <fstream>
#include <mutex>

namespace minidb {

enum class LogRecordType { BEGIN, COMMIT, ABORT, INSERT, DELETE };

struct LogRecord {
    lsn_t lsn;
    txn_id_t txn_id;
    LogRecordType type;
    RecordId rid;
    std::vector<char> tuple_data;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& log_path);
    ~WriteAheadLog();

    lsn_t append(const LogRecord& record);
    void flush();
    std::vector<LogRecord> read_all();

private:
    std::string log_path_;
    std::fstream file_;
    std::mutex latch_;
    lsn_t next_lsn_;
};

} // namespace minidb
