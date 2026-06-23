#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct LogRecord {
    uint64_t    lsn;
    uint64_t    txn_id;
    std::string op;
    std::string table;
    std::string key;
    std::string value;
};

class WAL {
public:
    explicit WAL(const std::string& path);

    void log(uint64_t txn_id, const std::string& op,
             const std::string& table = "",
             const std::string& key   = "",
             const std::string& value = "");

    std::vector<LogRecord> read_all() const;

private:
    std::string   path_;
    uint64_t      next_lsn_ = 1;
};
