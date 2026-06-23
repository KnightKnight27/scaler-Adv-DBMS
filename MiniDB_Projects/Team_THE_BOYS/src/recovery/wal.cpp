#include "recovery/wal.h"

#include <filesystem>
#include <sstream>

namespace minidb {

namespace {

std::string SerializeRowValues(const Row& row) {
    std::ostringstream oss;
    for (const auto& v : row.values) {
        if (v.type == ValueType::NULL_TYPE) {
            oss << "0;";
        } else if (v.type == ValueType::INT) {
            oss << "1:" << std::get<int64_t>(v.data) << ';';
        } else {
            oss << "2:" << std::get<std::string>(v.data) << ';';
        }
    }
    return oss.str();
}

Row DeserializeRowValues(const std::string& data) {
    Row row;
    std::istringstream iss(data);
    std::string token;
    while (std::getline(iss, token, ';')) {
        if (token.empty()) continue;
        if (token[0] == '0') {
            row.values.push_back(Value::Null());
        } else if (token[0] == '1') {
            row.values.push_back(Value::Int(std::stoll(token.substr(2))));
        } else if (token[0] == '2') {
            row.values.push_back(Value::Str(token.substr(2)));
        }
    }
    return row;
}

}  // namespace

WriteAheadLog::WriteAheadLog(std::string filepath) : filepath_(std::move(filepath)) {
    namespace fs = std::filesystem;
    if (!fs::exists(filepath_)) {
        std::ofstream create(filepath_, std::ios::binary);
    }
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
}

uint64_t WriteAheadLog::Append(const LogRecord& record) {
    LogRecord r = record;
    r.lsn = next_lsn_++;
    std::ostringstream oss;
    oss << r.lsn << '|' << static_cast<int>(r.type) << '|' << r.txn_id << '|' << r.table << '|'
        << SerializeRowValues(r.row) << '|' << r.rid.page_id << '|' << r.rid.slot_id << '\n';
    file_ << oss.str();
    file_.flush();
    return r.lsn;
}

void WriteAheadLog::Flush() { file_.flush(); }

std::vector<LogRecord> WriteAheadLog::ReadAll() const {
    std::vector<LogRecord> records;
    std::ifstream in(filepath_);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        LogRecord rec;
        std::istringstream iss(line);
        std::string token;
        std::getline(iss, token, '|'); rec.lsn = std::stoull(token);
        std::getline(iss, token, '|'); rec.type = static_cast<LogRecordType>(std::stoi(token));
        std::getline(iss, token, '|'); rec.txn_id = std::stoi(token);
        std::getline(iss, token, '|'); rec.table = token;
        std::getline(iss, token, '|'); rec.row = DeserializeRowValues(token);
        std::getline(iss, token, '|'); rec.rid.page_id = std::stoi(token);
        std::getline(iss, token, '|'); rec.rid.slot_id = std::stoi(token);
        records.push_back(rec);
    }
    return records;
}

void WriteAheadLog::Clear() {
    file_.close();
    std::ofstream trunc(filepath_, std::ios::trunc);
    trunc.close();
    file_.open(filepath_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    next_lsn_ = 1;
}

}  // namespace minidb
