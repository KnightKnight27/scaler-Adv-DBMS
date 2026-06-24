#pragma once

#include <fstream>
#include <string>
#include <vector>

using namespace std;

#include "common/Types.hpp"

namespace minidb {

enum class LogRecordType { BEGIN, INSERT, DELETE, COMMIT, ABORT, CHECKPOINT };

struct LogRecord {
    LogRecordType type = LogRecordType::BEGIN;
    int txn_id = 0;
    string table;
    Row row;
    RowLocation location;
};

class WAL {
public:
    explicit WAL(const string& filepath);
    void setFilepath(const string& filepath) { filepath_ = filepath; }
    bool open();
    void close();
    void append(const LogRecord& record);
    vector<LogRecord> readAll() const;

private:
    string filepath_;
    mutable fstream file_;
    static string serialize(const LogRecord& rec);
    static LogRecord deserialize(const string& line);
};

}  // namespace minidb
