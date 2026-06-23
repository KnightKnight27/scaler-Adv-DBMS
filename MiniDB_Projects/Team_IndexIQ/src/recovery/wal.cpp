#include "recovery/wal.h"
#include <fstream>
#include <sstream>

WAL::WAL(const std::string& path) : path_(path) {
    auto records = read_all();
    if (!records.empty()) next_lsn_ = records.back().lsn + 1;
}

void WAL::log(uint64_t txn_id, const std::string& op,
              const std::string& table,
              const std::string& key,
              const std::string& value) {
    std::ofstream f(path_, std::ios::app);
    f << next_lsn_++ << "|" << txn_id << "|" << op << "|"
      << table << "|" << key << "|" << value << "\n";
}

std::vector<LogRecord> WAL::read_all() const {
    std::vector<LogRecord> records;
    std::ifstream f(path_);
    if (!f.is_open()) return records;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        LogRecord r;
        std::string tok;
        std::getline(ss, tok, '|'); r.lsn    = std::stoull(tok);
        std::getline(ss, tok, '|'); r.txn_id = std::stoull(tok);
        std::getline(ss, r.op,    '|');
        std::getline(ss, r.table, '|');
        std::getline(ss, r.key,   '|');
        std::getline(ss, r.value);
        records.push_back(r);
    }
    return records;
}
