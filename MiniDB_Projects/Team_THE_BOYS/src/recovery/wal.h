#pragma once

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "common/types.h"

namespace minidb {

struct LogRecord {
    uint64_t lsn = 0;
    LogRecordType type = LogRecordType::BEGIN;
    int txn_id = 0;
    std::string table;
    Row row;
    Rid rid;
};

class WriteAheadLog {
public:
    explicit WriteAheadLog(std::string filepath);

    uint64_t Append(const LogRecord& record);
    void Flush();
    std::vector<LogRecord> ReadAll() const;
    void Clear();

private:
    std::string filepath_;
    mutable std::fstream file_;
    uint64_t next_lsn_ = 1;
};

}  // namespace minidb
