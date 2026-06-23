#include "log_manager.h"

#include <sys/stat.h>

namespace minidb {

LogManager::LogManager(const std::string& wal_file) : file_(wal_file) {
    struct stat st;
    if (::stat(file_.c_str(), &st) == 0) size_ = st.st_size;
    // Append mode: existing log is preserved (recovery reads it before we append more).
    out_.open(file_, std::ios::out | std::ios::app | std::ios::binary);
}

LogManager::~LogManager() {
    if (out_.is_open()) { out_.flush(); out_.close(); }
}

int64_t LogManager::Append(const LogRecord& r) {
    std::string bytes = SerializeRecord(r);
    int64_t lsn = size_;
    out_.write(bytes.data(), bytes.size());
    size_ += static_cast<int64_t>(bytes.size());
    return lsn;
}

void LogManager::Flush() { out_.flush(); }

std::vector<LogRecord> LogManager::ReadAll() {
    std::vector<LogRecord> recs;
    std::ifstream in(file_, std::ios::in | std::ios::binary);
    if (!in.is_open()) return recs;
    std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    LogRecord r;
    while (DeserializeRecord(buf, pos, &r)) recs.push_back(r);
    return recs;
}

}  // namespace minidb
