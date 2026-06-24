#include "recovery/WAL.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std;

namespace minidb {

static const char* typeToString(LogRecordType t) {
    switch (t) {
        case LogRecordType::BEGIN: return "BEGIN";
        case LogRecordType::INSERT: return "INSERT";
        case LogRecordType::DELETE: return "DELETE";
        case LogRecordType::COMMIT: return "COMMIT";
        case LogRecordType::ABORT: return "ABORT";
        default: return "CHECKPOINT";
    }
}

static LogRecordType stringToType(const string& s) {
    if (s == "BEGIN") return LogRecordType::BEGIN;
    if (s == "INSERT") return LogRecordType::INSERT;
    if (s == "DELETE") return LogRecordType::DELETE;
    if (s == "COMMIT") return LogRecordType::COMMIT;
    if (s == "ABORT") return LogRecordType::ABORT;
    return LogRecordType::CHECKPOINT;
}

WAL::WAL(const string& filepath) : filepath_(filepath) {}

bool WAL::open() {
    filesystem::path p(filepath_);
    if (p.has_parent_path()) filesystem::create_directories(p.parent_path());
    file_.open(filepath_, ios::app | ios::out | ios::in);
    if (!file_.is_open()) {
        file_.open(filepath_, ios::out);
        file_.close();
        file_.open(filepath_, ios::app | ios::out | ios::in);
    }
    return file_.is_open();
}

void WAL::close() { if (file_.is_open()) file_.close(); }

string WAL::serialize(const LogRecord& rec) {
    ostringstream oss;
    oss << typeToString(rec.type) << "|" << rec.txn_id << "|" << rec.table << "|"
        << rec.location.page_id << "|" << rec.location.slot_index << "|";
    for (const auto& kv : rec.row) oss << kv.first << "=" << kv.second << ",";
    return oss.str();
}

LogRecord WAL::deserialize(const string& line) {
    LogRecord rec;
    istringstream iss(line);
    string type_str, txn_str, table, page_str, slot_str, row_str;
    getline(iss, type_str, '|'); getline(iss, txn_str, '|'); getline(iss, table, '|');
    getline(iss, page_str, '|'); getline(iss, slot_str, '|'); getline(iss, row_str, '|');
    rec.type = stringToType(type_str);
    rec.txn_id = atoi(txn_str.c_str());
    rec.table = table;
    rec.location.page_id = (uint32_t)atoi(page_str.c_str());
    rec.location.slot_index = (uint32_t)atoi(slot_str.c_str());
    istringstream row_iss(row_str);
    string pair;
    while (getline(row_iss, pair, ',')) {
        auto eq = pair.find('=');
        if (eq != string::npos) rec.row[pair.substr(0, eq)] = pair.substr(eq + 1);
    }
    return rec;
}

void WAL::append(const LogRecord& record) {
    file_ << serialize(record) << "\n";
    file_.flush();
}

vector<LogRecord> WAL::readAll() const {
    vector<LogRecord> records;
    ifstream in(filepath_);
    string line;
    while (getline(in, line))
        if (!line.empty()) records.push_back(deserialize(line));
    return records;
}

}  // namespace minidb
