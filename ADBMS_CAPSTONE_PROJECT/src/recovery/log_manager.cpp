#include "recovery/log_manager.h"
#include <iostream>

namespace minidb {

LogManager::LogManager(const std::string& log_file_name) : file_name_(log_file_name) {
    log_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    if (!log_io_.is_open()) {
        log_io_.clear();
        log_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out);
        log_io_.close();
        log_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    }
}

LogManager::~LogManager() {
    if (log_io_.is_open()) {
        log_io_.close();
    }
}

void LogManager::AppendLogRecord(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(latch_);
    
    // Clear flags and seek to end just in case we were reading
    log_io_.clear();
    log_io_.seekp(0, std::ios::end);
    
    log_io_.write(reinterpret_cast<const char*>(&record), sizeof(LogRecord));
    log_io_.flush();
    if (record.type_ == LogRecordType::INSERT || record.type_ == LogRecordType::DELETE) {
        std::cout << "[Diagnostic] WAL append: " << (record.type_ == LogRecordType::INSERT ? "INSERT" : "DELETE") << "\n";
    } // Force WAL semantics
}

void LogManager::ResetIterator() {
    std::lock_guard<std::mutex> lock(latch_);
    log_io_.clear();
    log_io_.seekg(0, std::ios::beg);
}

bool LogManager::GetNextLogRecord(LogRecord* record) {
    std::lock_guard<std::mutex> lock(latch_);
    if (log_io_.eof()) return false;
    
    log_io_.read(reinterpret_cast<char*>(record), sizeof(LogRecord));
    return log_io_.gcount() == sizeof(LogRecord);
}

} // namespace minidb
