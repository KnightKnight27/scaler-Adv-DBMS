#include "recovery/log_manager.h"
#include <iostream>

namespace minidb {

std::vector<uint8_t> LogRecord::Serialize() const {
    std::vector<uint8_t> buf;
    // Header
    auto append_int = [&buf](int32_t val) {
        uint8_t *p = reinterpret_cast<uint8_t*>(&val);
        buf.insert(buf.end(), p, p + 4);
    };
    
    append_int(lsn);
    append_int(prev_lsn);
    append_int(txn_id);
    append_int(static_cast<int32_t>(type));
    append_int(commit_ts);
    
    if (type == LogRecordType::UPDATE) {
        append_int(rid.page_id);
        append_int(rid.slot_id);
        
        append_int(before_image.data_.size());
        buf.insert(buf.end(), before_image.data_.begin(), before_image.data_.end());
        
        append_int(after_image.data_.size());
        buf.insert(buf.end(), after_image.data_.begin(), after_image.data_.end());
    }
    
    std::vector<uint8_t> final_buf;
    int32_t total_size = buf.size() + 4;
    uint8_t *p = reinterpret_cast<uint8_t*>(&total_size);
    final_buf.insert(final_buf.end(), p, p + 4);
    final_buf.insert(final_buf.end(), buf.begin(), buf.end());
    return final_buf;
}

LogRecord LogRecord::Deserialize(const uint8_t *data, size_t size, size_t &offset) {
    LogRecord record;
    
    auto read_int = [&](int32_t &val) {
        memcpy(&val, data + offset, 4);
        offset += 4;
    };
    
    int32_t total_size;
    read_int(total_size);
    
    read_int(record.lsn);
    read_int(record.prev_lsn);
    read_int(record.txn_id);
    
    int32_t type_val;
    read_int(type_val);
    record.type = static_cast<LogRecordType>(type_val);
    
    read_int(record.commit_ts);
    
    if (record.type == LogRecordType::UPDATE) {
        read_int(record.rid.page_id);
        int32_t slot;
        read_int(slot);
        record.rid.slot_id = slot;
        
        int32_t b_len;
        read_int(b_len);
        record.before_image.data_.resize(b_len);
        memcpy(record.before_image.data_.data(), data + offset, b_len);
        offset += b_len;
        
        int32_t a_len;
        read_int(a_len);
        record.after_image.data_.resize(a_len);
        memcpy(record.after_image.data_.data(), data + offset, a_len);
        offset += a_len;
    }
    
    return record;
}

LogManager::LogManager(const std::string &log_file) : log_file_(log_file), next_lsn_(1) {
    log_io_.open(log_file_, std::ios::in | std::ios::out | std::ios::binary);
    if (!log_io_.is_open()) {
        log_io_.clear();
        log_io_.open(log_file_, std::ios::out | std::ios::binary);
        log_io_.close();
        log_io_.open(log_file_, std::ios::in | std::ios::out | std::ios::binary);
    }
}

LogManager::~LogManager() {
    Flush();
    if (log_io_.is_open()) {
        log_io_.close();
    }
}

int32_t LogManager::AppendLogRecord(LogRecord &record) {
    std::lock_guard<std::mutex> lock(latch_);
    record.lsn = next_lsn_++;
    
    std::vector<uint8_t> data = record.Serialize();
    
    log_io_.seekp(0, std::ios::end);
    log_io_.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    return record.lsn;
}

void LogManager::Flush() {
    std::lock_guard<std::mutex> lock(latch_);
    log_io_.flush();
}

std::vector<LogRecord> LogManager::ReadLogs() {
    std::lock_guard<std::mutex> lock(latch_);
    std::vector<LogRecord> logs;
    
    log_io_.seekg(0, std::ios::end);
    std::streampos size = log_io_.tellg();
    if (size == 0) return logs;
    
    log_io_.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    log_io_.read(reinterpret_cast<char*>(buffer.data()), size);
    
    size_t offset = 0;
    while (offset < buffer.size()) {
        logs.push_back(LogRecord::Deserialize(buffer.data(), buffer.size(), offset));
    }
    
    if (!logs.empty()) {
        next_lsn_ = logs.back().lsn + 1;
    }
    
    return logs;
}

} // namespace minidb
