#include "recovery/wal.h"
#include <sstream>
#include <iostream>

namespace minidb {

WALManager::WALManager(const std::string& log_file)
    : log_file_(log_file) {
    // Open in append mode
    writer_.open(log_file_, std::ios::app);

    // Determine current LSN by reading existing records
    auto records = ReadLog();
    if (!records.empty()) {
        current_lsn_ = records.back().lsn + 1;
    }
}

WALManager::~WALManager() {
    if (writer_.is_open()) {
        writer_.flush();
        writer_.close();
    }
}

lsn_t WALManager::AppendLog(txn_id_t txn_id, LogType type,
                             const std::string& table_name,
                             const std::string& record_data,
                             const std::string& key_data) {
    std::lock_guard<std::mutex> lock(mutex_);

    LogRecord rec;
    rec.lsn         = current_lsn_++;
    rec.txn_id      = txn_id;
    rec.type        = type;
    rec.table_name  = table_name;
    rec.record_data = record_data;
    rec.key_data    = key_data;

    std::string line = Serialize(rec);
    writer_ << line << "\n";
    writer_.flush();  // WAL protocol: flush before acknowledging

    return rec.lsn;
}

void WALManager::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (writer_.is_open()) {
        writer_.flush();
    }
}

std::vector<LogRecord> WALManager::ReadLog() const {
    std::vector<LogRecord> records;
    std::ifstream reader(log_file_);
    if (!reader.is_open()) return records;

    std::string line;
    while (std::getline(reader, line)) {
        if (line.empty()) continue;
        try {
            records.push_back(Deserialize(line));
        } catch (...) {
            // Skip corrupted records
        }
    }
    return records;
}

void WALManager::ClearLog() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (writer_.is_open()) {
        writer_.close();
    }
    // Truncate the file
    writer_.open(log_file_, std::ios::trunc);
    current_lsn_ = 0;
}

std::string WALManager::LogTypeToString(LogType type) {
    switch (type) {
        case LogType::BEGIN:      return "BEGIN";
        case LogType::INSERT:     return "INSERT";
        case LogType::DELETE_LOG: return "DELETE";
        case LogType::COMMIT:     return "COMMIT";
        case LogType::ABORT:      return "ABORT";
    }
    return "UNKNOWN";
}

LogType WALManager::StringToLogType(const std::string& s) {
    if (s == "BEGIN")  return LogType::BEGIN;
    if (s == "INSERT") return LogType::INSERT;
    if (s == "DELETE") return LogType::DELETE_LOG;
    if (s == "COMMIT") return LogType::COMMIT;
    if (s == "ABORT")  return LogType::ABORT;
    return LogType::BEGIN; // fallback
}

std::string WALManager::Serialize(const LogRecord& rec) {
    // Format: LSN|TXN_ID|TYPE|TABLE|KEY_DATA|RECORD_DATA
    // Use '|' delimiter (record_data uses pipe internally, but we
    // base64 or escape it — for simplicity, use '~' for record_data delimiter)
    std::ostringstream oss;
    oss << rec.lsn << "|"
        << rec.txn_id << "|"
        << LogTypeToString(rec.type) << "|"
        << rec.table_name << "|"
        << rec.key_data << "|"
        << rec.record_data;
    return oss.str();
}

LogRecord WALManager::Deserialize(const std::string& line) {
    LogRecord rec;
    std::vector<std::string> parts;
    std::istringstream iss(line);
    std::string token;

    // Split on '|' but only first 5 delimiters (record_data may contain '|')
    int count = 0;
    size_t pos = 0;
    for (int i = 0; i < 5; i++) {
        size_t next = line.find('|', pos);
        if (next == std::string::npos) break;
        parts.push_back(line.substr(pos, next - pos));
        pos = next + 1;
        count++;
    }
    // Rest is record_data
    if (pos < line.size()) {
        parts.push_back(line.substr(pos));
    }

    if (parts.size() >= 3) {
        rec.lsn     = std::stoull(parts[0]);
        rec.txn_id  = static_cast<txn_id_t>(std::stoul(parts[1]));
        rec.type    = StringToLogType(parts[2]);
    }
    if (parts.size() >= 4) rec.table_name  = parts[3];
    if (parts.size() >= 5) rec.key_data    = parts[4];
    if (parts.size() >= 6) rec.record_data = parts[5];

    return rec;
}

} // namespace minidb
